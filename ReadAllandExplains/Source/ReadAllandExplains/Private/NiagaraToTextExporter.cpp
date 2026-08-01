// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraToTextExporter.h"
#include "AssetTextSnapshot.h"

#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "UObject/Class.h"

namespace NiagaraTextExportImpl
{
	template <typename TEnum>
	static FString EnumToString(const TEnum Value)
	{
		if (const UEnum* Enum = StaticEnum<TEnum>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Value));
		}
		return FString::Printf(TEXT("%d"), static_cast<int32>(Value));
	}

	static const TCHAR* BoolText(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	static FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : TEXT("<null>");
	}

	static FString GuidText(const FGuid& Guid)
	{
		return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("<none>");
	}

	static FString ExportNiagaraVariableValue(
		const FNiagaraParameterStore& Store,
		const FNiagaraVariable& Variable)
	{
		const FNiagaraTypeDefinition& Type = Variable.GetType();
		if (Type.IsDataInterface())
		{
			return ObjectPath(Store.GetDataInterface(Variable));
		}
		if (Type.IsUObject())
		{
			return ObjectPath(Store.GetUObject(Variable));
		}

		const uint8* ParameterData = Store.GetParameterData(Variable);
		UScriptStruct* ScriptStruct = Type.GetScriptStruct();
		if (!ParameterData || !ScriptStruct)
		{
			return ParameterData ? TEXT("<raw value: unsupported type>") : TEXT("<unset>");
		}

		TArray<uint8> ConvertedData;
		ConvertedData.SetNumZeroed(FMath::Max(Variable.GetSizeInBytes(), ScriptStruct->GetStructureSize()));
		if (!Store.CopyParameterData(Variable, ConvertedData.GetData()))
		{
			return TEXT("<copy failed>");
		}

		FString Value;
		ScriptStruct->ExportText(
			Value,
			ConvertedData.GetData(),
			nullptr,
			nullptr,
			PPF_Copy | PPF_Delimited,
			nullptr);
		return Value.IsEmpty() ? TEXT("<empty>") : Value;
	}

	static void EmitParameterStore(
		const FNiagaraParameterStore& Store,
		const FString& Heading,
		FString& Out)
	{
		TArray<FNiagaraVariable> Variables;
		Store.GetParameters(Variables);
		Variables.Sort([](const FNiagaraVariable& A, const FNiagaraVariable& B)
		{
			return A.GetName().LexicalLess(B.GetName());
		});

		Out += FString::Printf(TEXT("### %s\n\n"), *Heading);
		Out += TEXT("| Name | Type | Value / Object |\n");
		Out += TEXT("|------|------|----------------|\n");
		for (const FNiagaraVariable& Variable : Variables)
		{
			Out += FString::Printf(
				TEXT("| %s | %s | %s |\n"),
				*FAssetTextSnapshot::MarkdownCell(Variable.GetName().ToString()),
				*FAssetTextSnapshot::MarkdownCell(Variable.GetType().GetNameText().ToString()),
				*FAssetTextSnapshot::MarkdownCell(ExportNiagaraVariableValue(Store, Variable)));
		}
		if (Variables.Num() == 0)
		{
			Out += TEXT("| (none) | | |\n");
		}
		Out += TEXT("\n");
	}

	static UNiagaraGraph* GetGraphFromSource(const UNiagaraScriptSourceBase* SourceBase)
	{
		const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(SourceBase);
		return Source ? Source->NodeGraph : nullptr;
	}

	static void BuildUpstreamTraversal(
		UNiagaraNode* Node,
		TSet<const UNiagaraNode*>& Visited,
		TArray<UNiagaraNode*>& OutTraversal)
	{
		if (!Node || Visited.Contains(Node)) return;
		Visited.Add(Node);

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin)
				{
					BuildUpstreamTraversal(Cast<UNiagaraNode>(LinkedPin->GetOwningNode()), Visited, OutTraversal);
				}
			}
		}
		OutTraversal.Add(Node);
	}

	static void EmitModuleStacks(UNiagaraGraph* Graph, FString& Out)
	{
		Out += TEXT("## Executable Module Stacks\n\n");
		if (!Graph)
		{
			Out += TEXT("- (source graph unavailable)\n\n");
			return;
		}

		TArray<UNiagaraNodeOutput*> Outputs;
		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UNiagaraNodeOutput* OutputNode = Cast<UNiagaraNodeOutput>(GraphNode))
			{
				Outputs.Add(OutputNode);
			}
		}
		Outputs.Sort([](const UNiagaraNodeOutput& A, const UNiagaraNodeOutput& B)
		{
			if (A.GetUsage() != B.GetUsage())
			{
				return static_cast<int32>(A.GetUsage()) < static_cast<int32>(B.GetUsage());
			}
			return A.GetUsageId().ToString() < B.GetUsageId().ToString();
		});

		for (UNiagaraNodeOutput* Output : Outputs)
		{
			if (!Output) continue;
			Out += FString::Printf(
				TEXT("### %s [%s]\n\n"),
				*EnumToString(Output->GetUsage()),
				*GuidText(Output->GetUsageId()));
			Out += TEXT("| Order | Module / Function | Enabled | Script / Signature | SelectedVersion | NodeGuid |\n");
			Out += TEXT("|------:|-------------------|:-------:|--------------------|-----------------|----------|\n");

			TArray<UNiagaraNode*> Traversal;
			TSet<const UNiagaraNode*> Visited;
			BuildUpstreamTraversal(Output, Visited, Traversal);
			int32 ModuleOrder = 0;
			for (UNiagaraNode* Node : Traversal)
			{
				UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node);
				if (!FunctionCall) continue;

				const FString Reference = FunctionCall->FunctionScript
					? FunctionCall->FunctionScript->GetPathName()
					: FunctionCall->Signature.Name.ToString();
				Out += FString::Printf(
					TEXT("| %d | %s | %s | %s | %s | %s |\n"),
					ModuleOrder++,
					*FAssetTextSnapshot::MarkdownCell(FunctionCall->GetFunctionName()),
					FunctionCall->GetDesiredEnabledState() == ENodeEnabledState::Disabled ? TEXT("false") : TEXT("true"),
					*FAssetTextSnapshot::MarkdownCell(Reference),
					*GuidText(FunctionCall->SelectedScriptVersion),
					*GuidText(FunctionCall->NodeGuid));
			}
			if (ModuleOrder == 0)
			{
				Out += TEXT("| 0 | (no function-call modules) | | | | |\n");
			}
			Out += TEXT("\n");
		}
	}

	static void EmitScriptVersion(
		UNiagaraScript* Script,
		const FNiagaraAssetVersion& Version,
		FString& Out)
	{
		if (!Script) return;
		const FGuid VersionGuid = Script->IsVersioningEnabled() ? Version.VersionGuid : FGuid();
		const FVersionedNiagaraScriptData* ScriptData = Script->GetScriptData(VersionGuid);
		const UNiagaraScriptSourceBase* SourceBase = Script->GetSource(VersionGuid);
		UNiagaraGraph* Graph = GetGraphFromSource(SourceBase);

		Out += FString::Printf(
			TEXT("## Script Version %d.%d [%s]\n\n"),
			Version.MajorVersion,
			Version.MinorVersion,
			*GuidText(Version.VersionGuid));
		Out += FString::Printf(TEXT("- IsExposedVersion: %s\n"),
			Script->GetExposedVersion().VersionGuid == Version.VersionGuid ? TEXT("true") : TEXT("false"));
		Out += TEXT("- Source: `") + ObjectPath(SourceBase) + TEXT("`\n\n");

		if (ScriptData)
		{
			Out += FAssetTextSnapshot::ExportStructProperties(
				FVersionedNiagaraScriptData::StaticStruct(),
				ScriptData,
				Script,
				TEXT("Version Metadata and Persistent Fields"));
		}
		EmitModuleStacks(Graph, Out);
		Out += FAssetTextSnapshot::ExportGraph(
			Graph,
			FString::Printf(TEXT("Niagara Source Graph %d.%d"), Version.MajorVersion, Version.MinorVersion));
	}

	static void EmitScript(UNiagaraScript* Script, const FString& Heading, FString& Out)
	{
		if (!Script)
		{
			Out += FString::Printf(TEXT("## %s\n\n- (null)\n\n"), *Heading);
			return;
		}

		Out += FString::Printf(TEXT("# %s\n\n"), *Heading);
		Out += TEXT("- Name: `") + Script->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + Script->GetPathName() + TEXT("`\n");
		Out += TEXT("- Usage: `") + EnumToString(Script->GetUsage()) + TEXT("`\n");
		Out += TEXT("- UsageId: `") + GuidText(Script->GetUsageId()) + TEXT("`\n");
		Out += FString::Printf(TEXT("- VersioningEnabled: %s\n\n"), BoolText(Script->IsVersioningEnabled()));
		Out += FAssetTextSnapshot::ExportObjectProperties(Script, TEXT("Script Persistent Properties"));
		EmitParameterStore(Script->RapidIterationParameters, TEXT("Rapid Iteration Parameters"), Out);

		TArray<FNiagaraAssetVersion> Versions = Script->GetAllAvailableVersions();
		if (Versions.Num() == 0)
		{
			FNiagaraAssetVersion Fallback;
			Fallback.VersionGuid = FGuid();
			Versions.Add(Fallback);
		}
		for (const FNiagaraAssetVersion& Version : Versions)
		{
			EmitScriptVersion(Script, Version, Out);
		}
	}

	static void EmitRenderer(
		const UNiagaraRendererProperties* Renderer,
		const int32 RendererIndex,
		FString& Out)
	{
		if (!Renderer) return;
		Out += FAssetTextSnapshot::ExportObjectProperties(
			Renderer,
			FString::Printf(TEXT("Renderer %d: %s"), RendererIndex, *Renderer->GetClass()->GetName()));
	}

	static void EmitEmitterVersion(
		const FVersionedNiagaraEmitter& VersionedEmitter,
		const FString& Heading,
		FString& Out)
	{
		UNiagaraEmitter* Emitter = VersionedEmitter.Emitter;
		FVersionedNiagaraEmitterData* Data = VersionedEmitter.GetEmitterData();
		Out += FString::Printf(TEXT("## %s\n\n"), *Heading);
		if (!Emitter || !Data)
		{
			Out += TEXT("- (invalid emitter version)\n\n");
			return;
		}

		Out += TEXT("- EmitterAsset: `") + Emitter->GetPathName() + TEXT("`\n");
		Out += TEXT("- VersionGuid: `") + GuidText(VersionedEmitter.Version) + TEXT("`\n");
		Out += TEXT("- SimTarget: `") + EnumToString(Data->SimTarget) + TEXT("`\n");
		Out += FString::Printf(TEXT("- LocalSpace: %s\n"), BoolText(Data->bLocalSpace));
		Out += FString::Printf(TEXT("- Determinism: %s\n"), BoolText(Data->bDeterminism));
		Out += FString::Printf(TEXT("- RequiresPersistentIDs: %s\n"), BoolText(Data->RequiresPersistentIDs()));
		Out += FString::Printf(TEXT("- RendererCount: %d\n"), Data->GetRenderers().Num());
		Out += FString::Printf(TEXT("- SimulationStageCount: %d\n\n"), Data->GetSimulationStages().Num());

		Out += FAssetTextSnapshot::ExportStructProperties(
			FVersionedNiagaraEmitterData::StaticStruct(),
			Data,
			Emitter,
			TEXT("Complete Versioned Emitter Data"));
		EmitParameterStore(Data->RendererBindings, TEXT("Renderer Bindings"), Out);

		Out += TEXT("## Renderers\n\n");
		for (int32 Index = 0; Index < Data->GetRenderers().Num(); ++Index)
		{
			EmitRenderer(Data->GetRenderers()[Index], Index, Out);
		}
		if (Data->GetRenderers().Num() == 0) Out += TEXT("- (none)\n\n");

		Out += TEXT("## Simulation Stages\n\n");
		for (int32 Index = 0; Index < Data->GetSimulationStages().Num(); ++Index)
		{
			if (const UNiagaraSimulationStageBase* Stage = Data->GetSimulationStages()[Index])
			{
				Out += FAssetTextSnapshot::ExportObjectProperties(
					Stage,
					FString::Printf(TEXT("Simulation Stage %d: %s"), Index, *Stage->GetName()));
			}
		}
		if (Data->GetSimulationStages().Num() == 0) Out += TEXT("- (none)\n\n");

		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts, false, false);
		for (int32 ScriptIndex = 0; ScriptIndex < Scripts.Num(); ++ScriptIndex)
		{
			UNiagaraScript* Script = Scripts[ScriptIndex];
			EmitScript(
				Script,
				FString::Printf(TEXT("Emitter Script %d: %s"), ScriptIndex, Script ? *EnumToString(Script->GetUsage()) : TEXT("<null>")),
				Out);
		}

		UNiagaraGraph* SharedGraph = GetGraphFromSource(Data->GraphSource);
		EmitModuleStacks(SharedGraph, Out);
		Out += FAssetTextSnapshot::ExportGraph(SharedGraph, TEXT("Emitter Shared Source Graph"));
	}

	static void EmitEmitterAsset(UNiagaraEmitter* Emitter, FString& Out)
	{
		if (!Emitter) return;
		Out += TEXT("# Niagara Emitter Export (for AI / reconstruction)\n\n");
		Out += TEXT("- Name: `") + Emitter->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + Emitter->GetPathName() + TEXT("`\n");
		Out += FString::Printf(TEXT("- VersioningEnabled: %s\n\n"), BoolText(Emitter->IsVersioningEnabled()));
		Out += FAssetTextSnapshot::ExportObjectProperties(Emitter, TEXT("Emitter Asset Persistent Properties"));

		TArray<FNiagaraAssetVersion> Versions = Emitter->GetAllAvailableVersions();
		if (Versions.Num() == 0)
		{
			Versions.Add(Emitter->GetExposedVersion());
		}
		for (const FNiagaraAssetVersion& Version : Versions)
		{
			const FGuid VersionGuid = Emitter->IsVersioningEnabled() ? Version.VersionGuid : FGuid();
			EmitEmitterVersion(
				FVersionedNiagaraEmitter(Emitter, VersionGuid),
				FString::Printf(TEXT("Emitter Version %d.%d [%s]"), Version.MajorVersion, Version.MinorVersion, *GuidText(Version.VersionGuid)),
				Out);
		}
	}

	static void EmitSystem(UNiagaraSystem* System, FString& Out)
	{
		if (!System) return;
		Out += TEXT("# Niagara System Export (for AI / reconstruction)\n\n");
		Out += TEXT("- Name: `") + System->GetName() + TEXT("`\n");
		Out += TEXT("- ObjectPath: `") + System->GetPathName() + TEXT("`\n");
		Out += FString::Printf(TEXT("- EmitterCount: %d\n"), System->GetEmitterHandles().Num());
		Out += FString::Printf(TEXT("- WarmupTime: %g\n"), System->GetWarmupTime());
		Out += FString::Printf(TEXT("- WarmupTickCount: %d\n"), System->GetWarmupTickCount());
		Out += FString::Printf(TEXT("- Determinism: %s\n"), BoolText(System->NeedsDeterminism()));
		Out += FString::Printf(TEXT("- RandomSeed: %d\n\n"), System->GetRandomSeed());
		Out += FAssetTextSnapshot::ExportObjectProperties(System, TEXT("System Persistent Properties"));
		EmitParameterStore(System->GetExposedParameters(), TEXT("User / Exposed Parameters"), Out);

		EmitScript(System->GetSystemSpawnScript(), TEXT("System Spawn Script"), Out);
		EmitScript(System->GetSystemUpdateScript(), TEXT("System Update Script"), Out);

		Out += TEXT("# Emitter Handles\n\n");
		for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
		{
			const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[Index];
			Out += FString::Printf(TEXT("## Handle %d: %s\n\n"), Index, *Handle.GetName().ToString());
			Out += TEXT("- Id: `") + GuidText(Handle.GetId()) + TEXT("`\n");
			Out += TEXT("- UniqueInstanceName: `") + Handle.GetUniqueInstanceName() + TEXT("`\n");
			Out += FString::Printf(TEXT("- Enabled: %s\n"), BoolText(Handle.GetIsEnabled()));
			Out += FString::Printf(TEXT("- Isolated: %s\n\n"), BoolText(Handle.IsIsolated()));
			EmitEmitterVersion(
				Handle.GetInstance(),
				FString::Printf(TEXT("Handle %d Versioned Emitter"), Index),
				Out);
		}
	}
}

using namespace NiagaraTextExportImpl;

FString FNiagaraToTextExporter::ExportNiagaraAssetToText(UObject* NiagaraAsset)
{
	if (!NiagaraAsset)
	{
		return FString();
	}

	FString Out;
	if (UNiagaraSystem* System = Cast<UNiagaraSystem>(NiagaraAsset))
	{
		EmitSystem(System, Out);
	}
	else if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(NiagaraAsset))
	{
		EmitEmitterAsset(Emitter, Out);
	}
	else if (UNiagaraScript* Script = Cast<UNiagaraScript>(NiagaraAsset))
	{
		Out += TEXT("# Niagara Script Export (for AI / reconstruction)\n\n");
		EmitScript(Script, TEXT("Niagara Script"), Out);
	}

	return Out;
}