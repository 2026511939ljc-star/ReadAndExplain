# ReadAllandExplains 4.8.0-preview.2 安装指南

ReadAllandExplains 4.8.0-preview.2 面向 Unreal Engine 5.7 / Windows 64 位。Release ZIP 同时包含 UE 编辑器插件、CodeBuddy Skill、只读 MCP 与诊断命令。

## 环境要求

- Unreal Engine 5.7，Windows 64 位。
- 一个 C++ 或 Blueprint UE 项目。
- 如需 CodeBuddy/MCP：CodeBuddy 与可从 `PATH` 调用的 Python 3.9 或更高版本。
- 不需要安装 npm 包，也不需要全局安装 Python 依赖；MCP 仅使用 Python 标准库。

## 安装 UE 插件

1. 关闭 Unreal Editor。
2. 从 GitHub Release 下载 `ReadAllandExplains_4.8.0-preview.2_UE5.7_Win64.zip`。
3. 在项目根目录创建 `Plugins` 文件夹（若尚不存在）。
4. 解压后确认描述符路径为：

```text
<YourProject>/Plugins/ReadAllandExplains/ReadAllandExplains.uplugin
```

5. 使用 Unreal Engine 5.7 打开项目。
6. 如果编辑器提示重新编译模块，选择确认；Release ZIP 已包含 Win64 二进制，正常情况下无需本机编译。
7. 在 `Edit > Plugins` 中搜索 `ReadAllandExplains`，确认插件已启用，然后按提示重启编辑器。

不要解压成双层目录，例如：

```text
<YourProject>/Plugins/ReadAllandExplains/ReadAllandExplains/ReadAllandExplains.uplugin
```

## 验证 UE 安装

1. 在 Content Browser 选择受支持资产。
2. 右键选择“导出所有选中为 AI 可读文档”，或选择“生成 AI Context Pack（含项目依赖）”。
3. 确认项目中出现：

```text
<YourProject>/Saved/ReadAllandExplainsExports
```

4. 正式 Context Pack 位于 `ContextPacks/ContextPack_*`，其 `context-pack.json` 应为 `state: complete`。
5. `.tmp` 目录是失败或未完成诊断数据，不应作为正式 Pack 使用。

## 安装 CodeBuddy 集成

### 从 GitHub Marketplace 清单安装

在 CodeBuddy 中执行：

```text
/plugin marketplace add 2026511939ljc-star/ReadAndExplain
/plugin install readallandexplains@readallandexplains-marketplace
/reload-plugins
```

随后运行：

```text
/readallandexplains:readallandexplains-doctor
```

### 从本地插件目录测试

在已解压插件目录中执行：

```powershell
codebuddy plugin validate .
codebuddy --plugin-dir .
```

CodeBuddy 启动 MCP 时调用系统中的 `python`。可用以下命令检查：

```powershell
python --version
```

## 导出目录发现

MCP 按以下顺序查找导出数据：

1. 启动参数 `--root`。
2. 环境变量 `READALL_EXPORT_ROOT`。
3. CodeBuddy 当前工作区或父目录中的 UE 项目。
4. 当前目录的 `Saved/ReadAllandExplainsExports`。

自动发现失败时，在启动 CodeBuddy 前设置：

```powershell
$env:READALL_EXPORT_ROOT = "D:/YourProject/Saved/ReadAllandExplainsExports"
codebuddy --plugin-dir "D:/YourProject/Plugins/ReadAllandExplains"
```

## 升级 4.6 到 4.7

1. 关闭 Unreal Editor 和正在使用该插件的 CodeBuddy 会话。
2. 备份现有 `Plugins/ReadAllandExplains` 目录。
3. 删除旧插件目录，避免旧文件残留。
4. 解压 4.8.0-preview.2 ZIP 到相同位置。
5. 重启 UE，并在 CodeBuddy 中执行 `/reload-plugins`。
6. 再次运行诊断命令并生成一个新的 Context Pack。

