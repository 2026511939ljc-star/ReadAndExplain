# ReadAllandExplains

ReadAllandExplains is an Unreal Engine 5.7 editor plugin that exports selected Unreal assets as compact, AI-readable documents. It preserves useful technical structure while adding artist-friendly summaries, dependency context, feature tags, and ready-to-use AI prompts.

> **Release status:** `4.5.0-preview.1` is a preview release. Test it in a copy of your project before adopting it in production.

## Features

- Exports multiple selected assets in one operation.
- Generates an AI Context Pack from selected root assets and supported `/Game/` dependencies, with a configurable recursion depth from 0 to 4.
- Integrates with the Content Browser, the Window menu, and Reference Viewer node menus.
- Produces readable Markdown or text plus optional machine-readable `.meta.json` sidecars.
- Deduplicates Niagara curves by a stable fingerprint while preserving every source location through `usedBy`.
- Adds dependencies, referencers, feature tags, artist-oriented explanations, and configurable AI hand-off prompts.
- Includes a read-only local MCP server and a companion Skill for progressive index-to-summary-to-detail retrieval.
- Supports compact output for lower token usage and reconstruction-oriented output for maximum graph detail.
- Provides editor console commands for scripted and unattended asset or Context Pack export workflows.
- Writes UTF-8 files with a BOM for reliable Chinese text display in Windows editors.

## Supported assets

| Asset type | Exported information |
| --- | --- |
| Blueprint | Reflected properties, graphs, nodes, pins, defaults, links, and native clipboard text |
| Material and Material Instance | Material properties, parameters, graph structure, expressions, connections, functions, and inheritance context |
| Material Function | Function graph, expressions, pins, links, and dependencies |
| Niagara System, Emitter, and Script | Source structure, parameters, modules, renderer details, source curve keys, project module graphs, and Schema 2 graph/node/pin/link IR |
| Static Mesh | Bounds, LOD geometry statistics, material slots, UV-channel counts, and collision summary |
| Texture | Resolution, mip count, compression, texture group, sRGB, and virtual-texture metadata |
| Data Table | Row structure and complete table data |
| Curve Table | Curve names, key counts, and time/value ranges |

Texture exports contain metadata only; the plugin does not read pixel data or generate thumbnails.

## Requirements

- Unreal Engine 5.7
- Windows 64-bit for the included precompiled editor binary
- An editor project; the plugin module is editor-only

The release includes source code. If the included binary does not match your exact UE 5.7 build, remove the plugin's `Binaries` and `Intermediate` directories and rebuild it with your engine toolchain.

## Installation

1. Download the ZIP attached to the GitHub Release.
2. Close Unreal Editor.
3. Extract the `ReadAllandExplains` folder to your project:

   ```text
   <YourProject>/Plugins/ReadAllandExplains/
   ```

4. Confirm that this file exists:

   ```text
   <YourProject>/Plugins/ReadAllandExplains/ReadAllandExplains.uplugin
   ```

5. Open the project. If necessary, enable **ReadAllandExplains** under **Edit > Plugins**, then restart the editor.

## Editor usage

### Content Browser

1. Select one or more supported assets.
2. Right-click the selection.
3. Open the **ReadAllandExplains** section and choose the export action.

The same export action is also available from the main **Window > ReadAllandExplains** menu.

### Reference Viewer

Select supported asset nodes in Reference Viewer, right-click a selected node, and use the **ReadAllandExplains** export action. All supported selected nodes are exported as a batch.

### Console and automation

Export explicit object paths:

```text
ReadAllandExplains.ExportAssets /Game/Folder/M_Asset.M_Asset /Game/Folder/BP_Tool.BP_Tool
```

Calling the command without object paths exports the current Content Browser selection:

```text
ReadAllandExplains.ExportAssets
```

Export selected roots plus supported project dependencies as one Context Pack:

```text
ReadAllandExplains.ExportContextPack /Game/Folder/NS_Effect.NS_Effect
```

Example unattended workflow:

```powershell
UnrealEditor-Cmd.exe "D:\Projects\MyProject\MyProject.uproject" -unattended -nop4 -nosplash -NullRHI -NoSound -ExecCmds="ReadAllandExplains.ExportAssets /Game/Folder/M_Asset.M_Asset,Quit"
```

`GetTheMeaning.ExportAssets` remains available as a legacy command alias.

### Skill and MCP

- The companion Skill is located at `Skills/readallandexplains/SKILL.md`.
- The read-only stdio MCP server is located at `Integrations/MCP/readallandexplains_mcp.py`.
- Start it with Python 3.10+ and pass `--root <YourProject>/Saved/ReadAllandExplainsExports`, or set `READALL_EXPORT_ROOT`.
- Use `Integrations/MCP/mcp-config.example.json` as a client configuration template.
- The MCP provides Context Pack discovery, asset search, compact summaries, precise graph/renderer/curve detail, and text search without loading complete exports.

