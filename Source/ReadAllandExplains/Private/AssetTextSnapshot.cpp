// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTextSnapshot.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
	static FString BoolText(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	static FString EscapeMultiline(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Value;
	}

	static bool ShouldExportProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		const EPropertyFlags ExcludedFlags =
			CPF_Transient |
			CPF_DuplicateTransient |
			CPF_NonPIEDuplicateTransient;

		return !Property->HasAnyPropertyFlags(ExcludedFlags);
	}

	static FString ExportPropertyValue(
		const FProperty* Property,
		const void* Container,
		UObject* Owner)
	{
		if (!Property || !Container)
		{
			return TEXT("<invalid>");
		}

		FString Value;
		if (!Property->ExportText_InContainer(
			0,
			Value,
			Container,
			nullptr,
			Owner,
			PPF_Copy | PPF_Delimited | PPF_ExportsNotFullyQualified,
			Owner))
		{
			return TEXT("<unexportable>");
		}

		return EscapeMultiline(Value);
	}

	static FString ContainerTypeToString(const EPinContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EPinContainerType::Array: return TEXT("Array");
		case EPinContainerType::Set: return TEXT("Set");
		case EPinContainerType::Map: return TEXT("Map");
		case EPinContainerType::None:
		default: return TEXT("None");
		}
	}

	static FString DirectionToString(const EEdGraphPinDirection Direction)
	{
		switch (Direction)
		{
		case EGPD_Input: return TEXT("Input");
		case EGPD_Output: return TEXT("Output");
		default: return TEXT("Unknown");
		}
	}

	static FString PinTypeToString(const FEdGraphPinType& PinType)
	{
		const UObject* SubCategoryObject = PinType.PinSubCategoryObject.Get();
		return FString::Printf(
			TEXT("Category=%s; SubCategory=%s; SubObject=%s; Container=%s; Ref=%s; Const=%s; Weak=%s; Wrapper=%s"),
			*PinType.PinCategory.ToString(),
			*PinType.PinSubCategory.ToString(),
			SubCategoryObject ? *SubCategoryObject->GetPathName() : TEXT("<none>"),
			*ContainerTypeToString(PinType.ContainerType),
			*BoolText(PinType.bIsReference),
			*BoolText(PinType.bIsConst),
			*BoolText(PinType.bIsWeakPointer),
			*BoolText(PinType.bIsUObjectWrapper));
	}

	static FString StableNodeId(const UEdGraphNode* Node, const int32 FallbackIndex)
	{
		if (Node && Node->NodeGuid.IsValid())
		{
			return Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		}
		return FString::Printf(TEXT("index-%d"), FallbackIndex);
	}

	static FString StablePinId(const UEdGraphPin* Pin, const int32 FallbackIndex)
	{
		if (Pin && Pin->PinId.IsValid())
		{
			return Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens);
		}
		return FString::Printf(TEXT("pin-%d"), FallbackIndex);
	}
}

FString FAssetTextSnapshot::MarkdownCell(const FString& Text)
{
	FString Out = EscapeMultiline(Text);
	Out.ReplaceInline(TEXT("|"), TEXT("\\|"));
	return Out;
}

FString FAssetTextSnapshot::ExportStructProperties(
	const UStruct* Struct,
	const void* StructData,
	UObject* Owner,
	const FString& Heading)
{
	if (!Struct || !StructData)
	{
		return FString::Printf(TEXT("### %s\n\n- (unavailable)\n\n"), *Heading);
	}

	FString Out;
	Out += FString::Printf(TEXT("### %s\n\n"), *Heading);
	Out += TEXT("| Property | Type | Value |\n");
	Out += TEXT("|----------|------|-------|\n");

	int32 ExportedCount = 0;
	for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		if (!ShouldExportProperty(Property))
		{
			continue;
		}

		const FString Value = ExportPropertyValue(Property, StructData, Owner);
		Out += FString::Printf(
			TEXT("| %s | %s | %s |\n"),
			*MarkdownCell(Property->GetName()),
			*MarkdownCell(Property->GetCPPType()),
			*MarkdownCell(Value));
		++ExportedCount;
	}

	if (ExportedCount == 0)
	{
		Out += TEXT("| (none) | | |\n");
	}
	Out += TEXT("\n");
	return Out;
}

