# MCP 中文乱码修复记录

## 结论

ReadAllandExplains 导出的 Context Pack 文件本身没有乱码。乱码发生在 Windows 上的 MCP stdio 输出通道：Python 的 `sys.stdout.write()` 继承系统代码页 `cp936/GBK`，但 MCP 客户端按照 UTF-8 解码 JSON-RPC 消息。

修复后，MCP 协议输出直接写入 UTF-8 原始字节，不再依赖 Windows 文本代码页。

## 问题现象

- 直接读取 Context Pack 的 Markdown 和 JSON 时中文正常。
- 通过 MCP 调用同一份数据时中文乱码，或客户端报告 UTF-8 解码失败。
- 修复前抓取 MCP stdout 原始字节时，能找到 GBK 编码的“材质”，找不到 UTF-8 编码的“材质”。
- 严格 UTF-8 解码会在类似 `B2` 的 GBK 字节处失败。

## 根因

服务端输入已经显式按 UTF-8 读取：

```python
request = json.loads(raw_line.decode("utf-8-sig"))
```

但是输出曾使用文本流：

```python
sys.stdout.write(json.dumps(outgoing, ensure_ascii=False) + "\n")
```

`ensure_ascii=False` 会保留真实中文字符。Windows 启动宿主若把 stdout 配置为 `cp936`，中文就会以 GBK 字节写入管道，违反 MCP JSON-RPC 使用 UTF-8 的预期。

## 修复内容

协议输出改为显式 UTF-8 字节：

```python
payload = (json.dumps(outgoing, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
sys.stdout.buffer.write(payload)
sys.stdout.buffer.flush()
```

Windows 批处理入口增加兼容性兜底：

```bat
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"
```

直接写 `sys.stdout.buffer` 是根本修复；环境变量是对自测输出、诊断输出及其他文本流的第二层保护。

## 修改位置

权威开发源码：

- `Integrations/MCP/readallandexplains_mcp.py`
- `Integrations/MCP/readallandexplains_mcp.bat`
- `Tests/MCP/test_contract.py`

真实 UE 项目部署副本：

- `D:/UE5/Trans/Plugins/ReadAllandExplains/Integrations/MCP/readallandexplains_mcp.py`
- `D:/UE5/Trans/Plugins/ReadAllandExplains/Integrations/MCP/readallandexplains_mcp.bat`

## 回归测试

新增测试 `test_stdio_emits_utf8_when_windows_text_encoding_is_cp936`：

1. 创建包含中文参数“泡沫强度”的临时 Context Pack。
2. 强制子进程环境 `PYTHONIOENCODING=cp936`，模拟 Windows 中文代码页。
3. 通过真实 stdio 发送 `get_asset_summary` JSON-RPC 请求。
4. 对 stdout 原始字节执行严格 UTF-8 解码。
5. 验证 JSON 可解析，并且中文参数仍为“泡沫强度”。

完整测试命令：

```powershell
python -m unittest discover -s ReadAllandExplains/Tests/MCP -p test_contract.py -v
```

2026-08-04 验证结果：`26/26` 通过。

## MCP 调用关系

```text
插件或脚本存在
  != 当前对话已挂载 MCP
当前对话已挂载 MCP
  != 每轮对话都会调用 MCP
```

正常流程是：

1. 客户端读取当前智能体或插件的 MCP 配置。
2. 客户端启动 stdio 服务并完成 MCP 握手。
3. 模型看到服务公布的工具列表。
4. 只有问题涉及 Context Pack、资产检索或 UE 结构分析时，模型才按需调用。
5. 工具返回的 JSON-RPC 结果被加入当前对话上下文。

因此，不同智能体、不同工作区或在配置更新前创建的旧对话，可能没有挂载该 MCP。修复服务代码后，应重启客户端或创建新对话，使 MCP 进程重新启动并加载新代码。

## 快速排查顺序

1. 先严格读取最新 Context Pack，确认磁盘文件是否为合法 UTF-8。
2. 检查当前对话是否能看到 `list_context_packs`、`search_assets` 等工具。
3. 若工具存在但中文乱码，捕获 stdout 原始字节，分别检查 UTF-8 与 GBK 中文标记。
4. 确认运行中的 MCP 进程来自预期的开发目录或部署目录。
5. 修复或更新脚本后重启 MCP 客户端，旧进程不会自动加载新代码。

## 避免复发

- MCP stdout 只输出协议消息，不打印调试日志。
- 协议消息始终通过 `sys.stdout.buffer` 写 UTF-8 字节。
- 调试信息只写 stderr，并避免让 stderr 混入 stdout。
- 保留模拟 `cp936` 的子进程回归测试。
- 发布或部署后，用包含中文的真实 MCP 调用做一次原始字节验证。
