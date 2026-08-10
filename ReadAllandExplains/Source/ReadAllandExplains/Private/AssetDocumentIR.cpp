// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDocumentIR.h"

#include "AssetTextSnapshot.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ReadAllDocumentIRImpl
{
	static FString MermaidLabel(FString Value)
	{
		Value.ReplaceInline(TEXT("\""), TEXT("'"));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Value;
	}

	static TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	static void AppendParameterTable(const TArray<FReadAllParameterClue>& Clues, FString& Out)
	{
		Out += TEXT("## 参数线索\n\n");
		Out += TEXT("> 以下参数来自资产的实际可编辑或公开数据，可作为跨系统追踪的起点；同名不等于已连接。\n\n");
		Out += TEXT("| 参数/入口 | 类型 | 当前值/说明 |\n");
		Out += TEXT("|-----------|------|--------------|\n");
		for (const FReadAllParameterClue& Clue : Clues)
		{
			Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Clue.Name)
				+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Clue.Kind)
				+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Clue.Value) + TEXT(" |\n");
		}
		if (Clues.Num() == 0)
		{
			Out += TEXT("| (未发现可公开参数) | | |\n");
		}
		Out += TEXT("\n");
	}

	static void AppendRelationships(const FReadAllAssetDocumentIR& Document, FString& Out)
	{
		Out += TEXT("---\n\n## 直接依赖 / 反向引用\n\n");
		Out += TEXT("> `依赖` 表示当前资产使用了谁；`反向引用` 表示谁正在使用当前资产。\n\n");
		Out += FString::Printf(TEXT("- 直接依赖：%d\n- 反向引用：%d\n\n"), Document.Dependencies.Num(), Document.Referencers.Num());
		Out += TEXT("```mermaid\ngraph LR\n");
		Out += TEXT("  ROOT[\"") + MermaidLabel(Document.AssetName) + TEXT("\"]\n");
		for (int32 Index = 0; Index < Document.Dependencies.Num(); ++Index)
		{
			Out += FString::Printf(TEXT("  ROOT --> D%d[\"%s\"]\n"), Index, *MermaidLabel(Document.Dependencies[Index]));
		}
		for (int32 Index = 0; Index < Document.Referencers.Num(); ++Index)
		{
			Out += FString::Printf(TEXT("  R%d[\"%s\"] --> ROOT\n"), Index, *MermaidLabel(Document.Referencers[Index]));
		}
		if (Document.Dependencies.Num() == 0 && Document.Referencers.Num() == 0)
		{
			Out += TEXT("  ROOT\n");
		}
		Out += TEXT("```\n\n");
	}

	static void AppendGraphIR(const TArray<FReadAllGraphIR>& Graphs, const EReadAllExportMode Mode, FString& Out)
	{
		if (Graphs.IsEmpty())
		{
			return;
		}

		Out += TEXT("---\n\n## 结构化图 IR\n\n");
		Out += TEXT("> 节点、引脚和连线由导出器一次采集，并同时供 Markdown、JSON 与未来 MCP 使用。\n\n");
		Out += TEXT("| 图 | 类型 | 节点 | 引脚 | 连线 |\n");
		Out += TEXT("|----|------|-----:|-----:|-----:|\n");
		for (const FReadAllGraphIR& Graph : Graphs)
		{
			int32 PinCount = 0;
			for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
			{
				PinCount += Node.Pins.Num();
			}
			Out += FString::Printf(TEXT("| %s | %s | %d | %d | %d |\n"),
				*FAssetTextSnapshot::MarkdownCell(Graph.Name),
				*FAssetTextSnapshot::MarkdownCell(Graph.Kind),
				Graph.Nodes.Num(),
				PinCount,
				Graph.Links.Num());
		}
		Out += TEXT("\n");

		if (!ReadAllExportModeIncludes(Mode, EReadAllExportMode::Full))
		{
			Out += TEXT("- 当前模式仅显示图统计；完整节点、引脚和连线已写入 `.meta.json`。\n\n");
			return;
		}

		for (const FReadAllGraphIR& Graph : Graphs)
		{
			Out += TEXT("### ") + Graph.Name + TEXT("\n\n");
			Out += TEXT("#### 节点\n\n| Id | 标题 | 类 | 位置 | 引脚数 |\n|----|------|----|------|------:|\n");
			for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
			{
				Out += FString::Printf(TEXT("| %s | %s | %s | (%d, %d) | %d |\n"),
					*FAssetTextSnapshot::MarkdownCell(Node.Id),
					*FAssetTextSnapshot::MarkdownCell(Node.Title),
					*FAssetTextSnapshot::MarkdownCell(Node.ClassName),
					Node.PositionX,
					Node.PositionY,
					Node.Pins.Num());
			}

			Out += TEXT("\n#### 连线\n\n| 来源 | 目标 | 类型 |\n|------|------|------|\n");
			for (const FReadAllGraphLinkIR& Link : Graph.Links)
			{
				Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Link.FromNodeId + TEXT(".") + Link.FromPinId)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Link.ToNodeId + TEXT(".") + Link.ToPinId)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Link.Kind) + TEXT(" |\n");
			}
			if (Graph.Links.IsEmpty())
			{
				Out += TEXT("| (无连线) | | |\n");
			}
			Out += TEXT("\n");
		}
	}

	static void AppendNiagaraDetails(const FReadAllAssetDocumentIR& Document, const EReadAllExportMode Mode, FString& Out)
	{
		if (Document.NiagaraRenderers.IsEmpty() && Document.NiagaraCurves.IsEmpty()) return;

		Out += TEXT("---\n\n## Niagara 表现细节\n\n");
		if (!Document.NiagaraRenderers.IsEmpty())
		{
			Out += TEXT("### Renderers\n\n");
			Out += TEXT("| Emitter | Renderer | 类型 | 启用 | Source | 材质 | 绑定数 |\n");
			Out += TEXT("|---------|----------|------|:----:|--------|------|------:|\n");
			for (const FReadAllNiagaraRendererIR& Renderer : Document.NiagaraRenderers)
			{
				Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Renderer.EmitterPath)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Renderer.Name)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Renderer.ClassPath)
					+ TEXT(" | ") + (Renderer.bEnabled ? TEXT("true") : TEXT("false"))
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Renderer.SourceMode)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(FString::Join(Renderer.Materials, TEXT(", ")))
					+ FString::Printf(TEXT(" | %d |\n"), Renderer.Bindings.Num());
			}
			Out += TEXT("\n");

			if (ReadAllExportModeIncludes(Mode, EReadAllExportMode::Full))
			{
				for (const FReadAllNiagaraRendererIR& Renderer : Document.NiagaraRenderers)
				{
					Out += TEXT("#### ") + Renderer.Name + TEXT(" 绑定\n\n");
					Out += TEXT("| 显示名 | Niagara 变量 | 数据集名 | 类型 | 来源 | 有效 | 源中存在 |\n");
					Out += TEXT("|--------|--------------|----------|------|------|:----:|:--------:|\n");
					for (const FReadAllNiagaraRendererBindingIR& Binding : Renderer.Bindings)
					{
						Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Binding.DisplayName)
							+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Binding.VariableName)
							+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Binding.DataSetName)
							+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Binding.Type)
							+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Binding.SourceMode)
							+ TEXT(" | ") + (Binding.bValid ? TEXT("true") : TEXT("false"))
							+ TEXT(" | ") + (Binding.bExistsOnSource ? TEXT("true") : TEXT("false")) + TEXT(" |\n");
					}
					if (Renderer.Bindings.IsEmpty()) Out += TEXT("| (无绑定) | | | | | | |\n");
					Out += TEXT("\n");
				}
			}
		}

		if (!Document.NiagaraCurves.IsEmpty())
		{
			Out += TEXT("### Curves\n\n");
			Out += TEXT("> 已按完整 Key、插值、切线、外推与曲线配置生成稳定指纹；同形副本合并后通过 `usedBy` 保留全部来源。\n\n");
			Out += TEXT("| 曲线 ID | 类型 | 通道 | Keys | 时间范围 | 使用位置 | 外部资产 |\n");
			Out += TEXT("|-----------|------|------|-----:|----------|---------:|----------|\n");
			for (const FReadAllNiagaraCurveIR& Curve : Document.NiagaraCurves)
			{
				int32 KeyCount = 0;
				for (const FReadAllNiagaraCurveChannelIR& Channel : Curve.Channels) KeyCount += Channel.Keys.Num();
				Out += TEXT("| ") + FAssetTextSnapshot::MarkdownCell(Curve.Id)
					+ TEXT(" | ") + FAssetTextSnapshot::MarkdownCell(Curve.ClassPath)
					+ FString::Printf(TEXT(" | %d | %d | %g - %g | %d | "), Curve.Channels.Num(), KeyCount, Curve.MinTime, Curve.MaxTime, Curve.UsedBy.Num())
					+ FAssetTextSnapshot::MarkdownCell(Curve.CurveAssetPath) + TEXT(" |\n");
			}
			Out += TEXT("\n- 原始 Key、插值、切线和所有 `usedBy` 已写入 `.meta.json`，可由配套 Skill/MCP 按需读取。\n\n");
		}
	}

	static TSharedRef<FJsonObject> MakePinJson(const FReadAllGraphPinIR& Pin)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Pin.Id);
		Json->SetStringField(TEXT("name"), Pin.Name);
		Json->SetStringField(TEXT("direction"), Pin.Direction);
		Json->SetStringField(TEXT("type"), Pin.Type);
		Json->SetStringField(TEXT("defaultValue"), Pin.DefaultValue);
		return Json;
	}

	static TSharedRef<FJsonObject> MakeNodeJson(const FReadAllGraphNodeIR& Node)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Node.Id);
		Json->SetStringField(TEXT("name"), Node.Name);
		Json->SetStringField(TEXT("className"), Node.ClassName);
		Json->SetStringField(TEXT("title"), Node.Title);
		Json->SetStringField(TEXT("comment"), Node.Comment);
		Json->SetStringField(TEXT("referencePath"), Node.ReferencePath);
		Json->SetStringField(TEXT("calleeGraphId"), Node.CalleeGraphId);
		Json->SetStringField(TEXT("selectedVersion"), Node.SelectedVersion);
		Json->SetBoolField(TEXT("enabled"), Node.bEnabled);
		Json->SetNumberField(TEXT("positionX"), Node.PositionX);
		Json->SetNumberField(TEXT("positionY"), Node.PositionY);
		TArray<TSharedPtr<FJsonValue>> Pins;
		Pins.Reserve(Node.Pins.Num());
		for (const FReadAllGraphPinIR& Pin : Node.Pins)
		{
			Pins.Add(MakeShared<FJsonValueObject>(MakePinJson(Pin)));
		}
		Json->SetArrayField(TEXT("pins"), Pins);
		return Json;
	}

	/**
	 * Builds the native graphIndex for one graph.
	 *
	 * This is a Raw Fact produced by the exporter, not a Derived View rebuilt by a
	 * consumer. MCP 4.8 derives an equivalent index in memory and flags it with
	 * GRAPH_INDEX_DERIVED; when this field is present that warning is unnecessary.
	 *
	 * Ordering rule: every array preserves the original graph IR order so that a
	 * native index and a derived index enumerate targets identically. Do not sort.
	 */
	static TSharedRef<FJsonObject> MakeGraphIndexJson(const FReadAllGraphIR& Graph)
	{
		TSharedRef<FJsonObject> Index = MakeShared<FJsonObject>();

		int32 PinCount = 0;
		for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
		{
			PinCount += Node.Pins.Num();
		}
		Index->SetNumberField(TEXT("nodeCount"), Graph.Nodes.Num());
		Index->SetNumberField(TEXT("pinCount"), PinCount);
		Index->SetNumberField(TEXT("linkCount"), Graph.Links.Num());

		// A node is an entry point when no link terminates on it. Root and output
		// nodes therefore surface first without needing a class-name allow-list.
		TSet<FString> NodesWithIncomingLinks;
		NodesWithIncomingLinks.Reserve(Graph.Links.Num());
		for (const FReadAllGraphLinkIR& Link : Graph.Links)
		{
			NodesWithIncomingLinks.Add(Link.ToNodeId);
		}

		TArray<TSharedPtr<FJsonValue>> EntryPoints;
		TArray<TSharedPtr<FJsonValue>> SearchIndex;
		SearchIndex.Reserve(Graph.Nodes.Num());

		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			const FReadAllGraphNodeIR& Node = Graph.Nodes[NodeIndex];

			// json_pointer lets a consumer cite the exact evidence location without
			// guessing how the array was serialised.
			const FString NodePointer = FString::Printf(TEXT("/nodes/%d"), NodeIndex);

			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("nodeId"), Node.Id);
			Entry->SetStringField(TEXT("name"), Node.Name);
			Entry->SetStringField(TEXT("className"), Node.ClassName);
			Entry->SetStringField(TEXT("title"), Node.Title);
			Entry->SetNumberField(TEXT("pinCount"), Node.Pins.Num());
			Entry->SetStringField(TEXT("jsonPointer"), NodePointer);
			SearchIndex.Add(MakeShared<FJsonValueObject>(Entry));

			if (!NodesWithIncomingLinks.Contains(Node.Id))
			{
				TSharedRef<FJsonObject> EntryPoint = MakeShared<FJsonObject>();
				EntryPoint->SetStringField(TEXT("nodeId"), Node.Id);
				EntryPoint->SetStringField(TEXT("name"), Node.Name);
				EntryPoint->SetStringField(TEXT("className"), Node.ClassName);
				EntryPoint->SetStringField(TEXT("jsonPointer"), NodePointer);
				EntryPoints.Add(MakeShared<FJsonValueObject>(EntryPoint));
			}
		}

		Index->SetArrayField(TEXT("entryPoints"), EntryPoints);
		Index->SetArrayField(TEXT("searchIndex"), SearchIndex);
		return Index;
	}

	static TSharedRef<FJsonObject> MakeGraphJson(const FReadAllGraphIR& Graph)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Graph.Id);
		Json->SetStringField(TEXT("name"), Graph.Name);
		Json->SetStringField(TEXT("kind"), Graph.Kind);

		TArray<TSharedPtr<FJsonValue>> Nodes;
		Nodes.Reserve(Graph.Nodes.Num());
		for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(MakeNodeJson(Node)));
		}
		Json->SetArrayField(TEXT("nodes"), Nodes);

		TArray<TSharedPtr<FJsonValue>> Links;
		Links.Reserve(Graph.Links.Num());
		for (const FReadAllGraphLinkIR& Link : Graph.Links)
		{
			TSharedRef<FJsonObject> LinkJson = MakeShared<FJsonObject>();
			LinkJson->SetStringField(TEXT("fromNodeId"), Link.FromNodeId);
			LinkJson->SetStringField(TEXT("fromPinId"), Link.FromPinId);
			LinkJson->SetStringField(TEXT("toNodeId"), Link.ToNodeId);
			LinkJson->SetStringField(TEXT("toPinId"), Link.ToPinId);
			LinkJson->SetStringField(TEXT("kind"), Link.Kind);
			Links.Add(MakeShared<FJsonValueObject>(LinkJson));
		}
		Json->SetArrayField(TEXT("links"), Links);

		// Emitted last so the index always reflects the arrays actually serialised above.
		Json->SetObjectField(TEXT("graphIndex"), MakeGraphIndexJson(Graph));
		return Json;
	}

	static TSharedRef<FJsonObject> MakeRendererJson(const FReadAllNiagaraRendererIR& Renderer)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Renderer.Id);
		Json->SetStringField(TEXT("emitterPath"), Renderer.EmitterPath);
		Json->SetStringField(TEXT("emitterVersion"), Renderer.EmitterVersion);
		Json->SetNumberField(TEXT("index"), Renderer.Index);
		Json->SetStringField(TEXT("name"), Renderer.Name);
		Json->SetStringField(TEXT("classPath"), Renderer.ClassPath);
		Json->SetStringField(TEXT("sourceMode"), Renderer.SourceMode);
		Json->SetBoolField(TEXT("enabled"), Renderer.bEnabled);
		Json->SetArrayField(TEXT("materials"), MakeStringArray(Renderer.Materials));

		TArray<TSharedPtr<FJsonValue>> Bindings;
		for (const FReadAllNiagaraRendererBindingIR& Binding : Renderer.Bindings)
		{
			TSharedRef<FJsonObject> BindingJson = MakeShared<FJsonObject>();
			BindingJson->SetStringField(TEXT("displayName"), Binding.DisplayName);
			BindingJson->SetStringField(TEXT("variableName"), Binding.VariableName);
			BindingJson->SetStringField(TEXT("dataSetName"), Binding.DataSetName);
			BindingJson->SetStringField(TEXT("type"), Binding.Type);
			BindingJson->SetStringField(TEXT("sourceMode"), Binding.SourceMode);
			BindingJson->SetBoolField(TEXT("valid"), Binding.bValid);
			BindingJson->SetBoolField(TEXT("existsOnSource"), Binding.bExistsOnSource);
			Bindings.Add(MakeShared<FJsonValueObject>(BindingJson));
		}
		Json->SetArrayField(TEXT("bindings"), Bindings);

		TArray<TSharedPtr<FJsonValue>> Properties;
		for (const FReadAllNiagaraPropertyIR& Property : Renderer.Properties)
		{
			TSharedRef<FJsonObject> PropertyJson = MakeShared<FJsonObject>();
			PropertyJson->SetStringField(TEXT("name"), Property.Name);
			PropertyJson->SetStringField(TEXT("type"), Property.Type);
			PropertyJson->SetStringField(TEXT("category"), Property.Category);
			PropertyJson->SetStringField(TEXT("value"), Property.Value);
			Properties.Add(MakeShared<FJsonValueObject>(PropertyJson));
		}
		Json->SetArrayField(TEXT("properties"), Properties);
		return Json;
	}

	static TSharedRef<FJsonObject> MakeCurveJson(const FReadAllNiagaraCurveIR& Curve)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), Curve.Id);
		Json->SetStringField(TEXT("fingerprint"), Curve.Fingerprint);
		Json->SetStringField(TEXT("objectPath"), Curve.ObjectPath);
		Json->SetStringField(TEXT("classPath"), Curve.ClassPath);
		Json->SetStringField(TEXT("ownerGraphId"), Curve.OwnerGraphId);
		Json->SetStringField(TEXT("curveAssetPath"), Curve.CurveAssetPath);
		Json->SetStringField(TEXT("exposedName"), Curve.ExposedName);
		Json->SetArrayField(TEXT("usedBy"), MakeStringArray(Curve.UsedBy));
		Json->SetNumberField(TEXT("usageCount"), Curve.UsedBy.Num());
		Json->SetBoolField(TEXT("useLUT"), Curve.bUseLUT);
		Json->SetBoolField(TEXT("exposeCurve"), Curve.bExposeCurve);
		Json->SetNumberField(TEXT("minTime"), Curve.MinTime);
		Json->SetNumberField(TEXT("maxTime"), Curve.MaxTime);

		TArray<TSharedPtr<FJsonValue>> Channels;
		for (const FReadAllNiagaraCurveChannelIR& Channel : Curve.Channels)
		{
			TSharedRef<FJsonObject> ChannelJson = MakeShared<FJsonObject>();
			ChannelJson->SetStringField(TEXT("name"), Channel.Name);
			ChannelJson->SetStringField(TEXT("preInfinityExtrapolation"), Channel.PreInfinityExtrapolation);
			ChannelJson->SetStringField(TEXT("postInfinityExtrapolation"), Channel.PostInfinityExtrapolation);
			TArray<TSharedPtr<FJsonValue>> Keys;
			for (const FReadAllNiagaraCurveKeyIR& Key : Channel.Keys)
			{
				TSharedRef<FJsonObject> KeyJson = MakeShared<FJsonObject>();
				KeyJson->SetNumberField(TEXT("time"), Key.Time);
				KeyJson->SetNumberField(TEXT("value"), Key.Value);
				KeyJson->SetStringField(TEXT("interpolation"), Key.Interpolation);
				KeyJson->SetStringField(TEXT("tangentMode"), Key.TangentMode);
				KeyJson->SetStringField(TEXT("tangentWeightMode"), Key.TangentWeightMode);
				KeyJson->SetNumberField(TEXT("arriveTangent"), Key.ArriveTangent);
				KeyJson->SetNumberField(TEXT("arriveTangentWeight"), Key.ArriveTangentWeight);
				KeyJson->SetNumberField(TEXT("leaveTangent"), Key.LeaveTangent);
				KeyJson->SetNumberField(TEXT("leaveTangentWeight"), Key.LeaveTangentWeight);
				Keys.Add(MakeShared<FJsonValueObject>(KeyJson));
			}
			ChannelJson->SetArrayField(TEXT("keys"), Keys);
			Channels.Add(MakeShared<FJsonValueObject>(ChannelJson));
		}
		Json->SetArrayField(TEXT("channels"), Channels);
		return Json;
	}
}

