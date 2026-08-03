# ReadAllandExplains

ReadAllandExplains 是面向 Unreal Engine 技术美术资产的 AI Context Compiler。插件把 Blueprint、Material、Niagara 等资产导出为便于人类速读的 Markdown，以及便于 AI/工具消费的结构化 JSON 元数据。

## 4.2.0-preview.1

1. Niagara System、Emitter、Script 的源图已增量接入 Schema 2 统一图 IR，输出稳定的 Graph、Node、Pin、Link 数据。
2. 保留原有 `_ReadableNiagara.md` 详细文本导出，不修改旧导出器和使用入口，降低升级风险。
3. Niagara Parameter Map 连线单独标记为 `parameter-map`，其他连线标记为 `data`，便于后续参数级 DAG 分析。
4. Blueprint、Material、Niagara 现在可以通过同一套 `.meta.json` 图结构供 AI 与后续工具消费。
5. Compact Markdown、参数线索、直接依赖、AI 提示词和现有命令行入口保持兼容。

## 使用方式

1. 将 `ReadAllandExplains` 文件夹放入项目的 `Plugins` 目录。
2. 使用 Unreal Engine 5.7 打开项目并启用插件。
3. 在内容浏览器选中资产，使用 ReadAllandExplains 导出入口。
4. 导出结果位于项目 `Saved/ReadAllandExplainsExports` 目录。

无界面导出命令：`ReadAllandExplains.ExportAssets`。旧命令 `GetTheMeaning.ExportAssets` 继续兼容。

## 输出

- Blueprint：`_ReadableCode.txt` 与 `.meta.json`
- Material / Material Function：`_ReadableMaterial.md` 与 `.meta.json`
- Niagara System / Emitter / Script：`_ReadableNiagara.md` 与 `.meta.json`

默认推荐使用 Compact 模式。旧的详细 Niagara 文本仍会保留，新增统一 IR 主要写入元数据 JSON，并在 Markdown 中显示图统计。

## 当前边界

- Niagara 已完成源图级统一 IR；完整参数级 DAG 仍在后续计划中。
- 当前依赖关系为直接依赖，尚未递归展开整条依赖链。
- Live Sync、反向导入和正式 Marketplace 发布包尚未完成。

## 构建验证

`4.2.0-preview.1` 已在 Unreal Engine 5.7 / Win64 下通过完整 C++ 编译与链接验证。