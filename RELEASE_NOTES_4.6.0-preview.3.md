# ReadAllandExplains 4.6.0-preview.3

本版本完成 SyncLive Lite：在用户批准具体补充计划后，CodeBuddy 可以让正在运行的 UE 编辑器生成受限的定向静态 Context Pack，再自动续接分析。

## SyncLive Lite

1. MCP 新增 `request_targeted_snapshot` 与 `get_snapshot_request_status`。
2. 请求必须携带用户许可、1 至 5 个 `/Game/` 资产、依赖深度 0 或 1、完整基线 Pack ID 和精确指纹。
3. MCP 使用 `Pending/*.json.tmp` 原子替换为 `Pending/*.json`，避免 UE 读取半写请求。
4. UE 编辑器每个轮询周期最多处理一个请求，并使用 `pending → processing → complete/failed/rejected` 状态机。
5. UE 消费端独立复验请求 ID、模式、许可、资产范围、依赖深度、支持类型和基线 Pack 指纹；不能用手工请求绕过 MCP 限制。
6. 新 Pack Manifest 写入 `originRequestId` 和 `basePackId`，便于 Skill 验证和从中断问题续答。
7. PIE 期间暂停处理；编辑器中断后的遗留请求会进入可审计失败状态，若完成结果已写出则不会被恢复逻辑覆盖。
8. SyncLive Lite 只生成静态 Context Pack，不执行任意命令、不修改资产、不保存关卡、不控制运行时。

## Skill

1. 历史笼统许可不能用于新的未展示范围。
2. 默认补拍深度为 0，只有补充计划明确包含直接依赖时才使用 1。
3. `pending`、`processing`、`complete`、`failed`、`rejected` 分别处理；失败不自动扩大范围或无限重试。
4. 完成后核对 `state`、`originRequestId`、`basePackId`、根资产和新指纹，再重新读取 coverage 并续答。

## 仓库管理

1. 明确仓库内 Skill 为唯一可编辑源，工作区 `.agent` 与 UE 项目插件目录均为安装或部署副本。
2. 新增仓库资产、版本、分支、提交和发布管理文档，以及带 SHA256 校验的 Skill 单向同步脚本。
3. 契约测试新增 UE、CodeBuddy 与 Marketplace 三处产品版本一致性检查，并验证当前发布说明与打包清单。
4. 构建产物、缓存、Context Pack、备份和本地部署副本继续保留在 Git 之外。

## 回滚

开发前提交：`facb406754496e4bceb956f0d3412feeaade3964`

远程回滚标签：

```powershell
git switch iteration/vnext
git reset --hard backup/pre-synclive-lite-20260804-facb406
```

离线完整 Bundle：

```text
C:/Users/albertinsli/CodeBuddy/FluidFlux_Fork/work/deploy_backups/ReadAllandExplains_pre_synclive_lite_20260804_facb406.bundle
```

如当前改动需要保留，先创建新分支或提交，再执行硬回滚。

## 验证

- MCP、SyncLive Lite、Skill、版本及管理契约测试 `28/28` 通过。
- CodeBuddy 插件清单、Skill Frontmatter、Python 语法、PowerShell 语法和版本一致性校验通过。
- 产品 C++ 代码已在 Unreal Engine 5.7 / Win64 Development 完成 UHT、全部编译、静态库与 DLL 链接；本轮纯管理改动未重新编译 UE。
- 本机 UE 5.7 安装中两份旧 HoudiniEngine 源码副本会污染默认 BuildPlugin 规则扫描；使用禁用默认引擎插件的隔离 HostProject 完成了本插件验证，未修改引擎安装。

```powershell
python -m unittest discover -s Tests/MCP -p "test_*.py" -v
```
