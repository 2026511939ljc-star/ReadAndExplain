# ReadAllandExplains 4.5.0-preview.1

本版本优先提升 AI 兼容性和读取效率：取消额外浏览器文件，并加入可直接使用的 Skill 与只读 MCP。

## 主要更新

1. 不再生成 `_NiagaraCurves.svg` 或其他 HTML/SVG 浏览器文件，默认保持 Markdown + JSON 输出。
2. Niagara 曲线完整 Key、插值、切线、外推、指纹和 `usedBy` 继续保存在 `.meta.json`，数据不丢失。
3. 新增 `Skills/readallandexplains/SKILL.md`，统一 UE 资产、曲线、Renderer、依赖、节点图和 Custom HLSL 的渐进式分析流程。
4. 新增无第三方运行依赖的只读 stdio MCP，直接查询 `Saved/ReadAllandExplainsExports` 缓存。
5. MCP 提供 Context Pack 列表、资产搜索、紧凑摘要、按段详情和跨 Markdown/JSON 文本检索。
6. MCP 缓存已解析 JSON，并限制读取范围为选定 Context Pack，避免重复加载大型文件和越界读取。
7. Context Pack 内的使用说明改为“索引 → 摘要 → 目标片段”，优先使用 Skill/MCP 按需读取。

## MCP 工具

- `list_context_packs`
- `search_assets`
- `get_asset_summary`
- `get_asset_detail`
- `search_export_text`

## AI 集成目录

```text
ReadAllandExplains/
├── Skills/readallandexplains/SKILL.md
└── Integrations/MCP/
    ├── readallandexplains_mcp.py
    ├── readallandexplains_mcp.bat
    └── mcp-config.example.json
```

## 兼容性

- Unreal Engine 5.7 / Win64 Editor
- Python 3.10+，MCP 无第三方运行依赖
- Schema 2 元数据保持兼容
- MCP 当前读取导出缓存，不实时修改 UE 或 `.uasset`

## 验证状态

- MCP Python 语法检查通过。
- UE 5.7 / Win64 Development 编译成功，13 个构建动作全部完成，并已部署至 Trans 项目。
- 新 DLL 与编译产物 SHA-256 一致，Trans 无界面启动并完成引擎初始化。
- 真实 Niagara System 导出成功：3 个资产成功、0 失败、0 跳过；新 Context Pack 仅包含 Markdown/JSON，SVG/HTML 文件数为 0。
- 最新 Context Pack 的 MCP 自动发现、资产索引与 `ScaleSpriteSize` 完整曲线查询通过；曲线 Key、指纹和 4 个 `usedBy` 均保留。
