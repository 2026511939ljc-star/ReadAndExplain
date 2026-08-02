// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;

/** Lightweight exporters for common art assets that do not own editable node graphs. */
class READALLANDEXPLAINS_API FCommonAssetToTextExporter
{
public:
	static bool Supports(const UObject* Asset);
	static FString ExportAssetToText(UObject* Asset);
	static FString GetExportFolderName(const UObject* Asset);
	static FString GetFileSuffix(const UObject* Asset);
};
