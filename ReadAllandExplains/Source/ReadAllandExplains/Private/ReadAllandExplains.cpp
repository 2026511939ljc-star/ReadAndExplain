// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReadAllandExplains.h"
#include "BlueprintToTextExporter.h"
#include "CommonAssetToTextExporter.h"
#include "AssetInsightExporter.h"
#include "MaterialToTextExporter.h"
#include "NiagaraToTextExporter.h"
#include "ReadAllandExplainsSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/Blueprint.h"
#include "Engine/CurveTable.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunctionInterface.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserMenuContexts.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "UObject/SoftObjectPath.h"
// 引用查看器（Reference Viewer）集成所需
#include "GraphEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "ReferenceViewer/EdGraphNode_Reference.h"
#include "ReferenceViewer/EdGraph_ReferenceViewer.h"

#define LOCTEXT_NAMESPACE "FReadAllandExplainsModule"

namespace ReadAllandExplainsExportImpl
{
	// 所有 AI 可读文档的统一导出根目录
	static FString GetExportRootDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("ReadAllandExplainsExports");
	}

	static FString GetBlueprintExportDir()
	{
		return GetExportRootDir() / TEXT("Blueprints");
	}

	static FString GetMaterialExportDir()
	{
		return GetExportRootDir() / TEXT("Materials");
	}

	static FString GetNiagaraExportDir()
	{
		return GetExportRootDir() / TEXT("Niagara");
	}

	static FString GetCommonAssetExportDir(const UObject* Asset)
	{
		return GetExportRootDir() / FCommonAssetToTextExporter::GetExportFolderName(Asset);
	}

	static bool EnsureDir(const FString& Dir)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*Dir))
		{
			return PlatformFile.CreateDirectoryTree(*Dir);
		}
		return true;
	}

	// 保存文本到指定目录/文件；返回实际保存路径，失败返回空串
	static FString SaveText(const FString& Text, const FString& Dir, const FString& AssetName, const FString& FileSuffix)
	{
		if (Text.IsEmpty() || AssetName.IsEmpty()) return FString();
		if (!EnsureDir(Dir)) return FString();

		const FString SavePath = Dir / (AssetName + FileSuffix);
		// Windows 上部分编辑器会把无 BOM 的 UTF-8 误判为本地 ANSI 编码，
		// 导致美术同学直接打开中文文档时看到乱码。统一写成带 BOM 的 UTF-8，
		// 内容仍然是标准 UTF-8，同时兼容记事本、Office、Markdown 编辑器和 AI 工具。
		if (FFileHelper::SaveStringToFile(Text, *SavePath, FFileHelper::EEncodingOptions::ForceUTF8))
		{
			return SavePath;
		}
		return FString();
	}

	static FString SaveAssetDocument(
		const FAssetData& AssetData,
		UObject* Asset,
		const FString& TechnicalDocument,
		const FString& Directory,
		const FString& FileSuffix)
	{
		if (!Asset || TechnicalDocument.IsEmpty()) return FString();

		const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
		const EReadAllExportMode Mode = Settings ? Settings->ExportMode : EReadAllExportMode::Compact;
		const FReadAllAssetDocumentIR Document = FAssetInsightExporter::BuildDocument(AssetData, Asset, TechnicalDocument);
		const FString SavedPath = SaveText(Document.RenderMarkdown(Mode), Directory, Asset->GetName(), FileSuffix);
		if (SavedPath.IsEmpty()) return FString();

		if (!Settings || Settings->bWriteMetadataJson)
		{
			const FString MetadataPath = SaveText(Document.RenderMetadataJson(Mode), Directory, Asset->GetName(), TEXT(".meta.json"));
			if (MetadataPath.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("ReadAllandExplains could not write metadata for %s"), *Asset->GetPathName());
			}
		}
		return SavedPath;
	}

	// 尝试把单个资产按类型导出；返回：0=成功, 1=失败（类型支持但导出空或保存失败）, 2=跳过（类型不支持）
	enum class EExportResult : uint8 { Success, Failed, Skipped };

	static EExportResult ExportOneAsset(const FAssetData& AssetData, FString& OutSavedPath, const FString& ExportRootOverride = FString())
	{
		UObject* Obj = AssetData.GetAsset();
		if (!Obj) return EExportResult::Failed;

		const FString BlueprintDir = ExportRootOverride.IsEmpty() ? GetBlueprintExportDir() : ExportRootOverride / TEXT("Blueprints");
		const FString MaterialDir = ExportRootOverride.IsEmpty() ? GetMaterialExportDir() : ExportRootOverride / TEXT("Materials");
		const FString NiagaraDir = ExportRootOverride.IsEmpty() ? GetNiagaraExportDir() : ExportRootOverride / TEXT("Niagara");
		const FString CommonDir = ExportRootOverride.IsEmpty()
			? GetCommonAssetExportDir(Obj)
			: ExportRootOverride / FCommonAssetToTextExporter::GetExportFolderName(Obj);

		// 蓝图
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Obj))
		{
			const FString Saved = SaveAssetDocument(AssetData, Blueprint, FBlueprintToTextExporter::ExportBlueprintToText(Blueprint), BlueprintDir, TEXT("_ReadableCode.txt"));
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		// 材质 / 材质实例（注意：UMaterialInstance 也是 UMaterialInterface）
		if (UMaterialInterface* MatIface = Cast<UMaterialInterface>(Obj))
		{
			const FString Saved = SaveAssetDocument(AssetData, MatIface, FMaterialToTextExporter::ExportMaterialToText(MatIface), MaterialDir, TEXT("_ReadableMaterial.md"));
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		// 材质函数
		if (UMaterialFunctionInterface* MatFunc = Cast<UMaterialFunctionInterface>(Obj))
		{
			const FString Saved = SaveAssetDocument(AssetData, MatFunc, FMaterialToTextExporter::ExportMaterialFunctionToText(MatFunc), MaterialDir, TEXT("_ReadableMaterialFunction.md"));
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		// Niagara System / Emitter / Script（Module、Dynamic Input 等也属于 UNiagaraScript）
		if (Obj->IsA<UNiagaraSystem>() || Obj->IsA<UNiagaraEmitter>() || Obj->IsA<UNiagaraScript>())
		{
			const FString Saved = SaveAssetDocument(AssetData, Obj, FNiagaraToTextExporter::ExportNiagaraAssetToText(Obj), NiagaraDir, TEXT("_ReadableNiagara.md"));
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		if (FCommonAssetToTextExporter::Supports(Obj))
		{
			const FString Saved = SaveAssetDocument(
				AssetData,
				Obj,
				FCommonAssetToTextExporter::ExportAssetToText(Obj),
				CommonDir,
				FCommonAssetToTextExporter::GetFileSuffix(Obj));
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		return EExportResult::Skipped;
	}

	static bool IsSupportedAssetData(const FAssetData& AssetData)
	{
		return AssetData.IsInstanceOf(UBlueprint::StaticClass())
			|| AssetData.IsInstanceOf(UMaterialInterface::StaticClass())
			|| AssetData.IsInstanceOf(UMaterialFunctionInterface::StaticClass())
			|| AssetData.IsInstanceOf(UNiagaraSystem::StaticClass())
			|| AssetData.IsInstanceOf(UNiagaraEmitter::StaticClass())
			|| AssetData.IsInstanceOf(UNiagaraScript::StaticClass())
			|| AssetData.IsInstanceOf(UStaticMesh::StaticClass())
			|| AssetData.IsInstanceOf(UTexture::StaticClass())
			|| AssetData.IsInstanceOf(UDataTable::StaticClass())
			|| AssetData.IsInstanceOf(UCurveTable::StaticClass());
	}

	static TArray<FAssetData> GetSelectedAssetsFromContentBrowser()
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
		return SelectedAssets;
	}

	static void NotifyEmpty(const FText& Message)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	// 打开导出根目录
	static void OpenExportRootDir()
	{
		const FString Dir = GetExportRootDir();
		EnsureDir(Dir);
		FPlatformProcess::ExploreFolder(*Dir);
	}
}

using namespace ReadAllandExplainsExportImpl;

// 批量导出：从一组 FAssetData 路由到对应导出器，返回结果与可打开路径
struct FBatchExportResult
{
	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	int32 SkippedCount = 0;
	FString FirstSavedPath;
	TArray<FAssetData> ExportedAssets;
	TArray<FString> SavedPaths;
};

static FBatchExportResult ExportAssetDataList(const TArray<FAssetData>& AssetList, const FString& ExportRootOverride = FString())
{
	FBatchExportResult R;
	for (const FAssetData& AssetData : AssetList)
	{
		FString SavedPath;
		const ReadAllandExplainsExportImpl::EExportResult Result = ReadAllandExplainsExportImpl::ExportOneAsset(AssetData, SavedPath, ExportRootOverride);
		switch (Result)
		{
		case ReadAllandExplainsExportImpl::EExportResult::Success:
			++R.SuccessCount;
			if (R.FirstSavedPath.IsEmpty()) R.FirstSavedPath = SavedPath;
			R.ExportedAssets.Add(AssetData);
			R.SavedPaths.Add(SavedPath);
			break;
		case ReadAllandExplainsExportImpl::EExportResult::Failed:
			++R.FailedCount;
			break;
		case ReadAllandExplainsExportImpl::EExportResult::Skipped:
		default:
			++R.SkippedCount;
			break;
		}
	}
	if (R.ExportedAssets.Num() > 0)
	{
		const FString IndexDir = ExportRootOverride.IsEmpty() ? GetExportRootDir() : ExportRootOverride;
		const FString IndexText = FAssetInsightExporter::BuildBatchIndex(R.ExportedAssets, R.SavedPaths);
		SaveText(IndexText, IndexDir, TEXT("index"), TEXT(".md"));
		const FString IndexJson = FAssetInsightExporter::BuildBatchIndexJson(R.ExportedAssets, R.SavedPaths);
		SaveText(IndexJson, IndexDir, TEXT("index"), TEXT(".json"));
	}
	return R;
}

static TArray<FAssetData> CollectContextPackAssets(const TArray<FAssetData>& Roots, const int32 MaxDepth)
{
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = Module.Get();
	TArray<FAssetData> Result;
	TArray<TPair<FAssetData, int32>> Queue;
	TSet<FString> SeenObjectPaths;

	for (const FAssetData& Root : Roots)
	{
		const FString ObjectPath = Root.GetObjectPathString();
		if (!Root.IsValid() || SeenObjectPaths.Contains(ObjectPath)) continue;
		SeenObjectPaths.Add(ObjectPath);
		Result.Add(Root);
		Queue.Emplace(Root, 0);
	}

	for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
	{
		const FAssetData& Current = Queue[QueueIndex].Key;
		const int32 CurrentDepth = Queue[QueueIndex].Value;
		if (CurrentDepth >= MaxDepth) continue;

		TArray<FName> Dependencies;
		Registry.GetDependencies(Current.PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
		Dependencies.Sort(FNameLexicalLess());
		for (const FName Dependency : Dependencies)
		{
			if (!Dependency.ToString().StartsWith(TEXT("/Game/"))) continue;
			TArray<FAssetData> DependencyAssets;
			Registry.GetAssetsByPackageName(Dependency, DependencyAssets);
			DependencyAssets.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetObjectPathString() < B.GetObjectPathString();
			});
			for (const FAssetData& DependencyAsset : DependencyAssets)
			{
				if (!IsSupportedAssetData(DependencyAsset)) continue;
				const FString ObjectPath = DependencyAsset.GetObjectPathString();
				if (SeenObjectPaths.Contains(ObjectPath)) continue;
				SeenObjectPaths.Add(ObjectPath);
				Result.Add(DependencyAsset);
				Queue.Emplace(DependencyAsset, CurrentDepth + 1);
			}
		}
	}

	return Result;
}

static FString BuildContextPackManifest(
	const TArray<FAssetData>& Roots,
	const FBatchExportResult& Result,
	const int32 DependencyDepth)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("documentType"), TEXT("ReadAllandExplainsContextPack"));
	Root->SetStringField(TEXT("createdUtc"), FDateTime::UtcNow().ToIso8601());
	Root->SetNumberField(TEXT("dependencyDepth"), DependencyDepth);
	Root->SetNumberField(TEXT("exportedAssetCount"), Result.ExportedAssets.Num());
	Root->SetNumberField(TEXT("failedCount"), Result.FailedCount);
	Root->SetNumberField(TEXT("skippedCount"), Result.SkippedCount);

	TArray<TSharedPtr<FJsonValue>> RootValues;
	for (const FAssetData& RootAsset : Roots)
	{
		RootValues.Add(MakeShared<FJsonValueString>(RootAsset.GetObjectPathString()));
	}
	Root->SetArrayField(TEXT("rootAssets"), RootValues);

	TArray<TSharedPtr<FJsonValue>> AssetValues;
	for (int32 Index = 0; Index < Result.ExportedAssets.Num(); ++Index)
	{
		TSharedRef<FJsonObject> Asset = MakeShared<FJsonObject>();
		Asset->SetStringField(TEXT("objectPath"), Result.ExportedAssets[Index].GetObjectPathString());
		Asset->SetStringField(TEXT("packageName"), Result.ExportedAssets[Index].PackageName.ToString());
		Asset->SetStringField(TEXT("exportFile"), Result.SavedPaths.IsValidIndex(Index) ? Result.SavedPaths[Index] : FString());
		AssetValues.Add(MakeShared<FJsonValueObject>(Asset));
	}
	Root->SetArrayField(TEXT("assets"), AssetValues);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

