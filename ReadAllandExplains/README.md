# ReadAllandExplains

ReadAllandExplains 是面向 Unreal Engine 技术美术资产的 AI Context Compiler。插件把 Blueprint、Material、Niagara 等资产导出为便于人类速读的 Markdown，以及便于 AI 和工具消费的结构化 JSON 元数据。

## 4.6.0-preview.3（开发中）

1. 保留 preview.2 的大任务拆分、材质/Niagara 优先、coverage、严格 UTF-8 和证据式渐进读取。
2. 新增 SyncLive Lite：用户批准具体补充计划后，MCP 可向正在运行的 UE 编辑器提交 1 至 5 个 `/Game/` 资产的定向静态补快照请求。
3. 请求强制绑定完整基线 Pack 指纹，依赖深度仅允许 0 或 1；UE 消费端会独立复验许可、范围、资产类型和指纹。
4. 请求采用 `pending → processing → complete/failed/rejected` 状态机，文件原子落盘，编辑器中断后可审计恢复。
5. 新 Pack 记录 `originRequestId` 与 `basePackId`；Skill 核验后从被阻塞的小任务继续，不要求用户重复问题。
6. SyncLive Lite 不执行任意命令，不修改资产、不保存关卡、不控制或观测运行时。

## 4.5.0-preview.1

1. 新增 AI Context Pack：以所选资产为根，递归收集 `/Game/` 下受支持的项目依赖并输出独立上下文目录。
2. Niagara 曲线按 Key、插值、切线、外推与配置生成稳定指纹，同形副本合并且通过 `usedBy` 保留全部来源。
3. 曲线完整 Key 保留在 `.meta.json` 中，不额外生成浏览器文件；配套 Skill/MCP 可按需读取并解释曲线。
4. Context Pack 内含 `README.md`、`context-pack.json`、批量索引和分类资产文档。
5. Renderer 明细、项目自定义模块递归图与 Schema 2 `graphs` 继续保持兼容。
6. 新增只读本地 MCP 与配套 Skill，使用“索引 → 摘要 → 目标片段”的渐进式读取流程，避免 AI 一次加载大型导出文件。

## 使用方式

1. 将 `ReadAllandExplains` 文件夹放入项目的 `Plugins` 目录。
2. 使用 Unreal Engine 5.7 打开项目并启用插件。
3. 普通导出：在内容浏览器选择“导出所有选中为 AI 可读文档”。
4. 完整上下文：选择“生成 AI Context Pack（含项目依赖）”。
5. 导出结果位于项目 `Saved/ReadAllandExplainsExports` 目录。

无界面命令：`ReadAllandExplains.ExportAssets` 和 `ReadAllandExplains.ExportContextPack`。旧命令 `GetTheMeaning.ExportAssets` 继续兼容。

## Skill、MCP 与 CodeBuddy

- Skill 位于 `skills/readallandexplains/SKILL.md`，负责 UE 资产解释、曲线分析、HLSL 阅读和按需查询流程；这是唯一可编辑源，工作区 `.agent/skills` 只是安装副本。
- 使用 `Scripts/SyncWorkspaceSkill.ps1` 将仓库 Skill 单向同步到指定工作区；同步后重新加载 CodeBuddy 或开启新对话。
- MCP 位于 `Integrations/MCP/readallandexplains_mcp.py`；读取 Context Pack，并在明确许可后写入受限 SyncLive 请求，但从不修改 `.uasset`。
- CodeBuddy 原生入口为 `.codebuddy-plugin/plugin.json` 和 `.mcp.json`；本地验证运行 `codebuddy plugin validate .`，测试运行 `codebuddy --plugin-dir .`。
- 源码、构建产物、部署副本、导出数据和发布流程的完整边界见 `docs/REPOSITORY_MANAGEMENT.md`。
- CodeBuddy 会通过 `CODEBUDDY_PROJECT_DIR` 自动寻找当前或嵌套 UE 项目的 `Saved/ReadAllandExplainsExports`；仍可用 `READALL_EXPORT_ROOT` 或 `--root` 显式覆盖。
- 诊断命令：`/readallandexplains:readallandexplains-doctor`。
- Windows 可通过 `Integrations/MCP/readallandexplains_mcp.bat` 单独启动；通用客户端配置参考 `Integrations/MCP/mcp-config.example.json`。
- MCP 工具：5 个读取工具，以及授权后使用的 `request_targeted_snapshot`、`get_snapshot_request_status`。
- 运行契约测试：`python -m unittest discover -s Tests/MCP -p "test_*.py" -v`。

## 输出

- Blueprint：`_ReadableCode.txt` 与 `.meta.json`
- Material / Material Function：`_ReadableMaterial.md` 与 `.meta.json`
- Niagara System / Emitter / Script：`_ReadableNiagara.md` 与 `.meta.json`

默认推荐使用 Compact 模式。旧的详细 Niagara 文本仍会保留，新增统一 IR 主要写入元数据 JSON，并在 Markdown 中显示图统计。

## 当前边界

- Context Pack 只递归 `/Game/` 下插件可导出的项目资产；引擎内容和不支持的资产类型保留在依赖清单中但不单独导出。
- SyncLive Lite 需要 UE 编辑器正在运行且插件已加载；PIE 期间暂停处理。它只生成静态补充 Pack，不是实时运行时桥接。
- 完整参数级 DAG、运行时观测、反向导入和正式 Marketplace 发布包尚未完成。

## 构建验证

`4.6.0-preview.3` 的 C++ 产品代码已通过 Unreal Engine 5.7 / Win64 Development 的 UHT、完整编译和 DLL 链接；当前仓库的 MCP、SyncLive Lite、Skill、版本及管理契约测试为 `28/28` 通过。`4.5.0-preview.1` 已完成 Trans 无界面加载和真实 Niagara Context Pack 导出验证；新包仅包含 Markdown/JSON，SVG/HTML 文件数为 0。
