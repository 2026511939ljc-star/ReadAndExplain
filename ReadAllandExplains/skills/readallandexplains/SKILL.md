---
name: readallandexplains
description: 分析 ReadAllandExplains 导出的 Unreal Engine AI Context Pack、Markdown 和 Schema JSON。用于解释 UE 材质、Material Custom HLSL、Niagara System/Emitter/Module、Renderer、曲线、参数依赖和与视觉表现相关的蓝图调用；按小任务渐进读取，识别当前快照缺口，生成定向补快照计划，并给出有证据的美术建议。
---

# ReadAllandExplains

把当前完整 Context Pack 视为静态事实来源。目标是“快照 → 快速还原 → 理解表现 → 美术建议”，不是一次加载整个项目。

## 硬性边界

- 只把快照中实际存在的节点、Pin、Link、参数、绑定、曲线和依赖标为“已确认”。
- 允许基于已确认结构做“结构推断”，但必须说明依据和置信度。
- 快照未包含某资产或字段时，写“当前快照尚未包含，需要补充”，不得写成“该资产不存在”“没有该逻辑”或凭经验补全。
- 静态快照不能证明逐帧 GPU 结果、动态 Render Target 内容、PIE 时序或运行时参数最终值；把这些列为“需要 UE 运行时验证”。
- 优先分析材质和 Niagara。蓝图只追踪视觉表现所需的参数写入者、动态材质、Niagara User 参数、启停事件和资产赋值入口。
- 重要结论必须回指 `evidence`；Evidence 证明快照内容，不自动证明运行时行为。

## 分步工作流

### 1. 拆分任务

收到跨多个资产或系统的大问题时，先拆成最多 3 个可独立验证的小任务，例如：

1. 根资产和 Renderer 输出。
2. 材质表现链与实例覆盖。
3. Niagara 生成、更新、曲线和材质绑定。

选择与用户问题最相关的一项先完成。每项都设停止条件：已能解释当前局部的结构、视觉作用和主要调节入口，或发现阻塞证据缺口。不要同时展开全部蓝图、材质和 Niagara。

默认下钻预算为最多 5 个资产、直接依赖深度 1。超过预算时先汇总补充计划并请求许可，不递归抓取整个项目。

### 2. 读取已有快照

1. 未指定 Context Pack 时调用 `list_context_packs`，选择最新 `state=complete` 的包；用户指定包时直接使用。
2. 调用 `search_assets` 定位根资产，不先读取完整 Markdown 或 `.meta.json`。
3. 调用 `get_asset_summary` 获取资产类型、参数线索、依赖数量、Graph、Renderer 和 Curve 摘要。
4. 涉及跨资产关系或 `dependency_count>0` 时，调用 `get_asset_detail(section="coverage")`，先看哪些直接依赖已在包内、哪些 `/Game/` 依赖仍需定向补充。
5. 只为当前小任务读取详情：
   - 参数：`parameters`
   - 依赖覆盖：`coverage`
   - 依赖或反向引用：`dependencies`、`referencers`
   - 图列表：`graphs`
   - 单张完整图：`graph`，并提供 `item_id`
   - Renderer：`renderers`
   - 曲线：`curves`，单条曲线提供 `item_id`
   - 人类摘要：`readable`，使用 `offset`、`limit` 分段
   - 完整结构：仅确有必要时使用 `metadata`
6. 查 Custom HLSL、节点名、参数名或模块名时，先调用 `search_export_text`，命中后再读取目标段落或图。

若 MCP 不可用，按同样顺序直接读取 `context-pack.json` → `index.json` → 目标资产文档 → 目标 `.meta.json`。禁止无目标地加载整个导出目录。

### 3. 校验每次返回

- `error` 非空时不得把返回内容当事实。
- `warnings` 和 `missing_fields` 必须进入当前缺口判断。
- `page.truncated=true` 时按 `next_cursor` 继续读取，直到当前问题所需部分完整；不得把第一页当成完整 Graph 或列表。
- 使用 `pack_id`、`state` 和 `fingerprint` 保证同一轮证据来自同一个完整快照，不混用不同 Pack。
- Markdown 用于速读，`.meta.json` 用于核对节点、Pin、Link、Renderer、绑定和曲线 Key。
- 文本若返回 `TEXT_ENCODING_INVALID`，停止引用该文档并要求重新生成 UTF-8 快照；不得输出替换字符或猜测乱码原文。

## 缺口与补快照

发现缺口时先完成不受影响的分析，再按以下三类汇总，不逐项打断用户：

