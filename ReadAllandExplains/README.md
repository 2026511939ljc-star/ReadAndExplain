# ReadAllandExplains

ReadAllandExplains 是面向 Unreal Engine 技术美术资产的 AI Context Compiler。插件把 Blueprint、Material、Niagara 等资产导出为便于人类速读的 Markdown，以及便于 AI 和工具消费的结构化 JSON 元数据。

## 4.6.0-preview.2（开发中）

1. Skill 将大型问题拆成最多 3 个小任务，默认优先材质和 Niagara；下钻预算为最多 5 个资产、直接依赖深度 1。
2. `get_asset_detail(section="coverage")` 返回当前资产直接依赖的包内覆盖、缺失 `/Game/` 依赖、外部依赖与定向补拍候选，不增加新的 MCP 工具。
3. 缺口分为“阻塞结论”“提高置信度”“运行时验证”，先完成已有证据可回答的部分，再一次性请求补拍许可。
4. 文本读取严格使用 UTF-8/UTF-8 BOM；非法编码返回 `TEXT_ENCODING_INVALID`，不再用替换字符掩盖乱码。
5. 保留 4.6.0-preview.1 的 `rae.mcp/1.0` Envelope、Evidence、稳定错误、分页、原子 Context Pack、Manifest 指纹和 CodeBuddy 原生接入。
6. Golden Context Pack 契约测试扩展到依赖 coverage、中文往返、非法编码和 Skill 渐进式工作流。

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

- Skill 位于 `skills/readallandexplains/SKILL.md`，负责 UE 资产解释、曲线分析、HLSL 阅读和按需查询流程。
- MCP 位于 `Integrations/MCP/readallandexplains_mcp.py`，只读访问导出缓存，不直接修改 `.uasset`。
- CodeBuddy 原生入口为 `.codebuddy-plugin/plugin.json` 和 `.mcp.json`；本地验证运行 `codebuddy plugin validate .`，测试运行 `codebuddy --plugin-dir .`。
- CodeBuddy 会通过 `CODEBUDDY_PROJECT_DIR` 自动寻找当前或嵌套 UE 项目的 `Saved/ReadAllandExplainsExports`；仍可用 `READALL_EXPORT_ROOT` 或 `--root` 显式覆盖。
- 诊断命令：`/readallandexplains:readallandexplains-doctor`。
- Windows 可通过 `Integrations/MCP/readallandexplains_mcp.bat` 单独启动；通用客户端配置参考 `Integrations/MCP/mcp-config.example.json`。
- MCP 工具：`list_context_packs`、`search_assets`、`get_asset_summary`、`get_asset_detail`、`search_export_text`。
- 运行契约测试：`python -m unittest discover -s Tests/MCP -p "test_*.py" -v`。

## 输出

- Blueprint：`_ReadableCode.txt` 与 `.meta.json`
- Material / Material Function：`_ReadableMaterial.md` 与 `.meta.json`
- Niagara System / Emitter / Script：`_ReadableNiagara.md` 与 `.meta.json`

默认推荐使用 Compact 模式。旧的详细 Niagara 文本仍会保留，新增统一 IR 主要写入元数据 JSON，并在 Markdown 中显示图统计。

## 当前边界

- Context Pack 只递归 `/Game/` 下插件可导出的项目资产；引擎内容和不支持的资产类型保留在依赖清单中但不单独导出。
- 当前 MCP 查询导出缓存，不实时连接 UE；刷新资产后需要重新导出 Context Pack。
- 完整参数级 DAG、Live Sync、反向导入和正式 Marketplace 发布包尚未完成。

## 构建验证

`4.5.0-preview.1` 已通过 Unreal Engine 5.7 / Win64 Development 完整 C++ 编译、DLL 链接、Trans 无界面加载和真实 Niagara Context Pack 导出验证；新包仅包含 Markdown/JSON，SVG/HTML 文件数为 0。
