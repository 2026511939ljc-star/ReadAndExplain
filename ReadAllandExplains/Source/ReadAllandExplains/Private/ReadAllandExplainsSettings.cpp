// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReadAllandExplainsSettings.h"

#define LOCTEXT_NAMESPACE "ReadAllandExplainsSettings"

#if WITH_EDITOR
FText UReadAllandExplainsSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "ReadAllandExplains");
}

FText UReadAllandExplainsSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "将 UE 资产导出为适合 AI 阅读的 Markdown 与结构化元数据；推荐使用 Compact 精简模式。");
}
#endif

FString ReadAllExportModeToString(const EReadAllExportMode Mode)
{
	switch (Mode)
	{
	case EReadAllExportMode::Artist: return TEXT("Artist");
	case EReadAllExportMode::Compact: return TEXT("Compact");
	case EReadAllExportMode::Full: return TEXT("Full");
	case EReadAllExportMode::Reconstruction: return TEXT("Reconstruction");
	default: return TEXT("Compact");
	}
}

FString ReadAllPromptModeToString(const EReadAllPromptMode Mode)
{
	switch (Mode)
	{
	case EReadAllPromptMode::Explain: return TEXT("Explain");
	case EReadAllPromptMode::Review: return TEXT("Review");
	case EReadAllPromptMode::Optimize: return TEXT("Optimize");
	case EReadAllPromptMode::Trace: return TEXT("Trace");
	case EReadAllPromptMode::Custom: return TEXT("Custom");
	default: return TEXT("Explain");
	}
}

bool ReadAllExportModeIncludes(const EReadAllExportMode Current, const EReadAllExportMode Required)
{
	return static_cast<uint8>(Current) >= static_cast<uint8>(Required);
}

#undef LOCTEXT_NAMESPACE
