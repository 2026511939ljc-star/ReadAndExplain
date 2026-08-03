// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDocumentIR.h"

class UObject;
struct FAssetData;

/** Adds the artist-facing explanation, direct dependency graph and AI hand-off text. */
class READALLANDEXPLAINS_API FAssetInsightExporter
{
public:
	static FReadAllAssetDocumentIR BuildDocument(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument);
	static FString DecorateDocument(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument);
	static FString BuildMetadataJson(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument);
	static FString BuildBatchIndex(const TArray<FAssetData>& Assets, const TArray<FString>& SavedPaths);
	static FString BuildBatchIndexJson(const TArray<FAssetData>& Assets, const TArray<FString>& SavedPaths);
	static void CollectParameterClues(UObject* Asset, TArray<FReadAllParameterClue>& OutClues);
};