static FBatchExportResult ExportContextPack(const TArray<FAssetData>& Roots, FString& OutPackDir)
{
	const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
	const int32 DependencyDepth = FMath::Clamp(Settings ? Settings->ContextPackDependencyDepth : 2, 0, 4);
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	OutPackDir = GetExportRootDir() / TEXT("ContextPacks") / (TEXT("ContextPack_") + Timestamp);

	const TArray<FAssetData> Assets = CollectContextPackAssets(Roots, DependencyDepth);
	FBatchExportResult Result = ExportAssetDataList(Assets, OutPackDir);
	SaveText(BuildContextPackManifest(Roots, Result, DependencyDepth), OutPackDir, TEXT("context-pack"), TEXT(".json"));

	FString Readme;
	Readme += TEXT("# ReadAllandExplains Context Pack\n\n");
	Readme += FString::Printf(TEXT("- 根资产：%d\n- 项目依赖递归层级：%d\n- 成功导出：%d\n- 失败：%d\n- 跳过：%d\n\n"),
		Roots.Num(), DependencyDepth, Result.SuccessCount, Result.FailedCount, Result.SkippedCount);
	Readme += TEXT("## 使用方式\n\n优先把本目录的 `context-pack.json`、`index.json` 和根资产文档交给 AI；需要分析具体节点、Renderer 或曲线时，再按需读取对应 `.meta.json`。推荐使用配套 ReadAllandExplains Skill 与 MCP，先读摘要、再读取目标片段，避免一次加载完整大文件。\n\n");
	Readme += TEXT("```text\n请把这个 ReadAllandExplains Context Pack 作为一个整体分析。先解释根资产的视觉目标与执行流程，再沿 index 中的真实依赖检查自定义模块、材质、Renderer 绑定与曲线。不要把同名参数直接当作已连接。\n```\n");
	SaveText(Readme, OutPackDir, TEXT("README"), TEXT(".md"));
	return Result;
}

