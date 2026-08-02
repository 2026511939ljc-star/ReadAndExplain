// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDocumentIR.h"

#include "AssetTextSnapshot.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ReadAllDocumentIRImpl
{
	static FString MermaidLabel(FString Value)
	{
		Value.ReplaceInline(TEXT("\""), TEXT("'"));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Value;
	}

	static TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	static void AppendParameterTable(const TArray<FReadAllParameterClue>& Clues, FString& Out)
	{
		Out += TEXT("## 参数线索\n\n");
		Out += TEXT("> 以下参数来自资产的实际可编辑或公开数据，可作为跨系统追踪的起点；同名不等于已连接。\n\n");
		Out += TEXT("| 参数/入口 | 类型 | 当前值/说明 |\n");
		Out += TEXT("|-----------|------|--------------|\n");
		for (const FReadAllParameterClue& Clue : Clues)
		{
			Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Clue.Name)
				+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Clue.Kind)
				+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Clue.Value) + TEXT(" |\n");
		}
		if (Clues.Num() == 0)
		{
			Out += TEXT("| (未发现可公开参数) | | |\n");
		}
		Out += TEXT("\n");
	}

	static void AppendRelationships(const FReadAllAssetDocumentIR& Document, FString& Out)
	{
		Out += TEXT("---\n\n## 直接依赖 / 反向引用\n\n");
		Out += TEXT("> `依赖` 表示当前资产使用了谁；`反向引用` 表示谁正在使用当前资产。\n\n");
		Out += FString::Printf(TEXT("- 直接依赖：%d\n- 反向引用：%d\n\n"), Document.Dependencies.Num(), Document.Referencers.Num());
		Out += TEXT("```mermaid\ngraph LR\n");
		Out += TEXT("  ROOT[\"") + MermaidLabel(Document.AssetName) + TEXT("\"]\n");
		for (int32 Index = 0; Index < Document.Dependencies.Num(); ++Index)
		{
			Out += FString::Printf(TEXT("  ROOT --> D%d[\"%s\"]\n"), Index, *MermaidLabel(Document.Dependencies[Index]));
		}
		for (int32 Index = 0; Index < Document.Referencers.Num(); ++Index)
		{
			Out += FString::Printf(TEXT("  R%d[\"%s\"] --> ROOT\n"), Index, *MermaidLabel(Document.Referencers[Index]));
		}
		if (Document.Dependencies.Num() == 0 && Document.Referencers.Num() == 0)
		{
			Out += TEXT("  ROOT\n");
		}
		Out += TEXT("```\n\n");
	}

	static void AppendGraphIR(const TArray<FReadAllGraphIR>& Graphs, const EReadAllExportMode Mode, FString& Out)
	{
		if (Graphs.IsEmpty())
		{
			return;
		}

		Out += TEXT("---\n\n## 结构化图 IR\n\n");
		Out += TEXT("> 节点、引脚和连线由导出器一次采集，并同时供 Markdown、JSON 与未来 MCP 使用。\n\n");
		Out += TEXT("| 图 | 类型 | 节点 | 引脚 | 连线 |\n");
		Out += TEXT("|----|------|-----:|-----:|-----:|\n");
		for (const FReadAllGraphIR& Graph : Graphs)
		{
			int32 PinCount = 0;
			for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
			{
				PinCount += Node.Pins.Num();
			}
			Out += FString::Printf(TEXT("| %s | %s | %d | %d | %d |\n"),
				*FAssetTextSnapshot::MarkdownCell(Graph.Name),
				*FAssetTextSnapshot::MarkdownCell(Graph.Kind),
				Graph.Nodes.Num(),
				PinCount,
				Graph.Links.Num());
		}
		Out += TEXT("\n");

		if (!ReadAllExportModeIncludes(Mode, EReadAllExportMode::Full))
		{
			Out += TEXT("- 当前模式仅显示图统计；完整节点、引脚和连线已写入 `.meta.json`。\n\n");
			return;
		}

		for (const FReadAllGraphIR& Graph : Graphs)
		{
			Out += TEXT("### ") + Graph.Name + TEXT("\n\n");
			Out += TEXT("#### 节点\n\n| Id | 标题 | 类 | 位置 | 引脚数 |\n|----|------|----|------|------:|\n");
			for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
			{
				Out += FString::Printf(TEXT("| %s | %s | %s | (%d, %d) | %d |\n"),
					*FAssetTextSnapshot::MarkdownCell(Node.Id),
					*FAssetTextSnapshot::MarkdownCell(Node.Title),
					*FAssetTextSnapshot::MarkdownCell(Node.ClassName),
					Node.PositionX,
					Node.PositionY,
					Node.Pins.Num());
			}

			Out += TEXT("\n#### 连线\n\n| 来源 | 目标 | 类型 |\n|------|------|------|\n");
			for (const FReadAllGraphLinkIR& Link : Graph.Links)
			{
				Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Link.FromNodeId + TEXT(".") + Link.FromPinId)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Link.ToNodeId + TEXT(".") + Link.ToPinId)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Link.Kind) + TEXT(" |\n");
			}
			if (Graph.Links.IsEmpty())
			{
				Out += TEXT("| (无连线) | | |\n");
			}
			Out += TEXT("\n");
		}
	}

	static TSharedRef<FJsonObject> MakePinJson(const FReadAllGraphPinIR& Pin)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Pin.Id);
		Json->SetStringField(TEXT("name"), Pin.Name);
		Json->SetStringField(TEXT("direction"), Pin.Direction);
		Json->SetStringField(TEXT("type"), Pin.Type);
		Json->SetStringField(TEXT("defaultValue"), Pin.DefaultValue);
		return Json;
	}

	static TSharedRef<FJsonObject> MakeNodeJson(const FReadAllGraphNodeIR& Node)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Node.Id);
		Json->SetStringField(TEXT("name"), Node.Name);
		Json->SetStringField(TEXT("className"), Node.ClassName);
		Json->SetStringField(TEXT("title"), Node.Title);
		Json->SetStringField(TEXT("comment"), Node.Comment);
		Json->SetNumberField(TEXT("positionX"), Node.PositionX);
		Json->SetNumberField(TEXT("positionY"), Node.PositionY);
		TArray<TSharedPtr<FJsonValue>> Pins;
		Pins.Reserve(Node.Pins.Num());
		for (const FReadAllGraphPinIR& Pin : Node.Pins)
		{
			Pins.Add(MakeShared<FJsonValueObject>(MakePinJson(Pin)));
		}
		Json->SetArrayField(TEXT("pins"), Pins);
		return Json;
	}

	static TSharedRef<FJsonObject> MakeGraphJson(const FReadAllGraphIR& Graph)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Graph.Id);
		Json->SetStringField(TEXT("name"), Graph.Name);
		Json->SetStringField(TEXT("kind"), Graph.Kind);

		TArray<TSharedPtr<FJsonValue>> Nodes;
		Nodes.Reserve(Graph.Nodes.Num());
		for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(MakeNodeJson(Node)));
		}
		Json->SetArrayField(TEXT("nodes"), Nodes);

		TArray<TSharedPtr<FJsonValue>> Links;
		Links.Reserve(Graph.Links.Num());
		for (const FReadAllGraphLinkIR& Link : Graph.Links)
		{
			TSharedRef<FJsonObject> LinkJson = MakeShared<FJsonObject>();
			LinkJson->SetStringField(TEXT("fromNodeId"), Link.FromNodeId);
			LinkJson->SetStringField(TEXT("fromPinId"), Link.FromPinId);
			LinkJson->SetStringField(TEXT("toNodeId"), Link.ToNodeId);
			LinkJson->SetStringField(TEXT("toPinId"), Link.ToPinId);
			LinkJson->SetStringField(TEXT("kind"), Link.Kind);
			Links.Add(MakeShared<FJsonValueObject>(LinkJson));
		}
		Json->SetArrayField(TEXT("links"), Links);
		return Json;
	}
}

