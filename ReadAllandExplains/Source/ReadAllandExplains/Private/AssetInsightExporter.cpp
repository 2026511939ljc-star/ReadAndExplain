// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetInsightExporter.h"
#include "AssetTextSnapshot.h"
#include "ReadAllandExplainsSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/Blueprint.h"
#include "Engine/CurveTable.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Curves/RichCurve.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

namespace AssetInsightImpl
{
	static FString AssetKind(const UObject* Asset)
	{
		if (!Asset) return TEXT("未知资产");
		if (Asset->IsA<UBlueprint>()) return TEXT("蓝图 / 工具逻辑");
		if (Asset->IsA<UMaterialInterface>()) return TEXT("材质 / 画面表现");
		if (Asset->IsA<UMaterialFunctionInterface>()) return TEXT("材质函数 / 可复用表现逻辑");
		if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>()) return TEXT("Niagara / 特效表现");
		if (Asset->IsA<UStaticMesh>()) return TEXT("静态模型 / 场景或特效载体");
		if (Asset->IsA<UTexture>()) return TEXT("贴图 / 颜色、遮罩或流动数据");
		if (Asset->IsA<UDataTable>()) return TEXT("数据表 / 批量配置");
		if (Asset->IsA<UCurveTable>()) return TEXT("曲线表 / 随时间或数值变化");
		return Asset->GetClass()->GetName();
	}

	static FString ArtistConcerns(const UObject* Asset)
	{
		if (!Asset) return TEXT("先看资产用途、输入和输出。");
		if (Asset->IsA<UBlueprint>()) return TEXT("先看暴露参数、执行入口、生成了什么，以及它最终控制了哪些材质或特效。");
		if (Asset->IsA<UMaterialInterface>()) return TEXT("先看颜色、明暗、粗糙度、金属感、透明、法线，以及参数来自哪一级材质实例。");
		if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>()) return TEXT("先看发射数量、速度、生命周期、大小、颜色、Renderer 和对外暴露的 User 参数。");
		if (Asset->IsA<UStaticMesh>()) return TEXT("先看模型面数、LOD、UV、材质槽、碰撞，以及它是否适合场景或特效使用。");
		if (Asset->IsA<UTexture>()) return TEXT("先看尺寸、sRGB、压缩方式和用途；颜色图、法线、遮罩、Flow Map 的设置不能混用。");
		if (Asset->IsA<UDataTable>()) return TEXT("先看每一列代表什么、哪些行是预设，以及修改一行会影响哪些工具。");
		if (Asset->IsA<UCurveTable>()) return TEXT("先看曲线控制的是速度、大小、透明还是颜色，以及变化区间是否符合预期。");
		return TEXT("先看资产用途、输入、输出和直接引用关系。");
	}

	static FString MermaidLabel(FString Value)
	{
		Value.ReplaceInline(TEXT("\""), TEXT("'"));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Value;
	}

	static void SortUniqueClues(TArray<FReadAllParameterClue>& Clues)
	{
		Clues.Sort([](const FReadAllParameterClue& A, const FReadAllParameterClue& B)
		{
			const int32 NameCompare = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
			return NameCompare == 0 ? A.Kind < B.Kind : NameCompare < 0;
		});
		TSet<FString> Seen;
		Clues.RemoveAll([&Seen](const FReadAllParameterClue& Clue)
		{
			const FString Key = Clue.Name.ToLower() + TEXT("|") + Clue.Kind.ToLower();
			if (Seen.Contains(Key)) return true;
			Seen.Add(Key);
			return false;
		});
	}

	static FString FindFirstClue(const TArray<FReadAllParameterClue>& Clues, const TArray<FString>& Terms)
	{
		for (const FReadAllParameterClue& Clue : Clues)
		{
			const FString LowerName = Clue.Name.ToLower();
			for (const FString& Term : Terms)
			{
				if (LowerName.Contains(Term)) return Clue.Name;
			}
		}
		return FString();
	}

	static FString FindFirstUnusedClue(
		const TArray<FReadAllParameterClue>& Clues,
		const TArray<FString>& Terms,
		TSet<FString>& UsedNames)
	{
		for (const FString& Term : Terms)
		{
			for (const FReadAllParameterClue& Clue : Clues)
			{
				const FString LowerName = Clue.Name.ToLower();
				if (UsedNames.Contains(LowerName) || !LowerName.Contains(Term)) continue;
				UsedNames.Add(LowerName);
				return Clue.Name;
			}
		}
		return FString();
	}

	static bool HasFeature(const TArray<FString>& FeatureTags, const TCHAR* Feature)
	{
		return FeatureTags.ContainsByPredicate([Feature](const FString& Tag)
		{
			return Tag.Equals(Feature, ESearchCase::IgnoreCase);
		});
	}

	static bool ContainsAny(const FString& LowerText, const TArray<FString>& Terms)
	{
		for (const FString& Term : Terms)
		{
			if (LowerText.Contains(Term)) return true;
		}
		return false;
	}

	static void CollectFeatureTags(
		UObject* Asset,
		const TArray<FReadAllParameterClue>& Clues,
		const TArray<FReadAllGraphIR>& Graphs,
		const TArray<FString>& Dependencies,
		TArray<FString>& OutTags)
	{
		OutTags.Reset();
		if (!Asset) return;

		FString SearchText = Asset->GetPathName().ToLower();
		for (const FReadAllParameterClue& Clue : Clues)
		{
			SearchText += TEXT("|") + Clue.Name.ToLower() + TEXT("|") + Clue.Kind.ToLower() + TEXT("|") + Clue.Value.ToLower();
		}
		FString DependencyText;
		for (const FString& Dependency : Dependencies)
		{
			DependencyText += TEXT("|") + Dependency.ToLower();
		}
		for (const FReadAllGraphIR& Graph : Graphs)
		{
			SearchText += TEXT("|") + Graph.Name.ToLower() + TEXT("|") + Graph.Kind.ToLower();
			for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
			{
				SearchText += TEXT("|") + Node.ClassName.ToLower() + TEXT("|") + Node.Title.ToLower() + TEXT("|") + Node.Comment.ToLower();
			}
		}
		const FString ExtendedSearchText = SearchText + DependencyText;

		if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Asset))
		{
			OutTags.Add(TEXT("material"));
			if (Asset->IsA<UMaterialInstance>()) OutTags.Add(TEXT("material-instance"));
			if (UMaterial* Material = MaterialInterface->GetMaterial())
			{
				if (Material->MaterialDomain == MD_PostProcess) OutTags.Add(TEXT("post-process"));
				if (Material->MaterialDomain == MD_UI) OutTags.Add(TEXT("ui"));
				if (Material->BlendMode != BLEND_Opaque && Material->BlendMode != BLEND_Masked) OutTags.Add(TEXT("translucent"));
				for (int32 ModelIndex = 0; ModelIndex < static_cast<int32>(MSM_MAX); ++ModelIndex)
				{
					const EMaterialShadingModel Model = static_cast<EMaterialShadingModel>(ModelIndex);
					if (!Material->GetShadingModels().HasShadingModel(Model)) continue;
					const FString ModelName = StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(ModelIndex).ToLower();
					if (ModelName.Contains(TEXT("subsurface")) || ModelName.Contains(TEXT("preintegratedskin")))
					{
						OutTags.AddUnique(TEXT("skin"));
					}
				}
			}
		}
		else if (Asset->IsA<UMaterialFunctionInterface>()) OutTags.Add(TEXT("material-function"));
		else if (Asset->IsA<UBlueprint>()) OutTags.Add(TEXT("blueprint"));
		else if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>()) OutTags.Add(TEXT("niagara"));
		else if (Asset->IsA<UStaticMesh>()) OutTags.Add(TEXT("static-mesh"));
		else if (Asset->IsA<UTexture>()) OutTags.Add(TEXT("texture"));
		else if (Asset->IsA<UDataTable>()) OutTags.Add(TEXT("data-table"));
		else if (Asset->IsA<UCurveTable>()) OutTags.Add(TEXT("curve-table"));

		if (ExtendedSearchText.Contains(TEXT("fluidflux")))
		{
			OutTags.AddUnique(TEXT("fluidflux"));
			OutTags.AddUnique(TEXT("water"));
		}
		else if (ContainsAny(ExtendedSearchText, {TEXT("flow map"), TEXT("flowmap"), TEXT("flow_amount"), TEXT("flow amount"), TEXT("foam"), TEXT("water material")}))
		{
			OutTags.AddUnique(TEXT("water"));
		}
		if (ContainsAny(ExtendedSearchText, {TEXT("layerblend"), TEXT("matlayerblend"), TEXT("materialattributelayers"), TEXT("material layer")})) OutTags.AddUnique(TEXT("layered-material"));
		if (ContainsAny(SearchText, {TEXT("world position offset"), TEXT("worldpositionoffset"), TEXT("vertexdisplace"), TEXT("vertex offset")})) OutTags.AddUnique(TEXT("vertex-animation"));
		if (ContainsAny(ExtendedSearchText, {TEXT("emissive"), TEXT("glow"), TEXT("发光")})) OutTags.AddUnique(TEXT("emissive"));
		if (ContainsAny(ExtendedSearchText, {TEXT("vertexcolor"), TEXT("vertex color"), TEXT("顶点色")})) OutTags.AddUnique(TEXT("vertex-color"));
		if (ContainsAny(ExtendedSearchText, {TEXT("splash"), TEXT("spray"), TEXT("水花")})) OutTags.AddUnique(TEXT("splash"));
		if (!HasFeature(OutTags, TEXT("ui")) && ContainsAny(ExtendedSearchText, {TEXT("/ui/"), TEXT("/slate/"), TEXT("m_ui_"), TEXT("widget material")})) OutTags.AddUnique(TEXT("ui"));
		if (!HasFeature(OutTags, TEXT("skin")) && ContainsAny(ExtendedSearchText, {TEXT("/skin/"), TEXT("m_skin_"), TEXT("skin material")})) OutTags.AddUnique(TEXT("skin"));
		OutTags.Sort();
	}

	static TArray<FString> FirstClueNames(const TArray<FReadAllParameterClue>& Clues, const int32 Maximum)
	{
		TArray<FString> Names;
		for (const FReadAllParameterClue& Clue : Clues)
		{
			if (!Clue.Name.IsEmpty()) Names.AddUnique(Clue.Name);
			if (Names.Num() >= Maximum) break;
		}
		return Names;
	}

	static TArray<FString> SelectMaterialPromptClueNames(
		const TArray<FReadAllParameterClue>& Clues,
		const TArray<FString>& FeatureTags,
		const int32 Maximum)
	{
		TArray<FString> Names;
		TSet<FString> UsedNames;
		auto AddPreferred = [&Clues, &Names, &UsedNames, Maximum](const TArray<FString>& Terms)
		{
			if (Names.Num() >= Maximum) return;
			for (const FString& Term : Terms)
			{
				for (const FReadAllParameterClue& Clue : Clues)
				{
					const FString LowerName = Clue.Name.ToLower();
					if (Clue.Name.IsEmpty() || UsedNames.Contains(LowerName) || !LowerName.Contains(Term)) continue;
					UsedNames.Add(LowerName);
					Names.Add(Clue.Name);
					return;
				}
			}
		};

		AddPreferred({TEXT("base color"), TEXT("basecolor"), TEXT("albedo"), TEXT("diffuse"), TEXT("color"), TEXT("colour"), TEXT("tint"), TEXT("颜色")});
		AddPreferred({TEXT("roughness"), TEXT("rough"), TEXT("粗糙")});
		AddPreferred({TEXT("metallic"), TEXT("metalness"), TEXT("metallness"), TEXT("metal roughness"), TEXT("metal"), TEXT("金属")});
		if (HasFeature(FeatureTags, TEXT("emissive")))
		{
			AddPreferred({TEXT("emissive"), TEXT("hdr"), TEXT("glow"), TEXT("发光")});
		}
		if (HasFeature(FeatureTags, TEXT("vertex-animation")))
		{
			AddPreferred({TEXT("vertexdisplace"), TEXT("vertex displace"), TEXT("world position offset"), TEXT("worldpositionoffset"), TEXT("displace"), TEXT("offset")});
		}
		AddPreferred({TEXT("flickerspeed"), TEXT("speed"), TEXT("timescale"), TEXT("time scale"), TEXT("velocity"), TEXT("flow"), TEXT("panner"), TEXT("流动"), TEXT("速度")});
		AddPreferred({TEXT("normal"), TEXT("bump"), TEXT("法线")});
		AddPreferred({TEXT("opacity"), TEXT("transparen"), TEXT("fade"), TEXT("alpha"), TEXT("透明")});
		if (!HasFeature(FeatureTags, TEXT("emissive")))
		{
			AddPreferred({TEXT("emissive"), TEXT("hdr"), TEXT("glow"), TEXT("发光")});
		}
		AddPreferred({TEXT("mask"), TEXT("遮罩")});

		return Names.Num() > 0 ? Names : FirstClueNames(Clues, Maximum);
	}

	static FString BuildDynamicArtistFocus(
		UObject* Asset,
		const TArray<FReadAllParameterClue>& Clues,
		const TArray<FString>& FeatureTags)
	{
		if (!Asset) return TEXT("先确认资产用途、输入和输出。");

		FString FeatureLead;
		if (HasFeature(FeatureTags, TEXT("fluidflux"))) FeatureLead = TEXT("这是 FluidFlux 水体相关资产，先确认流向、速度、深度、泡沫与法线是否协调。");
		else if (HasFeature(FeatureTags, TEXT("water"))) FeatureLead = TEXT("这是水体或流动表现资产，先确认 Flow Map、流速、泡沫、透明与表面法线。");
		else if (HasFeature(FeatureTags, TEXT("skin"))) FeatureLead = TEXT("这是皮肤/次表面材质，先确认肤色、粗糙度、法线、Opacity 与 Subsurface Profile。");
		else if (HasFeature(FeatureTags, TEXT("post-process"))) FeatureLead = TEXT("这是后处理材质，先确认屏幕空间输入、混合位置、曝光与输出范围。");
		else if (HasFeature(FeatureTags, TEXT("ui"))) FeatureLead = TEXT("这是 UI 材质，先确认颜色、透明、遮罩、UV 与 Slate/UMG 使用方式。");
		else if (HasFeature(FeatureTags, TEXT("layered-material"))) FeatureLead = TEXT("这是分层材质，先确认各层用途、混合顺序与遮罩通道。");

		if (Asset->IsA<UMaterialInterface>() || Asset->IsA<UMaterialFunctionInterface>())
		{
			TArray<FString> FocusParts;
			TSet<FString> UsedNames;
			auto AddFocus = [&FocusParts, &UsedNames, &Clues](const TArray<FString>& Terms, const TCHAR* Label)
			{
				const FString Name = FindFirstUnusedClue(Clues, Terms, UsedNames);
				if (!Name.IsEmpty()) FocusParts.Add(FString::Printf(TEXT("`%s`（%s）"), *Name, Label));
			};
			AddFocus({TEXT("basecolor"), TEXT("base color"), TEXT("albedo"), TEXT("color"), TEXT("colour"), TEXT("tint"), TEXT("颜色")}, TEXT("颜色/色调"));
			AddFocus({TEXT("roughness"), TEXT("rough"), TEXT("粗糙")}, TEXT("粗糙与高光"));
			AddFocus({TEXT("metallic"), TEXT("metalness"), TEXT("metallness"), TEXT("metal roughness"), TEXT("metal"), TEXT("金属")}, TEXT("金属感"));
			AddFocus({TEXT("opacity"), TEXT("alpha"), TEXT("transparen"), TEXT("透明")}, TEXT("透明/遮罩"));
			AddFocus({TEXT("maskemissive"), TEXT("emissive"), TEXT("hdr"), TEXT("glow"), TEXT("发光")}, TEXT("自发光"));
			if (HasFeature(FeatureTags, TEXT("vertex-animation")))
			{
				AddFocus({TEXT("vertexdisplace"), TEXT("vertex displace"), TEXT("world position offset"), TEXT("worldpositionoffset"), TEXT("displace"), TEXT("offset")}, TEXT("顶点位移"));
			}
			AddFocus({TEXT("flickerspeed"), TEXT("speed"), TEXT("timescale"), TEXT("time scale"), TEXT("velocity"), TEXT("flow"), TEXT("panner"), TEXT("流动"), TEXT("速度")}, TEXT("流动速度"));
			AddFocus({TEXT("foam"), TEXT("泡沫")}, TEXT("泡沫"));
			AddFocus({TEXT("normaldetail"), TEXT("normal"), TEXT("bump"), TEXT("法线")}, TEXT("表面细节"));
			if (FocusParts.Num() > 0)
			{
				const FString ParameterFocus = TEXT("根据实际参数，优先检查 ") + FString::Join(FocusParts, TEXT("、")) + TEXT("。");
				return FeatureLead.IsEmpty() ? ParameterFocus : FeatureLead + ParameterFocus;
			}
		}

		const TArray<FString> Names = FirstClueNames(Clues, 5);
		if (Names.Num() > 0)
		{
			const FString ParameterFocus = TEXT("这个资产实际公开的关键入口是：`") + FString::Join(Names, TEXT("`、`")) + TEXT("`。先从这些参数确认变化是否符合预期。");
			return FeatureLead.IsEmpty() ? ParameterFocus : FeatureLead + ParameterFocus;
		}
		return FeatureLead.IsEmpty() ? ArtistConcerns(Asset) : FeatureLead;
	}

	static FString BuildDynamicPrompt(
		UObject* Asset,
		const TArray<FReadAllParameterClue>& Clues,
		const TArray<FString>& FeatureTags,
		const UReadAllandExplainsSettings* Settings)
	{
		if (Settings && Settings->PromptMode == EReadAllPromptMode::Custom && !Settings->CustomPrompt.TrimStartAndEnd().IsEmpty())
		{
			return Settings->CustomPrompt.TrimStartAndEnd();
		}

		const bool bIsMaterialAsset = Asset && (Asset->IsA<UMaterialInterface>() || Asset->IsA<UMaterialFunctionInterface>());
		const TArray<FString> Names = bIsMaterialAsset
			? SelectMaterialPromptClueNames(Clues, FeatureTags, 6)
			: FirstClueNames(Clues, 6);
		const FString NamedContext = Names.Num() > 0
			? TEXT("请重点结合参数 `") + FString::Join(Names, TEXT("`、`")) + TEXT("`。")
			: FString();

		FString TaskLead;
		switch (Settings ? Settings->PromptMode : EReadAllPromptMode::Explain)
		{
		case EReadAllPromptMode::Review: TaskLead = TEXT("请检查这个资产是否存在参数命名、重复逻辑、异常设置或容易误用的地方。"); break;
		case EReadAllPromptMode::Optimize: TaskLead = TEXT("请找出最值得优化的三项，先说明画面影响，再说明性能或维护收益。"); break;
		case EReadAllPromptMode::Trace: TaskLead = TEXT("请追踪关键参数从哪里进入、经过什么节点或函数、最后影响哪个画面结果。"); break;
		case EReadAllPromptMode::Explain:
		default: TaskLead = TEXT("请用美术同学能直接理解的语言解释这个资产怎样产生当前画面。"); break;
		}

		if (bIsMaterialAsset)
		{
			const FString Speed = FindFirstClue(Clues, {TEXT("speed"), TEXT("velocity"), TEXT("flow"), TEXT("panner"), TEXT("流动"), TEXT("速度")});
			const FString Foam = FindFirstClue(Clues, {TEXT("foam"), TEXT("泡沫")});
			FString Extra;
			if (HasFeature(FeatureTags, TEXT("fluidflux"))) Extra = TEXT("这是 FluidFlux 水体相关资产。请重点追踪流向/速度、深度、泡沫、法线与材质输出之间的真实连接。");
			else if (HasFeature(FeatureTags, TEXT("water"))) Extra = TEXT("这是水体或流动材质。请重点分析 Flow Map、Panner、泡沫、透明度与法线如何共同形成运动感。");
			else if (HasFeature(FeatureTags, TEXT("skin"))) Extra = TEXT("这是皮肤/次表面材质。请重点分析肤色、粗糙度、微法线、Opacity 和 Subsurface Profile 的配合。");
			else if (HasFeature(FeatureTags, TEXT("post-process"))) Extra = TEXT("这是后处理材质。请重点分析 SceneTexture/屏幕 UV、Blendable 位置、曝光与最终颜色输出。");
			else if (HasFeature(FeatureTags, TEXT("ui"))) Extra = TEXT("这是 UI 材质。请重点分析颜色、透明、遮罩、UV 以及在 Slate/UMG 中的显示边界。");
			else if (HasFeature(FeatureTags, TEXT("layered-material"))) Extra = TEXT("这是分层材质。请列出各材质层、混合顺序、遮罩来源及最终影响的材质属性。");
			else Extra = TEXT("指出哪些参数控制颜色、明暗、质感和贴图混合。");
			if (!Speed.IsEmpty()) Extra += TEXT("并说明怎样通过 `") + Speed + TEXT("` 调整流动节奏。");
			if (!Foam.IsEmpty()) Extra += TEXT("说明 `") + Foam + TEXT("` 对泡沫形态的影响。");
			return TaskLead + NamedContext + Extra;
		}
		if (Asset && (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>()))
		{
			FString Extra = TEXT("说明粒子怎样生成、运动、消失，以及由哪个 Renderer 显示。");
			if (HasFeature(FeatureTags, TEXT("splash"))) Extra += TEXT("这是水花/飞沫效果，请重点检查生成区域、初速度、生命周期和碰撞后的消亡。");
			if (HasFeature(FeatureTags, TEXT("vertex-color"))) Extra += TEXT("请追踪 Static Mesh 采样得到的顶点色通道、阈值判断和粒子生成区域之间的连接。");
			return TaskLead + NamedContext + Extra;
		}
		if (Asset && Asset->IsA<UBlueprint>())
		{
			return TaskLead + NamedContext + TEXT("说明执行入口、数据传递和最终修改或生成的资产。");
		}
		return TaskLead + NamedContext + TEXT("给出三条可以直接操作的建议。");
	}

	static void CollectRelationships(const FAssetData& AssetData, TArray<FString>& OutDependencies, TArray<FString>& OutReferencers)
	{
		FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Registry = Module.Get();
		TArray<FName> Dependencies;
		TArray<FName> Referencers;
		Registry.GetDependencies(AssetData.PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		Registry.GetReferencers(AssetData.PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		Dependencies.Sort(FNameLexicalLess());
		Referencers.Sort(FNameLexicalLess());
		for (const FName Name : Dependencies) OutDependencies.Add(Name.ToString());
		for (const FName Name : Referencers) OutReferencers.Add(Name.ToString());
	}

	static FString CleanGraphText(FString Value)
	{
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Value.TrimStartAndEnd();
	}

	static FString StableObjectId(const FGuid& Guid, const UObject* Object, const TCHAR* Prefix)
	{
		if (Guid.IsValid())
		{
			return Guid.ToString(EGuidFormats::DigitsWithHyphens);
		}
		return FString(Prefix) + (Object ? Object->GetPathName() : TEXT("unknown"));
	}

	static FString MaterialPinId(const FString& NodeId, const TCHAR* Direction, const int32 Index)
	{
		return FString::Printf(TEXT("%s:%s:%d"), *NodeId, Direction, Index);
	}

	static void SortGraph(FReadAllGraphIR& Graph)
	{
		Graph.Nodes.Sort([](const FReadAllGraphNodeIR& A, const FReadAllGraphNodeIR& B)
		{
			return A.Id < B.Id;
		});
		Graph.Links.Sort([](const FReadAllGraphLinkIR& A, const FReadAllGraphLinkIR& B)
		{
			const FString AKey = A.FromNodeId + TEXT("|") + A.FromPinId + TEXT("|") + A.ToNodeId + TEXT("|") + A.ToPinId;
			const FString BKey = B.FromNodeId + TEXT("|") + B.FromPinId + TEXT("|") + B.ToNodeId + TEXT("|") + B.ToPinId;
			return AKey < BKey;
		});
	}

	static void CollectMaterialGraph(
		const FString& GraphName,
		const FString& GraphId,
		const FString& GraphKind,
		const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions,
		UMaterial* RootMaterial,
		TArray<FReadAllGraphIR>& OutGraphs)
	{
		FReadAllGraphIR Graph;
		Graph.Id = GraphId;
		Graph.Name = GraphName;
		Graph.Kind = GraphKind;

		TMap<const UMaterialExpression*, FString> NodeIds;
		for (const UMaterialExpression* Expression : Expressions)
		{
			if (Expression)
			{
				NodeIds.Add(Expression, StableObjectId(Expression->MaterialExpressionGuid, Expression, TEXT("material-node:")));
			}
		}

		for (const UMaterialExpression* Expression : Expressions)
		{
			if (!Expression) continue;
			UMaterialExpression* MutableExpression = const_cast<UMaterialExpression*>(Expression);
			const FString NodeId = NodeIds.FindChecked(Expression);

			FReadAllGraphNodeIR Node;
			Node.Id = NodeId;
			Node.Name = Expression->GetName();
			Node.ClassName = Expression->GetClass()->GetName();
			Node.Title = CleanGraphText(Expression->GetDescription());
			if (Node.Title.IsEmpty()) Node.Title = Node.Name;
			if (const UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				Node.Title = TEXT("Material Function: ") + (FunctionCall->MaterialFunction ? FunctionCall->MaterialFunction->GetName() : TEXT("<未指定>"));
			}
			else if (const UMaterialExpressionNamedRerouteDeclaration* Declaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expression))
			{
				Node.Title = TEXT("Named Reroute: ") + Declaration->Name.ToString();
			}
			else if (const UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression))
			{
				Node.Title = TEXT("Named Reroute Usage: ") + (IsValid(Usage->Declaration) ? Usage->Declaration->Name.ToString() : TEXT("<无效声明>"));
			}
			Node.Comment = CleanGraphText(Expression->Desc);
			Node.PositionX = Expression->MaterialExpressionEditorX;
			Node.PositionY = Expression->MaterialExpressionEditorY;

			const int32 InputCount = MutableExpression->CountInputs();
			for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
			{
				const FExpressionInput* Input = MutableExpression->GetInput(InputIndex);
				FReadAllGraphPinIR Pin;
				Pin.Id = MaterialPinId(NodeId, TEXT("in"), InputIndex);
				Pin.Name = MutableExpression->GetInputName(InputIndex).ToString();
				if (Pin.Name.IsEmpty()) Pin.Name = FString::Printf(TEXT("Input%d"), InputIndex);
				Pin.Direction = TEXT("Input");
				Pin.Type = FString::Printf(TEXT("MaterialValueType:%d"), static_cast<int32>(MutableExpression->GetInputValueType(InputIndex)));
				Pin.DefaultValue = Input && Input->Expression ? FString() : TEXT("<default>");
				Node.Pins.Add(MoveTemp(Pin));
			}

			TArray<FExpressionOutput>& Outputs = MutableExpression->GetOutputs();
			for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
			{
				FReadAllGraphPinIR Pin;
				Pin.Id = MaterialPinId(NodeId, TEXT("out"), OutputIndex);
				Pin.Name = Outputs[OutputIndex].OutputName.ToString();
				if (Pin.Name.IsEmpty()) Pin.Name = FString::Printf(TEXT("Output%d"), OutputIndex);
				Pin.Direction = TEXT("Output");
				Pin.Type = FString::Printf(TEXT("MaterialValueType:%d"), static_cast<int32>(MutableExpression->GetOutputValueType(OutputIndex)));
				Node.Pins.Add(MoveTemp(Pin));
			}

			if (const UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression))
			{
				if (IsValid(Usage->Declaration))
				{
					Node.Pins.Add({NodeId + TEXT(":named-declaration"), TEXT("Declaration"), TEXT("Input"), TEXT("NamedReroute"), Usage->Declaration->Name.ToString()});
				}
			}
			Graph.Nodes.Add(MoveTemp(Node));
		}

		for (const UMaterialExpression* Expression : Expressions)
		{
			if (!Expression) continue;
			UMaterialExpression* MutableExpression = const_cast<UMaterialExpression*>(Expression);
			const FString ConsumerId = NodeIds.FindChecked(Expression);
			const int32 InputCount = MutableExpression->CountInputs();
			for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
			{
				const FExpressionInput* Input = MutableExpression->GetInput(InputIndex);
				if (!Input || !Input->Expression) continue;
				const FString* ProducerId = NodeIds.Find(Input->Expression);
				if (!ProducerId) continue;
				const bool bNamedReroute = Input->Expression->IsA<UMaterialExpressionNamedRerouteBase>();
				Graph.Links.Add({
					*ProducerId,
					MaterialPinId(*ProducerId, TEXT("out"), Input->OutputIndex),
					ConsumerId,
					MaterialPinId(ConsumerId, TEXT("in"), InputIndex),
					bNamedReroute ? TEXT("named-reroute") : TEXT("data")});
			}

			if (const UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression))
			{
				if (IsValid(Usage->Declaration))
				{
					if (const FString* DeclarationId = NodeIds.Find(Usage->Declaration))
					{
						Graph.Links.Add({
							*DeclarationId,
							MaterialPinId(*DeclarationId, TEXT("out"), 0),
							ConsumerId,
							ConsumerId + TEXT(":named-declaration"),
							TEXT("named-reroute")});
					}
				}
			}
		}

		if (RootMaterial)
		{
			FReadAllGraphNodeIR RootNode;
			RootNode.Id = RootMaterial->GetPathName() + TEXT(":material-root");
			RootNode.Name = RootMaterial->GetName();
			RootNode.ClassName = TEXT("MaterialRoot");
			RootNode.Title = TEXT("最终材质属性");
			for (int32 PropertyIndex = 0; PropertyIndex < static_cast<int32>(MP_MAX); ++PropertyIndex)
			{
				const EMaterialProperty Property = static_cast<EMaterialProperty>(PropertyIndex);
				const FExpressionInput* Input = RootMaterial->GetExpressionInputForProperty(Property);
				if (!Input || !Input->Expression) continue;
				const FString PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(Property);
				const FString RootPinId = MaterialPinId(RootNode.Id, TEXT("in"), PropertyIndex);
				RootNode.Pins.Add({RootPinId, PropertyName, TEXT("Input"), TEXT("MaterialProperty"), FString()});
				if (const FString* ProducerId = NodeIds.Find(Input->Expression))
				{
					Graph.Links.Add({
						*ProducerId,
						MaterialPinId(*ProducerId, TEXT("out"), Input->OutputIndex),
						RootNode.Id,
						RootPinId,
						Input->Expression->IsA<UMaterialExpressionNamedRerouteBase>() ? TEXT("named-reroute-root") : TEXT("material-root")});
				}
			}
			Graph.Nodes.Add(MoveTemp(RootNode));
		}

		SortGraph(Graph);
		OutGraphs.Add(MoveTemp(Graph));
	}

	static FString BlueprintPinId(const FString& NodeId, const UEdGraphPin* Pin, const int32 PinIndex)
	{
		return Pin && Pin->PinId.IsValid()
			? Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens)
			: FString::Printf(TEXT("%s:pin:%d"), *NodeId, PinIndex);
	}

	static FString NiagaraPinType(const UEdGraphPin* Pin)
	{
		if (!Pin) return FString();
		FString Type = Pin->PinType.PinCategory.ToString();
		if (!Pin->PinType.PinSubCategory.IsNone())
		{
			Type += TEXT(":") + Pin->PinType.PinSubCategory.ToString();
		}
		if (const UObject* TypeObject = Pin->PinType.PinSubCategoryObject.Get())
		{
			Type += TEXT(":") + TypeObject->GetPathName();
		}
		return Type;
	}

	static void CollectBlueprintGraphs(const UBlueprint* Blueprint, TArray<FReadAllGraphIR>& OutGraphs)
	{
		if (!Blueprint) return;
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		Graphs.RemoveAll([](const UEdGraph* Graph) { return Graph == nullptr; });
		Graphs.Sort([](const UEdGraph& A, const UEdGraph& B) { return A.GetPathName() < B.GetPathName(); });

		for (UEdGraph* SourceGraph : Graphs)
		{
			FReadAllGraphIR Graph;
			Graph.Id = SourceGraph->GetPathName();
			Graph.Name = SourceGraph->GetName();
			Graph.Kind = TEXT("BlueprintGraph");

			TMap<const UEdGraphNode*, FString> NodeIds;
			for (const UEdGraphNode* Node : SourceGraph->Nodes)
			{
				if (Node) NodeIds.Add(Node, StableObjectId(Node->NodeGuid, Node, TEXT("blueprint-node:")));
			}

			for (const UEdGraphNode* Node : SourceGraph->Nodes)
			{
				if (!Node) continue;
				const FString NodeId = NodeIds.FindChecked(Node);
				FReadAllGraphNodeIR NodeIR;
				NodeIR.Id = NodeId;
				NodeIR.Name = Node->GetName();
				NodeIR.ClassName = Node->GetClass()->GetName();
				NodeIR.Title = CleanGraphText(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
				if (NodeIR.Title.IsEmpty()) NodeIR.Title = NodeIR.Name;
				NodeIR.Comment = CleanGraphText(Node->NodeComment);
				NodeIR.PositionX = Node->NodePosX;
				NodeIR.PositionY = Node->NodePosY;
				for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
				{
					const UEdGraphPin* Pin = Node->Pins[PinIndex];
					if (!Pin) continue;
					FReadAllGraphPinIR PinIR;
					PinIR.Id = BlueprintPinId(NodeId, Pin, PinIndex);
					PinIR.Name = Pin->PinName.ToString();
					PinIR.Direction = Pin->Direction == EGPD_Output ? TEXT("Output") : TEXT("Input");
					PinIR.Type = UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString();
					PinIR.DefaultValue = Pin->GetDefaultAsString();
					NodeIR.Pins.Add(MoveTemp(PinIR));
				}
				Graph.Nodes.Add(MoveTemp(NodeIR));
			}

			for (const UEdGraphNode* Node : SourceGraph->Nodes)
			{
				if (!Node) continue;
				const FString SourceNodeId = NodeIds.FindChecked(Node);
				for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
				{
					const UEdGraphPin* Pin = Node->Pins[PinIndex];
					if (!Pin || Pin->Direction != EGPD_Output) continue;
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (!LinkedPin) continue;
						const UEdGraphNode* TargetNode = LinkedPin->GetOwningNode();
						const FString* TargetNodeId = NodeIds.Find(TargetNode);
						if (!TargetNodeId) continue;
						const int32 TargetPinIndex = TargetNode->Pins.IndexOfByKey(const_cast<UEdGraphPin*>(LinkedPin));
						Graph.Links.Add({
							SourceNodeId,
							BlueprintPinId(SourceNodeId, Pin, PinIndex),
							*TargetNodeId,
							BlueprintPinId(*TargetNodeId, LinkedPin, TargetPinIndex),
							Pin->PinType.PinCategory.ToString().Equals(TEXT("exec"), ESearchCase::IgnoreCase) ? TEXT("exec") : TEXT("data")});
					}
				}
			}

			SortGraph(Graph);
			OutGraphs.Add(MoveTemp(Graph));
		}
	}

	static UNiagaraGraph* GetNiagaraGraphFromSource(const UNiagaraScriptSourceBase* SourceBase)
	{
		const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(SourceBase);
		return Source ? Source->NodeGraph : nullptr;
	}

	static void CollectNiagaraSourceGraph(
		UNiagaraGraph* SourceGraph,
		const FString& GraphKind,
		TSet<FString>& CollectedGraphIds,
		TArray<FReadAllGraphIR>& OutGraphs,
		const int32 RecursionDepth = 0)
	{
		if (!SourceGraph) return;
		const FString GraphId = SourceGraph->GetPathName();
		if (CollectedGraphIds.Contains(GraphId)) return;
		CollectedGraphIds.Add(GraphId);

		FReadAllGraphIR Graph;
		Graph.Id = GraphId;
		Graph.Name = SourceGraph->GetName();
		Graph.Kind = GraphKind;

		TMap<const UEdGraphNode*, FString> NodeIds;
		TArray<UNiagaraGraph*> ProjectCalleeGraphs;
		for (const UEdGraphNode* Node : SourceGraph->Nodes)
		{
			if (Node) NodeIds.Add(Node, StableObjectId(Node->NodeGuid, Node, TEXT("niagara-node:")));
		}

		for (const UEdGraphNode* Node : SourceGraph->Nodes)
		{
			if (!Node) continue;
			const FString NodeId = NodeIds.FindChecked(Node);
			FReadAllGraphNodeIR NodeIR;
			NodeIR.Id = NodeId;
			NodeIR.Name = Node->GetName();
			NodeIR.ClassName = Node->GetClass()->GetName();
			NodeIR.Title = CleanGraphText(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
			if (const UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				NodeIR.Title = FunctionCall->GetFunctionName();
				NodeIR.bEnabled = FunctionCall->GetDesiredEnabledState() != ENodeEnabledState::Disabled;
				NodeIR.SelectedVersion = FunctionCall->SelectedScriptVersion.IsValid()
					? FunctionCall->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens)
					: FString();
				if (FunctionCall->FunctionScript)
				{
					NodeIR.ReferencePath = FunctionCall->FunctionScript->GetPathName();
					NodeIR.Comment = NodeIR.ReferencePath;
					if (RecursionDepth < 4 && NodeIR.ReferencePath.StartsWith(TEXT("/Game/")))
					{
						if (UNiagaraGraph* CalledGraph = FunctionCall->GetCalledGraph())
						{
							NodeIR.CalleeGraphId = CalledGraph->GetPathName();
							ProjectCalleeGraphs.AddUnique(CalledGraph);
						}
					}
				}
				else
				{
					NodeIR.ReferencePath = FunctionCall->Signature.Name.ToString();
				}
			}
			if (NodeIR.Title.IsEmpty()) NodeIR.Title = NodeIR.Name;
			if (NodeIR.Comment.IsEmpty()) NodeIR.Comment = CleanGraphText(Node->NodeComment);
			NodeIR.PositionX = Node->NodePosX;
			NodeIR.PositionY = Node->NodePosY;

			for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
			{
				const UEdGraphPin* Pin = Node->Pins[PinIndex];
				if (!Pin) continue;
				FReadAllGraphPinIR PinIR;
				PinIR.Id = BlueprintPinId(NodeId, Pin, PinIndex);
				PinIR.Name = Pin->PinName.ToString();
				PinIR.Direction = Pin->Direction == EGPD_Output ? TEXT("Output") : TEXT("Input");
				PinIR.Type = NiagaraPinType(Pin);
				PinIR.DefaultValue = Pin->GetDefaultAsString();
				NodeIR.Pins.Add(MoveTemp(PinIR));
			}
			Graph.Nodes.Add(MoveTemp(NodeIR));
		}

		for (const UEdGraphNode* Node : SourceGraph->Nodes)
		{
			if (!Node) continue;
			const FString SourceNodeId = NodeIds.FindChecked(Node);
			for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
			{
				const UEdGraphPin* Pin = Node->Pins[PinIndex];
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					const UEdGraphNode* TargetNode = LinkedPin->GetOwningNode();
					const FString* TargetNodeId = NodeIds.Find(TargetNode);
					if (!TargetNodeId) continue;
					const int32 TargetPinIndex = TargetNode->Pins.IndexOfByKey(const_cast<UEdGraphPin*>(LinkedPin));
					if (TargetPinIndex == INDEX_NONE) continue;
					const FString SourceType = NiagaraPinType(Pin);
					const FString TargetType = NiagaraPinType(LinkedPin);
					const bool bParameterMap = SourceType.Contains(TEXT("ParameterMap"), ESearchCase::IgnoreCase)
						|| TargetType.Contains(TEXT("ParameterMap"), ESearchCase::IgnoreCase);
					Graph.Links.Add({
						SourceNodeId,
						BlueprintPinId(SourceNodeId, Pin, PinIndex),
						*TargetNodeId,
						BlueprintPinId(*TargetNodeId, LinkedPin, TargetPinIndex),
						bParameterMap ? TEXT("parameter-map") : TEXT("data")});

				}
			}
		}

		SortGraph(Graph);
		OutGraphs.Add(MoveTemp(Graph));
		for (UNiagaraGraph* CalleeGraph : ProjectCalleeGraphs)
		{
			CollectNiagaraSourceGraph(CalleeGraph, TEXT("NiagaraProjectModuleGraph"), CollectedGraphIds, OutGraphs, RecursionDepth + 1);
		}
	}

	static void CollectNiagaraScriptGraphs(
		UNiagaraScript* Script,
		const FString& GraphKind,
		TSet<FString>& CollectedGraphIds,
		TArray<FReadAllGraphIR>& OutGraphs)
	{
		if (!Script) return;
		TArray<FNiagaraAssetVersion> Versions = Script->GetAllAvailableVersions();
		if (Versions.IsEmpty())
		{
			FNiagaraAssetVersion Fallback;
			Fallback.VersionGuid = FGuid();
			Versions.Add(Fallback);
		}
		for (const FNiagaraAssetVersion& Version : Versions)
		{
			const FGuid VersionGuid = Script->IsVersioningEnabled() ? Version.VersionGuid : FGuid();
			CollectNiagaraSourceGraph(GetNiagaraGraphFromSource(Script->GetSource(VersionGuid)), GraphKind, CollectedGraphIds, OutGraphs);
		}
	}

	static void CollectNiagaraEmitterVersionGraphs(
		const FVersionedNiagaraEmitter& VersionedEmitter,
		TSet<FString>& CollectedGraphIds,
		TArray<FReadAllGraphIR>& OutGraphs)
	{
		FVersionedNiagaraEmitterData* Data = VersionedEmitter.GetEmitterData();
		if (!Data) return;
		CollectNiagaraSourceGraph(GetNiagaraGraphFromSource(Data->GraphSource), TEXT("NiagaraEmitterGraph"), CollectedGraphIds, OutGraphs);
		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts, false, false);
		for (UNiagaraScript* Script : Scripts)
		{
			CollectNiagaraScriptGraphs(Script, TEXT("NiagaraEmitterScriptGraph"), CollectedGraphIds, OutGraphs);
		}
	}

	static void CollectNiagaraGraphs(UObject* Asset, TArray<FReadAllGraphIR>& OutGraphs)
	{
		TSet<FString> CollectedGraphIds;
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
		{
			CollectNiagaraScriptGraphs(System->GetSystemSpawnScript(), TEXT("NiagaraSystemSpawnGraph"), CollectedGraphIds, OutGraphs);
			CollectNiagaraScriptGraphs(System->GetSystemUpdateScript(), TEXT("NiagaraSystemUpdateGraph"), CollectedGraphIds, OutGraphs);
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				CollectNiagaraEmitterVersionGraphs(Handle.GetInstance(), CollectedGraphIds, OutGraphs);
			}
		}
		else if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset))
		{
			TArray<FNiagaraAssetVersion> Versions = Emitter->GetAllAvailableVersions();
			if (Versions.IsEmpty()) Versions.Add(Emitter->GetExposedVersion());
			for (const FNiagaraAssetVersion& Version : Versions)
			{
				const FGuid VersionGuid = Emitter->IsVersioningEnabled() ? Version.VersionGuid : FGuid();
				CollectNiagaraEmitterVersionGraphs(FVersionedNiagaraEmitter(Emitter, VersionGuid), CollectedGraphIds, OutGraphs);
			}
		}
		else if (UNiagaraScript* Script = Cast<UNiagaraScript>(Asset))
		{
			CollectNiagaraScriptGraphs(Script, TEXT("NiagaraScriptGraph"), CollectedGraphIds, OutGraphs);
		}
	}

	template <typename TEnum>
	static FString NiagaraEnumName(const TEnum Value)
	{
		if (const UEnum* Enum = StaticEnum<TEnum>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Value));
		}
		return FString::Printf(TEXT("%d"), static_cast<int32>(Value));
	}

	static FString ExportReflectedValue(const FProperty* Property, const UObject* Object)
	{
		if (!Property || !Object) return FString();
		FString Value;
		Property->ExportText_InContainer(
			0,
			Value,
			Object,
			nullptr,
			const_cast<UObject*>(Object),
			PPF_Copy | PPF_Delimited | PPF_ExportsNotFullyQualified,
			const_cast<UObject*>(Object));
		return Value;
	}

	static void CollectRenderer(
		const FVersionedNiagaraEmitter& VersionedEmitter,
		const UNiagaraRendererProperties* Renderer,
		const int32 RendererIndex,
		TSet<FString>& CollectedIds,
		TArray<FReadAllNiagaraRendererIR>& OutRenderers)
	{
		if (!Renderer || !VersionedEmitter.Emitter) return;
		const FString Version = VersionedEmitter.Version.IsValid()
			? VersionedEmitter.Version.ToString(EGuidFormats::DigitsWithHyphens)
			: TEXT("unversioned");
		const FString Id = VersionedEmitter.Emitter->GetPathName() + TEXT("|") + Version + FString::Printf(TEXT("|renderer-%d"), RendererIndex);
		if (CollectedIds.Contains(Id)) return;
		CollectedIds.Add(Id);

		FReadAllNiagaraRendererIR Result;
		Result.Id = Id;
		Result.EmitterPath = VersionedEmitter.Emitter->GetPathName();
		Result.EmitterVersion = Version;
		Result.Index = RendererIndex;
		Result.Name = Renderer->GetName();
		Result.ClassPath = Renderer->GetClass()->GetPathName();
		Result.SourceMode = NiagaraEnumName(Renderer->GetCurrentSourceMode());
		Result.bEnabled = Renderer->GetIsEnabled();

		TArray<UMaterialInterface*> Materials;
		Renderer->GetUsedMaterials(nullptr, Materials);
		for (const UMaterialInterface* Material : Materials)
		{
			if (Material) Result.Materials.AddUnique(Material->GetPathName());
		}
		Result.Materials.Sort();

		for (const FNiagaraVariableAttributeBinding* Binding : Renderer->GetAttributeBindings())
		{
			if (!Binding) continue;
			FReadAllNiagaraRendererBindingIR BindingIR;
#if WITH_EDITORONLY_DATA
			BindingIR.DisplayName = Binding->GetName().ToString();
#endif
			const FNiagaraVariableBase& Variable = Binding->GetParamMapBindableVariable();
			const FNiagaraVariableBase DataSetVariable = Binding->GetDataSetBindableVariable();
			BindingIR.VariableName = Variable.GetName().ToString();
			BindingIR.DataSetName = DataSetVariable.GetName().ToString();
			BindingIR.Type = Binding->GetType().GetNameText().ToString();
			BindingIR.SourceMode = NiagaraEnumName(Binding->GetBindingSourceMode());
			BindingIR.bValid = Binding->IsValid();
			BindingIR.bExistsOnSource = Binding->DoesBindingExistOnSource();
			if (BindingIR.DisplayName.IsEmpty()) BindingIR.DisplayName = BindingIR.VariableName;
			Result.Bindings.Add(MoveTemp(BindingIR));
		}

		for (TFieldIterator<FProperty> It(Renderer->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)
				|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient)) continue;
			const FString Value = ExportReflectedValue(Property, Renderer);
			if (Value.IsEmpty()) continue;
			Result.Properties.Add({Property->GetName(), Property->GetCPPType(), Property->GetMetaData(TEXT("Category")), Value});
		}
		OutRenderers.Add(MoveTemp(Result));
	}

	static void CollectEmitterRenderers(
		const FVersionedNiagaraEmitter& VersionedEmitter,
		TSet<FString>& CollectedIds,
		TArray<FReadAllNiagaraRendererIR>& OutRenderers)
	{
		FVersionedNiagaraEmitterData* Data = VersionedEmitter.GetEmitterData();
		if (!Data) return;
		for (int32 Index = 0; Index < Data->GetRenderers().Num(); ++Index)
		{
			CollectRenderer(VersionedEmitter, Data->GetRenderers()[Index], Index, CollectedIds, OutRenderers);
		}
	}

	static void CollectNiagaraRenderers(UObject* Asset, TArray<FReadAllNiagaraRendererIR>& OutRenderers)
	{
		OutRenderers.Reset();
		TSet<FString> CollectedIds;
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
		{
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				CollectEmitterRenderers(Handle.GetInstance(), CollectedIds, OutRenderers);
			}
		}
		else if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset))
		{
			TArray<FNiagaraAssetVersion> Versions = Emitter->GetAllAvailableVersions();
			if (Versions.IsEmpty()) Versions.Add(Emitter->GetExposedVersion());
			for (const FNiagaraAssetVersion& Version : Versions)
			{
				const FGuid VersionGuid = Emitter->IsVersioningEnabled() ? Version.VersionGuid : FGuid();
				CollectEmitterRenderers(FVersionedNiagaraEmitter(Emitter, VersionGuid), CollectedIds, OutRenderers);
			}
		}
		OutRenderers.Sort([](const FReadAllNiagaraRendererIR& A, const FReadAllNiagaraRendererIR& B) { return A.Id < B.Id; });
	}

	static FString BuildCurveFingerprint(const FReadAllNiagaraCurveIR& Curve)
	{
		FString Canonical;
		Canonical += Curve.ClassPath + TEXT("|") + Curve.CurveAssetPath + TEXT("|");
		Canonical += Curve.bUseLUT ? TEXT("lut=1|") : TEXT("lut=0|");
		Canonical += Curve.bExposeCurve ? TEXT("expose=1|") : TEXT("expose=0|");
		for (const FReadAllNiagaraCurveChannelIR& Channel : Curve.Channels)
		{
			Canonical += Channel.Name + TEXT("|") + Channel.PreInfinityExtrapolation + TEXT("|") + Channel.PostInfinityExtrapolation + TEXT("|");
			for (const FReadAllNiagaraCurveKeyIR& Key : Channel.Keys)
			{
				Canonical += FString::Printf(
					TEXT("%.9g,%.9g,%s,%s,%s,%.9g,%.9g,%.9g,%.9g;"),
					Key.Time,
					Key.Value,
					*Key.Interpolation,
					*Key.TangentMode,
					*Key.TangentWeightMode,
					Key.ArriveTangent,
					Key.ArriveTangentWeight,
					Key.LeaveTangent,
					Key.LeaveTangentWeight);
			}
		}
		return FMD5::HashAnsiString(*Canonical).ToLower();
	}

	static void CollectCurveDataInterface(
		UNiagaraDataInterfaceCurveBase* CurveInterface,
		const FString& OwnerGraphId,
		TSet<FString>& CollectedObjectPaths,
		TMap<FString, int32>& CurveIndexByFingerprint,
		TArray<FReadAllNiagaraCurveIR>& OutCurves)
	{
		if (!CurveInterface) return;
		const FString ObjectPath = CurveInterface->GetPathName();
		if (CollectedObjectPaths.Contains(ObjectPath)) return;
		CollectedObjectPaths.Add(ObjectPath);

		FReadAllNiagaraCurveIR Result;
		Result.ObjectPath = ObjectPath;
		Result.ClassPath = CurveInterface->GetClass()->GetPathName();
		Result.OwnerGraphId = OwnerGraphId;
		Result.ExposedName = CurveInterface->ExposedName.ToString();
		Result.bUseLUT = CurveInterface->bUseLUT != 0;
		Result.bExposeCurve = CurveInterface->bExposeCurve != 0;
#if WITH_EDITORONLY_DATA
		Result.CurveAssetPath = CurveInterface->CurveAsset ? CurveInterface->CurveAsset->GetPathName() : FString();
#endif

		TArray<UNiagaraDataInterfaceCurveBase::FCurveData> CurveChannels;
		CurveInterface->GetCurveData(CurveChannels);
		bool bHasRange = false;
		for (const UNiagaraDataInterfaceCurveBase::FCurveData& CurveData : CurveChannels)
		{
			if (!CurveData.Curve) continue;
			FReadAllNiagaraCurveChannelIR Channel;
			Channel.Name = CurveData.Name.ToString();
			Channel.PreInfinityExtrapolation = NiagaraEnumName(CurveData.Curve->PreInfinityExtrap.GetValue());
			Channel.PostInfinityExtrapolation = NiagaraEnumName(CurveData.Curve->PostInfinityExtrap.GetValue());
			for (const FRichCurveKey& Key : CurveData.Curve->GetConstRefOfKeys())
			{
				FReadAllNiagaraCurveKeyIR KeyIR;
				KeyIR.Time = Key.Time;
				KeyIR.Value = Key.Value;
				KeyIR.Interpolation = NiagaraEnumName(Key.InterpMode.GetValue());
				KeyIR.TangentMode = NiagaraEnumName(Key.TangentMode.GetValue());
				KeyIR.TangentWeightMode = NiagaraEnumName(Key.TangentWeightMode.GetValue());
				KeyIR.ArriveTangent = Key.ArriveTangent;
				KeyIR.ArriveTangentWeight = Key.ArriveTangentWeight;
				KeyIR.LeaveTangent = Key.LeaveTangent;
				KeyIR.LeaveTangentWeight = Key.LeaveTangentWeight;
				Channel.Keys.Add(MoveTemp(KeyIR));
				if (!bHasRange)
				{
					Result.MinTime = Key.Time;
					Result.MaxTime = Key.Time;
					bHasRange = true;
				}
				else
				{
					Result.MinTime = FMath::Min(Result.MinTime, Key.Time);
					Result.MaxTime = FMath::Max(Result.MaxTime, Key.Time);
				}
			}
			Result.Channels.Add(MoveTemp(Channel));
		}

		Result.Fingerprint = BuildCurveFingerprint(Result);
		Result.Id = TEXT("curve-") + Result.Fingerprint.Left(16);
		Result.UsedBy.AddUnique(OwnerGraphId.IsEmpty() ? ObjectPath : OwnerGraphId + TEXT(" :: ") + ObjectPath);
		if (int32* ExistingIndex = CurveIndexByFingerprint.Find(Result.Fingerprint))
		{
			FReadAllNiagaraCurveIR& Existing = OutCurves[*ExistingIndex];
			for (const FString& Usage : Result.UsedBy)
			{
				Existing.UsedBy.AddUnique(Usage);
			}
			Existing.UsedBy.Sort();
			return;
		}

		CurveIndexByFingerprint.Add(Result.Fingerprint, OutCurves.Num());
		OutCurves.Add(MoveTemp(Result));
	}

	static void CollectCurvesUnderObject(
		UObject* Root,
		const FString& OwnerGraphId,
		TSet<FString>& CollectedObjectPaths,
		TMap<FString, int32>& CurveIndexByFingerprint,
		TArray<FReadAllNiagaraCurveIR>& OutCurves)
	{
		if (!Root) return;
		if (UNiagaraDataInterfaceCurveBase* RootCurve = Cast<UNiagaraDataInterfaceCurveBase>(Root))
		{
			CollectCurveDataInterface(RootCurve, OwnerGraphId, CollectedObjectPaths, CurveIndexByFingerprint, OutCurves);
		}
		TArray<UObject*> Objects;
		GetObjectsWithOuter(Root, Objects, true);
		for (UObject* Object : Objects)
		{
			CollectCurveDataInterface(Cast<UNiagaraDataInterfaceCurveBase>(Object), OwnerGraphId, CollectedObjectPaths, CurveIndexByFingerprint, OutCurves);
		}
	}

	static void CollectNiagaraCurves(
		UObject* Asset,
		const TArray<FReadAllGraphIR>& Graphs,
		TArray<FReadAllNiagaraCurveIR>& OutCurves)
	{
		OutCurves.Reset();
		TSet<FString> CollectedObjectPaths;
		TMap<FString, int32> CurveIndexByFingerprint;
		for (const FReadAllGraphIR& Graph : Graphs)
		{
			if (UNiagaraGraph* SourceGraph = Cast<UNiagaraGraph>(StaticFindObject(UNiagaraGraph::StaticClass(), nullptr, *Graph.Id)))
			{
				CollectCurvesUnderObject(SourceGraph, Graph.Id, CollectedObjectPaths, CurveIndexByFingerprint, OutCurves);
			}
		}
		CollectCurvesUnderObject(Asset, FString(), CollectedObjectPaths, CurveIndexByFingerprint, OutCurves);
		OutCurves.Sort([](const FReadAllNiagaraCurveIR& A, const FReadAllNiagaraCurveIR& B) { return A.Id < B.Id; });
	}

	static void CollectNiagaraDetails(
		UObject* Asset,
		const TArray<FReadAllGraphIR>& Graphs,
		TArray<FReadAllNiagaraRendererIR>& OutRenderers,
		TArray<FReadAllNiagaraCurveIR>& OutCurves)
	{
		OutRenderers.Reset();
		OutCurves.Reset();
		if (!Asset || (!Asset->IsA<UNiagaraSystem>() && !Asset->IsA<UNiagaraEmitter>() && !Asset->IsA<UNiagaraScript>())) return;
		CollectNiagaraRenderers(Asset, OutRenderers);
		CollectNiagaraCurves(Asset, Graphs, OutCurves);
	}

	static void CollectGraphIR(UObject* Asset, TArray<FReadAllGraphIR>& OutGraphs)
	{
		OutGraphs.Reset();
		if (!Asset) return;

		if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Asset))
		{
			if (UMaterial* Material = MaterialInterface->GetMaterial())
			{
				CollectMaterialGraph(Material->GetName(), Material->GetPathName(), TEXT("Material"), Material->GetExpressions(), Material, OutGraphs);
			}
		}
		else if (UMaterialFunctionInterface* Function = Cast<UMaterialFunctionInterface>(Asset))
		{
			CollectMaterialGraph(Function->GetName(), Function->GetPathName(), TEXT("MaterialFunction"), Function->GetExpressions(), nullptr, OutGraphs);
		}
		else if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			CollectBlueprintGraphs(Blueprint, OutGraphs);
		}
		else if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>())
		{
			CollectNiagaraGraphs(Asset, OutGraphs);
		}
	}
}

