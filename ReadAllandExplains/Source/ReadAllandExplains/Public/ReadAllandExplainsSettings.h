// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ReadAllandExplainsSettings.generated.h"

UENUM()
enum class EReadAllExportMode : uint8
{
	Artist UMETA(DisplayName = "Artist - 美术速读"),
	Compact UMETA(DisplayName = "Compact - 精简低 Token"),
	Full UMETA(DisplayName = "Full - 完整技术信息"),
	Reconstruction UMETA(DisplayName = "Reconstruction - 重建级数据")
};

UENUM()
enum class EReadAllPromptMode : uint8
{
	Explain UMETA(DisplayName = "Explain - 解释资产"),
	Review UMETA(DisplayName = "Review - 检查问题"),
	Optimize UMETA(DisplayName = "Optimize - 优化建议"),
	Trace UMETA(DisplayName = "Trace - 追踪因果"),
	Custom UMETA(DisplayName = "Custom - 自定义")
};

/** Project-local export preferences. Available under Editor Preferences > Plugins. */
UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "ReadAllandExplains"))
class READALLANDEXPLAINS_API UReadAllandExplainsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif

	/** Compact is the recommended default. Reconstruction preserves the largest amount of graph data. */
	UPROPERTY(EditAnywhere, Config, Category = "Export", meta = (DisplayName = "导出模式"))
	EReadAllExportMode ExportMode = EReadAllExportMode::Compact;

	/** Write a small machine-readable sidecar next to every Markdown/text export. */
	UPROPERTY(EditAnywhere, Config, Category = "Export", meta = (DisplayName = "写入 asset.meta.json"))
	bool bWriteMetadataJson = true;

	/** Controls the AI hand-off text appended to each exported document. */
	UPROPERTY(EditAnywhere, Config, Category = "AI", meta = (DisplayName = "AI 提示词模式"))
	EReadAllPromptMode PromptMode = EReadAllPromptMode::Explain;

	UPROPERTY(EditAnywhere, Config, Category = "AI", meta = (DisplayName = "自定义提示词", MultiLine = true, EditCondition = "PromptMode == EReadAllPromptMode::Custom", EditConditionHides))
	FString CustomPrompt;
};

READALLANDEXPLAINS_API FString ReadAllExportModeToString(EReadAllExportMode Mode);
READALLANDEXPLAINS_API FString ReadAllPromptModeToString(EReadAllPromptMode Mode);
READALLANDEXPLAINS_API bool ReadAllExportModeIncludes(EReadAllExportMode Current, EReadAllExportMode Required);