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
	FString ReferencePath;
	FString CalleeGraphId;
	FString SelectedVersion;
	bool bEnabled = true;
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

/** One persistent editable renderer property. */
struct FReadAllNiagaraPropertyIR
{
	FString Name;
	FString Type;
	FString Category;
	FString Value;
};

/** One renderer attribute binding and the Niagara variable it consumes. */
struct FReadAllNiagaraRendererBindingIR
{
	FString DisplayName;
	FString VariableName;
	FString DataSetName;
	FString Type;
	FString SourceMode;
	bool bValid = false;
	bool bExistsOnSource = false;
};

/** Renderer facts collected from a specific emitter version. */
struct FReadAllNiagaraRendererIR
{
	FString Id;
	FString EmitterPath;
	FString EmitterVersion;
	int32 Index = 0;
	FString Name;
	FString ClassPath;
	FString SourceMode;
	bool bEnabled = true;
	TArray<FString> Materials;
	TArray<FReadAllNiagaraRendererBindingIR> Bindings;
	TArray<FReadAllNiagaraPropertyIR> Properties;
};

/** One editable rich-curve key. */
struct FReadAllNiagaraCurveKeyIR
{
	float Time = 0.0f;
	float Value = 0.0f;
	FString Interpolation;
	FString TangentMode;
	FString TangentWeightMode;
	float ArriveTangent = 0.0f;
	float ArriveTangentWeight = 0.0f;
	float LeaveTangent = 0.0f;
	float LeaveTangentWeight = 0.0f;
};

/** One channel from a Niagara float/vector/color curve data interface. */
struct FReadAllNiagaraCurveChannelIR
{
	FString Name;
	FString PreInfinityExtrapolation;
	FString PostInfinityExtrapolation;
	TArray<FReadAllNiagaraCurveKeyIR> Keys;
};

/** A Niagara curve data interface with all source keys retained. */
struct FReadAllNiagaraCurveIR
{
	FString Id;
	FString Fingerprint;
	FString ObjectPath;
	FString ClassPath;
	FString OwnerGraphId;
	FString CurveAssetPath;
	FString ExposedName;
	TArray<FString> UsedBy;
	bool bUseLUT = false;
	bool bExposeCurve = false;
	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	TArray<FReadAllNiagaraCurveChannelIR> Channels;
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
	TArray<FReadAllNiagaraRendererIR> NiagaraRenderers;
	TArray<FReadAllNiagaraCurveIR> NiagaraCurves;

	/** Artist skips technical graph dumps; all modes retain facts, relationships and AI hand-off text. */
	FString RenderMarkdown(EReadAllExportMode Mode) const;

	/** Small machine-readable sidecar. Technical Markdown is referenced, not duplicated. */
	FString RenderMetadataJson(EReadAllExportMode Mode) const;
};