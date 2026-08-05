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
#include "Engine/DataAsset.h"
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
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IO/IoHash.h"
#include "UObject/SoftObjectPath.h"
#include "Containers/Ticker.h"
#include "Editor.h"
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

	static FString SanitizeExportStem(const FString& AssetName)
	{
		FString Result;
		Result.Reserve(AssetName.Len());
		for (const TCHAR Character : AssetName)
		{
			const bool bControlCharacter = Character < 32;
			const bool bInvalidCharacter = FString(TEXT("<>:\"/\\|?*")).Contains(FString::Chr(Character));
			Result.AppendChar(bControlCharacter || bInvalidCharacter ? TEXT('_') : Character);
		}
		while (!Result.IsEmpty() && (Result.EndsWith(TEXT(".")) || Result.EndsWith(TEXT(" "))))
		{
			Result.LeftChopInline(1);
		}
		if (Result.IsEmpty() || Result == TEXT(".") || Result == TEXT("..")) Result = TEXT("Asset");
		if (Result.Len() > 80) Result.LeftInline(80);
		return Result;
	}

	static FString BuildStableExportStem(const FAssetData& AssetData)
	{
		const FString ObjectPath = AssetData.GetObjectPathString();
		const FTCHARToUTF8 ObjectPathUtf8(*ObjectPath);
		const FString ObjectPathHash = LexToString(FIoHash::HashBuffer(ObjectPathUtf8.Get(), ObjectPathUtf8.Length())).Left(12);
		return SanitizeExportStem(AssetData.AssetName.ToString()) + TEXT("__") + ObjectPathHash;
	}

	static bool MetadataBelongsToAsset(const FString& MetadataPath, const FString& ObjectPath)
	{
		FString JsonText;
		TSharedPtr<FJsonObject> Metadata;
		if (!FFileHelper::LoadFileToString(JsonText, *MetadataPath)) return false;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Metadata) || !Metadata.IsValid()) return false;
		FString ExistingObjectPath;
		return Metadata->TryGetStringField(TEXT("objectPath"), ExistingObjectPath) && ExistingObjectPath == ObjectPath;
	}

	static FString ResolveExportStem(
		const FAssetData& AssetData,
		const FString& Directory,
		const FString& FileSuffix,
		const bool bWriteMetadata,
		const bool bForceDisambiguation)
	{
		const FString LegacyStem = SanitizeExportStem(AssetData.AssetName.ToString());
		if (bForceDisambiguation) return BuildStableExportStem(AssetData);
		const FString DocumentPath = Directory / (LegacyStem + FileSuffix);
		const FString MetadataPath = Directory / (LegacyStem + TEXT(".meta.json"));
		if (!FPaths::FileExists(DocumentPath) && (!bWriteMetadata || !FPaths::FileExists(MetadataPath))) return LegacyStem;
		return MetadataBelongsToAsset(MetadataPath, AssetData.GetObjectPathString()) ? LegacyStem : BuildStableExportStem(AssetData);
	}

	static bool ReserveOutputPath(const FString& Path, TSet<FString>* ReservedPathKeys)
	{
		if (!ReservedPathKeys) return true;
		FString Key = FPaths::ConvertRelativePathToFull(Path);
		Key.ReplaceInline(TEXT("\\"), TEXT("/"));
		Key.ToLowerInline();
		if (ReservedPathKeys->Contains(Key)) return false;
		ReservedPathKeys->Add(Key);
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

	static FString SaveTextAtomic(const FString& Text, const FString& Dir, const FString& AssetName, const FString& FileSuffix)
	{
		if (Text.IsEmpty() || AssetName.IsEmpty() || !EnsureDir(Dir)) return FString();
		const FString FinalPath = Dir / (AssetName + FileSuffix);
		const FString TemporaryPath = FinalPath + TEXT(".tmp");
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.DeleteFile(*TemporaryPath);
		if (!FFileHelper::SaveStringToFile(Text, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8)) return FString();
		if (!IFileManager::Get().Move(*FinalPath, *TemporaryPath, true, false, false, true))
		{
			PlatformFile.DeleteFile(*TemporaryPath);
			return FString();
		}
		return FinalPath;
	}

	static FString SaveAssetDocument(
		const FAssetData& AssetData,
		UObject* Asset,
		const FString& TechnicalDocument,
		const FString& Directory,
		const FString& FileSuffix,
		FString& OutMetadataPath,
		const bool bRequireMetadata,
		const bool bForceDisambiguation,
		TSet<FString>* ReservedPathKeys)
	{
		OutMetadataPath.Reset();
		if (!Asset || TechnicalDocument.IsEmpty()) return FString();

		const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
		const EReadAllExportMode Mode = Settings ? Settings->ExportMode : EReadAllExportMode::Compact;
		const bool bWriteMetadata = bRequireMetadata || !Settings || Settings->bWriteMetadataJson;
		const FString ExportStem = ResolveExportStem(AssetData, Directory, FileSuffix, bWriteMetadata, bForceDisambiguation);
		const FString DocumentPath = Directory / (ExportStem + FileSuffix);
		const FString MetadataPath = Directory / (ExportStem + TEXT(".meta.json"));
		if (!ReserveOutputPath(DocumentPath, ReservedPathKeys)
			|| (bWriteMetadata && !ReserveOutputPath(MetadataPath, ReservedPathKeys)))
		{
			UE_LOG(LogTemp, Error, TEXT("ReadAllandExplains detected an output path collision for %s"), *Asset->GetPathName());
			return FString();
		}

		const FReadAllAssetDocumentIR Document = FAssetInsightExporter::BuildDocument(AssetData, Asset, TechnicalDocument);
		const FString SavedPath = SaveText(Document.RenderMarkdown(Mode), Directory, ExportStem, FileSuffix);
		if (SavedPath.IsEmpty()) return FString();

		if (bWriteMetadata)
		{
			OutMetadataPath = SaveText(Document.RenderMetadataJson(Mode), Directory, ExportStem, TEXT(".meta.json"));
			if (OutMetadataPath.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("ReadAllandExplains could not write metadata for %s"), *Asset->GetPathName());
				return FString();
			}
		}
		return SavedPath;
	}

	// 尝试把单个资产按类型导出；返回：0=成功, 1=失败（类型支持但导出空或保存失败）, 2=跳过（类型不支持）
	enum class EExportResult : uint8 { Success, Failed, Skipped };

	static EExportResult ExportOneAsset(
		const FAssetData& AssetData,
		FString& OutSavedPath,
		FString& OutMetadataPath,
		const FString& ExportRootOverride = FString(),
		const bool bRequireMetadata = false,
		const bool bForceDisambiguation = false,
		TSet<FString>* ReservedPathKeys = nullptr)
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
			const FString Saved = SaveAssetDocument(AssetData, Blueprint, FBlueprintToTextExporter::ExportBlueprintToText(Blueprint), BlueprintDir, TEXT("_ReadableCode.txt"), OutMetadataPath, bRequireMetadata, bForceDisambiguation, ReservedPathKeys);
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		// 材质 / 材质实例（注意：UMaterialInstance 也是 UMaterialInterface）
		if (UMaterialInterface* MatIface = Cast<UMaterialInterface>(Obj))
		{
			const FString Saved = SaveAssetDocument(AssetData, MatIface, FMaterialToTextExporter::ExportMaterialToText(MatIface), MaterialDir, TEXT("_ReadableMaterial.md"), OutMetadataPath, bRequireMetadata, bForceDisambiguation, ReservedPathKeys);
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		// 材质函数
		if (UMaterialFunctionInterface* MatFunc = Cast<UMaterialFunctionInterface>(Obj))
		{
			const FString Saved = SaveAssetDocument(AssetData, MatFunc, FMaterialToTextExporter::ExportMaterialFunctionToText(MatFunc), MaterialDir, TEXT("_ReadableMaterialFunction.md"), OutMetadataPath, bRequireMetadata, bForceDisambiguation, ReservedPathKeys);
			if (Saved.IsEmpty()) return EExportResult::Failed;
			OutSavedPath = Saved;
			return EExportResult::Success;
		}

		// Niagara System / Emitter / Script（Module、Dynamic Input 等也属于 UNiagaraScript）
		if (Obj->IsA<UNiagaraSystem>() || Obj->IsA<UNiagaraEmitter>() || Obj->IsA<UNiagaraScript>())
		{
			const FString Saved = SaveAssetDocument(AssetData, Obj, FNiagaraToTextExporter::ExportNiagaraAssetToText(Obj), NiagaraDir, TEXT("_ReadableNiagara.md"), OutMetadataPath, bRequireMetadata, bForceDisambiguation, ReservedPathKeys);
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
				FCommonAssetToTextExporter::GetFileSuffix(Obj),
				OutMetadataPath,
				bRequireMetadata,
				bForceDisambiguation,
				ReservedPathKeys);
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
			|| AssetData.IsInstanceOf(UCurveTable::StaticClass())
			|| AssetData.IsInstanceOf(UEnum::StaticClass())
			|| AssetData.IsInstanceOf(UDataAsset::StaticClass());
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
	int32 AttemptedCount = 0;
	int32 SuccessCount = 0;
	int32 FailedCount = 0;
	int32 SkippedCount = 0;
	bool bIndexMarkdownWritten = false;
	bool bIndexJsonWritten = false;
	bool bReadmeWritten = false;
	bool bCompleteManifestWritten = false;
	bool bPublished = false;
	FString FirstSavedPath;
	TArray<FAssetData> ExportedAssets;
	TArray<FString> SavedPaths;
	TArray<FString> MetadataPaths;
	TArray<FString> Errors;
};

