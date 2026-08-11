# ReadAllandExplains 4.8.0-preview.1

ReadAllandExplains 4.8.0-preview.1 turns the Context Pack from a document you must read whole into an evidence base you can query progressively. 4.7 established whether the facts were authoritative, complete and publishable. 4.8 makes those facts answerable without spending the entire budget on a single read.

ReadAllandExplains 4.8.0-preview.1 将 Context Pack 从"必须整份读完的文档"变为"可按需查询的证据库"。4.7 解决的是事实是否权威、是否完整、是否可发布；4.8 让这些事实可以在不耗尽预算的前提下被回答。

This is a **preview release**. All new capability lives in the MCP layer. The UE editor plugin, the C++ code and the export structure are unchanged from 4.7.0, so existing 4.7 Context Packs work without re-export.

这是**预览版**。全部新能力位于 MCP 层，UE 编辑器插件、C++ 代码与导出结构与 4.7.0 完全一致，因此现有 4.7 Context Pack 无需重新导出即可使用。

---

## Why this release exists

A Niagara waterfall system with 48 nodes and 49 links is about 101,734 characters as a single graph read. That exceeds the 60,000 character budget and returns `RESULT_TOO_LARGE`. Reading it in mechanical pages is no better, because arbitrary page boundaries cut the computation closure apart: you get half a parameter chain and no way to tell what was severed.

4.8 replaces "read the whole file, then understand it" with "see what exists, locate the target, take the minimum closure, explain it with evidence".

```text
4.7  whole-graph read     48 nodes / 49 links / ~101,734 chars -> RESULT_TOO_LARGE
4.8  located slice        upstream 2 hops / 6 nodes / 5 links / 6,016 chars -> complete, pin-level evidence
```

一个 48 节点、49 连线的 Niagara 瀑布系统，整图读取约 101,734 字符，超出 60,000 字符预算并返回 `RESULT_TOO_LARGE`；机械分页同样不可行，因为任意页边界会切断计算闭包。4.8 用"先看有什么、定位目标、只取最小闭包、带证据解释"取代"读完整文件再理解"。

---

## New in this release

### Five progressive query tools

| Tool | Purpose |
|---|---|
| `get_asset_outline` | Per-graph node, pin and link counts plus stable Readable section ids and character sizes, so you can know how large something is before deciding not to read it |
| `locate_graph_target` | Deterministic Graph, Node and Pin location with exact-id, exact-text, prefix and substring ranking; also enumerates targets when no query is given |
| `get_graph_subgraph` | Directional BFS slice around one exact target with explicit hop, node and character budgets |
| `get_readable_sections` | Stable section ids with exact line and Unicode character ranges |
| `get_readable_section` | Reads one section by id with character-accurate pagination |

The Readable index converges 34 raw headings into 7 stable sections. On the hero asset the `technical` section alone is 88,601 characters. Knowing its size first, and then deciding not to read it, is the point.

Readable 索引将 34 个原始标题收敛为 7 个稳定章节；英雄资产的 `technical` 单章即 88,601 字符。**先知道它有多大，再决定不读它**，这正是稳定索引的价值。

### Discovery without reading the whole graph

Omitting `query` on `locate_graph_target` enumerates targets in Graph IR order and returns `resolution=listed` with `list_mode=true` and `match_type=listed`. Add `kind_filter` to select by exact `className` or `kind`. This closes the cold-start case where you do not yet know what a node is called.

A `not_found` result is not a dead end. It returns up to 10 `available_samples`, a `discovery_hint` and a `TARGET_NOT_FOUND_SAMPLES_PROVIDED` warning, while stating explicitly that **a miss does not prove the node, Graph or asset is absent**.

省略 `query` 即按图 IR 原始顺序枚举目标，返回 `resolution=listed`；`kind_filter` 支持按精确 `className` 或 `kind` 筛选。这解决了"尚不知道节点叫什么"的冷启动问题。`not_found` 不再是死胡同，会返回最多 10 条 `available_samples` 与引导提示，同时明确声明**未找到不证明该节点、Graph 或资产不存在**。

### Honest budget diagnostics

Every slice reports whether it was complete. On truncation it returns `truncated=true`, the reason (`max_hops`, `max_nodes` or `max_characters`), the exact boundary links with their `json_pointer`, and `omitted_node_count`. Verified across both truncation reasons:

| Asset type | Discovery path | Slice | Truncation reason | Boundary |
|---|---|---|---|---|
| Niagara emitter graph, 48 nodes | known name, direct locate | 6 nodes / 5 links / 6,016 chars | `max_nodes` at limit 1 | `/graphs/1/links/45`, `omitted_node_count=5` |
| Material graph, 10 nodes | unknown name, enumerate first | 4 nodes / 3 links / 5,241 chars | `max_hops` at limit 2 | `/graphs/0/links/0` and `/links/5`, `omitted_node_count=2` |

Two asset types, two discovery paths, two truncation reasons, identical evidence and `json_pointer` back-link formats.

### Writes are bound to an explicit baseline

`request_targeted_snapshot` now **requires** `pack_path`. The service will no longer implicitly select a complete Pack, which removes a real class of error: requesting new evidence against a Pack the evidence never came from. On a fingerprint mismatch the error returns `expected`, `received`, `pack_id`, `available_complete_packs` and an actionable hint.