4.7 可以读取 4.6 的旧 Pack；新 Pack 增加更严格的 Metadata 映射与完整性校验。建议升级后重新生成关键资产快照。

## 卸载

1. 关闭 Unreal Editor。
2. 删除 `<YourProject>/Plugins/ReadAllandExplains`。
3. 如已安装 CodeBuddy 插件，在 CodeBuddy 插件管理中卸载 `readallandexplains`。
4. 项目 `Saved/ReadAllandExplainsExports` 仅包含导出快照，可按需保留或手动删除，不影响 `.uasset`。

## 常见问题

### 插件未显示

确认 `ReadAllandExplains.uplugin` 没有位于双层目录，并确认项目使用 UE 5.7。

### MCP 无法启动

确认 `python --version` 可执行且版本不低于 3.9，然后 `/reload-plugins`。本版本不需要 npm。

### 找不到 Context Pack

先在 UE 中生成 Context Pack，或设置 `READALL_EXPORT_ROOT` 指向 `<YourProject>/Saved/ReadAllandExplainsExports`。

### SyncLive 请求一直是 pending

保持 UE 编辑器开启、插件已加载且不处于 PIE；检查 Editor Preferences 中 ReadAllandExplains 的 SyncLive Lite 开关。

### Pack 被报告为不完整

不要使用 `.tmp` 目录。查看其 `context-pack.json` 中的 `errors`，修复资产加载或写入问题后重新生成快照。

---

# Installation (English)

ReadAllandExplains 4.8.0-preview.2 targets Unreal Engine 5.7 on Windows 64-bit. The Release ZIP includes the UE editor plugin, CodeBuddy Skill, read-only MCP server, and diagnostics command.

## Requirements

- Unreal Engine 5.7 on Windows 64-bit.
- A Blueprint or C++ Unreal project.
- For CodeBuddy/MCP: CodeBuddy and Python 3.9+ available as `python` on `PATH`.
- No npm package or third-party Python dependency is required.

## Install the UE plugin

1. Close Unreal Editor.
2. Download `ReadAllandExplains_4.8.0-preview.2_UE5.7_Win64.zip` from GitHub Releases.
3. Extract it so the descriptor is located at:

```text
<YourProject>/Plugins/ReadAllandExplains/ReadAllandExplains.uplugin
```

4. Open the project with Unreal Engine 5.7.
5. Enable `ReadAllandExplains` in `Edit > Plugins`, then restart the editor if requested.
6. The ZIP already includes the Win64 editor binary, so a local compile should normally not be required.

## Verify the installation

1. Select a supported asset in Content Browser.
2. Run the AI-readable export or generate an AI Context Pack with project dependencies.
3. Confirm that `<YourProject>/Saved/ReadAllandExplainsExports` exists.
4. A published Pack has `state: complete`. A `.tmp` directory is diagnostic data, not a usable Pack.

## Install the CodeBuddy integration

Run in CodeBuddy:

```text
/plugin marketplace add 2026511939ljc-star/ReadAndExplain
/plugin install readallandexplains@readallandexplains-marketplace
/reload-plugins
/readallandexplains:readallandexplains-doctor
```

For local testing, run from the extracted plugin directory:

```powershell
codebuddy plugin validate .
codebuddy --plugin-dir .
```

If auto-discovery cannot find the exports, set:

```powershell
$env:READALL_EXPORT_ROOT = "D:/YourProject/Saved/ReadAllandExplainsExports"
```

## Upgrade from 4.6

Close UE and CodeBuddy, back up and remove the old plugin directory, extract 4.8.0-preview.2 to the same location, restart UE, reload CodeBuddy plugins, run diagnostics, and generate a new Pack. Existing 4.6 Packs remain readable, but regenerating important snapshots is recommended.

## Uninstall

Close UE, delete `<YourProject>/Plugins/ReadAllandExplains`, and uninstall the CodeBuddy plugin if installed. Exported snapshots under `Saved/ReadAllandExplainsExports` can be retained or deleted independently; no `.uasset` is modified.