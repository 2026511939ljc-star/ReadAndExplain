# ReadAllandExplains

ReadAllandExplains 是面向 Unreal Engine 技术美术资产的 AI Context Compiler。插件把 Blueprint、Material、Niagara 等资产导出为便于人类速读的 Markdown，以及便于 AI/工具消费的结构化 JSON 元数据。

## 4.3.0-preview.1

1. Niagara Renderer 现可输出类型、材质、Source Mode、属性绑定和可编辑配置。
2. Float、Vector、Color 曲线现可输出原始 Key、插值、切线、权重和前后外推模式。
3. `/Game/` 下项目自定义模块可递归展开为 `NiagaraProjectModuleGraph`，带循环去重和最大深度限制。
4. 函数调用节点新增引用路径、被调图、脚本版本和启用状态。
5. 保留 4.2 的 Schema 2 `graphs` 与旧 `_ReadableNiagara.md` 输出兼容。

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

- 项目自定义 Niagara 模块已可递归展开；全部引擎内置模块仍只保留引用与输入。
- 同源曲线可能因不同脚本上下文产生重复记录，曲线指纹去重将在后续版本完善。
- Markdown 当前展示曲线概览，完整 Key 数据位于 `.meta.json`。
- 完整参数级 DAG、Live Sync、反向导入和正式 Marketplace 发布包尚未完成。

## 构建验证

`4.3.0-preview.1` 已在 Unreal Engine 5.7 / Win64 下通过完整 C++ 编译、DLL 链接和真实 Niagara System 导出验证。