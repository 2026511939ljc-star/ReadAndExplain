# ReadAllandExplains for CodeBuddy

本仓库同时是 Unreal Engine 插件与 CodeBuddy 插件。CodeBuddy 插件包含渐进式分析 MCP、Skill 和诊断命令；MCP 读取 Context Pack，并且只在用户明确批准具体补充范围后写入受限 SyncLive 请求，始终不会修改 `.uasset`。

## 本地测试

在仓库根目录执行：

```powershell
codebuddy plugin validate .
codebuddy --plugin-dir .
```

进入 CodeBuddy 后运行：

```text
/reload-plugins
/readallandexplains:readallandexplains-doctor
```

随后可以直接提问：

```text
列出最新 ReadAllandExplains Context Pack 里的资产。
解释 NS_Foo 的 Graph、Renderer 和关键曲线，并给出 Evidence。
如果关键材质尚未包含，先列出具体补充计划，得到我的许可后再定向补快照并继续分析。
```

SyncLive Lite 测试时保持 UE 编辑器打开且不要进入 PIE。CodeBuddy 会先展示资产范围、数量、依赖深度和基线 Pack 指纹；只有你明确许可后才提交请求。请求状态长期为 `pending` 时，检查插件是否已加载以及 Editor Preferences > Plugins > ReadAllandExplains 中的 SyncLive Lite 开关。

## 数据发现顺序

MCP 按以下顺序确定导出根目录：

1. MCP 参数 `--root`。
2. 环境变量 `READALL_EXPORT_ROOT`。
3. CodeBuddy 当前工作区或父目录中的 UE 项目：`<Project>/Saved/ReadAllandExplainsExports`。
4. 当前目录的 `Saved/ReadAllandExplainsExports`。

如果自动发现不适合当前目录，在启动 CodeBuddy 前设置：

```powershell
$env:READALL_EXPORT_ROOT = "D:/YourProject/Saved/ReadAllandExplainsExports"
codebuddy --plugin-dir .
```

## 市场安装

仓库发布到 GitHub 后，用户可以在 CodeBuddy 中执行：

```text
/plugin marketplace add 2026511939ljc-star/ReadAndExplain
/plugin install readallandexplains@readallandexplains-marketplace
/reload-plugins
```

ReadAllandExplains 4.8.0-preview.2 要求本机 `python` 3.9 或更高版本可从 `PATH` 调用。MCP 仅使用 Python 标准库，不需要 npm 或额外 Python 包；独立 Node 运行时可在后续版本提供。