FString FReadAllAssetDocumentIR::RenderMarkdown(const EReadAllExportMode Mode) const
{
	FString Out;
	Out += TEXT("# UE 资产速读：ReadAllandExplains\n\n");
	Out += TEXT("你好同学，下面先用美术能直接理解的方式概括 **") + AssetName + TEXT("**，再保留可供 AI 深挖的技术信息。\n\n");
	Out += TEXT("## 一眼看懂\n\n");
	Out += TEXT("- **资产类型：** ") + AssetKind + TEXT("\n");
	Out += TEXT("- **特征标签：** ") + (FeatureTags.IsEmpty() ? TEXT("general") : FString::Join(FeatureTags, TEXT("、"))) + TEXT("\n");
	Out += TEXT("- **美术关注点：** ") + ArtistFocus + TEXT("\n");
	Out += TEXT("- **建议追问：** “") + SuggestedPrompt + TEXT("”\n");
	Out += TEXT("- **当前导出模式：** ") + ReadAllExportModeToString(Mode) + TEXT("\n");
	Out += TEXT("- **使用建议：** 先看上面的参数线索和依赖关系；需要时再阅读后面的材质、蓝图或 Niagara 技术细节。\n\n---\n\n");

	ReadAllDocumentIRImpl::AppendParameterTable(ParameterClues, Out);
	ReadAllDocumentIRImpl::AppendRelationships(*this, Out);
	ReadAllDocumentIRImpl::AppendGraphIR(Graphs, Mode, Out);

	if (Mode != EReadAllExportMode::Artist && !TechnicalMarkdown.IsEmpty())
	{
		Out += TEXT("---\n\n## 技术细节（") + ReadAllExportModeToString(Mode) + TEXT("）\n\n");
		Out += TechnicalMarkdown;
		if (!Out.EndsWith(TEXT("\n"))) Out += TEXT("\n");
		Out += TEXT("\n");
	}

	Out += TEXT("---\n\n## 交给 AI 的下一步提示词\n\n```text\n");
	Out += TEXT("请阅读这份 ReadAllandExplains 导出文档，并基于实际数据回答。\n资产：") + ObjectPath + TEXT("\n");
	Out += SuggestedPrompt + TEXT("\n");
	Out += TEXT("不要臆测文档中不存在的连接；不确定时请明确指出需要回到 UE 核对的节点或参数。\n```\n");
	return Out;
}

