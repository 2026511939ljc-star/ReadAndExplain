// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;
struct FAssetData;

/** A parameter-like value used to find possible hand-offs between asset systems. */
struct FReadAllParameterClue
{
	FString Name;
	FString Kind;
	FString Value;
};

/** Adds the artist-facing explanation, direct dependency graph and AI hand-off text. */
class READALLANDEXPLAINS_API FAssetInsightExporter
{
public:
	static FString DecorateDocument(const FAssetData& AssetData, UObject* Asset, const FString& TechnicalDocument);
	static FString BuildBatchIndex(const TArray<FAssetData>& Assets, const TArray<FString>& SavedPaths);
	static void CollectParameterClues(UObject* Asset, TArray<FReadAllParameterClue>& OutClues);
};