A successful re-snapshot produces a new Pack carrying `originRequestId`, `basePackId` and `basePackFingerprint` together, so new evidence is always traceable to which request, against which baseline, and when.

`request_targeted_snapshot` 的 `pack_path` 改为**必填**，服务不再隐式选择 complete Pack，消除了"针对并非证据来源的 Pack 请求补拍"这一类真实错误。指纹不匹配时返回 `expected`、`received`、`pack_id`、`available_complete_packs` 与操作提示。补拍成功产出的新 Pack 同时携带 `originRequestId`、`basePackId` 与 `basePackFingerprint`，因此新证据始终可追溯到哪个请求、基于哪个基线、何时产生。

---

## Verification

| Gate | Result |
|---|---|
| MCP contract tests | 51 passed |
| Golden Pack regression | 22 passed |
| Dual-client acceptance | Trans and CodeBuddy, field-for-field identical across 12 checks |
| Read-only compatibility scan | 86 Packs, 1,902 assets, 1,902 outlines succeeded, 0 failed |
| Hero paths | Niagara emitter graph and Material graph, both end to end |
| SyncLive re-snapshot loop | Verified including fingerprint-mismatch rejection |

Two unrelated clients ran the same question set independently against the same immutable Pack and agreed field for field on the Pack fingerprint, all four `graph_id` values, seven `section_id` values, four location candidates, the unique `node_id`, the 6/5/6016 slice, every `pin_id`, the truncated 1/0/1954 state, the boundary link `/graphs/1/links/45` and `omitted_node_count=5`.

That is the core claim of this release: **what 4.8 returns is a reproducible deterministic fact, not an approximation the model improvises each time. Determinism can be re-checked by a third party; explanation cannot.**

两个互不相关的客户端在同一不可变 Pack 上独立执行同一套问题，12 项逐字段一致。这是本版本的核心论据：**4.8 返回的是可复现的确定性事实，而不是模型每次即兴生成的近似解释。确定性可以被第三方复核，解释不能。**

---

## Compatibility

Existing 4.7 Context Packs are fully supported. Because indexes are derived deterministically in MCP memory rather than read from native export fields, queries against them return `GRAPH_INDEX_DERIVED` and `READABLE_INDEX_DERIVED`. These are **provenance hints, not degradation**, and must not be presented as native UE fields.

Known legacy boundaries are reported rather than silently ignored: legacy materials with duplicate node IDs stop precise traversal with `GRAPH_DATA_INVALID`; stable pin identity requires `nodeId + pinId` together. The compatibility scan found 559 assets flagged `GRAPH_IDENTITY_DEGRADED` and 3 flagged `GRAPH_LINKS_INCOMPLETE` out of 1,902.

现有 4.7 Context Pack 完全受支持。由于索引在 MCP 内存中确定性派生而非取自原生导出字段，查询会返回 `GRAPH_INDEX_DERIVED` 与 `READABLE_INDEX_DERIVED`——它们是**来源提示而非降级**，不得作为 UE 原生字段呈现。

---

## Not in this preview

Stated explicitly so nothing here is over-claimed:

- **Native `graphIndex`.** Indexes are still derived in MCP memory. Moving them into the plugin export side requires a C++ change, a UE 5.7 Win64 build and a full Pack regression, none of which are claimed here.
- **Material Function as an independent path.** The Material graph is verified; material functions reuse the same pipeline but were not tested separately.
- **Larger re-snapshot scopes.** Only a single asset at `dependency_depth=0` was verified.
- **Runtime facts.** A static snapshot cannot prove PIE timing, per-frame GPU results, dynamic render target contents or final parameter values. These must be marked unverifiable rather than inferred.

本预览版未包含：原生 `graphIndex`（仍为 MCP 内存派生，前移需 C++ 改动与 UE 5.7 编译）、材质函数独立链路、更大范围补拍（仅验证单资产 `dependency_depth=0`）、以及任何运行时事实（静态快照无法证明 PIE 时序、逐帧 GPU 结果与参数最终值）。

---

## Install

1. Requires Unreal Engine 5.7 on Windows 64-bit, and `python` 3.9 or newer callable from `PATH`. The MCP server uses only the Python standard library.
2. Download `ReadAllandExplains_4.8.0-preview.1_UE5.7_Win64.zip`.
3. Extract into your project's `Plugins/ReadAllandExplains` directory.
4. Restart the UE editor and reload CodeBuddy plugins.
5. See `docs/INSTALLATION.md` for full steps, and `docs/V4.8_ENVIRONMENT_SETUP.md` for MCP wiring, the unfamiliar-asset discovery flow and troubleshooting.

Upgrading from 4.7.0 requires no re-export. Existing Packs remain valid and continue to be read as authoritative Raw Facts.

从 4.7.0 升级无需重新导出资产，现有 Pack 仍然有效并继续作为权威 Raw Facts 读取。

---

## Design boundary

Three layers, and the boundary between them is not negotiable:

```text
Raw Facts         exported by UE, never rewritten by AI
Derived Views     deterministically derived by program, reproducible, never written back
Semantic Overlay  AI interpretation, may be unknown, never written into facts
```

**AI may explain UE. AI may not rewrite UE facts.**

三层边界不可协商：Raw Facts 由 UE 导出，AI 不可改写；Derived Views 由程序确定性派生，可重建且不反向覆盖；Semantic Overlay 是 AI 解释，允许 unknown，不写回事实。**AI 可以解释 UE，但不能篡改 UE 事实。**
