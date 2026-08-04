# ReadAllandExplains 4.6.0-preview.1

本版本开始建立可信查询层，重点不是增加更多读取接口，而是让每次查询都可验证、可分页、可测试，并确保 MCP 不会读取正在写入或已损坏的 Context Pack。

## 主要更新

1. 现有 5 个 MCP 工具统一返回 `rae.mcp/1.0` Envelope：`pack`、`asset`、`data`、`evidence`、`page`、`warnings`、`missing_fields` 与 `error`。
2. 错误改为稳定机器码，例如 `PACK_NOT_FOUND`、`PACK_INCOMPLETE`、`SCHEMA_UNSUPPORTED`、`ASSET_NOT_FOUND`、`INVALID_SECTION` 和 `RESULT_TOO_LARGE`。
3. 工具声明 `outputSchema`、`readOnlyHint`、`destructiveHint=false`、`idempotentHint=true` 与 `openWorldHint=false`，同时返回文本和 `structuredContent`。
4. 列表与文本详情支持机器可读分页：`offset`、`limit`、`next_cursor`、`total` 与 `truncated`。
5. 重要结果携带来源文件、JSON Pointer、资产路径、目标 ID 或行号，Skill 要求先核对 Evidence、Warnings 和 Missing Fields 再形成结论。
6. MCP 对协议版本做真实兼容检查，并只自动选择 `complete` 且 Schema 受支持的 Context Pack。
7. Context Pack 先写入同目录 `.tmp`，Manifest 标记 `writing`；文件全部生成后写入 `complete` 清单，再重命名为正式目录。
8. Manifest 增加 `packId`、`state`、包指纹，以及各文件的包内相对路径、大小与 BLAKE3-160 内容指纹。
9. MCP 会校验新 Manifest 文件清单的目录边界、存在性和大小，异常包返回 `PACK_INCOMPLETE`。
10. 新增无第三方依赖的 Golden Context Pack 契约测试。

## 验证

- Unreal Engine 5.7 / Win64 Development 增量编译和 DLL 链接成功。
- 独立 CompileHost 真实导出 `DefaultMaterial` Context Pack 成功，Manifest 为 `state=complete`，无 `.tmp` 残留。
- Manifest 文件清单共 5 项，路径均为包内相对路径，文件存在且大小一致。
- MCP 成功自动发现新包并读取 `DefaultMaterial` 摘要，返回 Manifest 自带 BLAKE3-160 包指纹和元数据 Evidence。
- 14 项 MCP 契约测试通过。

运行测试：

```powershell
python -m unittest discover -s Tests/MCP -p "test_*.py" -v
```

## 当前边界

- 文件清单当前校验路径、存在性与大小；BLAKE3 文件内容复算可在后续按性能策略增加。
- 节点、Pin、Renderer 的跨版本稳定 ID 仍需继续在 UE 导出 IR 层完善。
- Graph Query、结构化 Diff 和 TA Audit 仍属于后续版本。
