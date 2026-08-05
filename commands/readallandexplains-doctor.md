---
description: "检查 ReadAllandExplains MCP、UE 导出目录和 Context Pack 是否已准备好"
argument-hint: "[可选：ReadAllandExplainsExports 目录]"
---

检查 ReadAllandExplains 的 CodeBuddy 接入状态。

1. 确认环境变量 `CODEBUDDY_PLUGIN_ROOT` 指向已安装插件目录。
2. Windows PowerShell 下，如果 `$ARGUMENTS` 非空，运行：
   `python "$env:CODEBUDDY_PLUGIN_ROOT/Integrations/MCP/readallandexplains_mcp.py" --root "$ARGUMENTS" --diagnose`
3. Windows PowerShell 下，如果 `$ARGUMENTS` 为空，运行：
   `python "$env:CODEBUDDY_PLUGIN_ROOT/Integrations/MCP/readallandexplains_mcp.py" --diagnose`
4. macOS/Linux 下使用 `${CODEBUDDY_PLUGIN_ROOT}` 替代 `$env:CODEBUDDY_PLUGIN_ROOT`。
5. 报告 Python、自动识别的导出根目录、Context Pack 数量和下一步处理。
6. 如果没有 Context Pack，提示用户在 UE 内容浏览器执行“生成 AI Context Pack（含项目依赖）”，然后运行 `/reload-plugins` 或新建对话重试。
7. 不修改 `.uasset`、导出文档或 CodeBuddy 配置。
