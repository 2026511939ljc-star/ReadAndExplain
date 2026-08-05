# 仓库与资产管理

本文规定 ReadAllandExplains 的权威来源、生成物边界、版本规则和提交发布流程，解决源码、CodeBuddy 安装副本、UE 部署副本与 Context Pack 混在一起的问题。

## 目录职责

| 类别 | 权威位置 | 是否进入 Git | 说明 |
|---|---|---:|---|
| UE 插件源码 | `Source/`、`Config/`、`Resources/`、`ReadAllandExplains.uplugin` | 是 | 产品实现与插件元数据 |
| MCP 源码 | `Integrations/MCP/` | 是 | stdio 查询与 SyncLive Lite 请求通道 |
| Skill 源码 | `skills/readallandexplains/SKILL.md` | 是 | 唯一可编辑 Skill 源 |
| CodeBuddy 集成 | `.codebuddy-plugin/`、`.mcp.json`、`commands/`、`CODEBUDDY.md` | 是 | 插件发现、命令和启动配置 |
| 自动测试 | `Tests/` | 是 | MCP、Skill、版本、管理契约与 Golden 比较器 |
| Golden 运行入口 | `Scripts/RunGoldenPackRegression.ps1` | 是 | 从真实 UE 资产导出并调用 Golden 比较器 |
| 本地 Golden 数据 | `Tests/Golden/*.local.json`、`Tests/Golden/.baselines/`、`Tests/Golden/.reports/` | 否 | 含项目资产路径、规范化基线和报告，仅本机使用 |
| 产品文档 | `README.md`、`docs/`、当前及历史 `RELEASE_NOTES_*` | 是 | 当前说明、治理规则和历史记录 |
| UE 构建产物 | `Binaries/`、`Intermediate/` | 否 | 本机可重新生成，发布包另行构建 |
| Python/CodeBuddy 缓存 | `__pycache__/`、`.plugins-cache.json` | 否 | 本机状态，不具备可移植性 |
| 导出数据 | UE 项目的 `Saved/ReadAllandExplainsExports/` | 否 | 用户资产快照，不属于插件源码 |
| 工作区 Skill 副本 | `<workspace>/.agent/skills/readallandexplains/` | 否 | 从仓库 Skill 安装生成，只用于当前工作区加载 |
| UE 部署副本 | `<UEProject>/Plugins/ReadAllandExplains/` | 否 | 从已验证源码复制，不能作为开发源 |
| 备份与 Bundle | `work/deploy_backups/` 等仓库外目录 | 否 | 回滚材料，不参与正常开发 |

## 单一权威源

1. 所有代码和 Skill 修改只在本仓库完成；外层工作目录名可能保留历史版本号，不作为产品版本依据。
2. 产品版本只以 `ReadAllandExplains.uplugin` 及下述一致性规则为准。
3. `skills/readallandexplains/SKILL.md` 是唯一 Skill 源；工作区 `.agent` 副本只能通过同步脚本更新。
4. UE 项目插件目录只接收已验证版本，不从部署目录回拷源码覆盖仓库。
5. Context Pack 是只读输入数据；不要放进插件仓库，也不要把它当成代码版本依据。
6. 历史发布说明保持不变；当前状态统一写在插件 `README.md` 和当前版本说明中。

## 版本规则

发布版本必须在以下三个文件中完全一致：

- `ReadAllandExplains.uplugin` 的 `VersionName`
- `.codebuddy-plugin/plugin.json` 的 `version`
- `.codebuddy-plugin/marketplace.json` 中插件的 `version`

`Version` 整数只递增，不复用。MCP 的 `SERVER_VERSION` 是协议实现版本，可以与产品版本不同。当前发布说明文件名必须与产品版本一致，并由 `Config/FilterPlugin.ini` 收入发布包。

## 分支与提交

- `main`：已发布、可回退的稳定版本。
- `iteration/vnext`：当前开发集成线。
- 功能或修复使用小而可验证的提交；提交标题使用 `Develop:`、`Fix:`、`Docs:`、`Test:` 或 `Release:` 前缀。
- 不使用构建产物来掩盖未提交源码；`git status --short` 必须能清楚说明每一项变化。
- 创建发布标签前，先确保工作树干净、测试通过、版本一致，并记录真实 UE 编译或导出验证结果。

## Skill 同步

仓库 Skill 更新并通过测试后，运行：

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/SyncWorkspaceSkill.ps1 -WorkspaceRoot <workspace-root>
```

脚本只从仓库复制到工作区 `.agent`，并校验 SHA256。同步后，新对话或重新加载插件才能可靠使用新版本；当前已启动会话可能继续持有旧内容。

## 提交检查

```powershell
python -m unittest discover -s Tests/MCP -p "test_*.py" -v
codebuddy plugin validate .
git diff --check
git status --short
git diff --stat
```

涉及 C++ 或导出结构时，还应完成 UE 5.7 Win64 Development 编译；涉及 Niagara、Material 或 Blueprint 数据结构时，应生成真实 Context Pack 做回归。只修改文档、Skill 或 Python 时，不伪称已完成 UE 编译。

## 发布检查

1. 更新三个产品版本字段并通过版本一致性测试。
2. 更新插件 `README.md`、当前 `RELEASE_NOTES_*` 和 `Config/FilterPlugin.ini`。
3. 运行 Python 契约测试和 CodeBuddy 插件校验。
4. 需要时完成 UE 编译、部署及真实资产导出验证。
5. 提交 `iteration/vnext`，推送远端并记录提交哈希。
6. 确认发布内容后再合并 `main`、创建版本标签和发布包。
7. 部署与备份保留在仓库外，并记录来源提交；不要将其混入 Git。