FString FReadAllAssetDocumentIR::RenderMarkdown(const EReadAllExportMode Mode) const
{
	FString Out;
	Out += TEXT("# UE 资产速读：ReadAllandExplains\n\n");
	Out += TEXT("你好同学，下面先用美术能直接理解的方式概括 **") + AssetName + TEXT("**，再保留可供 AI 深挖的技术信息。\n\n");
	Out += TEXT("## 一眼看懂\n\n");
	Out += TEXT("- **资产类型：** ") + AssetKind + TEXT("\n");
	Out += TEXT("- **特征标签：** ") + (FeatureTags.IsEmpty() ? TEXT("general") : FString::Join(FeatureTags, TEXT("、"))) + TEXT("\n");
	Out += TEXT("- **美术关注点：** ") + ArtistFocus + TEXT("\n");
	Out += TEXT("- **建议追问：** “") + SuggestedPrompt + TEXT("”\n");
	Out += TEXT("- **当前导出模式：** ") + ReadAllExportModeToString(Mode) + TEXT("\n");
	Out += TEXT("- **使用建议：** 先看上面的参数线索和依赖关系；需要时再阅读后面的材质、蓝图或 Niagara 技术细节。\n\n---\n\n");

	ReadAllDocumentIRImpl::AppendParameterTable(ParameterClues, Out);
	ReadAllDocumentIRImpl::AppendRelationships(*this, Out);
	ReadAllDocumentIRImpl::AppendGraphIR(Graphs, Mode, Out);
	ReadAllDocumentIRImpl::AppendNiagaraDetails(*this, Mode, Out);

	if (Mode != EReadAllExportMode::Artist && !TechnicalMarkdown.IsEmpty())
	{
		Out += TEXT("---\n\n## 技术细节（") + ReadAllExportModeToString(Mode) + TEXT("）\n\n");
		Out += TechnicalMarkdown;
		if (!Out.EndsWith(TEXT("\n"))) Out += TEXT("\n");
		Out += TEXT("\n");
	}

	Out += TEXT("---\n\n## 交给 AI 的下一步提示词\n\n```text\n");
	Out += TEXT("请阅读这份 ReadAllandExplains 导出文档，并基于实际数据回答。\n资产：") + ObjectPath + TEXT("\n");
	Out += SuggestedPrompt + TEXT("\n");
	Out += TEXT("不要臆测文档中不存在的连接；不确定时请明确指出需要回到 UE 核对的节点或参数。\n```\n");
	return Out;
}