FString FAssetTextSnapshot::ExportObjectProperties(
	const UObject* Object,
	const FString& Heading)
{
	if (!Object)
	{
		return FString::Printf(TEXT("### %s\n\n- (null)\n\n"), *Heading);
	}

	FString Out;
	Out += FString::Printf(TEXT("### %s\n\n"), *Heading);
	Out += TEXT("- Class: `") + Object->GetClass()->GetPathName() + TEXT("`\n");
	Out += TEXT("- ObjectPath: `") + Object->GetPathName() + TEXT("`\n\n");
	Out += TEXT("| Property | Type | Value |\n");
	Out += TEXT("|----------|------|-------|\n");

	int32 ExportedCount = 0;
	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		if (!ShouldExportProperty(Property))
		{
			continue;
		}

		const FString Value = ExportPropertyValue(Property, Object, const_cast<UObject*>(Object));
		Out += FString::Printf(
			TEXT("| %s | %s | %s |\n"),
			*MarkdownCell(Property->GetName()),
			*MarkdownCell(Property->GetCPPType()),
			*MarkdownCell(Value));
		++ExportedCount;
	}

	if (ExportedCount == 0)
	{
		Out += TEXT("| (none) | | |\n");
	}
	Out += TEXT("\n");
	return Out;
}

FString FAssetTextSnapshot::ExportGraph(
	const UEdGraph* Graph,
	const FString& Heading,
	const bool bIncludeNativeClipboardText)
{
	FString Out;
	Out += FString::Printf(TEXT("## %s\n\n"), *Heading);
	if (!Graph)
	{
		Out += TEXT("- (graph unavailable)\n\n");
		return Out;
	}

	Out += TEXT("- GraphClass: `") + Graph->GetClass()->GetPathName() + TEXT("`\n");
	Out += TEXT("- GraphPath: `") + Graph->GetPathName() + TEXT("`\n");
	Out += FString::Printf(TEXT("- NodeCount: %d\n\n"), Graph->Nodes.Num());

	TMap<const UEdGraphNode*, int32> NodeIndexByPtr;
	for (int32 NodeIndex = 0; NodeIndex < Graph->Nodes.Num(); ++NodeIndex)
	{
		if (const UEdGraphNode* Node = Graph->Nodes[NodeIndex])
		{
			NodeIndexByPtr.Add(Node, NodeIndex);
		}
	}

	Out += TEXT("### Node Index\n\n");
	Out += TEXT("| Index | StableId | Class | Title | Position | EnabledState |\n");
	Out += TEXT("|------:|----------|-------|-------|----------|--------------|\n");
	for (int32 NodeIndex = 0; NodeIndex < Graph->Nodes.Num(); ++NodeIndex)
	{
		const UEdGraphNode* Node = Graph->Nodes[NodeIndex];
		if (!Node)
		{
			continue;
		}

		Out += FString::Printf(
			TEXT("| %d | %s | %s | %s | (%d, %d) | %d |\n"),
			NodeIndex,
			*StableNodeId(Node, NodeIndex),
			*MarkdownCell(Node->GetClass()->GetPathName()),
			*MarkdownCell(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()),
			Node->NodePosX,
			Node->NodePosY,
			static_cast<int32>(Node->GetDesiredEnabledState()));
	}
	Out += TEXT("\n");

	for (int32 NodeIndex = 0; NodeIndex < Graph->Nodes.Num(); ++NodeIndex)
	{
		const UEdGraphNode* Node = Graph->Nodes[NodeIndex];
		if (!Node)
		{
			continue;
		}

		Out += FString::Printf(
			TEXT("### Node %d: %s\n\n"),
			NodeIndex,
			*MarkdownCell(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
		Out += TEXT("- StableId: `") + StableNodeId(Node, NodeIndex) + TEXT("`\n");
		Out += TEXT("- ObjectName: `") + Node->GetName() + TEXT("`\n");
		Out += TEXT("- Class: `") + Node->GetClass()->GetPathName() + TEXT("`\n");
		Out += FString::Printf(
			TEXT("- Position: (%d, %d), Size: (%d, %d), AdvancedPinDisplay: %d\n"),
			Node->NodePosX,
			Node->NodePosY,
			Node->NodeWidth,
			Node->NodeHeight,
			static_cast<int32>(Node->AdvancedPinDisplay));
		Out += TEXT("- Comment: ") + MarkdownCell(Node->NodeComment) + TEXT("\n\n");

		Out += ExportObjectProperties(Node, TEXT("Reflected Node Properties"));

		Out += TEXT("#### Pins\n\n");
		Out += TEXT("| Pin | StableId | Direction | Type | Default | AutoDefault | DefaultObject | DefaultText | Flags | Links |\n");
		Out += TEXT("|-----|----------|-----------|------|---------|-------------|---------------|-------------|-------|-------|\n");
		for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
		{
			const UEdGraphPin* Pin = Node->Pins[PinIndex];
			if (!Pin)
			{
				continue;
			}

			TArray<FString> LinkLabels;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin)
				{
					continue;
				}
				const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
				const int32* LinkedNodeIndex = LinkedNode ? NodeIndexByPtr.Find(LinkedNode) : nullptr;
				LinkLabels.Add(FString::Printf(
					TEXT("N%d.%s[%s]"),
					LinkedNodeIndex ? *LinkedNodeIndex : -1,
					*LinkedPin->PinName.ToString(),
					*StablePinId(LinkedPin, INDEX_NONE)));
			}

			const FString Flags = FString::Printf(
				TEXT("Hidden=%s; NotConnectable=%s; ReadOnly=%s; IgnoreDefault=%s; Advanced=%s; Orphaned=%s"),
				*BoolText(Pin->bHidden),
				*BoolText(Pin->bNotConnectable),
				*BoolText(Pin->bDefaultValueIsReadOnly),
				*BoolText(Pin->bDefaultValueIsIgnored),
				*BoolText(Pin->bAdvancedView),
				*BoolText(Pin->bOrphanedPin));

			Out += FString::Printf(
				TEXT("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n"),
				*MarkdownCell(Pin->PinName.ToString()),
				*StablePinId(Pin, PinIndex),
				*DirectionToString(Pin->Direction),
				*MarkdownCell(PinTypeToString(Pin->PinType)),
				*MarkdownCell(Pin->DefaultValue),
				*MarkdownCell(Pin->AutogeneratedDefaultValue),
				*MarkdownCell(Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT("<none>")),
				*MarkdownCell(Pin->DefaultTextValue.ToString()),
				*MarkdownCell(Flags),
				*MarkdownCell(FString::Join(LinkLabels, TEXT(", "))));
		}
		Out += TEXT("\n");
	}

	if (bIncludeNativeClipboardText)
	{
		TSet<UObject*> NodesToExport;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->CanDuplicateNode())
			{
				NodesToExport.Add(Node);
			}
		}

		Out += TEXT("### Native UE Clipboard Graph Text\n\n");
		Out += TEXT("This block uses Unreal's own node exporter and can be used as a high-fidelity reconstruction aid. Root/output connections are also preserved in the tables above.\n\n");
		if (NodesToExport.Num() == 0)
		{
			Out += TEXT("- (no duplicable nodes)\n\n");
		}
		else
		{
			FString NativeText;
			FEdGraphUtilities::ExportNodesToText(NodesToExport, NativeText);
			Out += TEXT("```text\n") + NativeText + TEXT("\n```\n\n");
		}
	}

	return Out;
}