static FBatchExportResult ExportAssetDataList(
	const TArray<FAssetData>& AssetList,
	const FString& ExportRootOverride = FString(),
	const bool bRequireMetadata = false)
{
	FBatchExportResult R;
	R.AttemptedCount = AssetList.Num();
	TMap<FString, int32> CollisionGroupCounts;
	for (const FAssetData& AssetData : AssetList)
	{
		UObject* Asset = AssetData.GetAsset();
		if (!Asset) continue;
		FString Folder;
		if (Asset->IsA<UBlueprint>()) Folder = TEXT("Blueprints");
		else if (Asset->IsA<UMaterialInterface>() || Asset->IsA<UMaterialFunctionInterface>()) Folder = TEXT("Materials");
		else if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>()) Folder = TEXT("Niagara");
		else Folder = FCommonAssetToTextExporter::GetExportFolderName(Asset);
		FString CollisionKey = Folder + TEXT("/") + SanitizeExportStem(AssetData.AssetName.ToString());
		CollisionKey.ToLowerInline();
		++CollisionGroupCounts.FindOrAdd(CollisionKey);
	}

	TSet<FString> ReservedPathKeys;
	for (const FAssetData& AssetData : AssetList)
	{
		UObject* Asset = AssetData.GetAsset();
		FString Folder;
		if (Asset && Asset->IsA<UBlueprint>()) Folder = TEXT("Blueprints");
		else if (Asset && (Asset->IsA<UMaterialInterface>() || Asset->IsA<UMaterialFunctionInterface>())) Folder = TEXT("Materials");
		else if (Asset && (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>())) Folder = TEXT("Niagara");
		else if (Asset) Folder = FCommonAssetToTextExporter::GetExportFolderName(Asset);
		FString CollisionKey = Folder + TEXT("/") + SanitizeExportStem(AssetData.AssetName.ToString());
		CollisionKey.ToLowerInline();
		const bool bForceDisambiguation = CollisionGroupCounts.FindRef(CollisionKey) > 1;

		FString SavedPath;
		FString MetadataPath;
		const ReadAllandExplainsExportImpl::EExportResult Result = ReadAllandExplainsExportImpl::ExportOneAsset(
			AssetData,
			SavedPath,
			MetadataPath,
			ExportRootOverride,
			bRequireMetadata,
			bForceDisambiguation,
			&ReservedPathKeys);
		switch (Result)
		{
		case ReadAllandExplainsExportImpl::EExportResult::Success:
			++R.SuccessCount;
			if (R.FirstSavedPath.IsEmpty()) R.FirstSavedPath = SavedPath;
			R.ExportedAssets.Add(AssetData);
			R.SavedPaths.Add(SavedPath);
			R.MetadataPaths.Add(MetadataPath);
			break;
		case ReadAllandExplainsExportImpl::EExportResult::Failed:
			++R.FailedCount;
			R.Errors.Add(FString::Printf(TEXT("ASSET_EXPORT_FAILED:%s"), *AssetData.GetObjectPathString()));
			break;
		case ReadAllandExplainsExportImpl::EExportResult::Skipped:
		default:
			++R.SkippedCount;
			R.Errors.Add(FString::Printf(TEXT("ASSET_UNSUPPORTED:%s"), *AssetData.GetObjectPathString()));
			break;
		}
	}
	if (R.ExportedAssets.Num() > 0)
	{
		const FString IndexDir = ExportRootOverride.IsEmpty() ? GetExportRootDir() : ExportRootOverride;
		TArray<FString> IndexSavedPaths = R.SavedPaths;
		TArray<FString> IndexMetadataPaths = R.MetadataPaths;
		if (!ExportRootOverride.IsEmpty())
		{
			const FString RelativeBase = FPaths::ConvertRelativePathToFull(ExportRootOverride) + TEXT("/");
			auto MakeRelativePaths = [&RelativeBase](TArray<FString>& Paths)
			{
				for (FString& Path : Paths)
				{
					if (Path.IsEmpty()) continue;
					Path = FPaths::ConvertRelativePathToFull(Path);
					FPaths::MakePathRelativeTo(Path, *RelativeBase);
					Path.ReplaceInline(TEXT("\\"), TEXT("/"));
				}
			};
			MakeRelativePaths(IndexSavedPaths);
			MakeRelativePaths(IndexMetadataPaths);
		}
		const FString IndexText = FAssetInsightExporter::BuildBatchIndex(R.ExportedAssets, IndexSavedPaths, IndexMetadataPaths);
		R.bIndexMarkdownWritten = !SaveText(IndexText, IndexDir, TEXT("index"), TEXT(".md")).IsEmpty();
		const FString IndexJson = FAssetInsightExporter::BuildBatchIndexJson(R.ExportedAssets, IndexSavedPaths, IndexMetadataPaths);
		R.bIndexJsonWritten = !SaveText(IndexJson, IndexDir, TEXT("index"), TEXT(".json")).IsEmpty();
		if (!R.bIndexMarkdownWritten) R.Errors.Add(TEXT("INDEX_MARKDOWN_WRITE_FAILED"));
		if (!R.bIndexJsonWritten) R.Errors.Add(TEXT("INDEX_JSON_WRITE_FAILED"));
	}
	else
	{
		R.Errors.Add(TEXT("NO_ASSETS_EXPORTED"));
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
	const FString& PackId,
	const FString& PackDir,
	const FString& State,
	const TArray<FAssetData>& Roots,
	const FBatchExportResult& Result,
	const int32 DependencyDepth,
	const FString& OriginRequestId = FString(),
	const FString& BasePackId = FString(),
	bool* OutAllFilesReadable = nullptr)
{
	if (OutAllFilesReadable) *OutAllFilesReadable = true;
	TArray<FString> PackFiles;
	if (State != TEXT("writing"))
	{
		IFileManager::Get().FindFilesRecursive(PackFiles, *PackDir, TEXT("*"), true, false, true);
		const FString RootManifestPath = FPaths::ConvertRelativePathToFull(PackDir / TEXT("context-pack.json"));
		PackFiles.RemoveAll([&RootManifestPath](const FString& Path)
		{
			return FPaths::ConvertRelativePathToFull(Path).Equals(RootManifestPath, ESearchCase::IgnoreCase);
		});
		PackFiles.Sort();
	}

	TArray<TSharedPtr<FJsonValue>> FileValues;
	FString FingerprintSource;
	const FString RelativeBase = FPaths::ConvertRelativePathToFull(PackDir) + TEXT("/");
	for (const FString& FilePath : PackFiles)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
		{
			if (OutAllFilesReadable) *OutAllFilesReadable = false;
			continue;
		}
		FString RelativePath = FPaths::ConvertRelativePathToFull(FilePath);
		FPaths::MakePathRelativeTo(RelativePath, *RelativeBase);
		RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));
		const FString Fingerprint = FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num()).ToString().ToLower();

		TSharedRef<FJsonObject> File = MakeShared<FJsonObject>();
		File->SetStringField(TEXT("path"), RelativePath);
		File->SetNumberField(TEXT("size"), Bytes.Num());
		File->SetStringField(TEXT("fingerprint"), TEXT("sha1:") + Fingerprint);
		FileValues.Add(MakeShared<FJsonValueObject>(File));
		FingerprintSource += RelativePath + TEXT(":") + FString::FromInt(Bytes.Num()) + TEXT(":") + Fingerprint + TEXT("\n");
	}
	const FTCHARToUTF8 FingerprintUtf8(*FingerprintSource);
	const FString PackFingerprint = FSHA1::HashBuffer(FingerprintUtf8.Get(), FingerprintUtf8.Length()).ToString().ToLower();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("documentType"), TEXT("ReadAllandExplainsContextPack"));
	Root->SetStringField(TEXT("packId"), PackId);
	Root->SetStringField(TEXT("state"), State);
	Root->SetStringField(TEXT("createdUtc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("fingerprint"), TEXT("sha1:") + PackFingerprint);
	Root->SetNumberField(TEXT("dependencyDepth"), DependencyDepth);
	Root->SetNumberField(TEXT("attemptedAssetCount"), Result.AttemptedCount);
	Root->SetNumberField(TEXT("exportedAssetCount"), Result.ExportedAssets.Num());
	Root->SetNumberField(TEXT("failedCount"), Result.FailedCount);
	Root->SetNumberField(TEXT("skippedCount"), Result.SkippedCount);
	Root->SetArrayField(TEXT("files"), FileValues);
	if (!Result.Errors.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> ErrorValues;
		for (const FString& Error : Result.Errors)
		{
			ErrorValues.Add(MakeShared<FJsonValueString>(Error));
		}
		Root->SetArrayField(TEXT("errors"), ErrorValues);
	}
	if (!OriginRequestId.IsEmpty()) Root->SetStringField(TEXT("originRequestId"), OriginRequestId);
	if (!BasePackId.IsEmpty()) Root->SetStringField(TEXT("basePackId"), BasePackId);

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
		auto MakeRelativePath = [&RelativeBase](FString Path)
		{
			if (!Path.IsEmpty())
			{
				Path = FPaths::ConvertRelativePathToFull(Path);
				FPaths::MakePathRelativeTo(Path, *RelativeBase);
				Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			}
			return Path;
		};
		Asset->SetStringField(TEXT("exportFile"), MakeRelativePath(Result.SavedPaths.IsValidIndex(Index) ? Result.SavedPaths[Index] : FString()));
		if (Result.MetadataPaths.IsValidIndex(Index) && !Result.MetadataPaths[Index].IsEmpty())
		{
			Asset->SetStringField(TEXT("metadataFile"), MakeRelativePath(Result.MetadataPaths[Index]));
		}
		AssetValues.Add(MakeShared<FJsonValueObject>(Asset));
	}
	Root->SetArrayField(TEXT("assets"), AssetValues);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

static bool ValidateStagedContextPack(
	const TArray<FAssetData>& Roots,
	const FString& PackDir,
	const FBatchExportResult& Result,
	FString& OutError)
{
	if (Result.AttemptedCount <= 0)
	{
		OutError = TEXT("NO_ASSETS_ATTEMPTED");
		return false;
	}
	if (Result.SuccessCount != Result.AttemptedCount || Result.FailedCount != 0 || Result.SkippedCount != 0)
	{
		OutError = TEXT("PARTIAL_ASSET_EXPORT");
		return false;
	}
	if (Result.ExportedAssets.Num() != Result.AttemptedCount
		|| Result.SavedPaths.Num() != Result.AttemptedCount
		|| Result.MetadataPaths.Num() != Result.AttemptedCount)
	{
		OutError = TEXT("ASSET_FILE_MAPPING_INCOMPLETE");
		return false;
	}
	if (!Result.bIndexMarkdownWritten || !Result.bIndexJsonWritten || !Result.bReadmeWritten)
	{
		OutError = TEXT("REQUIRED_PACK_FILE_WRITE_FAILED");
		return false;
	}

	TSet<FString> ExportedObjectPaths;
	TSet<FString> FileKeys;
	for (int32 Index = 0; Index < Result.ExportedAssets.Num(); ++Index)
	{
		ExportedObjectPaths.Add(Result.ExportedAssets[Index].GetObjectPathString());
		for (const FString& Path : {Result.SavedPaths[Index], Result.MetadataPaths[Index]})
		{
			if (Path.IsEmpty() || IFileManager::Get().FileSize(*Path) <= 0)
			{
				OutError = TEXT("REQUIRED_ASSET_FILE_MISSING");
				return false;
			}
			FString Key = FPaths::ConvertRelativePathToFull(Path);
			Key.ReplaceInline(TEXT("\\"), TEXT("/"));
			Key.ToLowerInline();
			if (FileKeys.Contains(Key))
			{
				OutError = TEXT("DUPLICATE_ASSET_FILE_PATH");
				return false;
			}
			FileKeys.Add(Key);
		}
	}
	for (const FAssetData& Root : Roots)
	{
		if (!ExportedObjectPaths.Contains(Root.GetObjectPathString()))
		{
			OutError = TEXT("ROOT_ASSET_NOT_EXPORTED");
			return false;
		}
	}
	for (const TCHAR* RequiredFile : {TEXT("README.md"), TEXT("index.md"), TEXT("index.json")})
	{
		if (IFileManager::Get().FileSize(*(PackDir / RequiredFile)) <= 0)
		{
			OutError = FString::Printf(TEXT("REQUIRED_FILE_MISSING:%s"), RequiredFile);
			return false;
		}
	}
	TArray<FString> TemporaryFiles;
	IFileManager::Get().FindFilesRecursive(TemporaryFiles, *PackDir, TEXT("*.tmp"), true, false, true);
	if (!TemporaryFiles.IsEmpty())
	{
		OutError = TEXT("TEMPORARY_FILE_REMAINS");
		return false;
	}
	FString IndexJson;
	TSharedPtr<FJsonObject> IndexObject;
	if (!FFileHelper::LoadFileToString(IndexJson, *(PackDir / TEXT("index.json"))))
	{
		OutError = TEXT("INDEX_JSON_INVALID");
		return false;
	}
	const TSharedRef<TJsonReader<>> IndexReader = TJsonReaderFactory<>::Create(IndexJson);
	if (!FJsonSerializer::Deserialize(IndexReader, IndexObject) || !IndexObject.IsValid())
	{
		OutError = TEXT("INDEX_JSON_INVALID");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* IndexAssets = nullptr;
	if (!IndexObject->TryGetArrayField(TEXT("assets"), IndexAssets) || !IndexAssets || IndexAssets->Num() != Result.AttemptedCount)
	{
		OutError = TEXT("INDEX_ASSET_COUNT_MISMATCH");
		return false;
	}
	return true;
}

static FBatchExportResult ExportContextPack(
	const TArray<FAssetData>& Roots,
	FString& OutPackDir,
	const int32 DependencyDepthOverride = INDEX_NONE,
	const FString& OriginRequestId = FString(),
	const FString& BasePackId = FString())
{
	const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
	const int32 ConfiguredDepth = Settings ? Settings->ContextPackDependencyDepth : 2;
	const int32 DependencyDepth = FMath::Clamp(DependencyDepthOverride == INDEX_NONE ? ConfiguredDepth : DependencyDepthOverride, 0, 4);
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString RequestSuffix;
	if (!OriginRequestId.IsEmpty())
	{
		const FTCHARToUTF8 RequestUtf8(*OriginRequestId);
		RequestSuffix = TEXT("_") + LexToString(FIoHash::HashBuffer(RequestUtf8.Get(), RequestUtf8.Length())).Left(12);
	}
	const FString UniqueSuffix = TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const FString PackId = TEXT("ContextPack_") + Timestamp + RequestSuffix + UniqueSuffix;
	const FString PacksRoot = GetExportRootDir() / TEXT("ContextPacks");
	const FString FinalPackDir = PacksRoot / PackId;
	const FString TemporaryPackDir = PacksRoot / (PackId + TEXT(".tmp"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FBatchExportResult Result;
	OutPackDir.Reset();

	auto FailPack = [&](const FString& Error)
	{
		Result.Errors.AddUnique(Error);
		if (PlatformFile.DirectoryExists(*TemporaryPackDir))
		{
			SaveTextAtomic(
				BuildContextPackManifest(PackId, TemporaryPackDir, TEXT("failed"), Roots, Result, DependencyDepth, OriginRequestId, BasePackId),
				TemporaryPackDir,
				TEXT("context-pack"),
				TEXT(".json"));
		}
		UE_LOG(LogTemp, Error, TEXT("ReadAllandExplains Context Pack failed [%s]: %s"), *Error, *TemporaryPackDir);
	};

	if (!EnsureDir(PacksRoot) || PlatformFile.DirectoryExists(*FinalPackDir) || PlatformFile.DirectoryExists(*TemporaryPackDir) || !EnsureDir(TemporaryPackDir))
	{
		FailPack(TEXT("PACK_DIRECTORY_CREATE_FAILED"));
		return Result;
	}
	OutPackDir = TemporaryPackDir;
	if (SaveTextAtomic(
		BuildContextPackManifest(PackId, TemporaryPackDir, TEXT("writing"), Roots, Result, DependencyDepth, OriginRequestId, BasePackId),
		TemporaryPackDir,
		TEXT("context-pack"),
		TEXT(".json")).IsEmpty())
	{
		FailPack(TEXT("WRITING_MANIFEST_WRITE_FAILED"));
		return Result;
	}

	const TArray<FAssetData> Assets = CollectContextPackAssets(Roots, DependencyDepth);
	Result = ExportAssetDataList(Assets, TemporaryPackDir, true);

	FString Readme;
	Readme += TEXT("# ReadAllandExplains Context Pack\n\n");
	Readme += FString::Printf(TEXT("- 根资产：%d\n- 项目依赖递归层级：%d\n- 成功导出：%d\n- 失败：%d\n- 跳过：%d\n\n"),
		Roots.Num(), DependencyDepth, Result.SuccessCount, Result.FailedCount, Result.SkippedCount);
	Readme += TEXT("## 使用方式\n\n优先把本目录的 `context-pack.json`、`index.json` 和根资产文档交给 AI；需要分析具体节点、Renderer 或曲线时，再按需读取对应 `.meta.json`。推荐使用配套 ReadAllandExplains Skill 与 MCP，先读摘要、再读取目标片段，避免一次加载完整大文件。\n\n");
	Readme += TEXT("```text\n请把这个 ReadAllandExplains Context Pack 作为一个整体分析。先解释根资产的视觉目标与执行流程，再沿 index 中的真实依赖检查自定义模块、材质、Renderer 绑定与曲线。不要把同名参数直接当作已连接。\n```\n");
	Result.bReadmeWritten = !SaveText(Readme, TemporaryPackDir, TEXT("README"), TEXT(".md")).IsEmpty();
	if (!Result.bReadmeWritten) Result.Errors.Add(TEXT("README_WRITE_FAILED"));

	FString ValidationError;
	if (!ValidateStagedContextPack(Roots, TemporaryPackDir, Result, ValidationError))
	{
		FailPack(ValidationError);
		return Result;
	}

	bool bAllManifestFilesReadable = false;
	const FString CompleteManifest = BuildContextPackManifest(
		PackId,
		TemporaryPackDir,
		TEXT("complete"),
		Roots,
		Result,
		DependencyDepth,
		OriginRequestId,
		BasePackId,
		&bAllManifestFilesReadable);
	if (!bAllManifestFilesReadable)
	{
		FailPack(TEXT("MANIFEST_FILE_READ_FAILED"));
		return Result;
	}
	Result.bCompleteManifestWritten = !SaveTextAtomic(CompleteManifest, TemporaryPackDir, TEXT("context-pack"), TEXT(".json")).IsEmpty();
	if (!Result.bCompleteManifestWritten)
	{
		FailPack(TEXT("COMPLETE_MANIFEST_WRITE_FAILED"));
		return Result;
	}
	if (!PlatformFile.MoveFile(*FinalPackDir, *TemporaryPackDir)
		|| !PlatformFile.DirectoryExists(*FinalPackDir)
		|| PlatformFile.DirectoryExists(*TemporaryPackDir))
	{
		FailPack(TEXT("PACK_ATOMIC_PUBLISH_FAILED"));
		return Result;
	}

	Result.bPublished = true;
	OutPackDir = FinalPackDir;
	return Result;
}

namespace ReadAllandExplainsSyncLive
{
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 MaxRootAssets = 5;
	static constexpr int32 MaxDependencyDepth = 1;
	static FTSTicker::FDelegateHandle TickerHandle;

	static FString RootDir() { return GetExportRootDir() / TEXT("SyncLive"); }
	static FString PendingDir() { return RootDir() / TEXT("Pending"); }
	static FString ProcessingDir() { return RootDir() / TEXT("Processing"); }
	static FString ResultsDir() { return RootDir() / TEXT("Results"); }
	static FString ArchiveDir() { return RootDir() / TEXT("Archive"); }

	static void EnsureQueueDirectories()
	{
		EnsureDir(PendingDir());
		EnsureDir(ProcessingDir());
		EnsureDir(ResultsDir());
		EnsureDir(ArchiveDir());
	}

	static bool IsSafeRequestId(const FString& RequestId)
	{
		if (RequestId.IsEmpty() || RequestId.Len() > 64) return false;
		for (const TCHAR Character : RequestId)
		{
			const bool bAsciiLetter = (Character >= TEXT('A') && Character <= TEXT('Z')) || (Character >= TEXT('a') && Character <= TEXT('z'));
			const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
			if (!bAsciiLetter && !bDigit && Character != TEXT('-') && Character != TEXT('_')) return false;
		}
		return true;
	}

	static bool LoadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			OutError = TEXT("Could not read request file.");
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			OutError = TEXT("Request is not valid JSON.");
			return false;
		}
		return true;
	}

	static bool SaveJsonObjectAtomic(const FString& Directory, const FString& FileName, const TSharedRef<FJsonObject>& Object)
	{
		EnsureDir(Directory);
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		const FString FinalPath = Directory / FileName;
		const FString TemporaryPath = FinalPath + TEXT(".tmp");
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.DeleteFile(*TemporaryPath);
		if (!FFileHelper::SaveStringToFile(Output, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8)) return false;
		if (!IFileManager::Get().Move(*FinalPath, *TemporaryPath, true, false, false, true))
		{
			PlatformFile.DeleteFile(*TemporaryPath);
			return false;
		}
		return true;
	}

	static bool WriteResult(
		const FString& RequestId,
		const FString& State,
		const FString& ErrorCode,
		const FString& Message,
		const FString& PackDir = FString(),
		const FBatchExportResult* ExportResult = nullptr,
		const FString& BasePackId = FString(),
		const FString& BasePackFingerprint = FString())
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
		Result->SetStringField(TEXT("requestId"), RequestId);
		Result->SetStringField(TEXT("state"), State);
		Result->SetStringField(TEXT("completedUtc"), FDateTime::UtcNow().ToIso8601());
		Result->SetStringField(TEXT("message"), Message);
		if (!ErrorCode.IsEmpty()) Result->SetStringField(TEXT("errorCode"), ErrorCode);
		if (!PackDir.IsEmpty())
		{
			Result->SetStringField(TEXT("outputPackDir"), PackDir);
			Result->SetStringField(TEXT("outputPackId"), FPaths::GetCleanFilename(PackDir));
		}
		if (!BasePackId.IsEmpty()) Result->SetStringField(TEXT("basePackId"), BasePackId);
		if (!BasePackFingerprint.IsEmpty()) Result->SetStringField(TEXT("basePackFingerprint"), BasePackFingerprint);
		if (ExportResult)
		{
			Result->SetNumberField(TEXT("successCount"), ExportResult->SuccessCount);
			Result->SetNumberField(TEXT("failedCount"), ExportResult->FailedCount);
			Result->SetNumberField(TEXT("skippedCount"), ExportResult->SkippedCount);
		}
		if (!SaveJsonObjectAtomic(ResultsDir(), RequestId + TEXT(".json"), Result))
		{
			UE_LOG(LogTemp, Error, TEXT("ReadAllandExplains SyncLive could not write result for request %s"), *RequestId);
			return false;
		}
		return true;
	}

	static void ArchiveRequest(const FString& ProcessingPath, const FString& RequestId)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const FString ArchivePath = ArchiveDir() / (RequestId + TEXT(".json"));
		if (PlatformFile.FileExists(*ArchivePath)) PlatformFile.DeleteFile(*ArchivePath);
		if (!PlatformFile.MoveFile(*ArchivePath, *ProcessingPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("ReadAllandExplains SyncLive could not archive request %s"), *RequestId);
		}
	}

	static bool FindPublishedPackForRequest(
		const FString& RequestId,
		FString& OutPackDir,
		FBatchExportResult& OutResult)
	{
		const FString PacksRoot = GetExportRootDir() / TEXT("ContextPacks");
		TArray<FString> PackDirectories;
		IFileManager::Get().FindFiles(PackDirectories, *(PacksRoot / TEXT("*")), false, true);
		PackDirectories.Sort();
		for (int32 Index = PackDirectories.Num() - 1; Index >= 0; --Index)
		{
			const FString PackId = PackDirectories[Index];
			if (PackId.EndsWith(TEXT(".tmp"), ESearchCase::IgnoreCase)) continue;
			const FString PackDir = PacksRoot / PackId;
			TSharedPtr<FJsonObject> Manifest;
			FString ParseError;
			if (!LoadJsonObject(PackDir / TEXT("context-pack.json"), Manifest, ParseError)) continue;
			FString State;
			FString OriginRequestId;
			FString ManifestPackId;
			Manifest->TryGetStringField(TEXT("state"), State);
			Manifest->TryGetStringField(TEXT("originRequestId"), OriginRequestId);
			Manifest->TryGetStringField(TEXT("packId"), ManifestPackId);
			if (State != TEXT("complete") || OriginRequestId != RequestId || ManifestPackId != PackId) continue;

			double ExportedCount = 0;
			double FailedCount = 0;
			double SkippedCount = 0;
			Manifest->TryGetNumberField(TEXT("exportedAssetCount"), ExportedCount);
			Manifest->TryGetNumberField(TEXT("failedCount"), FailedCount);
			Manifest->TryGetNumberField(TEXT("skippedCount"), SkippedCount);
			OutResult.SuccessCount = static_cast<int32>(ExportedCount);
			OutResult.FailedCount = static_cast<int32>(FailedCount);
			OutResult.SkippedCount = static_cast<int32>(SkippedCount);
			OutResult.bPublished = true;
			OutPackDir = PackDir;
			return true;
		}
		return false;
	}

	static void RecoverInterruptedRequests()
	{
		EnsureQueueDirectories();
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(ProcessingDir() / TEXT("*.json")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			const FString RequestId = FPaths::GetBaseFilename(File);
			const FString ProcessingPath = ProcessingDir() / File;
			const FString ExistingResultPath = ResultsDir() / (RequestId + TEXT(".json"));
			if (FPaths::FileExists(ExistingResultPath))
			{
				ArchiveRequest(ProcessingPath, RequestId);
				continue;
			}

			FString BasePackId;
			FString BasePackFingerprint;
			TSharedPtr<FJsonObject> Request;
			FString RequestError;
			if (LoadJsonObject(ProcessingPath, Request, RequestError))
			{
				Request->TryGetStringField(TEXT("basePackId"), BasePackId);
				Request->TryGetStringField(TEXT("basePackFingerprint"), BasePackFingerprint);
			}

			FString PublishedPackDir;
			FBatchExportResult RecoveredResult;
			const bool bPackPublished = FindPublishedPackForRequest(RequestId, PublishedPackDir, RecoveredResult);
			const bool bResultWritten = bPackPublished
				? WriteResult(
					RequestId,
					TEXT("complete"),
					FString(),
					TEXT("Targeted Context Pack completed; the result was recovered after an interrupted result write."),
					PublishedPackDir,
					&RecoveredResult,
					BasePackId,
					BasePackFingerprint)
				: WriteResult(
					RequestId,
					TEXT("failed"),
					TEXT("EDITOR_INTERRUPTED"),
					TEXT("The editor stopped before publishing this request. Submit a new authorized request to retry."),
					FString(),
					nullptr,
					BasePackId,
					BasePackFingerprint);
			if (bResultWritten) ArchiveRequest(ProcessingPath, RequestId);
		}
	}

	static void RejectRequest(const FString& ProcessingPath, const FString& RequestId, const FString& ErrorCode, const FString& Message)
	{
		if (WriteResult(RequestId, TEXT("rejected"), ErrorCode, Message))
		{
			ArchiveRequest(ProcessingPath, RequestId);
		}
		UE_LOG(LogTemp, Warning, TEXT("ReadAllandExplains SyncLive rejected %s: %s"), *RequestId, *Message);
	}

	static bool ProcessOneRequest(float)
	{
		const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
		if (!Settings || !Settings->bEnableSyncLiveLite) return true;
		if (GEditor && GEditor->PlayWorld) return true;

		EnsureQueueDirectories();
		RecoverInterruptedRequests();
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(PendingDir() / TEXT("*.json")), true, false);
		if (Files.IsEmpty()) return true;
		Files.Sort();

		const FString File = Files[0];
		const FString FileRequestId = FPaths::GetBaseFilename(File);
		const FString PendingPath = PendingDir() / File;
		const FString ProcessingPath = ProcessingDir() / File;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.MoveFile(*ProcessingPath, *PendingPath)) return true;

		TSharedPtr<FJsonObject> Request;
		FString ParseError;
		if (!LoadJsonObject(ProcessingPath, Request, ParseError))
		{
			RejectRequest(ProcessingPath, FileRequestId, TEXT("INVALID_REQUEST_JSON"), ParseError);
			return true;
		}

		FString RequestId;
		Request->TryGetStringField(TEXT("requestId"), RequestId);
		if (!IsSafeRequestId(RequestId) || RequestId != FileRequestId)
		{
			RejectRequest(ProcessingPath, FileRequestId, TEXT("REQUEST_ID_INVALID"), TEXT("requestId must match the safe request filename."));
			return true;
		}
		double RequestSchema = 0;
		Request->TryGetNumberField(TEXT("schemaVersion"), RequestSchema);
		if (static_cast<int32>(RequestSchema) != SchemaVersion)
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("REQUEST_SCHEMA_UNSUPPORTED"), TEXT("Unsupported SyncLive request schema."));
			return true;
		}
		bool bPermissionGranted = false;
		Request->TryGetBoolField(TEXT("permissionGranted"), bPermissionGranted);
		FString Mode;
		Request->TryGetStringField(TEXT("mode"), Mode);
		if (!bPermissionGranted || Mode != TEXT("targeted_context_pack"))
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("PERMISSION_REQUIRED"), TEXT("Only explicitly authorized targeted_context_pack requests are accepted."));
			return true;
		}

		double RequestedDepth = 0;
		Request->TryGetNumberField(TEXT("dependencyDepth"), RequestedDepth);
		const int32 DependencyDepth = static_cast<int32>(RequestedDepth);
		if (DependencyDepth < 0 || DependencyDepth > MaxDependencyDepth || RequestedDepth != static_cast<double>(DependencyDepth))
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("DEPENDENCY_DEPTH_INVALID"), TEXT("dependencyDepth must be an integer from 0 to 1."));
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* AssetValues = nullptr;
		if (!Request->TryGetArrayField(TEXT("assetPaths"), AssetValues) || !AssetValues || AssetValues->IsEmpty() || AssetValues->Num() > MaxRootAssets)
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("ASSET_LIMIT_INVALID"), TEXT("assetPaths must contain between 1 and 5 assets."));
			return true;
		}

		TArray<FAssetData> Roots;
		TSet<FString> SeenPaths;
		for (const TSharedPtr<FJsonValue>& Value : *AssetValues)
		{
			FString ObjectPath;
			if (!Value.IsValid() || !Value->TryGetString(ObjectPath))
			{
				RejectRequest(ProcessingPath, RequestId, TEXT("ASSET_PATH_INVALID"), TEXT("Every asset path must be a string."));
				return true;
			}
			ObjectPath.TrimStartAndEndInline();
			const FString ObjectName = FPaths::GetCleanFilename(ObjectPath);
			if (!ObjectPath.StartsWith(TEXT("/Game/")) || ObjectPath.Contains(TEXT("..")) || ObjectPath.Contains(TEXT("\n")) || ObjectPath.Contains(TEXT("\r")) || !ObjectName.Contains(TEXT(".")))
			{
				RejectRequest(ProcessingPath, RequestId, TEXT("ASSET_PATH_INVALID"), TEXT("Only canonical /Game/Package.Asset object paths are allowed."));
				return true;
			}
			if (SeenPaths.Contains(ObjectPath)) continue;
			SeenPaths.Add(ObjectPath);
			UObject* Asset = FSoftObjectPath(ObjectPath).TryLoad();
			if (!Asset)
			{
				RejectRequest(ProcessingPath, RequestId, TEXT("ASSET_NOT_FOUND"), FString::Printf(TEXT("Could not load asset: %s"), *ObjectPath));
				return true;
			}
			const FAssetData AssetData(Asset);
			if (!IsSupportedAssetData(AssetData))
			{
				RejectRequest(ProcessingPath, RequestId, TEXT("ASSET_UNSUPPORTED"), FString::Printf(TEXT("Unsupported asset type: %s"), *ObjectPath));
				return true;
			}
			Roots.Add(AssetData);
		}

		FString BasePackId;
		FString BasePackFingerprint;
		Request->TryGetStringField(TEXT("basePackId"), BasePackId);
		Request->TryGetStringField(TEXT("basePackFingerprint"), BasePackFingerprint);
		if (BasePackId.IsEmpty() || BasePackFingerprint.IsEmpty())
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("BASE_PACK_REQUIRED"), TEXT("The authorized request must identify its base Pack and fingerprint."));
			return true;
		}
		if (!IsSafeRequestId(BasePackId))
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("BASE_PACK_INVALID"), TEXT("basePackId contains unsafe characters."));
			return true;
		}
		const FString BaseManifestPath = GetExportRootDir() / TEXT("ContextPacks") / BasePackId / TEXT("context-pack.json");
		TSharedPtr<FJsonObject> BaseManifest;
		FString BaseManifestError;
		if (!LoadJsonObject(BaseManifestPath, BaseManifest, BaseManifestError))
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("BASE_PACK_NOT_FOUND"), TEXT("The authorized base Pack is unavailable."));
			return true;
		}
		FString ActualBasePackId;
		FString ActualBaseFingerprint;
		FString BaseState;
		BaseManifest->TryGetStringField(TEXT("packId"), ActualBasePackId);
		BaseManifest->TryGetStringField(TEXT("fingerprint"), ActualBaseFingerprint);
		BaseManifest->TryGetStringField(TEXT("state"), BaseState);
		if (ActualBasePackId != BasePackId || ActualBaseFingerprint != BasePackFingerprint || BaseState != TEXT("complete"))
		{
			RejectRequest(ProcessingPath, RequestId, TEXT("BASE_PACK_FINGERPRINT_MISMATCH"), TEXT("The authorized base Pack changed, is incomplete, or no longer matches its fingerprint."));
			return true;
		}

		FString PackDir;
		const FBatchExportResult ExportResult = ExportContextPack(Roots, PackDir, DependencyDepth, RequestId, BasePackId);
		const bool bResultWritten = WriteResult(
			RequestId,
			ExportResult.bPublished ? TEXT("complete") : TEXT("failed"),
			ExportResult.bPublished ? FString() : TEXT("EXPORT_FAILED"),
			ExportResult.bPublished ? TEXT("Targeted Context Pack completed.") : TEXT("Targeted Context Pack did not publish successfully."),
			PackDir,
			&ExportResult,
			BasePackId,
			BasePackFingerprint);
		if (bResultWritten) ArchiveRequest(ProcessingPath, RequestId);
		UE_LOG(LogTemp, Display, TEXT("ReadAllandExplains SyncLive %s: request=%s output=%s"), ExportResult.bPublished ? TEXT("complete") : TEXT("failed"), *RequestId, *PackDir);
		return true;
	}

	static void Start()
	{
		RecoverInterruptedRequests();
		const UReadAllandExplainsSettings* Settings = GetDefault<UReadAllandExplainsSettings>();
		const float Interval = FMath::Clamp(Settings ? Settings->SyncLivePollIntervalSeconds : 1.0f, 0.5f, 10.0f);
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ProcessOneRequest), Interval);
	}

	static void Stop()
	{
		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}
	}
}