void FAssetInsightExporter::CollectParameterClues(UObject* Asset, TArray<FReadAllParameterClue>& OutClues)
{
	OutClues.Reset();
	if (!Asset) return;

	if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		if (const UClass* GeneratedClass = Blueprint->GeneratedClass)
		{
			const UObject* CDO = GeneratedClass->GetDefaultObject();
			for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
				FString PropValue = AssetInsightImpl::ExportReflectedValue(Property, CDO);
				if (PropValue.IsEmpty()) PropValue = Property->GetCPPType();
				OutClues.Add({Property->GetName(), TEXT("蓝图变量"), PropValue});
			}
		}
	}
	else if (UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Material->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			float Value = 0.0f;
			Material->GetScalarParameterValue(Info, Value);
			OutClues.Add({Info.Name.ToString(), TEXT("材质标量"), FString::SanitizeFloat(Value)});
		}
		Infos.Reset();
		Ids.Reset();
		Material->GetAllVectorParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			FLinearColor Value = FLinearColor::Black;
			Material->GetVectorParameterValue(Info, Value);
			OutClues.Add({Info.Name.ToString(), TEXT("材质颜色/向量"), Value.ToString()});
		}
		Infos.Reset();
		Ids.Reset();
		Material->GetAllTextureParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			UTexture* Value = nullptr;
			Material->GetTextureParameterValue(Info, Value);
			OutClues.Add({Info.Name.ToString(), TEXT("材质贴图"), Value ? Value->GetPathName() : TEXT("<未设置>")});
		}
	}
	else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
	{
		TArray<FNiagaraVariable> Variables;
		System->GetExposedParameters().GetParameters(Variables);
		for (const FNiagaraVariable& Variable : Variables)
		{
			OutClues.Add({Variable.GetName().ToString(), TEXT("Niagara User 参数"), Variable.GetType().GetNameText().ToString()});
		}
	}
	else if (const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
	{
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			OutClues.Add({Slot.MaterialSlotName.ToString(), TEXT("模型材质槽"), Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : TEXT("<未设置>")});
		}
	}
	else if (const UTexture* Texture = Cast<UTexture>(Asset))
	{
		OutClues.Add({TEXT("sRGB"), TEXT("贴图设置"), Texture->SRGB ? TEXT("true") : TEXT("false")});
		OutClues.Add({TEXT("CompressionSettings"), TEXT("贴图设置"), FString::Printf(TEXT("%d"), static_cast<int32>(Texture->CompressionSettings.GetValue()))});
		OutClues.Add({TEXT("LODGroup"), TEXT("贴图设置"), FString::Printf(TEXT("%d"), static_cast<int32>(Texture->LODGroup.GetValue()))});
	}
	else if (const UDataTable* DataTable = Cast<UDataTable>(Asset))
	{
		if (const UScriptStruct* RowStruct = DataTable->GetRowStruct())
		{
			for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
			{
				OutClues.Add({It->GetName(), TEXT("数据表列"), It->GetCPPType()});
			}
		}
	}
	else if (const UCurveTable* CurveTable = Cast<UCurveTable>(Asset))
	{
		for (const TPair<FName, FRealCurve*>& Pair : CurveTable->GetRowMap())
		{
			OutClues.Add({Pair.Key.ToString(), TEXT("曲线名称"), Pair.Value ? FString::Printf(TEXT("%d keys"), Pair.Value->GetNumKeys()) : TEXT("<空>")});
		}
	}
	else if (const UEnum* Enum = Cast<UEnum>(Asset))
	{
		for (int32 Idx = 0; Idx < Enum->NumEnums(); ++Idx)
		{
			OutClues.Add({Enum->GetDisplayNameTextByIndex(Idx).ToString(), TEXT("枚举条目"), LexToString(Enum->GetValueByIndex(Idx))});
		}
	}
	else if (const UDataAsset* DataAsset = Cast<UDataAsset>(Asset))
	{
		for (TFieldIterator<FProperty> It(DataAsset->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
			FString PropValue = AssetInsightImpl::ExportReflectedValue(Property, DataAsset);
			if (PropValue.IsEmpty()) PropValue = TEXT("<无默认值>");
			OutClues.Add({Property->GetName(), TEXT("DataAsset 属性"), PropValue});
		}
	}

	AssetInsightImpl::SortUniqueClues(OutClues);
}

