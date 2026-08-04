---
name: readallandexplains
description: 分析 ReadAllandExplains 导出的 Unreal Engine AI Context Pack、Markdown 和 Schema JSON。用户要求解释 UE 蓝图、材质、Material Custom HLSL、Niagara System/Emitter/Module、Renderer 绑定、参数依赖、节点图或曲线，或提到 ReadAllandExplains/ReadAndExplain Context Pack 时使用。优先通过配套 MCP 渐进式查询，避免一次加载大型导出文件。
---

# ReadAllandExplains

把插件导出视为事实来源。只解释文档中实际存在的节点、连接、参数、曲线和依赖；信息缺失时明确指出需要回到 UE 核对。

## 查询顺序

1. 未指定 Context Pack 时，先调用 `list_context_packs` 选择最新包；用户给定目录时直接使用。
2. 调用 `search_assets` 定位资产，不要先读取完整 Markdown 或 `.meta.json`。
3. 调用 `get_asset_summary` 获取资产类型、参数线索、依赖数量及 Graph、Renderer、Curve 数量。
4. 只按问题调用 `get_asset_detail`：
   - 参数来源：`parameters`
   - 依赖或反向引用：`dependencies`、`referencers`
   - 图列表：`graphs`
   - 单张完整图：`graph`，同时提供 `item_id`
   - Renderer：`renderers`
   - 曲线列表或单条曲线：`curves`，单条时提供 `item_id`
   - 人类摘要：`readable`，使用 `offset` 和 `limit` 分段
   - 完整结构：仅确有必要时用 `metadata`
5. 查 Custom HLSL、节点名、参数名或模块名时，先调用 `search_export_text`；命中后再读取相关段落或图。

若 MCP 不可用，按相同顺序直接读取 `context-pack.json` → `index.json` → 目标资产文档 → 目标 `.meta.json`，禁止无目标地加载整个目录。

## 分析规则

- Markdown 用于速读与语义解释，`.meta.json` 用于核对准确的节点、Pin、Link、Renderer 和曲线 Key。
- 同名参数不等于已连接。只有 Pin/Link、绑定、调用关系或明确依赖能证明连接。
- `UnknownExpr` 表示节点存在但语义未完全翻译，不得说成节点丢失。
- 曲线按 `fingerprint` 去重，`usedBy`/`usageCount` 是同一形状的使用位置，不是多条不同曲线。
- 解释曲线时报告通道、Key 时间和值、插值、切线、时间范围、使用模块，并用“前快后慢、峰值、回落”等美术语言总结。
- 解释 Custom HLSL 时报告源码、输入及上游连接、输出、额外输出、Include、Define 和调用到的材质属性；任一字段未导出时明确标记缺失。
- 不把引擎内置依赖全部展开。优先 `/Game/` 项目依赖、自定义模块、材质、函数、贴图和 Renderer 绑定。
- 默认先给结论和美术含义，再给技术证据；除非用户要求，不输出整份原始 JSON。

## 常用任务

- “这个特效怎么工作的”：摘要 → Graph 列表 → Renderer → 关键曲线 → `/Game/` 依赖。
- “ScaleSpriteSize 曲线是什么样”：文本搜索名称 → 曲线列表 → 按 ID 读取完整曲线 → 总结 Key、插值和变化节奏。
- “材质里的 HLSL 做什么”：搜索 `Custom`、`HLSL` 或节点名 → 读取命中段落/图 → 沿输入输出说明。
- “哪里可能有问题”：检查无效 Renderer 绑定、源中不存在的变量、缺失依赖、未知节点、悬空 Pin/Link 和异常曲线范围。
- “两个版本有什么变化”：分别选定两个 Context Pack，按稳定资产路径、Graph ID 和曲线指纹比较；不要用文件时间代替结构差异。