static void NotifyContextPackResult(const FBatchExportResult& Result, const FString& PackDir)
{
	FNotificationInfo Info(FText::Format(
		LOCTEXT("ContextPackSummary", "AI Context Pack 生成完成：资产 {0}，失败 {1}，跳过 {2}。\n输出目录：{3}"),
		FText::AsNumber(Result.SuccessCount),
		FText::AsNumber(Result.FailedCount),
		FText::AsNumber(Result.SkippedCount),
		FText::FromString(PackDir)));
	Info.ExpireDuration = Result.FailedCount > 0 ? 8.0f : 6.0f;
	Info.Hyperlink = FSimpleDelegate::CreateLambda([PackDir]() { FPlatformProcess::ExploreFolder(*PackDir); });
	Info.HyperlinkText = LOCTEXT("OpenContextPackDir", "打开 Context Pack");
	FSlateNotificationManager::Get().AddNotification(Info);
	if (Result.SuccessCount > 0) FPlatformProcess::ExploreFolder(*PackDir);
}

static TArray<FAssetData> ResolveConsoleAssets(const TArray<FString>& Args)
{
	if (Args.IsEmpty())
	{
		return GetSelectedAssetsFromContentBrowser();
	}

	TArray<FAssetData> Assets;
	for (FString ObjectPath : Args)
	{
		ObjectPath.TrimQuotesInline();
		ObjectPath.TrimStartAndEndInline();
		if (ObjectPath.IsEmpty()) continue;

		UObject* Asset = FSoftObjectPath(ObjectPath).TryLoad();
		if (!Asset)
		{
			UE_LOG(LogTemp, Error, TEXT("ReadAllandExplains could not load asset: %s"), *ObjectPath);
			continue;
		}
		Assets.Emplace(Asset);
	}
	return Assets;
}