FReadAllAssetDocumentIR FAssetInsightExporter::BuildDocument(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument)
{
	FReadAllAssetDocumentIR Document;
	if (!Asset) return Document;

	const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
	Document.AssetName = Asset->GetName();
	Document.ObjectPath = Asset->GetPathName();
	Document.ClassPath = Asset->GetClass()->GetPathName();
	Document.AssetKind = AssetInsightImpl::AssetKind(Asset);
	Document.TechnicalMarkdown = TechnicalDocument;
	Document.PromptMode = Settings ? Settings->PromptMode : EReadAllPromptMode::Explain;
	CollectParameterClues(Asset, Document.ParameterClues);
	AssetInsightImpl::CollectRelationships(AssetData, Document.Dependencies, Document.Referencers);
	AssetInsightImpl::CollectGraphIR(Asset, Document.Graphs);
	AssetInsightImpl::CollectNiagaraDetails(Asset, Document.Graphs, Document.NiagaraRenderers, Document.NiagaraCurves);
	AssetInsightImpl::CollectFeatureTags(Asset, Document.ParameterClues, Document.Graphs, Document.Dependencies, Document.FeatureTags);
	Document.ArtistFocus = AssetInsightImpl::BuildDynamicArtistFocus(Asset, Document.ParameterClues, Document.FeatureTags);
	Document.SuggestedPrompt = AssetInsightImpl::BuildDynamicPrompt(Asset, Document.ParameterClues, Document.FeatureTags, Settings);
	return Document;
}

