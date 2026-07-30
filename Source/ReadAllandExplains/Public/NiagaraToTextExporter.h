// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;

/** Exports editable Niagara source assets to deterministic AI-readable documents. */
class READALLANDEXPLAINS_API FNiagaraToTextExporter
{
public:
	/** Supports UNiagaraSystem, UNiagaraEmitter and UNiagaraScript. */
	static FString ExportNiagaraAssetToText(UObject* NiagaraAsset);
};