static void ExportAssetsFromConsole(const TArray<FString>& Args)
{
	const TArray<FAssetData> Assets = ResolveConsoleAssets(Args);
	if (Assets.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("ReadAllandExplains.ExportAssets found no assets. Pass one or more object paths, for example /Game/Folder/M_Asset.M_Asset."));
		return;
	}

	const FBatchExportResult Result = ExportAssetDataList(Assets);
	UE_LOG(LogTemp, Display, TEXT("ReadAllandExplains export finished: success=%d failed=%d skipped=%d output=%s"),
		Result.SuccessCount,
		Result.FailedCount,
		Result.SkippedCount,
		*GetExportRootDir());
}

static void ExportContextPackFromConsole(const TArray<FString>& Args)
{
	const TArray<FAssetData> Roots = ResolveConsoleAssets(Args);
	if (Roots.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("ReadAllandExplains.ExportContextPack found no assets. Pass object paths or select assets in Content Browser."));
		return;
	}
	FString PackDir;
	const FBatchExportResult Result = ExportContextPack(Roots, PackDir);
	UE_LOG(LogTemp, Display, TEXT("ReadAllandExplains Context Pack finished: success=%d failed=%d skipped=%d output=%s"),
		Result.SuccessCount, Result.FailedCount, Result.SkippedCount, *PackDir);
}

