// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReadAllandExplainsSettings.h"

/** A parameter-like value used to find possible hand-offs between asset systems. */
struct FReadAllParameterClue
{
	FString Name;
	FString Kind;
	FString Value;
};

/** A stable graph pin shared by Markdown, JSON and future MCP output. */
struct FReadAllGraphPinIR
{
	FString Id;
	FString Name;
	FString Direction;
	FString Type;
	FString DefaultValue;
};

/** One graph node with stable identity, semantic title and all visible pins. */
struct FReadAllGraphNodeIR
{
	FString Id;
	FString Name;
	FString ClassName;
	FString Title;
	FString Comment;
	int32 PositionX = 0;
	int32 PositionY = 0;
	TArray<FReadAllGraphPinIR> Pins;
};

/** A directed pin-to-pin connection. Kind distinguishes data, exec, root and reroute links. */
struct FReadAllGraphLinkIR
{
	FString FromNodeId;
	FString FromPinId;
	FString ToNodeId;
	FString ToPinId;
	FString Kind;
};

/** A format-neutral graph containing nodes, pins and directed links. */
struct FReadAllGraphIR
{
	FString Id;
	FString Name;
	FString Kind;
	TArray<FReadAllGraphNodeIR> Nodes;
	TArray<FReadAllGraphLinkIR> Links;
};

/**
 * Small format-neutral representation shared by Markdown, JSON and future MCP output.
 * Exporters collect facts once; renderers decide how much detail to expose.
 */
struct READALLANDEXPLAINS_API FReadAllAssetDocumentIR
{
	FString AssetName;
	FString ObjectPath;
	FString ClassPath;
	FString AssetKind;
	FString ArtistFocus;
	FString SuggestedPrompt;
	FString TechnicalMarkdown;
	EReadAllPromptMode PromptMode = EReadAllPromptMode::Explain;

	TArray<FReadAllParameterClue> ParameterClues;
	TArray<FString> FeatureTags;
	TArray<FString> Dependencies;
	TArray<FString> Referencers;
	TArray<FReadAllGraphIR> Graphs;

	/** Artist skips technical graph dumps; all modes retain facts, relationships and AI hand-off text. */
	FString RenderMarkdown(EReadAllExportMode Mode) const;

	/** Small machine-readable sidecar. Technical Markdown is referenced, not duplicated. */
	FString RenderMetadataJson(EReadAllExportMode Mode) const;
};
