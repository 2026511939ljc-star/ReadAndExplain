# ReadAllandExplains

ReadAllandExplains 是面向 Unreal Engine 技术美术资产的 AI Context Compiler。插件把 Blueprint、Material、Niagara 等资产导出为便于人类速读的 Markdown，以及便于 AI 和工具消费的结构化 JSON 元数据。

## 4.6.0-preview.1（开发中）

1. 现有 5 个 MCP 工具统一返回 `rae.mcp/1.0` Envelope，包含 Pack、Asset、Evidence、分页、警告、缺失字段和稳定错误码。
2. 工具声明 `outputSchema` 与只读 Tool Annotations，同时返回文本和 `structuredContent`。
3. Context Pack 使用 `.tmp` 目录生成，Manifest 从 `writing` 切换为 `complete` 后再同目录重命名发布，避免读取半写快照。
4. Manifest 增加 `packId`、状态、包内相对路径、文件大小与 BLAKE3-160 内容指纹。
5. 新增无第三方依赖的 Golden Context Pack 契约测试，覆盖协议、Evidence、分页、错误码、Schema、writing 包跳过与路径越界。

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

## Skill 与 MCP

- Skill 位于 `Skills/readallandexplains/SKILL.md`，负责 UE 资产解释、曲线分析、HLSL 阅读和按需查询流程。
- MCP 位于 `Integrations/MCP/readallandexplains_mcp.py`，只读访问导出缓存，不直接修改 `.uasset`。
- Windows 可通过 `Integrations/MCP/readallandexplains_mcp.bat` 启动；客户端配置参考 `Integrations/MCP/mcp-config.example.json`。
- 可设置环境变量 `READALL_EXPORT_ROOT`，或启动时传入 `--root <ReadAllandExplainsExports>`。
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