static IConsoleObject* ExportAssetsConsoleCommand = nullptr;
static IConsoleObject* LegacyExportAssetsConsoleCommand = nullptr;
static IConsoleObject* ExportContextPackConsoleCommand = nullptr;

static void RegisterExportConsoleCommands()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	const TCHAR* Help = TEXT("Export UE assets as AI-readable documents. Usage: ReadAllandExplains.ExportAssets /Game/Path/Asset.Asset. With no paths, exports the current Content Browser selection.");
	ExportAssetsConsoleCommand = ConsoleManager.RegisterConsoleCommand(
		TEXT("ReadAllandExplains.ExportAssets"),
		Help,
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExportAssetsFromConsole),
		ECVF_Default);
	LegacyExportAssetsConsoleCommand = ConsoleManager.RegisterConsoleCommand(
		TEXT("GetTheMeaning.ExportAssets"),
		Help,
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExportAssetsFromConsole),
		ECVF_Default);
	ExportContextPackConsoleCommand = ConsoleManager.RegisterConsoleCommand(
		TEXT("ReadAllandExplains.ExportContextPack"),
		TEXT("Export selected roots plus supported /Game/ dependencies as one AI Context Pack. Usage: ReadAllandExplains.ExportContextPack /Game/Path/Asset.Asset."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ExportContextPackFromConsole),
		ECVF_Default);
}

