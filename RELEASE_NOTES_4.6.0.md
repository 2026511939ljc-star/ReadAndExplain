# ReadAllandExplains 4.6.0

The official release of ReadAllandExplains 4.6.0 consolidates three preview iterations (preview.1–3) plus CodeBuddy native integration, UTF-8 stdio fix, and repository management documentation into a single stable baseline.

ReadAllandExplains 4.6.0 正式版汇总三个 preview 迭代（preview.1–3）以及 CodeBuddy 原生接入、UTF-8 stdio 修复和仓库管理文档，形成单一稳定基线。

---

## Trustworthy Query Layer / 可信查询层 (preview.1)

1. All 5 MCP tools return a unified `rae.mcp/1.0` Envelope: `pack`, `asset`, `data`, `evidence`, `page`, `warnings`, `missing_fields`, and `error`.
2. Stable machine-readable error codes: `PACK_NOT_FOUND`, `PACK_INCOMPLETE`, `SCHEMA_UNSUPPORTED`, `ASSET_NOT_FOUND`, `INVALID_SECTION`, `RESULT_TOO_LARGE`, `TEXT_ENCODING_INVALID`.
3. Tool annotations declare `outputSchema`, `readOnlyHint`, `destructiveHint=false`, `idempotentHint=true`, `openWorldHint=false`; responses include both text and `structuredContent`.
4. Machine-readable pagination: `offset`, `limit`, `next_cursor`, `total`, `truncated`.
5. Key results carry Evidence: source file, JSON Pointer, asset path, target ID, or line number.

## Atomic Context Packs / 原子快照 (preview.1)

1. Context Pack writes to `.tmp` directory; Manifest marks `writing` state.
2. After all files complete, writes `complete` manifest, then atomically renames to publish.
3. Manifest includes `packId`, `state`, pack fingerprint, and per-file pack-relative paths, sizes, and BLAKE3-160 content fingerprints.
4. MCP validates new Manifest file list for directory boundary, existence, and size; incomplete packs return `PACK_INCOMPLETE`.

## Progressive Analysis / 渐进分析 (preview.2)

1. Skill breaks large questions into up to 3 subtasks, prioritizing Material and Niagara; Blueprint tracks only visual parameter writes and trigger entries.
2. Default drill-down budget: max 5 assets, depth 1; exceeding budget aggregates gaps instead of recursively fetching.
3. `get_asset_detail` adds `coverage` view: in-pack dependencies, missing `/Game/` dependencies, external dependencies, and targeted supplement suggestions.
4. Gap classification: "blocking conclusion", "improves confidence", "runtime verification"; completes unaffected analysis first, then generates a single supplement plan.
5. Supplement plan specifies assets/fields, purpose, expected answer, priority, count, depth, read-only scope, and Pack fingerprint; no background snapshot without user permission.
6. Strict UTF-8/UTF-8 BOM text reading; invalid encoding returns `TEXT_ENCODING_INVALID` instead of masking with replacement characters.

## SyncLive Lite / 定向补拍 (preview.3)

1. MCP adds `request_targeted_snapshot` and `get_snapshot_request_status`.
2. Requests require user permission, 1–5 `/Game/` assets, dependency depth 0 or 1, full baseline Pack ID, and exact fingerprint.
3. Atomic file writes: `Pending/*.json.tmp` → `Pending/*.json`, preventing UE from reading half-written requests.
4. UE editor processes max 1 request per poll cycle with `pending → processing → complete/failed/rejected` state machine.
5. UE consumer independently validates request ID, mode, permission, asset scope, dependency depth, supported types, and baseline Pack fingerprint.
6. New Pack Manifest records `originRequestId` and `basePackId` for Skill verification and resumption.
7. Paused during PIE; interrupted requests enter auditable failed state; completed results are not overwritten by recovery logic.
8. SyncLive Lite only generates static Context Packs; does not execute arbitrary commands, modify assets, save levels, or control runtime.

## CodeBuddy Native Integration / CodeBuddy 原生接入

1. Plugin includes 1 Command, 1 Skill, and 1 read-only MCP Server in `.codebuddy-plugin/plugin.json` and marketplace manifest.
2. MCP config uses relative paths only; compatible with MCP `2025-11-25` protocol version.
3. Auto-discovers current or nested UE project `Saved/ReadAllandExplainsExports` via `CODEBUDDY_PROJECT_DIR`.
4. Diagnostics command: `/readallandexplains:readallandexplains-doctor`.
5. Workspace Skill sync: `Scripts/SyncWorkspaceSkill.ps1` with SHA256 verification.

## UTF-8 Stdio Fix / Windows 编码修复

1. MCP protocol stdout explicitly emits UTF-8 bytes on Windows.
2. Batch launcher sets `PYTHONUTF8=1` and `PYTHONIOENCODING=utf-8`.
3. Regression test for `cp936` subprocess added to prevent Chinese encoding corruption.

## Repository Management / 仓库管理

1. Single editable Skill source in repository `skills/`; workspace `.agent` and UE plugin directories are installation/deployment copies only.
2. Repository asset, version, branch, commit, and release management documentation added.
3. Contract tests include UE, CodeBuddy, and Marketplace version consistency checks.

## Verification / 验证

- C++ product code compiled and DLL linked successfully on Unreal Engine 5.7 / Win64 Development.
- MCP, SyncLive Lite, Skill, version, and management contract tests: **28/28 passed**.
- CodeBuddy plugin manifest, Skill frontmatter, Python syntax, PowerShell syntax, and version consistency validated.
- Real `BP_FluxAllOne` Context Pack: dependency total 28, in-pack 17, missing project dependencies 9, external 2, 0 replaced characters.
- Real Niagara `FX_Syst_Waterfall_FXTex` Context Pack: 21 curve instances deduplicated to 3 unique curves across 12 sources.
- Deployed to Trans project; all key source files, MCP, Skill, and DLL SHA256 match commit source.

```powershell
python -m unittest discover -s Tests/MCP -p "test_*.py" -v
```

## Upgrade Path / 升级路径

From 4.5.0-preview.1: replace plugin directory, restart UE editor. Existing Context Packs remain compatible. CodeBuddy users should re-sync the Skill via `Scripts/SyncWorkspaceSkill.ps1`.

## Rollback / 回滚

```powershell
git switch iteration/vnext
git reset --hard backup/pre-synclive-lite-20260804-facb406
```

Offline bundle: `C:/Users/albertinsli/CodeBuddy/FluidFlux_Fork/work/deploy_backups/ReadAllandExplains_pre_synclive_lite_20260804_facb406.bundle`

## What's Next / 后续方向

- Stable node IDs and precise Evidence for cross-version tracking.
- Golden Pack regression suite for quality gating.
- Graph Query for cross-asset static assignment/reference chains.
- Semantic Diff → TA Audit.
- AnimBP state machine support.
- Material Custom HLSL export.
- Blueprint parameter values + UEnum + DataAsset export.
- Live Sync, reverse import, and public Marketplace release.