FString FAssetInsightExporter::DecorateDocument(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument)
{
	if (!Asset || TechnicalDocument.IsEmpty()) return TechnicalDocument;

	const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
	const EReadAllExportMode Mode = Settings ? Settings->ExportMode : EReadAllExportMode::Compact;
	return BuildDocument(AssetData, Asset, TechnicalDocument).RenderMarkdown(Mode);
}

FString FAssetInsightExporter::BuildMetadataJson(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument)
{
	if (!Asset) return FString();
	const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
	const EReadAllExportMode Mode = Settings ? Settings->ExportMode : EReadAllExportMode::Compact;
	return BuildDocument(AssetData, Asset, TechnicalDocument).RenderMetadataJson(Mode);
}

FString FAssetInsightExporter::BuildBatchIndex(const TArray<FAssetData>& Assets, const TArray<FString>& SavedPaths)
{
	FString Out;
	Out += TEXT("# ReadAllandExplains 批量导出索引\n\n");
	Out += TEXT("你好同学，我是 ReadAllandExplains。下面先把这批资产放到同一张关系表里，方便你追问“谁控制了谁”和“这个参数最后去了哪里”。\n\n");
	Out += TEXT("## 资产列表\n\n| 资产 | 类型 | 路径 | 导出文件 |\n|------|------|------|----------|\n");

	TMap<FString, TArray<FString>> AssetsByParameter;
	TMap<FString, TArray<FString>> AssetPathsByParameter;
	TMap<FString, FString> DisplayNameByParameter;

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		UObject* Asset = Assets[Index].GetAsset();
		const FString SavedPath = SavedPaths.IsValidIndex(Index) ? SavedPaths[Index] : TEXT("<未知>");
		Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Assets[Index].AssetName.ToString())
			+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(AssetInsightImpl::AssetKind(Asset))
			+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Assets[Index].GetObjectPathString())
			+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(SavedPath) + TEXT(" |\n");

		TArray<FReadAllParameterClue> Clues;
		CollectParameterClues(Asset, Clues);
		for (const FReadAllParameterClue& Clue : Clues)
		{
			const FString Key = Clue.Name.ToLower();
			DisplayNameByParameter.FindOrAdd(Key, Clue.Name);
			AssetsByParameter.FindOrAdd(Key).AddUnique(Assets[Index].AssetName.ToString() + TEXT(" [") + Clue.Kind + TEXT("]"));
			AssetPathsByParameter.FindOrAdd(Key).AddUnique(Assets[Index].GetObjectPathString());
		}
	}

	Out += TEXT("\n## 选中资产之间的直接依赖\n\n```mermaid\ngraph LR\n");
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = Module.Get();
	int32 EdgeCount = 0;
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		Out += FString::Printf(TEXT("  A%d[\"%s\"]\n"), Index, *AssetInsightImpl::MermaidLabel(Assets[Index].AssetName.ToString()));
		TArray<FName> Dependencies;
		Registry.GetDependencies(Assets[Index].PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		for (const FName Dependency : Dependencies)
		{
			for (int32 TargetIndex = 0; TargetIndex < Assets.Num(); ++TargetIndex)
			{
				if (Assets[TargetIndex].PackageName == Dependency)
				{
					Out += FString::Printf(TEXT("  A%d --> A%d\n"), Index, TargetIndex);
					++EdgeCount;
				}
			}
		}
	}
	if (EdgeCount == 0) Out += TEXT("  NONE[\"这批选中资产之间没有发现直接依赖\"]\n");
	Out += TEXT("```\n\n");

	Out += TEXT("## 同名参数追踪候选\n\n");
	Out += TEXT("> 同名参数是跨蓝图、材质和 Niagara 的排查起点，不代表它们一定已经连通。\n\n");
	Out += TEXT("| 参数名 | 出现位置 |\n|--------|----------|\n");
	TArray<FString> ParameterKeys;
	AssetsByParameter.GetKeys(ParameterKeys);
	ParameterKeys.Sort();
	int32 SharedCount = 0;
	for (const FString& Key : ParameterKeys)
	{
		const TArray<FString>& Locations = AssetsByParameter[Key];
		const TArray<FString>& AssetPaths = AssetPathsByParameter[Key];
		if (AssetPaths.Num() < 2) continue;
		Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(DisplayNameByParameter[Key])
			+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(FString::Join(Locations, TEXT(" → "))) + TEXT(" |\n");
		++SharedCount;
	}
	if (SharedCount == 0) Out += TEXT("| (没有发现跨资产同名参数) | |\n");

	Out += TEXT("\n## 推荐提问\n\n```text\n请根据这份 index 和各资产导出文档，从蓝图变量开始，追踪到材质参数、Material Function、贴图/MPC，再到 Niagara Renderer。先用美术语言给结论，再列出可核对的资产路径和参数名。不要把“同名”直接当成“已经连接”。\n```\n");
	return Out;
}