FString FReadAllAssetDocumentIR::RenderMetadataJson(const EReadAllExportMode Mode) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 2);
	Root->SetStringField(TEXT("exportMode"), ReadAllExportModeToString(Mode));
	Root->SetStringField(TEXT("promptMode"), ReadAllPromptModeToString(PromptMode));
	Root->SetStringField(TEXT("assetName"), AssetName);
	Root->SetStringField(TEXT("objectPath"), ObjectPath);
	Root->SetStringField(TEXT("classPath"), ClassPath);
	Root->SetStringField(TEXT("assetKind"), AssetKind);
	Root->SetArrayField(TEXT("featureTags"), ReadAllDocumentIRImpl::MakeStringArray(FeatureTags));
	Root->SetStringField(TEXT("artistFocus"), ArtistFocus);
	Root->SetStringField(TEXT("suggestedPrompt"), SuggestedPrompt);
	Root->SetBoolField(TEXT("technicalMarkdownIncluded"), Mode != EReadAllExportMode::Artist);
	Root->SetNumberField(TEXT("technicalCharacterCount"), TechnicalMarkdown.Len());
	Root->SetArrayField(TEXT("dependencies"), ReadAllDocumentIRImpl::MakeStringArray(Dependencies));
	Root->SetArrayField(TEXT("referencers"), ReadAllDocumentIRImpl::MakeStringArray(Referencers));

	TArray<TSharedPtr<FJsonValue>> ParameterValues;
	ParameterValues.Reserve(ParameterClues.Num());
	for (const FReadAllParameterClue& Clue : ParameterClues)
	{
		TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
		Parameter->SetStringField(TEXT("name"), Clue.Name);
		Parameter->SetStringField(TEXT("kind"), Clue.Kind);
		Parameter->SetStringField(TEXT("value"), Clue.Value);
		ParameterValues.Add(MakeShared<FJsonValueObject>(Parameter));
	}
	Root->SetArrayField(TEXT("parameters"), ParameterValues);

	TArray<TSharedPtr<FJsonValue>> GraphValues;
	GraphValues.Reserve(Graphs.Num());
	for (const FReadAllGraphIR& Graph : Graphs)
	{
		GraphValues.Add(MakeShared<FJsonValueObject>(ReadAllDocumentIRImpl::MakeGraphJson(Graph)));
	}
	Root->SetArrayField(TEXT("graphs"), GraphValues);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}