## Output

Exports are written under:

```text
<YourProject>/Saved/ReadAllandExplainsExports/
```

The plugin creates asset-specific subfolders:

```text
Blueprints/
Materials/
Niagara/
StaticMeshes/
Textures/
DataTables/
CurveTables/
```

When metadata output is enabled, each readable document is accompanied by an `<AssetName>.meta.json` sidecar.

## Settings

Open **Editor Preferences > Plugins > ReadAllandExplains**.

### Export modes

| Mode | Purpose |
| --- | --- |
| Artist | Fast, artist-oriented overview |
| Compact | Recommended default with lower token usage |
| Full | More complete technical information |
| Reconstruction | Maximum graph data for reconstruction-oriented workflows |

### Additional settings

- **Write asset.meta.json**: enables or disables structured metadata sidecars.
- **AI Prompt Mode**: `Explain`, `Review`, `Optimize`, `Trace`, or `Custom`.
- **Custom Prompt**: appended when Custom prompt mode is selected.

## What is new in 4.5.0-preview.1

- Generates recursive AI Context Packs with manifests, indexes, readable documents, and Schema JSON metadata.
- Deduplicates Niagara curves by stable fingerprints while preserving complete keys, interpolation, tangents, extrapolation, and all `usedBy` locations.
- Stops generating optional HTML/SVG browser files; the default Context Pack remains Markdown and JSON only.
- Adds a dependency-free, read-only stdio MCP server with five progressive retrieval tools.
- Adds a companion UE asset analysis Skill for Blueprint, Material, Custom HLSL, Niagara, renderer binding, dependency, graph, and curve interpretation.
- Preserves the existing renderer details, recursive project Niagara module graphs, Schema 2 `graphs`, and `_ReadableNiagara.md` output.
- Validated with Unreal Engine 5.7 / Win64, a real Niagara Context Pack, MCP discovery, asset indexing, and precise curve retrieval.


---

## 中文说明

ReadAllandExplains 是一个适用于 Unreal Engine 5.7 的编辑器插件，可将选中的 UE 资产导出成结构紧凑、方便 AI 阅读的文档。它在保留重要技术结构的同时，还会补充面向美术人员的概览、依赖关系、特征标签以及可直接交给 AI 使用的提示词。

> **发布状态：** `4.5.0-preview.1` 是预览版本。用于正式项目之前，建议先在项目副本中测试。

## 主要功能

- 一次批量导出多个选中的资产。
- 以所选资产为根生成 AI Context Pack，并按 0 至 4 层递归收集 `/Game/` 下受支持的项目依赖。
- 集成内容浏览器、Window 主菜单和 Reference Viewer 节点右键菜单。
- 生成易读的 Markdown／文本，并可同时生成机器可读的 `.meta.json` 元数据文件。
- Niagara 曲线按稳定指纹去重，同时通过 `usedBy` 保留所有来源位置。
- 输出依赖项、引用项、特征标签、美术向说明和可配置的 AI 交接提示词。
- 附带只读本地 MCP 与配套 Skill，以“索引 → 摘要 → 目标片段”方式渐进读取。
- 提供节省 Token 的精简模式，以及尽量保留图表信息的重建模式。
- 提供普通资产和 Context Pack 的编辑器控制台命令，支持脚本化和无人值守导出。
- 使用带 BOM 的 UTF-8 保存文件，保证中文在 Windows 常用编辑器中正确显示。

## 支持的资产

| 资产类型 | 导出内容 |
| --- | --- |
| Blueprint 蓝图 | 反射属性、Graph、节点、Pin、默认值、连接关系和 UE 原生剪贴板文本 |
| Material／Material Instance | 材质属性、参数、图表结构、表达式、连接、材质函数及继承关系 |
| Material Function | 函数图、表达式、Pin、连接和依赖关系 |
| Niagara System／Emitter／Script | 源资产结构、参数、模块、Renderer 详情、源曲线 Key、项目模块内部图，以及 Schema 2 Graph／Node／Pin／Link 统一 IR |
| Static Mesh | 包围盒、各级 LOD 几何统计、材质槽、UV 通道数量和碰撞摘要 |
| Texture | 分辨率、Mip 数量、压缩设置、Texture Group、sRGB 和虚拟纹理元数据 |
| Data Table | 行结构和完整表格数据 |
| Curve Table | 曲线名称、关键帧数量、时间范围和值范围 |

贴图只导出元数据；插件不会读取像素，也不会生成缩略图。

## 运行要求

- Unreal Engine 5.7
- 使用内置预编译编辑器 DLL 时需要 Windows 64 位
- 编辑器项目；插件模块仅在 Editor 中运行

