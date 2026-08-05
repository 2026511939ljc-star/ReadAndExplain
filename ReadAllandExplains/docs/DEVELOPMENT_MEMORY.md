# ReadAllandExplains Development Memory

> 本文件是仓库内的开发记忆与交接基线。代码、Git 与实际验证结果优先；每次重要里程碑后更新本文件，避免依赖聊天上下文。

## 产品定位

ReadAllandExplains 是 UE 技术美术资产的 AI Context Compiler。核心流程是：

1. 快照：从 UE 资产生成稳定、可审计的 Markdown 与 JSON。
2. 还原：保留 Blueprint、Material、Niagara 等资产的结构与证据。
3. 理解：通过只读 MCP 与 Skill 渐进读取 Context Pack。
4. 建议：基于真实证据给出可执行的美术与技术建议。

当前形态为 UE 编辑器插件、配套 Skill、只读 MCP、CodeBuddy 原生接入和经用户授权的 SyncLive Lite 定向补拍。

## 当前基线

- 开发分支：`iteration/vnext`
- 当前检查点提交：`73825d9`
- 远端回滚标签：`backup/task4-checkpoint-20260805-73825d9`
- 插件版本元数据：`4.7.0`（发布候选）
- 目标版本：`4.7.0`
- UE 目标环境：UE `5.7`，Win64
- 发布分支：`main`，不得在开发未验收时直接更新

需要回滚时，优先从标签 `backup/task4-checkpoint-20260805-73825d9` 创建恢复分支，不重写已发布历史。

## 已完成事项

Task 2、Task 3 与 Task 4 已进入检查点：

- Blueprint CDO 参数值导出。
- UEnum 与 DataAsset 专用导出。
- Material Custom HLSL 导出。
- Golden Pack 规范化、Baseline、语义比较与报告。
- 真实 UE 自动导出 Runner 与运行来源追踪。
- GitHub CI 中的 Golden 与 MCP 质量门禁。
- UE 5.7 编译验证通过。
- 五类真实资产连续两次回归，共 `10/10 PASS`。
- Golden 单元测试 `21/21 PASS`，MCP 契约测试 `31/31 PASS`。

Task 4 的核心是测试基础设施，不是 MCP 或 Skill 功能扩展。

## 4.7 开发范围

正式发布 4.7 前必须完成以下 P0：

1. 同名资产防覆盖：不同包路径中的同名资产必须写入不同、稳定且可追溯的文件路径；Markdown、Metadata、Index 与 Manifest 必须引用一致。
2. 严格完整发布：只有全部待导出资产成功、`failedCount=0`、`skippedCount=0`，且 README、Index、Metadata、Manifest 等必要文件均写入成功时，Context Pack 才能标记 `complete` 并发布。
3. 失败可诊断：失败 Pack 保留在 `.tmp` 目录并写入 `state=failed` Manifest，不能被 MCP、Skill 或 Golden Runner 当作正式 Pack。
4. SyncLive 一致性：SyncLive 结果只有在 Pack 已正式发布且严格完整时才能返回 `complete`。

本轮暂不扩展 Base Pack 与 Delta Pack 联合查询。Coverage 包路径到对象路径的自动规范化可在 P0 完成后单独评估，避免扩大 4.7 发布面。

## 数据正确性原则

- 不允许同名文件静默覆盖。
- Metadata 写入失败必须视为资产导出失败，不能只记录 Warning。
- Index Markdown 与 Index JSON 写入失败必须阻断完整发布。
- Manifest 文件清单中的路径、大小与指纹必须来自已成功落盘的文件。
- `complete` 是可供下游信任的事务状态，不是“至少成功一个资产”。
- 失败后允许修复根因并重新快照；不能通过反复快照掩盖确定性的导出错误。

## 验证门禁

每次准备发布候选版本时必须依次完成：

1. `git diff --check` 与工作树范围审查。
2. Golden Python 单元测试全通过。
3. MCP 契约测试全通过。
4. UE 5.7 隔离 CompileHost 编译通过。
5. 部署与真实五类资产连续两次回归，结果 `10/10 PASS`。
6. 核对实际加载 DLL、插件描述符、Git HEAD 与测试报告来源一致。
7. 更新版本号、Release Notes、安装包并检查 ZIP 不含 PDB、Intermediate、私有 Pack 或本机路径。
8. 获得用户发布许可后，才更新 `main`、正式标签与 GitHub Release。

## 操作约束

- 修改或部署 Trans 工程中的插件前必须关闭 UE，避免 DLL 锁定。
- 构建优先使用已验证的隔离 CompileHost，避免默认引擎插件污染。
- 不提交真实用户资产、Golden `.baselines`、测试报告、构建缓存或凭据。
- 日志外发前删除认证参数、用户名、用户 ID 和本机敏感路径。
- `D:/UE5/Trans/Saved/ReadAllandExplainsDeployBackups/ReadAllandExplains_4.6.0_before_4.7_p0_20260805_191113` 是空目录，不是有效二进制备份；源码回滚使用标签 `backup/task4-checkpoint-20260805-73825d9` 后重新构建。
- 开发提交保持单一目的；重大阶段先创建可回滚检查点。

## 当前执行状态

`2026-08-05`：用户已授权开始 4.7 开发，开发基于检查点 `73825d9`，当前代码尚未提交或发布。

已完成：

- 同名资产防覆盖：无碰撞资产保持旧文件名；同目录碰撞组使用完整对象路径的稳定哈希消歧，Markdown 与 Metadata 一一对应。
- Index 与 Manifest 同时记录 `exportFile` 和 `metadataFile`；保留旧二参数 C++ API 与旧 Schema 1 Pack 的 Metadata `objectPath` 回退兼容。
- 严格完整发布：资产、Metadata、README、`index.md`、`index.json`、Manifest 和原子目录发布全部成功后才设置 `bPublished=true`。
- 失败 Pack 保留在 `.tmp` 并写 `state=failed` 诊断 Manifest；MCP、Golden 和默认解析均拒绝 `.tmp`。
- SyncLive 只依据 `bPublished` 返回 `complete`，且结果 JSON 写入成功后才归档请求。
- MCP 与 Golden 增加文件集合、大小写无关路径唯一性、Metadata 身份、链接/reparse point 和旧 Pack 兼容校验。

当前验证：

- Golden 自动测试：`22/22 PASS`。
- MCP 契约测试：`32/32 PASS`。
- UE 5.7 隔离 CompileHost：最终 4.7 候选编译与 DLL 链接成功。
- 历史真实 Blueprint Context Pack 与旧 Golden baseline：`172/172` 文件一致，新增、修改、缺失均为 `0`。
- 新 DLL 已部署到 Trans；实际加载模块 SHA-256 为 `B7D94C6BA62CDB3BFD393037C51799C601D30F3D1569A70773AA8DF645C7D264`。
- 五类真实资产各连续导出两次，共 10 份正式完整 Pack；资产正文和 Metadata 均无非预期变化。
- 受管 Golden baseline 已批准新增 `attemptedAssetCount`、`metadataFile` 与 Index Metadata 列；10 份报告重新比较均为 `10/10 PASS`、`exitCode=0`、`failed=0`。
- `git diff --check`：通过。

当前代码、开发记忆和本地受管 baseline 尚未提交；正式版本号、Release Notes、安装包、`main`、4.7 标签与 GitHub Release 仍需用户单独许可。