static void UnregisterExportConsoleCommands()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	if (ExportAssetsConsoleCommand)
	{
		ConsoleManager.UnregisterConsoleObject(ExportAssetsConsoleCommand, false);
		ExportAssetsConsoleCommand = nullptr;
	}
	if (LegacyExportAssetsConsoleCommand)
	{
		ConsoleManager.UnregisterConsoleObject(LegacyExportAssetsConsoleCommand, false);
		LegacyExportAssetsConsoleCommand = nullptr;
	}
	if (ExportContextPackConsoleCommand)
	{
		ConsoleManager.UnregisterConsoleObject(ExportContextPackConsoleCommand, false);
		ExportContextPackConsoleCommand = nullptr;
	}
}

static void NotifyBatchResult(const FBatchExportResult& R)
{
	const FString RootDir = GetExportRootDir();
	const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
	const FString ModeName = ReadAllExportModeToString(Settings ? Settings->ExportMode : EReadAllExportMode::Compact);
	FNotificationInfo Info(FText::Format(
		LOCTEXT("BatchExportSummary", "AI 可读文档导出完成：成功 {0}，失败 {1}，跳过 {2}。\n模式：{3}\n输出目录：{4}"),
		FText::AsNumber(R.SuccessCount),
		FText::AsNumber(R.FailedCount),
		FText::AsNumber(R.SkippedCount),
		FText::FromString(ModeName),
		FText::FromString(RootDir)
	));
	Info.ExpireDuration = (R.FailedCount > 0) ? 8.0f : 5.0f;
	Info.Hyperlink = FSimpleDelegate::CreateLambda([]() { OpenExportRootDir(); });
	Info.HyperlinkText = LOCTEXT("OpenExportDir", "打开导出目录");
	FSlateNotificationManager::Get().AddNotification(Info);

	// 导出成功后自动打开资源管理器，并选中首个新生成的文件。
	if (R.SuccessCount > 0 && !R.FirstSavedPath.IsEmpty())
	{
		const FString AbsoluteSavedPath = FPaths::ConvertRelativePathToFull(R.FirstSavedPath);
		FPlatformProcess::ExploreFolder(*AbsoluteSavedPath);
	}
}

// 批量导出（内容浏览器入口）：遍历所有选中资产，按类型路由到对应导出器
static void ExportAllSelectedToAIDocs()
{
	const TArray<FAssetData> SelectedAssets = GetSelectedAssetsFromContentBrowser();
	if (SelectedAssets.Num() == 0)
	{
		NotifyEmpty(LOCTEXT("NoAssetSelected", "请先在内容浏览器中选中若干资产（蓝图/材质/材质函数/Niagara）。"));
		return;
	}

	const FBatchExportResult R = ExportAssetDataList(SelectedAssets);
	NotifyBatchResult(R);
}

static void ExportSelectedAsContextPack()
{
	const TArray<FAssetData> SelectedAssets = GetSelectedAssetsFromContentBrowser();
	if (SelectedAssets.IsEmpty())
	{
		NotifyEmpty(LOCTEXT("NoContextPackRootSelected", "请先在内容浏览器中选择 Context Pack 根资产。"));
		return;
	}
	FString PackDir;
	const FBatchExportResult Result = ExportContextPack(SelectedAssets, PackDir);
	NotifyContextPackResult(Result, PackDir);
}

// 批量导出（引用查看器入口）：从 UGraphNodeContextMenuContext 拿到选中的 Reference 节点 → FAssetData → 路由
static void ExportReferenceViewerSelectionToAIDocs(const UEdGraph* OwnerGraph)
{
	if (!OwnerGraph)
	{
		NotifyEmpty(LOCTEXT("NoRVGraph", "未能获取引用查看器图表。"));
		return;
	}

	TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(OwnerGraph);
	if (!GraphEditor.IsValid())
	{
		NotifyEmpty(LOCTEXT("NoRVGraphEditor", "未能找到引用查看器窗口。"));
		return;
	}

	TArray<FAssetData> AssetList;
	const TSet<UObject*>& SelectedNodes = GraphEditor->GetSelectedNodes();
	for (UObject* SelectedObject : SelectedNodes)
	{
		UEdGraphNode_Reference* RefNode = Cast<UEdGraphNode_Reference>(SelectedObject);
		if (!RefNode) continue;
		if (RefNode->IsCollapsed()) continue;

		const FAssetData& AssetData = RefNode->GetAssetData();
		if (AssetData.IsValid())
		{
			AssetList.Add(AssetData);
		}
	}

	if (AssetList.Num() == 0)
	{
		NotifyEmpty(LOCTEXT("NoRVAssetSelected", "请先在引用查看器中框选资产节点（材质/材质函数/蓝图）。"));
		return;
	}

	const FBatchExportResult R = ExportAssetDataList(AssetList);
	NotifyBatchResult(R);
}

