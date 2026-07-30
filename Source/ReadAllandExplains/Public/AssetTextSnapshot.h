// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UObject;
class UStruct;

/**
 * Deterministic, source-oriented text snapshots used by the AI-readable exporters.
 * Generated/compiled caches are intentionally omitted; editable UObject properties,
 * graph nodes, pins, defaults and links are retained.
 */
class READALLANDEXPLAINS_API FAssetTextSnapshot
{
public:
	/** Export all persistent reflected properties on an object. */
	static FString ExportObjectProperties(const UObject* Object, const FString& Heading);

	/** Export all persistent reflected properties in a UStruct instance. */
	static FString ExportStructProperties(
		const UStruct* Struct,
		const void* StructData,
		UObject* Owner,
		const FString& Heading);

	/** Export graph nodes, node properties, every pin/default/link and native UE clipboard text. */
	static FString ExportGraph(
		const UEdGraph* Graph,
		const FString& Heading,
		bool bIncludeNativeClipboardText = true);

	/** Markdown-safe single-line representation which preserves embedded newlines as \\n. */
	static FString MarkdownCell(const FString& Text);
};