FString FAssetInsightExporter::BuildBatchIndexJson(const TArray<FAssetData>& Assets, const TArray<FString>& SavedPaths)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("documentType"), TEXT("ReadAllandExplainsBatchIndex"));
	Root->SetNumberField(TEXT("assetCount"), Assets.Num());

	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = Module.Get();
	TArray<TSharedPtr<FJsonValue>> AssetValues;
	AssetValues.Reserve(Assets.Num());

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		UObject* Asset = Assets[Index].GetAsset();
		if (!Asset) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Assets[Index].AssetName.ToString());
		Entry->SetStringField(TEXT("objectPath"), Assets[Index].GetObjectPathString());
		Entry->SetStringField(TEXT("classPath"), Asset->GetClass()->GetPathName());
		Entry->SetStringField(TEXT("assetKind"), AssetInsightImpl::AssetKind(Asset));
		Entry->SetStringField(TEXT("exportFile"), SavedPaths.IsValidIndex(Index) ? SavedPaths[Index] : FString());

		TArray<FReadAllParameterClue> Clues;
		CollectParameterClues(Asset, Clues);
		TArray<TSharedPtr<FJsonValue>> ParameterValues;
		for (const FReadAllParameterClue& Clue : Clues)
		{
			TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
			Parameter->SetStringField(TEXT("name"), Clue.Name);
			Parameter->SetStringField(TEXT("kind"), Clue.Kind);
			Parameter->SetStringField(TEXT("value"), Clue.Value);
			ParameterValues.Add(MakeShared<FJsonValueObject>(Parameter));
		}
		Entry->SetArrayField(TEXT("parameters"), ParameterValues);

		TArray<FName> Dependencies;
		TArray<FName> Referencers;
		Registry.GetDependencies(Assets[Index].PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		Registry.GetReferencers(Assets[Index].PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		Dependencies.Sort(FNameLexicalLess());
		Referencers.Sort(FNameLexicalLess());

		TArray<TSharedPtr<FJsonValue>> DependencyValues;
		for (const FName Name : Dependencies) DependencyValues.Add(MakeShared<FJsonValueString>(Name.ToString()));
		Entry->SetArrayField(TEXT("dependencies"), DependencyValues);
		TArray<TSharedPtr<FJsonValue>> ReferencerValues;
		for (const FName Name : Referencers) ReferencerValues.Add(MakeShared<FJsonValueString>(Name.ToString()));
		Entry->SetArrayField(TEXT("referencers"), ReferencerValues);

		AssetValues.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Root->SetArrayField(TEXT("assets"), AssetValues);
	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}