- `阻塞结论`：缺少后无法回答当前核心问题，例如 Renderer 材质、父材质、材质函数或项目 Niagara Module。
- `提高置信度`：已有结构足够给初步结论，但实例覆盖、反向引用或参数写入者能提高准确度。
- `运行时验证`：静态补拍也无法获得，例如 GPU 逐帧结果和动态参数最终值。

需要补静态证据时，一次性输出“补充计划”，每项包含：

- 资产或字段。
- 为什么需要。
- 补充后能回答什么。
- 优先级：阻塞或增强。

同时说明本轮预计补充资产数、依赖深度、只读范围和当前 Pack 指纹。默认建议不超过 5 个资产、深度 1。

在用户明确许可前，不调用 `request_targeted_snapshot`，不写入请求文件，也不触发任何 UE 操作。许可必须发生在本轮列出具体资产、数量、深度、用途和当前 Pack 指纹之后；笼统的历史许可不能用于未展示的新范围。

用户许可后：

1. 再调用 `list_context_packs` 或当前读取结果，确认基线 Pack 仍为 `complete` 且指纹未变化。
2. 调用 `request_targeted_snapshot`，只提交已获许可的 1 至 5 个 `/Game/` 资产；默认 `dependency_depth=0`，只有计划明确包含直接依赖时才使用 `1`。必须传 `permission_granted=true`、基线 `pack_path` 和完全一致的 `base_pack_fingerprint`。
3. 保存返回的 `request_id`，调用 `get_snapshot_request_status` 查询状态。`pending` 表示 UE 尚未消费；`processing` 表示正在导出；`complete` 才能继续分析；`failed` 或 `rejected` 必须向用户说明错误码，不自动扩大范围或无限重试。
4. 状态长期停在 `pending` 时，说明 UE 编辑器可能未运行、插件未加载、处于 PIE，或 SyncLive Lite 已关闭；不得声称后台正在执行。
5. 完成后检查返回 Pack 的 `state=complete`、`originRequestId`、`basePackId`、根资产、时间和新指纹；重新读取目标资产及 coverage，再从中断的小任务续答，不要求用户重复原问题。

请求只生成静态 Context Pack，不执行控制台命令、不修改 UE 资产、不保存关卡，也不观测运行时 GPU。用户拒绝时，只给已确认内容和受影响结论。

## 资产分析规则

- 同名参数不等于已连接。只有 Pin/Link、绑定、调用关系或明确依赖能证明连接。
- `UnknownExpr` 表示节点存在但语义未完全翻译，不得说成节点丢失。
- 材质实例先核对父材质和覆盖参数；材质函数只沿当前视觉问题相关的调用链下钻。
- Niagara 按 System → Emitter → Spawn/Update/Simulation Stage → Renderer → 材质组织，报告执行顺序、参数来源与 Binding。
- 曲线按 `fingerprint` 去重；`usedBy`/`usageCount` 表示同一形状的使用位置，不是多条不同曲线。
- 解释曲线时报告通道、Key 时间和值、插值、切线、范围和使用模块，并用“前快后慢、峰值、回落”等美术语言概括。
- 解释 Custom HLSL 时报告源码、输入和上游连接、输出、额外输出、Include、Define 与最终材质属性；未导出的字段进入补充计划。
- 不默认展开 `/Engine/` 依赖。优先 `/Game/` 材质、材质函数、Niagara Module、Renderer 材质、MPC 和与当前问题相关的蓝图写入者。

## 输出检查点

每完成一个小任务，按以下顺序回答：

1. `本轮已确认`：结构和数据流，只陈述有证据的事实。
2. `美术含义与建议`：说明它如何影响颜色、泡沫、透明度、形态、节奏或性能。
3. `还需补充`：只列阻塞或明显提高置信度的缺口；没有则省略。
4. `下一步`：给出 1 至 3 个可继续分析的小任务，不自动无限下钻。

每条美术建议尽量包含目标效果、对应资产或参数、当前值或“当前值待补充”、调整方向、可能副作用和 UE 验证方法。默认先给结论和美术含义，再给精简 Evidence；除非用户要求，不输出整份原始 JSON。

## 常用路径

- “这个特效怎么工作的”：根资产摘要 → coverage → Renderer → 关键 Graph → 关键曲线 → 当前问题相关 `/Game/` 依赖。
- “材质为什么这样显示”：材质实例覆盖 → 父材质 → 目标材质属性链 → 相关材质函数 → 美术调节建议。
- “Niagara 怎么驱动材质”：Renderer Binding → Niagara 参数来源 → 材质参数消费者 → 曲线或模块写入点。
- “哪里可能有问题”：无效 Binding、源变量不存在、缺失项目依赖、未知节点、悬空 Link、异常曲线范围和透明 Overdraw 风险。
