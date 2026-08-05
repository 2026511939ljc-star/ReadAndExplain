# ReadAllandExplains 4.7.0

ReadAllandExplains 4.7.0 moves the project from a feature-complete prototype toward a release-grade UE asset context compiler. It adds Blueprint CDO values, UEnum and DataAsset export, readable Material Custom HLSL, deterministic Golden Pack regression, and strict Context Pack integrity rules.

ReadAllandExplains 4.7.0 将项目从功能型原型推进到可正式交付的 UE 资产上下文编译器：新增 Blueprint CDO 值、UEnum、DataAsset、Material Custom HLSL，并建立 Golden Pack 回归与严格的 Context Pack 完整性事务。

## Highlights / 核心提升

- Blueprint variables declared by the current generated class now export their Class Default Object values instead of only C++ type names.
- UEnum assets export entry display names and full 64-bit values.
- DataAsset assets have dedicated readable documents and structured Metadata.
- Root-reachable Material Custom nodes export readable pseudo HLSL with code, primary output type, and named inputs.
- Five real UE asset classes are covered by repeatable Golden Pack regression: Blueprint CDO, Material Custom HLSL, Niagara Renderer/Curve, UEnum, and DataAsset.

- Blueprint 当前生成类声明的变量现在导出 CDO 默认值，不再只显示 C++ 类型。
- UEnum 导出条目显示名和完整 64 位值。
- DataAsset 获得专用可读文档与结构化 Metadata。
- 普通 Material 中根属性可达的 Custom 节点可导出代码、主输出类型和命名输入的伪 HLSL。
- Blueprint CDO、Material Custom HLSL、Niagara Renderer/Curve、UEnum、DataAsset 五类真实资产纳入可重复 Golden Pack 回归。

## Context Pack Integrity / 数据完整性

- Assets with the same short name but different object paths no longer silently overwrite one another. Collision groups receive deterministic object-path-based suffixes.
- `exportFile` and `metadataFile` are mapped consistently in both Index and Manifest.
- Metadata write failure is now an asset export failure.
- A Pack is published as `complete` only when every attempted asset succeeds, failed/skipped counts are zero, README and both indexes are written, required asset documents and Metadata exist, the Manifest can enumerate every file, and the final directory publish succeeds.
- Failed Packs remain in `.tmp` with `state: failed` and diagnostic error codes when failure handling can run. Abrupt interruption may leave `writing` or `complete` inside a `.tmp` directory; MCP, Golden tests, and default Pack discovery reject every `.tmp` directory regardless of its state.
- SyncLive reports `complete` only after a Pack is formally published, archives requests only after the result JSON is persisted, and can recover a published Pack by `originRequestId` after an interrupted result write.

- 不同对象路径下的同名资产不再静默覆盖；碰撞组使用基于对象路径的稳定后缀消歧。
- Index 与 Manifest 同时一致记录 `exportFile` 和 `metadataFile`。
- Metadata 写入失败会使该资产导出失败。
- 只有全部资产成功、失败/跳过均为零、README 与双索引写入成功、正文和 Metadata 完整、Manifest 可枚举全部文件且最终目录发布成功时，Pack 才能成为 `complete`。
- 正常失败处理会在 `.tmp` 写入 `state: failed` 和诊断错误；突然中断可能留下 `writing` 或 `complete` 状态的 `.tmp`，但 MCP、Golden 与默认发现流程无条件拒绝所有 `.tmp`。
- SyncLive 仅在 Pack 正式发布后返回 `complete`，结果 JSON 落盘成功后才归档请求，并可按 `originRequestId` 恢复已发布但结果写入中断的 Pack。

## Golden Pack Quality Gate / Golden 回归门禁

- Deterministic normalization and semantic comparison for Pack, Index, readable documents, and Metadata.
- Managed baselines, explicit update controls, machine-readable reports, and provenance containing project, UE version, Git HEAD/dirty state, plugin descriptor hash, and loaded DLL hash.
- Windows GitHub Actions quality gate runs Golden and MCP test suites.
- Validation rejects temporary or partial Packs, duplicate case-insensitive paths, missing required files, duplicate asset mappings, and Metadata identity mismatches.

- 对 Pack、Index、可读文档和 Metadata 提供确定性规范化与语义比较。
- 受管 Baseline、显式更新许可、机器可读报告，以及项目、UE 版本、Git、描述符和实际 DLL 哈希来源追踪。
- Windows GitHub Actions 自动执行 Golden 与 MCP 测试。
- 验证器拒绝临时/部分 Pack、大小写等价重复路径、必要文件缺失、资产映射重复和 Metadata 身份串线。

## Compatibility / 兼容性

- Target: Unreal Engine 5.7, Windows 64-bit, Editor plugin.
- Existing 4.6 Context Packs remain readable through legacy Metadata object-path fallback.
- Legacy console command `GetTheMeaning.ExportAssets` remains available.
- CodeBuddy integration still uses the bundled standard-library-only Python MCP; npm is not required.

- 目标平台：Unreal Engine 5.7、Windows 64 位、Editor 插件。
- 通过旧 Metadata objectPath 回退继续读取 4.6 Context Pack。
- 保留旧控制台命令 `GetTheMeaning.ExportAssets`。
- CodeBuddy 继续使用随插件提供、仅依赖 Python 标准库的 MCP；无需 npm。

## Verification / 验证

- Golden unit tests: **22/22 passed**.
- MCP contract tests: **32/32 passed**.
- UE 5.7 Win64 plugin compilation and DLL link: passed.
- Real UE regression: five asset classes, two runs each, **10/10 passed**.
- No unexpected changes in asset readable documents or Metadata during the integrity-contract migration.

Final release artifacts are rebuilt after version stamping. The GitHub Release records the ZIP SHA-256 and final verification provenance.

## Installation / 安装

1. Close Unreal Editor.
2. Download `ReadAllandExplains_4.7.0_UE5.7_Win64.zip`.
3. Extract to `<YourProject>/Plugins/ReadAllandExplains`.
4. Open the project in UE 5.7, enable the plugin, and restart if requested.
5. For CodeBuddy, add the repository marketplace and install `readallandexplains@readallandexplains-marketplace`.

完整安装、升级、验证、卸载和故障排查见 `docs/INSTALLATION.md`。

## Known Limitations / 已知限制

- Only UE 5.7 / Win64 is packaged and verified in this release.
- Blueprint export covers Class Default Object values for variables declared by the current generated class; placed Actor/component instance overrides are not exported.
- Custom HLSL code is available in readable Material Markdown, while `.meta.json` retains generic node/Pin/Link topology; Define, Include, additional-output semantics, and Material Function Custom HLSL are not yet structured.
- SyncLive supplemental Packs remain independent snapshots. Base + Delta overlay queries are planned for a later release.
- Coverage dependencies originating as package paths may require a canonical `/Game/Package.Asset` object path for targeted snapshot submission.
- Runtime observation, AnimBP state machines, reverse import, and Marketplace publication are not part of 4.7.0.

## Upgrade / 升级

Upgrade from 4.6 by replacing the complete plugin directory while UE is closed, then reload CodeBuddy plugins. Do not overlay individual files onto the old directory. Existing exported Packs may be retained; regenerate important snapshots to receive the stricter 4.7 integrity contract.

从 4.6 升级时请在 UE 关闭状态下完整替换插件目录，再重载 CodeBuddy 插件；不要只覆盖部分文件。旧 Pack 可以保留，但建议重新生成关键快照以获得 4.7 的严格完整性契约。