FString FReadAllAssetDocumentIR::RenderMetadataJson(const EReadAllExportMode Mode) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 2);
	Root->SetStringField(TEXT("exportMode"), ReadAllExportModeToString(Mode));
	Root->SetStringField(TEXT("promptMode"), ReadAllPromptModeToString(PromptMode));
	Root->SetStringField(TEXT("assetName"), AssetName);
	Root->SetStringField(TEXT("objectPath"), ObjectPath);
	Root->SetStringField(TEXT("classPath"), ClassPath);
	Root->SetStringField(TEXT("assetKind"), AssetKind);
	Root->SetArrayField(TEXT("featureTags"), ReadAllDocumentIRImpl::MakeStringArray(FeatureTags));
	Root->SetStringField(TEXT("artistFocus"), ArtistFocus);
	Root->SetStringField(TEXT("suggestedPrompt"), SuggestedPrompt);
	Root->SetBoolField(TEXT("technicalMarkdownIncluded"), Mode != EReadAllExportMode::Artist);
	Root->SetNumberField(TEXT("technicalCharacterCount"), TechnicalMarkdown.Len());
	Root->SetArrayField(TEXT("dependencies"), ReadAllDocumentIRImpl::MakeStringArray(Dependencies));
	Root->SetArrayField(TEXT("referencers"), ReadAllDocumentIRImpl::MakeStringArray(Referencers));

	TArray<TSharedPtr<FJsonValue>> ParameterValues;
	ParameterValues.Reserve(ParameterClues.Num());
	for (const FReadAllParameterClue& Clue : ParameterClues)
	{
		TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
		Parameter->SetStringField(TEXT("name"), Clue.Name);
		Parameter->SetStringField(TEXT("kind"), Clue.Kind);
		Parameter->SetStringField(TEXT("value"), Clue.Value);
		ParameterValues.Add(MakeShared<FJsonValueObject>(Parameter));
	}
	Root->SetArrayField(TEXT("parameters"), ParameterValues);

	TArray<TSharedPtr<FJsonValue>> GraphValues;
	GraphValues.Reserve(Graphs.Num());
	for (const FReadAllGraphIR& Graph : Graphs)
	{
		GraphValues.Add(MakeShared<FJsonValueObject>(ReadAllDocumentIRImpl::MakeGraphJson(Graph)));
	}
	Root->SetArrayField(TEXT("graphs"), GraphValues);

	// Root-level native index. MCP treats the absence of this object as a signal
	// that it must rebuild the index itself and raises GRAPH_INDEX_DERIVED; when it
	// is present the index is a Raw Fact and no derivation warning is needed.
	// The per-graph "graphIndex" objects above carry the node-level detail; this
	// object is the asset-level roll-up plus a graph locator table.
	{
		TSharedRef<FJsonObject> RootIndex = MakeShared<FJsonObject>();
		int32 TotalNodes = 0;
		int32 TotalPins = 0;
		int32 TotalLinks = 0;
		TArray<TSharedPtr<FJsonValue>> GraphLocators;
		GraphLocators.Reserve(Graphs.Num());

		for (int32 GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
		{
			const FReadAllGraphIR& Graph = Graphs[GraphIndex];
			int32 GraphPinCount = 0;
			for (const FReadAllGraphNodeIR& Node : Graph.Nodes)
			{
				GraphPinCount += Node.Pins.Num();
			}
			TotalNodes += Graph.Nodes.Num();
			TotalPins += GraphPinCount;
			TotalLinks += Graph.Links.Num();

			TSharedRef<FJsonObject> Locator = MakeShared<FJsonObject>();
			Locator->SetStringField(TEXT("graphId"), Graph.Id);
			Locator->SetStringField(TEXT("name"), Graph.Name);
			Locator->SetStringField(TEXT("kind"), Graph.Kind);
			Locator->SetNumberField(TEXT("nodeCount"), Graph.Nodes.Num());
			Locator->SetNumberField(TEXT("pinCount"), GraphPinCount);
			Locator->SetNumberField(TEXT("linkCount"), Graph.Links.Num());
			Locator->SetStringField(TEXT("jsonPointer"), FString::Printf(TEXT("/graphs/%d"), GraphIndex));
			GraphLocators.Add(MakeShared<FJsonValueObject>(Locator));
		}

		RootIndex->SetNumberField(TEXT("indexVersion"), 1);
		RootIndex->SetStringField(TEXT("source"), TEXT("native"));
		RootIndex->SetNumberField(TEXT("graphCount"), Graphs.Num());
		RootIndex->SetNumberField(TEXT("nodeCount"), TotalNodes);
		RootIndex->SetNumberField(TEXT("pinCount"), TotalPins);
		RootIndex->SetNumberField(TEXT("linkCount"), TotalLinks);
		RootIndex->SetArrayField(TEXT("graphs"), GraphLocators);
		Root->SetObjectField(TEXT("graphIndex"), RootIndex);
	}

	TArray<TSharedPtr<FJsonValue>> RendererValues;
	RendererValues.Reserve(NiagaraRenderers.Num());
	for (const FReadAllNiagaraRendererIR& Renderer : NiagaraRenderers)
	{
		RendererValues.Add(MakeShared<FJsonValueObject>(ReadAllDocumentIRImpl::MakeRendererJson(Renderer)));
	}
	Root->SetArrayField(TEXT("niagaraRenderers"), RendererValues);

	TArray<TSharedPtr<FJsonValue>> CurveValues;
	CurveValues.Reserve(NiagaraCurves.Num());
	for (const FReadAllNiagaraCurveIR& Curve : NiagaraCurves)
	{
		CurveValues.Add(MakeShared<FJsonValueObject>(ReadAllDocumentIRImpl::MakeCurveJson(Curve)));
	}
	Root->SetArrayField(TEXT("niagaraCurves"), CurveValues);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}
