# ReadAllandExplains 4.8.0-preview.3

Adds a native graph index to the export and makes Niagara Custom HLSL code a
first-class fact. Both were verified on a real pack exported by this build, not
only by unit tests.

## Native graph index

The exporter now writes `graphIndex` as a Raw Fact instead of leaving every
consumer to rebuild it.

- Root level: `indexVersion`, `source: native`, asset-level `graphCount`,
  `nodeCount`, `pinCount`, `linkCount`, and a locator table with a `jsonPointer`
  per graph.
- Per graph: `nodeCount`, `pinCount`, `linkCount`, `entryPoints` and
  `searchIndex`, each entry carrying a `jsonPointer` so an answer can cite the
  exact evidence location.

Entry points are decided structurally as nodes with no incoming link, so root and
output nodes surface without a class-name allow list.

Arrays preserve the original graph order and are never sorted. This is what keeps
a natively indexed pack and a derived index enumerating targets identically, so
reading an old pack and a new pack cannot silently disagree.

## MCP consumes the index, but only when it is trustworthy

`graph_index_source` now reports `native_metadata_graph_index` for a verified
native pack and `GRAPH_INDEX_DERIVED` is no longer raised for it.

Presence of the field is deliberately not enough. The service accepts the index
only when `source` is `native`, the locator count and per-graph ids line up, and
the counts equal the real arrays. Any mismatch is treated as no native index and
the service falls back to deriving one, because a stale index that disagrees with
the graphs it describes is worse than no index at all.

## Niagara Custom HLSL

A `NiagaraNodeCustomHlsl` previously exported only identity, position and pins,
so the authored shader logic was invisible even though it often carries the
decisive behaviour of a module. The material exporter already emitted Custom
HLSL; Niagara did not.

The body is now stored verbatim and never summarised, and it surfaces in three
places: a fenced block in Markdown, `sourceCode` with `sourceCodeLanguage` and
`sourceCodeCharacterCount` in JSON, and the asset search text so a module can be
found by a symbol remembered from the shader rather than by node title.

The JSON fields are emitted only for nodes that genuinely carry code, so their
presence is itself evidence.

## Verification

Exported `ContextPack_20260811_204550_A311309F` with this build.

- Native index present with `source: native`, 2 graphs, 6 nodes, 14 pins, 5
  links, and zero disagreement between the index and the graphs it describes.
- MCP reports `native_metadata_graph_index` and no longer raises
  `GRAPH_INDEX_DERIVED`, closing the loop from exporter to consumer.
- 2 Custom HLSL nodes, both carrying a body, 511 and 554 characters, with
  `sourceCodeCharacterCount` matching the actual string length exactly. The text
  survives intact including the `GPU_SIMULATION` guard, the `View.WorldToClip`
  transform and the `ScreenPositionScaleBias.wz` swizzle, so no truncation or
  escaping damage occurred.
- Native and derived indexes agree across every local pack: 1,536 assets with
  graphs, 0 order mismatches, 0 count mismatches.
- Contract 54 and Golden 22 pass.

## Compatibility

Purely additive. No existing field changed, so 4.7 and earlier packs stay
readable and older consumers ignore the new objects. A pack without a native
index keeps working through the derived path and still reports
`GRAPH_INDEX_DERIVED`, which was confirmed against the 4.7 acceptance pack.

## Notes

The installed UE 5.7 ships a precompiled `UE5Rules.dll` that predates the local
Houdini install, so any build walking the `UnrealEditor` target fails with a
misleading `HoudiniEngine` rules error. `Scripts/BuildPluginIsolated.ps1` parks
the Houdini plugins for the build and restores them in a `finally` block, and it
treats a missing DLL as failure because `BuildPlugin` can report success while
skipping compilation entirely.

`Scripts/VerifyNativePack.ps1` checks a freshly exported pack for both claims
above and recomputes counts from the graphs array rather than trusting the index.