发布包同时包含源码。如果内置 DLL 与你的具体 UE 5.7 构建版本不兼容，请删除插件目录中的 `Binaries` 和 `Intermediate`，然后使用自己的引擎工具链重新编译。

## 安装方法

1. 下载 GitHub Release 中附带的 ZIP。
2. 关闭 Unreal Editor。
3. 将 `ReadAllandExplains` 文件夹解压到项目中：

   ```text
   <你的项目>/Plugins/ReadAllandExplains/
   ```

4. 确认以下文件存在：

   ```text
   <你的项目>/Plugins/ReadAllandExplains/ReadAllandExplains.uplugin
   ```

5. 打开项目。如有需要，在 **Edit > Plugins** 中启用 **ReadAllandExplains**，然后重启编辑器。

## 编辑器内使用

### 内容浏览器

1. 选中一个或多个受支持的资产。
2. 右键单击选中的资产。
3. 在 **ReadAllandExplains** 分类中执行导出操作。

也可以通过主菜单 **Window > ReadAllandExplains** 执行相同的批量导出。

### Reference Viewer

在 Reference Viewer 中选中受支持的资产节点，右键单击选中的节点，然后执行 **ReadAllandExplains** 导出。所有受支持的已选节点会被批量处理。

### 控制台与自动化

导出指定对象路径：

```text
ReadAllandExplains.ExportAssets /Game/Folder/M_Asset.M_Asset /Game/Folder/BP_Tool.BP_Tool
```

不提供对象路径时，将导出内容浏览器中当前选中的资产：

```text
ReadAllandExplains.ExportAssets
```

把所选根资产及其受支持的项目依赖导出为一个 Context Pack：

```text
ReadAllandExplains.ExportContextPack /Game/Folder/NS_Effect.NS_Effect
```

无人值守运行示例：

```powershell
UnrealEditor-Cmd.exe "D:\Projects\MyProject\MyProject.uproject" -unattended -nop4 -nosplash -NullRHI -NoSound -ExecCmds="ReadAllandExplains.ExportAssets /Game/Folder/M_Asset.M_Asset,Quit"
```

旧命令别名 `GetTheMeaning.ExportAssets` 仍然保留。

### Skill 与 MCP

- 配套 Skill 位于 `Skills/readallandexplains/SKILL.md`。
- 只读 stdio MCP 位于 `Integrations/MCP/readallandexplains_mcp.py`。
- 使用 Python 3.10+ 启动并传入 `--root <你的项目>/Saved/ReadAllandExplainsExports`，也可设置 `READALL_EXPORT_ROOT`。
- 客户端配置可参考 `Integrations/MCP/mcp-config.example.json`。
- MCP 支持 Context Pack 发现、资产搜索、紧凑摘要、Graph／Renderer／曲线精确读取和文本检索，无需一次加载完整导出文件。

## 输出目录

所有文件会写入：

```text
<你的项目>/Saved/ReadAllandExplainsExports/
```

插件会根据资产类型创建子目录：

```text
Blueprints/
Materials/
Niagara/
StaticMeshes/
Textures/
DataTables/
CurveTables/
```

启用元数据输出后，每份可读文档旁边还会生成一个 `<资产名称>.meta.json` 文件。

## 设置

打开 **Editor Preferences > Plugins > ReadAllandExplains**。

### 导出模式

| 模式 | 用途 |
| --- | --- |
| Artist | 适合美术人员快速阅读的概览 |
| Compact | 推荐默认值，降低 Token 占用 |
| Full | 输出更完整的技术信息 |
| Reconstruction | 最大程度保留图表数据，适合重建类工作流 |

### 其他设置

- **Write asset.meta.json**：控制是否生成结构化元数据文件。
- **AI Prompt Mode**：可选 `Explain`、`Review`、`Optimize`、`Trace` 或 `Custom`。
- **Custom Prompt**：选择 Custom 模式时追加的自定义提示词。

## 4.5.0-preview.1 更新内容

- 生成递归 AI Context Pack，包含清单、索引、可读文档和 Schema JSON 元数据。
- Niagara 曲线按稳定指纹去重，同时保留完整 Key、插值、切线、外推和全部 `usedBy` 来源。
- 停止生成可选 HTML/SVG 浏览器文件，Context Pack 默认只保留 Markdown 与 JSON。
- 新增无第三方依赖、纯只读的 stdio MCP，提供 5 个渐进式查询工具。
- 新增配套 UE 资产分析 Skill，用于蓝图、材质、Custom HLSL、Niagara、Renderer 绑定、依赖、节点图和曲线解释。
- 继续保留 Renderer 明细、项目 Niagara 模块递归图、Schema 2 `graphs` 与 `_ReadableNiagara.md` 兼容。
- 已通过 Unreal Engine 5.7 / Win64、真实 Niagara Context Pack、MCP 自动发现、资产索引和指定曲线读取验证。
