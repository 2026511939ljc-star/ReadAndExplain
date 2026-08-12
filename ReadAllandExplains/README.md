# ReadAllandExplains

[![Version](https://img.shields.io/badge/version-4.8.0--preview.3-blue)](https://github.com/2026511939ljc-star/ReadAndExplain/releases)
[![UE5](https://img.shields.io/badge/Unreal%20Engine-5.7-black)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/platform-Win64-lightgrey)]()
[![License](https://img.shields.io/badge/license-UE%20EULA-orange)](https://www.unrealengine.com/en-US/eula)

**An AI Context Compiler for Unreal Engine technical-art assets.**

[中文](#中文) | [English](#english)

---

## 中文

ReadAllandExplains 将 Blueprint、Material、Niagara 等资产导出为便于人类速读的 Markdown，以及便于 AI 和工具消费的结构化 JSON 元数据。配套只读 MCP 和 Skill 实现渐进式查询，避免 AI 一次加载大型导出文件。

### 核心特性

> `v4.8.0` 新增**原生图索引**与 **Niagara Custom HLSL 导出**。导出端直接产出 `graphIndex`（根级汇总 + 每图 `entryPoints`/`searchIndex`，每项带 `jsonPointer`），MCP 校验一致后报 `native_metadata_graph_index` 并不再发出 `GRAPH_INDEX_DERIVED`；索引与实际图数据不一致时自动回退到派生路径，因为过期索引比没有索引更危险。Niagara `CustomHlsl` 节点的代码体以**原文**导出，同时进入 Markdown 代码块、JSON `sourceCode` 字段与检索索引。纯新增字段，4.7 及更早 Pack 无需重新导出。
>
> `v4.8.0-preview.1` 新增渐进式证据查询：`get_asset_outline` 先给规模再决定读不读、`locate_graph_target` 确定性定位并支持省略 `query` 枚举发现、`get_graph_subgraph` 按跳数/节点/字符预算取最小闭包并诚实报告截断边界、稳定章节按 `section_id` 精确读取。写操作 `request_targeted_snapshot` 的 `pack_path` 改为必填，确保补拍始终绑定明确基线。全部新能力位于 MCP 层，UE 插件与导出结构不变，现有 4.7 Pack 无需重新导出。
>
> `v4.7.0` 在 4.6 可信查询与 SyncLive Lite 基础上，新增 Blueprint CDO、Enum、DataAsset、Material Custom HLSL、Golden Pack 回归，以及严格的 Context Pack 完整性事务。

- **统一 Schema 2 图 IR**：材质节点/Pin/Link 与蓝图执行流进入统一图中间表示，Markdown 与 JSON 共用同一 IR 生成。
- **Niagara 深度导出**：Renderer 明细、曲线原始 Key/插值/切线/外推、项目自定义模块递归图（深度 4、循环去重），曲线指纹去重并保留 `usedBy` 来源。
- **材质 Custom HLSL**：普通 Material 中根属性可达的 Custom 节点会在可读 Markdown 中导出代码体、主输出类型和自定义输入为伪 HLSL `CustomHLSL("type", "code", Input("name", value), ...)`；`.meta.json` 当前保留通用节点、Pin、Link 拓扑，尚未结构化保存 Custom Code、Define、Include 和额外输出语义。
- **蓝图 CDO 参数值**：蓝图当前生成类声明的变量导出 Class Default Object 默认值，不再只导出类型名；暂不覆盖关卡 Actor/组件实例覆盖值。
- **AI Context Pack**：以根资产递归收集 `/Game/` 下受支持依赖，默认深度 2（可配 0–4），生成独立上下文目录含 README、索引和分类资产文档。
- **渐进式 MCP 查询**：只读 stdio MCP 提供 Context Pack 列表、资产搜索、摘要、Graph/Renderer/曲线详情与文本检索，支持 `coverage` 依赖缺口视图。
- **SyncLive Lite**：用户批准补充计划后，MCP 可向正在运行的 UE 编辑器提交 1–5 个 `/Game/` 资产的定向静态补快照请求，原子落盘、状态机管理、PIE 暂停。
- **CodeBuddy 原生接入**：插件包含 1 个 Command、1 个 Skill、1 个只读 MCP，自动发现当前或嵌套 UE 项目导出目录。
- **可信查询层**：统一 `rae.mcp/1.0` Envelope、Evidence 证据链、稳定错误码、分页、`structuredContent`、`outputSchema` 与只读 Tool Annotations。
- **严格完整快照**：只有全部资产、Metadata、README、Index、Manifest 和最终目录发布成功时，Context Pack 才标记为 `complete`；失败 Pack 保留在 `.tmp` 并提供诊断错误。
- **同名资产安全**：不同包路径下的同名资产使用稳定对象路径哈希消歧，Index 与 Manifest 明确绑定正文和 Metadata。
- **Golden Pack 回归**：规范化比较、受管 Baseline、机器可读报告和运行来源追踪，为真实 UE 资产建立发布质量门禁。

### 支持的资产类型

| 类型 | 输出文件 | 说明 |
|------|----------|------|
| Blueprint | `_ReadableCode.txt` + `.meta.json` | 蓝图执行流展开 |
| Material / Material Instance | `_ReadableMaterial.md` + `.meta.json` | 节点图 IR + 参数 |
| Material Function | `_ReadableMaterialFunction.md` + `.meta.json` | 复用材质导出器 |
| Niagara System / Emitter / Script | `_ReadableNiagara.md` + `.meta.json` | Renderer + 曲线 + 模块递归图 |
| Enum | `_ReadableEnum.md` + `.meta.json` | 枚举条目显示名称与 64 位值 |
| DataAsset | `_ReadableDataAsset.md` + `.meta.json` | 当前类实例属性进入参数线索；Full/Reconstruction 模式追加详细反射表 |

**暂不支持**：Texture2D/TextureCube（二进制像素数据）、AnimBP 状态机、缩略图缓存、反向导入器。

### 安装

1. 关闭 Unreal Editor，下载 [最新 Release](https://github.com/2026511939ljc-star/ReadAndExplain/releases) 的 ZIP 包。
2. 解压并确认描述符位于 `<YourProject>/Plugins/ReadAllandExplains/ReadAllandExplains.uplugin`。
3. 用 Unreal Engine 5.7 打开项目，启用插件并按提示重启。
4. 如需 CodeBuddy/MCP，请安装 Python 3.9+；本版本不需要 npm 或第三方 Python 依赖。

完整安装、升级、验证、卸载与故障排查见 [安装指南](docs/INSTALLATION.md)。

### 使用

**普通导出**：在内容浏览器右键资产 → 选择「导出所有选中为 AI 可读文档」。

**完整上下文**：右键资产 → 选择「生成 AI Context Pack（含项目依赖）」。

**无界面命令**：
- `ReadAllandExplains.ExportAssets`（兼容旧 `GetTheMeaning.ExportAssets`）
- `ReadAllandExplains.ExportContextPack`

导出结果位于项目 `Saved/ReadAllandExplainsExports` 目录。

### Skill / MCP / CodeBuddy 集成

- **Skill**：位于 `skills/readallandexplains/SKILL.md`，负责 UE 资产解释、曲线分析、HLSL 阅读和按需查询流程。使用 `Scripts/SyncWorkspaceSkill.ps1` 同步到工作区。
- **MCP**：位于 `Integrations/MCP/readallandexplains_mcp.py`，只读 stdio MCP，提供 5 个查询工具和授权后的 SyncLive Lite 工具。Windows 通过 `readallandexplains_mcp.bat` 启动。
- **CodeBuddy**：原生入口为 `.codebuddy-plugin/plugin.json` 和 `.mcp.json`。验证：`codebuddy plugin validate .`；测试：`codebuddy --plugin-dir .`。诊断命令：`/readallandexplains:readallandexplains-doctor`。
- **自动发现**：CodeBuddy 通过 `CODEBUDDY_PROJECT_DIR` 自动寻找 UE 项目导出目录；可用 `READALL_EXPORT_ROOT` 或 `--root` 显式覆盖。

### 架构概览

```
UE Editor
  ├── AssetInsightExporter (主调度)
  │   ├── BlueprintToTextExporter
  │   ├── MaterialToTextExporter
  │   ├── NiagaraToTextExporter
  │   └── CommonAssetToTextExporter
  ├── AssetDocumentIR (Schema 2 统一图 IR)
  └── Context Pack (原子快照)
       ├── README.md / context-pack.json / index.json
       └── 分类资产文档 (.md + .meta.json)

MCP Server (readallandexplains_mcp.py)
  ├── list_packs / search_assets
  ├── get_asset_detail (含 coverage 视图)
  ├── get_graph / get_renderer_detail / get_curve_detail
  ├── search_text
  └── request_targeted_snapshot / get_snapshot_request_status (SyncLive Lite)

Skill (SKILL.md)
  └── 索引 → 摘要 → 目标片段 → 补拍授权 → 续答
```

### 构建与测试

```powershell
# 契约测试
python -m unittest discover -s Tests/MCP -p "test_*.py" -v

# Golden Pack 规范化/比较器测试
python -m unittest discover -s Tests/Golden -p "test_*.py" -v

# 真实 UE 资产回归（先关闭编辑器，并创建本地 cases 配置）
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunGoldenPackRegression.ps1 -Project "D:/YourProject/YourProject.uproject" -Cases "Tests/Golden/cases.local.json"

# CodeBuddy 插件验证
codebuddy plugin validate .
```

### 当前边界

- Context Pack 只递归 `/Game/` 下插件可导出的项目资产；引擎内容保留在依赖清单但不导出。
- SyncLive Lite 需 UE 编辑器运行且插件已加载；PIE 期间暂停。只生成静态补充 Pack，不是实时运行时桥接。
- 完整参数级 DAG、运行时观测、反向导入和 Marketplace 发布尚未完成。

---

## English

ReadAllandExplains exports Blueprint, Material, and Niagara assets as human-readable Markdown and structured JSON metadata for AI and tool consumption. A companion read-only MCP and Skill enable progressive retrieval, preventing AI from loading large export files at once.

### Key Features

> `v4.8.0` adds a **native graph index** and **Niagara Custom HLSL export**. The exporter now emits `graphIndex` directly (a root roll-up plus per-graph `entryPoints` and `searchIndex`, each entry carrying a `jsonPointer`). MCP verifies it against the graphs it describes and reports `native_metadata_graph_index` without raising `GRAPH_INDEX_DERIVED`; on any mismatch it falls back to deriving one, because a stale index is worse than no index. Niagara `CustomHlsl` node bodies are exported **verbatim** into a Markdown code block, a JSON `sourceCode` field and the search index. Purely additive, so 4.7 and earlier Packs need no re-export.
>
> `v4.8.0-preview.1` adds progressive evidence queries: `get_asset_outline` reports size before you decide whether to read, `locate_graph_target` locates deterministically and enumerates targets when `query` is omitted, `get_graph_subgraph` takes a minimum closure under hop, node and character budgets while reporting truncation boundaries honestly, and stable sections are read precisely by `section_id`. The write path `request_targeted_snapshot` now requires `pack_path`, so a re-snapshot is always bound to an explicit baseline. All new capability lives in the MCP layer; the UE plugin and export structure are unchanged and existing 4.7 Packs need no re-export.
>
> `v4.7.0` builds on the trustworthy 4.6 query layer and SyncLive Lite with Blueprint CDO, Enum, DataAsset, Material Custom HLSL, Golden Pack regression, and strict Context Pack integrity transactions.

- **Unified Schema 2 Graph IR**: Material nodes/Pins/Links and Blueprint execution flow enter a unified graph intermediate representation; Markdown and JSON share the same IR.
- **Deep Niagara Export**: Renderer details, curve raw Key/interpolation/tangent/extrapolation, project custom module recursive graphs (depth 4, cycle-deduplicated), curve fingerprint deduplication with `usedBy` provenance.
- **Material Custom HLSL**: Root-reachable Custom nodes in regular Materials export code, primary output type, and custom inputs to readable Markdown as pseudo-HLSL `CustomHLSL("type", "code", Input("name", value), ...)`. `.meta.json` currently retains generic node/Pin/Link topology; Custom Code, Define, Include, and additional-output semantics are not yet structured there.
- **Blueprint CDO Parameter Values**: Variables declared by the current generated Blueprint class export Class Default Object values instead of type names; placed Actor/component instance overrides are not covered yet.
- **AI Context Pack**: Recursively collects supported `/Game/` dependencies from a root asset (default depth 2, configurable 0–4), generating a standalone context directory with README, index, and categorized asset documents.
- **Progressive MCP Query**: Read-only stdio MCP provides Context Pack listing, asset search, summaries, Graph/Renderer/curve details, and text search, with a `coverage` dependency gap view.
- **SyncLive Lite**: After user approval, MCP can submit targeted static snapshot requests (1–5 `/Game/` assets) to a running UE editor, with atomic file writes, state machine management, and PIE pause.
- **CodeBuddy Native Integration**: Plugin includes 1 Command, 1 Skill, and 1 read-only MCP, with automatic discovery of current or nested UE project export directories.
- **Trustworthy Query Layer**: Unified `rae.mcp/1.0` Envelope, Evidence chain, stable error codes, pagination, `structuredContent`, `outputSchema`, and read-only Tool Annotations.
- **Strict Complete Snapshots**: A Pack becomes `complete` only after every asset, Metadata file, README, Index, Manifest, and final directory publication succeeds; failed Packs remain in `.tmp` with diagnostics.
- **Safe Duplicate Names**: Same-name assets from different package paths receive deterministic object-path-hash suffixes, with explicit document/Metadata mappings in Index and Manifest.
- **Golden Pack Regression**: Canonical comparison, managed baselines, machine-readable reports, and provenance provide a real-asset release quality gate.

### Supported Asset Types

| Type | Output Files | Description |
|------|-------------|-------------|
| Blueprint | `_ReadableCode.txt` + `.meta.json` | Execution flow expansion |
| Material / Material Instance | `_ReadableMaterial.md` + `.meta.json` | Node graph IR + parameters |
| Material Function | `_ReadableMaterialFunction.md` + `.meta.json` | Reuses material exporter |
| Niagara System / Emitter / Script | `_ReadableNiagara.md` + `.meta.json` | Renderer + curves + module recursive graph |
| Enum | `_ReadableEnum.md` + `.meta.json` | Entry display names and 64-bit values |
| DataAsset | `_ReadableDataAsset.md` + `.meta.json` | Current-class instance properties in parameter clues; detailed reflection in Full/Reconstruction modes |

**Not yet supported**: Texture2D/TextureCube (binary pixel data), AnimBP state machines, thumbnail caching, reverse importer.

### Installation

1. Close Unreal Editor and download the latest [Release ZIP](https://github.com/2026511939ljc-star/ReadAndExplain/releases).
2. Extract it so the descriptor is `<YourProject>/Plugins/ReadAllandExplains/ReadAllandExplains.uplugin`.
3. Open the project with Unreal Engine 5.7, enable the plugin, and restart if requested.
4. CodeBuddy/MCP requires Python 3.9+; this release requires neither npm nor third-party Python packages.

See the full [Installation Guide](docs/INSTALLATION.md) for upgrade, verification, uninstall, and troubleshooting steps.

### Usage

**Standard Export**: Right-click assets in Content Browser → select "Export Selected as AI-Readable Documents".

**Full Context**: Right-click an asset → select "Generate AI Context Pack (with project dependencies)".

**Console Commands**:
- `ReadAllandExplains.ExportAssets` (compatible with legacy `GetTheMeaning.ExportAssets`)
- `ReadAllandExplains.ExportContextPack`

Export results are in the project's `Saved/ReadAllandExplainsExports` directory.

### Skill / MCP / CodeBuddy Integration

- **Skill**: Located at `skills/readallandexplains/SKILL.md`. Handles UE asset interpretation, curve analysis, HLSL reading, and on-demand query flow. Use `Scripts/SyncWorkspaceSkill.ps1` to sync to workspaces.
- **MCP**: Located at `Integrations/MCP/readallandexplains_mcp.py`. Read-only stdio MCP with 5 query tools and permission-gated SyncLive Lite tools. Windows launcher: `readallandexplains_mcp.bat`.
- **CodeBuddy**: Native entry points are `.codebuddy-plugin/plugin.json` and `.mcp.json`. Validate: `codebuddy plugin validate .`; Test: `codebuddy --plugin-dir .`. Diagnostics: `/readallandexplains:readallandexplains-doctor`.
- **Auto-Discovery**: CodeBuddy uses `CODEBUDDY_PROJECT_DIR` to find UE project export directories; override with `READALL_EXPORT_ROOT` or `--root`.

### Architecture Overview

```
UE Editor
  ├── AssetInsightExporter (main dispatcher)
  │   ├── BlueprintToTextExporter
  │   ├── MaterialToTextExporter
  │   ├── NiagaraToTextExporter
  │   └── CommonAssetToTextExporter
  ├── AssetDocumentIR (Schema 2 unified graph IR)
  └── Context Pack (atomic snapshot)
       ├── README.md / context-pack.json / index.json
       └── Categorized asset docs (.md + .meta.json)

MCP Server (readallandexplains_mcp.py)
  ├── list_packs / search_assets
  ├── get_asset_detail (with coverage view)
  ├── get_graph / get_renderer_detail / get_curve_detail
  ├── search_text
  └── request_targeted_snapshot / get_snapshot_request_status (SyncLive Lite)

Skill (SKILL.md)
  └── Index → Summary → Target Fragment → Supplement Authorization → Continue
```

### Build & Test

```powershell
# Contract tests
python -m unittest discover -s Tests/MCP -p "test_*.py" -v

# Golden Pack normalizer/comparator tests
python -m unittest discover -s Tests/Golden -p "test_*.py" -v

# Real UE asset regression (close the editor and create a local cases config first)
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunGoldenPackRegression.ps1 -Project "D:/YourProject/YourProject.uproject" -Cases "Tests/Golden/cases.local.json"

# CodeBuddy plugin validation
codebuddy plugin validate .
```

### Current Limitations

- Context Pack only recursively exports supported `/Game/` project assets; engine content is listed in dependencies but not exported.
- SyncLive Lite requires a running UE editor with the plugin loaded; paused during PIE. It only generates static supplemental Packs, not a real-time runtime bridge.
- Supplemental Packs are independent snapshots; Base + Delta overlay queries are planned for a later release.
- Full parameter-level DAG, runtime observation, reverse import, and Marketplace publication are not yet complete.

### License

This plugin is subject to the [Unreal Engine End User License Agreement](https://www.unrealengine.com/en-US/eula). Source code is provided for use within Unreal Engine projects.