void FReadAllandExplainsModule::StartupModule()
{
	RegisterExportConsoleCommands();
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FReadAllandExplainsModule::RegisterMenus));
}

void FReadAllandExplainsModule::ShutdownModule()
{
	UnregisterExportConsoleCommands();
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FReadAllandExplainsModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Window 菜单：唯一入口「导出所有选中为 AI 可读文档」
	if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
	{
		FToolMenuSection& Section = WindowMenu->FindOrAddSection("ReadAllandExplains");
		Section.Label = LOCTEXT("ReadAllandExplainsMenu", "ReadAllandExplains");

		Section.AddMenuEntry(
			"ExportAllSelectedToAIDocs",
			LOCTEXT("ExportAllSelectedToAIDocs", "导出所有选中为 AI 可读文档"),
			LOCTEXT("ExportAllSelectedToAIDocsTooltip", "遍历内容浏览器中所有选中的资产：蓝图、完整材质图和 Niagara 源资产；输出到 Saved/ReadAllandExplainsExports。"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&ExportAllSelectedToAIDocs))
		);
		Section.AddMenuEntry(
			"ExportSelectedAsContextPack",
			LOCTEXT("ExportSelectedAsContextPack", "生成 AI Context Pack（含项目依赖）"),
			LOCTEXT("ExportSelectedAsContextPackTooltip", "把内容浏览器选择作为根资产，递归收集 /Game/ 下受支持的项目依赖，生成纯 Markdown/JSON 上下文包和索引。"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&ExportSelectedAsContextPack))
		);
	}

	// 内容浏览器右键：导出文档或生成完整 Context Pack
	if (UToolMenu* AssetContextMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu"))
	{
		FToolMenuSection& Section = AssetContextMenu->FindOrAddSection("ReadAllandExplainsAssetActions");
		Section.Label = LOCTEXT("ReadAllandExplainsSection", "ReadAllandExplains");

		// 选中资产中至少有一个支持类型（蓝图/材质/材质函数）才显示
		Section.AddDynamicEntry(
			"ExportAllSelectedToAIDocsContext",
			FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!Context) return;

				bool bHasSupported = false;
				for (const FAssetData& AssetData : Context->SelectedAssets)
				{
					if (IsSupportedAssetData(AssetData))
					{
						bHasSupported = true;
						break;
					}
				}
				if (!bHasSupported) return;

				InSection.AddMenuEntry(
					"ExportAllSelectedToAIDocs",
					LOCTEXT("ExportAllSelectedToAIDocs_CB", "导出所有选中为 AI 可读文档"),
					LOCTEXT("ExportAllSelectedToAIDocsCtxTooltip", "遍历所有选中的资产：蓝图、完整材质图和 Niagara 源资产；输出到 Saved/ReadAllandExplainsExports。"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&ExportAllSelectedToAIDocs))
				);
				InSection.AddMenuEntry(
					"ExportSelectedAsContextPack",
					LOCTEXT("ExportSelectedAsContextPack_CB", "生成 AI Context Pack（含项目依赖）"),
					LOCTEXT("ExportSelectedAsContextPackCtxTooltip", "递归收集 /Game/ 下受支持依赖并生成独立 Context Pack；递归层级可在 Editor Preferences > Plugins > ReadAllandExplains 设置。"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&ExportSelectedAsContextPack))
				);
			})
		);
	}

	// 引用查看器（Reference Viewer）：在图"节点"右键菜单里增加批量导出。
	//
	// 关键根因：SGraphEditorImpl::GenerateContextMenu 里有
	//     if (Context->Node) MenuName = GetNodeContextMenuName(Context->Node->GetClass());
	//     else               MenuName = Schema->GetContextMenuName();
	// 用户实际操作是在节点上右键，所以菜单名是
	//     "GraphEditor.GraphNodeContextMenu.EdGraphNode_Reference"
	// 而不是 Schema 菜单 "GraphEditor.GraphContextMenu.ReferenceViewerSchema"
	// （后者只在图空白处右键时生效，导致我们之前的 DynamicSection 根本不被执行）。
	//
	// ExtendMenu 返回占位 UToolMenu（bRegistered=false）；等引擎第一次在节点上右键时，
	// SGraphEditorImpl::RegisterContextMenu 会以正确 Parent 注册它，我们这里的 DynamicSection
	// 会在后续每次生成菜单时执行。
	auto AddRVExportDynamicSection = [](UToolMenu* InMenu)
	{
		if (!InMenu) return;

		UGraphNodeContextMenuContext* Context = InMenu->FindContext<UGraphNodeContextMenuContext>();
		if (!Context || !Context->Graph) return;

		// 双保险：仅对引用查看器的图生效
		if (!Context->Graph->IsA<UEdGraph_ReferenceViewer>()) return;

		// 找到对应的 SGraphEditor，判断当前选中里是否至少一个可用资产节点
		TSharedPtr<SGraphEditor> GraphEditor = SGraphEditor::FindGraphEditorForGraph(Context->Graph);
		if (!GraphEditor.IsValid()) return;

		bool bHasSupported = false;
		for (UObject* SelectedObject : GraphEditor->GetSelectedNodes())
		{
			UEdGraphNode_Reference* RefNode = Cast<UEdGraphNode_Reference>(SelectedObject);
			if (!RefNode || RefNode->IsCollapsed()) continue;
			const FAssetData& AssetData = RefNode->GetAssetData();
			if (!AssetData.IsValid()) continue;

			if (IsSupportedAssetData(AssetData))
			{
				bHasSupported = true;
				break;
			}
		}
		if (!bHasSupported) return;

		FToolMenuSection& Section = InMenu->AddSection(
			"ReadAllandExplainsRVSection",
			LOCTEXT("ReadAllandExplainsRVSectionLabel", "ReadAllandExplains")
		);

		TWeakObjectPtr<const UEdGraph> WeakGraph(Context->Graph);
		Section.AddMenuEntry(
			"ExportAllSelectedToAIDocs_RV",
			LOCTEXT("ExportAllSelectedToAIDocs_RV", "导出所有选中为 AI 可读文档"),
			LOCTEXT("ExportAllSelectedToAIDocs_RVTooltip", "遍历引用查看器中所有选中的蓝图、完整材质图和 Niagara 源资产；输出到 Saved/ReadAllandExplainsExports。"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakGraph]()
			{
				ExportReferenceViewerSelectionToAIDocs(WeakGraph.Get());
			}))
		);
	};

	// (1) 节点上右键的菜单（用户主要入口）
	if (UToolMenu* RVNodeMenu = UToolMenus::Get()->ExtendMenu("GraphEditor.GraphNodeContextMenu.EdGraphNode_Reference"))
	{
		RVNodeMenu->AddDynamicSection(
			"ReadAllandExplainsRVExport_Node",
			FNewToolMenuDelegate::CreateLambda(AddRVExportDynamicSection)
		);
	}

	// (2) 图空白处右键的菜单（作为保险入口，仅对 Reference Viewer Schema 生效）
	if (UToolMenu* RVSchemaMenu = UToolMenus::Get()->ExtendMenu("GraphEditor.GraphContextMenu.ReferenceViewerSchema"))
	{
		RVSchemaMenu->AddDynamicSection(
			"ReadAllandExplainsRVExport_Schema",
			FNewToolMenuDelegate::CreateLambda(AddRVExportDynamicSection)
		);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FReadAllandExplainsModule, ReadAllandExplains)