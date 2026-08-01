// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetInsightExporter.h"
#include "AssetTextSnapshot.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/CurveTable.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraParameterStore.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"

namespace AssetInsightImpl
{
	static FString AssetKind(const UObject* Asset)
	{
		if (!Asset) return TEXT("未知资产");
		if (Asset->IsA<UBlueprint>()) return TEXT("蓝图 / 工具逻辑");
		if (Asset->IsA<UMaterialInterface>()) return TEXT("材质 / 画面表现");
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
		if (Asset->IsA<UMaterialInterface>()) return TEXT("先看颜色、明暗、透明、粗糙度、流动速度、泡沫/遮罩，以及参数来自哪一级材质实例。");
		if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>()) return TEXT("先看发射数量、速度、生命周期、大小、颜色、Renderer 和对外暴露的 User 参数。");
		if (Asset->IsA<UStaticMesh>()) return TEXT("先看模型面数、LOD、UV、材质槽、碰撞，以及它是否适合场景或特效使用。");
		if (Asset->IsA<UTexture>()) return TEXT("先看尺寸、sRGB、压缩方式和用途；颜色图、法线、遮罩、Flow Map 的设置不能混用。");
		if (Asset->IsA<UDataTable>()) return TEXT("先看每一列代表什么、哪些行是预设，以及修改一行会影响哪些工具。");
		if (Asset->IsA<UCurveTable>()) return TEXT("先看曲线控制的是速度、大小、透明还是颜色，以及变化区间是否符合预期。");
		return TEXT("先看资产用途、输入、输出和直接引用关系。");
	}

	static FString SuggestedQuestions(const UObject* Asset)
	{
		if (Asset && Asset->IsA<UMaterialInterface>())
		{
			return TEXT("请用美术语言说明：颜色为什么显灰或过纯？如何压暗但保留层次？流动、泡沫和透明分别由哪个参数控制？");
		}
		if (Asset && (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>()))
		{
			return TEXT("请用特效美术语言说明：粒子从哪里生成、如何运动、何时消失、由哪个 Renderer 显示，以及怎样让节奏更快或更有层次？");
		}
		if (Asset && Asset->IsA<UBlueprint>())
		{
			return TEXT("请说明这个工具从哪个按钮或事件开始，关键参数怎样传递，最后修改或生成了哪些模型、材质和 Niagara？");
		}
		if (Asset && Asset->IsA<UStaticMesh>())
		{
			return TEXT("请从场景/建模角度检查面数、LOD、UV、材质槽和碰撞，并指出最值得优化的三项。");
		}
		return TEXT("请先用非程序语言说明这个资产的用途、可调整内容、上下游关系和常见风险，再给出三条可执行建议。");
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

	static FString BuildArtistHeader(UObject* Asset)
	{
		FString Out;
		Out += TEXT("# 你好同学，我是 ReadAllandExplains\n\n");
		Out += TEXT("接下来让我带你快速了解 **") + (Asset ? Asset->GetName() : TEXT("这个资产")) + TEXT("**。你不需要先理解程序术语，可以先看“美术速读”，需要排查时再向下看完整技术数据。\n\n");
		Out += TEXT("## 美术速读\n\n");
		Out += TEXT("- **它是什么：** ") + AssetKind(Asset) + TEXT("\n");
		Out += TEXT("- **先看什么：** ") + ArtistConcerns(Asset) + TEXT("\n");
		Out += TEXT("- **可以直接这样问：** “这个颜色有点灰、太纯，我想暗一点但不要脏”；“让流动更快”；“让粒子更有层次”；“这个模型为什么用了这张贴图？”\n");
		Out += TEXT("- **安全说明：** 本文档只读取资产，不会修改蓝图、材质、模型或 Niagara。\n\n---\n\n");
		return Out;
	}

	static FString BuildRelationshipSection(const FAssetData& AssetData)
	{
		FString Out;
		Out += TEXT("\n---\n## 跨资产关系 / 为什么会变成这样\n\n");
		Out += TEXT("> 这里显示保存到磁盘的直接引用。`依赖` 是它正在使用什么；`被引用` 是谁正在使用它。\n\n");

		FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Registry = Module.Get();
		TArray<FName> Dependencies;
		TArray<FName> Referencers;
		Registry.GetDependencies(AssetData.PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		Registry.GetReferencers(AssetData.PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		Dependencies.Sort(FNameLexicalLess());
		Referencers.Sort(FNameLexicalLess());

		Out += FString::Printf(TEXT("- 直接依赖：%d\n- 直接被引用：%d\n\n"), Dependencies.Num(), Referencers.Num());
		Out += TEXT("```mermaid\ngraph LR\n");
		Out += TEXT("  ROOT[\"") + MermaidLabel(AssetData.AssetName.ToString()) + TEXT("\"]\n");
		for (int32 Index = 0; Index < Dependencies.Num(); ++Index)
		{
			Out += FString::Printf(TEXT("  ROOT --> D%d[\"%s\"]\n"), Index, *MermaidLabel(Dependencies[Index].ToString()));
		}
		for (int32 Index = 0; Index < Referencers.Num(); ++Index)
		{
			Out += FString::Printf(TEXT("  R%d[\"%s\"] --> ROOT\n"), Index, *MermaidLabel(Referencers[Index].ToString()));
		}
		if (Dependencies.Num() == 0 && Referencers.Num() == 0)
		{
			Out += TEXT("  ROOT\n");
		}
		Out += TEXT("```\n\n");
		return Out;
	}

	static FString BuildParameterSection(UObject* Asset)
	{
		TArray<FReadAllParameterClue> Clues;
		FAssetInsightExporter::CollectParameterClues(Asset, Clues);
		FString Out;
		Out += TEXT("## 可调参数线索\n\n");
		Out += TEXT("> 这些名称会用于批量导出的“同名参数追踪”。同名表示值得检查，不代表引擎已经自动连接。\n\n");
		Out += TEXT("| 参数/插槽 | 类型 | 当前值或说明 |\n");
		Out += TEXT("|-----------|------|--------------|\n");
		for (const FReadAllParameterClue& Clue : Clues)
		{
			Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Clue.Name)
				+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Clue.Kind)
				+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Clue.Value) + TEXT(" |\n");
		}
		if (Clues.Num() == 0)
		{
			Out += TEXT("| (未发现公开参数) | | |\n");
		}
		Out += TEXT("\n");
		return Out;
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
			for (TFieldIterator<FProperty> It(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
				OutClues.Add({Property->GetName(), TEXT("蓝图变量"), Property->GetCPPType()});
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

	AssetInsightImpl::SortUniqueClues(OutClues);
}

FString FAssetInsightExporter::DecorateDocument(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument)
{
	if (!Asset || TechnicalDocument.IsEmpty()) return TechnicalDocument;

	FString Out = AssetInsightImpl::BuildArtistHeader(Asset);
	Out += TEXT("## 完整技术数据\n\n");
	Out += TechnicalDocument;
	Out += AssetInsightImpl::BuildRelationshipSection(AssetData);
	Out += AssetInsightImpl::BuildParameterSection(Asset);
	Out += TEXT("## 交给 AI 时可以直接复制这段话\n\n```text\n你好，请先把我当作美术同学，不要只给程序术语。\n资产：") + Asset->GetPathName() + TEXT("\n");
	Out += AssetInsightImpl::SuggestedQuestions(Asset);
	Out += TEXT("\n请把每条建议对应到本文档中的具体参数、节点或引用路径；不确定的地方请明确说不确定。\n```\n");
	return Out;
}

FString FAssetInsightExporter::BuildBatchIndex(const TArray<FAssetData>& Assets, const TArray<FString>& SavedPaths)
{
	FString Out;
	Out += TEXT("# ReadAllandExplains 批量导出索引\n\n");
	Out += TEXT("你好同学，我是 ReadAllandExplains。下面先把这批资产放到同一张关系表里，方便你追问“谁控制了谁”和“这个参数最后去了哪里”。\n\n");
	Out += TEXT("## 资产列表\n\n| 资产 | 类型 | 路径 | 导出文件 |\n|------|------|------|----------|\n");

	TMap<FString, TArray<FString>> AssetsByParameter;
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
		if (Locations.Num() < 2) continue;
		Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(DisplayNameByParameter[Key])
			+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(FString::Join(Locations, TEXT(" → "))) + TEXT(" |\n");
		++SharedCount;
	}
	if (SharedCount == 0) Out += TEXT("| (没有发现跨资产同名参数) | |\n");

	Out += TEXT("\n## 推荐提问\n\n```text\n请根据这份 index 和各资产导出文档，从蓝图变量开始，追踪到材质参数、Material Function、贴图/MPC，再到 Niagara Renderer。先用美术语言给结论，再列出可核对的资产路径和参数名。不要把“同名”直接当成“已经连接”。\n```\n");
	return Out;
}
