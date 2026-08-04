# ReadAllandExplains for CodeBuddy

本仓库同时是 Unreal Engine 插件与 CodeBuddy 插件。CodeBuddy 插件包含只读 MCP、渐进式分析 Skill 和诊断命令，不会修改 `.uasset`。

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
```

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

当前预览版要求本机 `python` 可从 `PATH` 调用。正式 npm/Node 独立运行时将在后续产品化版本处理，不阻塞当前 CodeBuddy 逻辑测试。
