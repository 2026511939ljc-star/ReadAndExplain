# ReadAllandExplains 开发仓库 / Development Repository

[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.7-black?logo=unrealengine&logoColor=white)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/platform-Windows%2064--bit-blue)](https://www.microsoft.com/windows)
[![Version](https://img.shields.io/badge/release-4.8.0-brightgreen)](ReadAllandExplains/RELEASE_NOTES_4.8.0.md)
[![Python](https://img.shields.io/badge/Python-3.9%2B-yellow?logo=python&logoColor=white)](https://www.python.org/)

> ### ⚠️ 运行环境 / Requirements
>
> | 项目 / Item | 要求 / Requirement |
> |---|---|
> | **Unreal Engine** | **5.7 — 硬要求 / required** (`.uplugin` 声明 `EngineVersion 5.7.0`) |
> | 操作系统 / OS | Windows 64 位 / 64-bit |
> | 模块类型 / Module | `Editor` — 仅编辑器加载，不进运行时或打包游戏 / editor-only |
> | Python | 3.9+ — 仅 CodeBuddy / MCP 需要，只用标准库 / MCP only, stdlib only |
>
> 插件含 C++ 编辑器模块并针对 5.7 编译。**其他 5.x 版本需自行以对应引擎重新编译**，并重跑契约与 Golden 回归确认导出字段未漂移。纯 UE 侧功能（右键导出 Context Pack）不需要 Python。
>
> The plugin ships a C++ editor module built against 5.7. **Other 5.x versions require rebuilding against that engine** and re-running the contract and Golden suites to confirm the exported fields have not drifted. The UE-side feature alone needs no Python.

[中文](#中文) | [English](#english)

---

## 中文

本仓库是 ReadAllandExplains 的唯一开发源码仓库。当前开发线为 `iteration/vnext`，已发布正式版本为 `4.8.0`。外层目录名 `ReadAllandExplains_4.1.0_dev` 是历史工作目录名，不代表当前产品版本；版本只以插件清单为准。

### 权威来源

- 插件源码根：[ReadAllandExplains](ReadAllandExplains/)
- UE 插件版本：[ReadAllandExplains.uplugin](ReadAllandExplains/ReadAllandExplains.uplugin)
- CodeBuddy Skill 源：[SKILL.md](ReadAllandExplains/skills/readallandexplains/SKILL.md)
- MCP 源：[readallandexplains_mcp.py](ReadAllandExplains/Integrations/MCP/readallandexplains_mcp.py)
- 当前能力说明：[README.md](ReadAllandExplains/README.md)
- 当前版本说明：[RELEASE_NOTES_4.8.0.md](ReadAllandExplains/RELEASE_NOTES_4.8.0.md)
- 安装与升级：[INSTALLATION.md](ReadAllandExplains/docs/INSTALLATION.md)
- 资产与发布管理规则：[REPOSITORY_MANAGEMENT.md](ReadAllandExplains/docs/REPOSITORY_MANAGEMENT.md)

仓库外的 `.agent/skills/readallandexplains`、UE 项目 `Plugins/ReadAllandExplains`、备份目录和导出的 Context Pack 都是安装、部署、备份或数据副本，不是源码来源，不应反向覆盖仓库。

### 分支说明

默认分支是 `iteration/vnext`，4.8 及后续开发都在这条线上。`main` 指向 `v4.7.0`，作为 4.7 及之前版本的归档保留，不再继续开发。

新开分支必须从 `iteration/vnext` 拉取：

```powershell
git checkout -b <branch-name> iteration/vnext
```

两条线没有共同祖先，原因与约束记录在 [REPOSITORY_MANAGEMENT.md](ReadAllandExplains/docs/REPOSITORY_MANAGEMENT.md) 的「历史谱系债务」一节。契约测试会对新建的自立根分支报错。

### 日常流程

```powershell
python -m unittest discover -s ReadAllandExplains/Tests/MCP -p "test_*.py" -v
powershell -ExecutionPolicy Bypass -File ReadAllandExplains/Scripts/SyncWorkspaceSkill.ps1 -WorkspaceRoot <workspace-root>
codebuddy plugin validate ReadAllandExplains
```

提交前确认 `git status --short` 只包含预期源码、测试或文档。`Binaries`、`Intermediate`、`Saved`、缓存、压缩包和本地安装副本均不进入 Git。

---

## English

This is the only source repository for ReadAllandExplains. Development happens on `iteration/vnext`, and the current released version is `4.8.0`. The outer directory name `ReadAllandExplains_4.1.0_dev` is a historical working directory name and does not indicate the product version; the plugin descriptor is the only source of truth for that.

### Authoritative sources

- Plugin source root: [ReadAllandExplains](ReadAllandExplains/)
- UE plugin version: [ReadAllandExplains.uplugin](ReadAllandExplains/ReadAllandExplains.uplugin)
- CodeBuddy Skill source: [SKILL.md](ReadAllandExplains/skills/readallandexplains/SKILL.md)
- MCP source: [readallandexplains_mcp.py](ReadAllandExplains/Integrations/MCP/readallandexplains_mcp.py)
- Current capabilities: [README.md](ReadAllandExplains/README.md)
- Current release notes: [RELEASE_NOTES_4.8.0.md](ReadAllandExplains/RELEASE_NOTES_4.8.0.md)
- Install and upgrade: [INSTALLATION.md](ReadAllandExplains/docs/INSTALLATION.md)
- Asset and release rules: [REPOSITORY_MANAGEMENT.md](ReadAllandExplains/docs/REPOSITORY_MANAGEMENT.md)

Anything outside this repository — `.agent/skills/readallandexplains`, a UE project's `Plugins/ReadAllandExplains`, backup directories, exported Context Packs — is an installation, deployment, backup or data copy. None of them are a source of truth, and none should be copied back over the repository.

### Branches

The default branch is `iteration/vnext`, which carries 4.8 and all later work. `main` points at `v4.7.0` and is kept as an archive of 4.7 and earlier; it is no longer developed.

New branches must start from `iteration/vnext`:

```powershell
git checkout -b <branch-name> iteration/vnext
```

The two lines share no common ancestor. The reason and the constraints that follow from it are recorded under "历史谱系债务" in [REPOSITORY_MANAGEMENT.md](ReadAllandExplains/docs/REPOSITORY_MANAGEMENT.md). A contract test fails on any branch started from a fresh root commit.

### Everyday workflow

```powershell
python -m unittest discover -s ReadAllandExplains/Tests/MCP -p "test_*.py" -v
powershell -ExecutionPolicy Bypass -File ReadAllandExplains/Scripts/SyncWorkspaceSkill.ps1 -WorkspaceRoot <workspace-root>
codebuddy plugin validate ReadAllandExplains
```

Before committing, confirm that `git status --short` contains only intended source, tests or documentation. `Binaries`, `Intermediate`, `Saved`, caches, archives and local install copies never enter Git.