static void NotifyContextPackResult(const FBatchExportResult& Result, const FString& PackDir)
{
	const FText SummaryFormat = Result.bPublished
		? LOCTEXT("ContextPackSummaryComplete", "AI Context Pack 生成完成：资产 {0}，失败 {1}，跳过 {2}。\n输出目录：{3}")
		: LOCTEXT("ContextPackSummaryFailed", "AI Context Pack 未发布：资产 {0}，失败 {1}，跳过 {2}。\n诊断目录：{3}");
	FNotificationInfo Info(FText::Format(
		SummaryFormat,
		FText::AsNumber(Result.SuccessCount),
		FText::AsNumber(Result.FailedCount),
		FText::AsNumber(Result.SkippedCount),
		FText::FromString(PackDir)));
	Info.ExpireDuration = Result.bPublished ? 6.0f : 10.0f;
	Info.Hyperlink = FSimpleDelegate::CreateLambda([PackDir]() { FPlatformProcess::ExploreFolder(*PackDir); });
	Info.HyperlinkText = Result.bPublished
		? LOCTEXT("OpenContextPackDir", "打开 Context Pack")
		: LOCTEXT("OpenFailedContextPackDir", "打开诊断目录");
	FSlateNotificationManager::Get().AddNotification(Info);
	if (Result.bPublished) FPlatformProcess::ExploreFolder(*PackDir);
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
	if (Result.bPublished)
	{
		UE_LOG(LogTemp, Display, TEXT("ReadAllandExplains Context Pack published: success=%d failed=%d skipped=%d output=%s"),
			Result.SuccessCount, Result.FailedCount, Result.SkippedCount, *PackDir);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ReadAllandExplains Context Pack failed: success=%d failed=%d skipped=%d diagnostics=%s"),
			Result.SuccessCount, Result.FailedCount, Result.SkippedCount, *PackDir);
	}
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
	ReadAllandExplainsSyncLive::Start();
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FReadAllandExplainsModule::RegisterMenus));
}

void FReadAllandExplainsModule::ShutdownModule()
{
	ReadAllandExplainsSyncLive::Stop();
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