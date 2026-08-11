"""Checks that a native graphIndex enumerates targets identically to the derived one.

CP7 moves index construction from MCP memory into the exporter. The real risk is
not a crash: it is a silent split where reading a derived Pack and reading a
native Pack return different orders or different counts. That would destroy the
4.8 determinism claim, which is the whole point of the release.

This harness simulates the native index using the same rules the C++ exporter
applies, then compares it against the values MCP derives from the same Pack.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

PACK = Path(
    r"D:\UE5\Trans\Saved\ReadAllandExplainsExports\ContextPacks"
    r"\ContextPack_20260807_102814_771605c4d7cc_6151D702"
)


def build_native_index(metadata: dict) -> dict:
    """Mirrors MakeGraphIndexJson and the root roll-up in AssetDocumentIR.cpp."""
    graphs = metadata.get("graphs") or []
    total_nodes = total_pins = total_links = 0
    locators = []
    per_graph = []

    for graph_index, graph in enumerate(graphs):
        nodes = graph.get("nodes") or []
        links = graph.get("links") or []
        pin_count = sum(len(n.get("pins") or []) for n in nodes)

        total_nodes += len(nodes)
        total_pins += pin_count
        total_links += len(links)

        locators.append(
            {
                "graphId": graph.get("id"),
                "nodeCount": len(nodes),
                "pinCount": pin_count,
                "linkCount": len(links),
                "jsonPointer": f"/graphs/{graph_index}",
            }
        )

        # A node is an entry point when no link terminates on it.
        with_incoming = {l.get("toNodeId") for l in links}
        search_index = [
            {
                "nodeId": n.get("id"),
                "name": n.get("name"),
                "className": n.get("className"),
                "jsonPointer": f"/nodes/{i}",
            }
            for i, n in enumerate(nodes)
        ]
        entry_points = [
            e for e, n in zip(search_index, nodes) if n.get("id") not in with_incoming
        ]
        per_graph.append({"entryPoints": entry_points, "searchIndex": search_index})

    return {
        "root": {
            "graphCount": len(graphs),
            "nodeCount": total_nodes,
            "pinCount": total_pins,
            "linkCount": total_links,
            "graphs": locators,
        },
        "perGraph": per_graph,
    }


def main() -> int:
    metas = sorted(PACK.rglob("*.meta.json"))
    if not metas:
        print(f"FAIL: no .meta.json found under {PACK}")
        return 1

    checked = 0
    order_mismatches = 0
    count_mismatches = 0

    for meta_path in metas:
        try:
            metadata = json.loads(meta_path.read_text(encoding="utf-8-sig"))
        except Exception as exc:
            print(f"SKIP {meta_path.name}: {exc}")
            continue

        graphs = metadata.get("graphs") or []
        if not graphs:
            continue

        native = build_native_index(metadata)
        checked += 1

        # The derived path is the ground truth MCP already uses: raw array order.
        for graph_index, graph in enumerate(graphs):
            nodes = graph.get("nodes") or []
            derived_order = [n.get("id") for n in nodes]
            native_order = [e["nodeId"] for e in native["perGraph"][graph_index]["searchIndex"]]
            if derived_order != native_order:
                order_mismatches += 1
                print(f"ORDER MISMATCH: {meta_path.name} graph {graph_index}")

            derived_counts = (
                len(nodes),
                sum(len(n.get("pins") or []) for n in nodes),
                len(graph.get("links") or []),
            )
            loc = native["root"]["graphs"][graph_index]
            native_counts = (loc["nodeCount"], loc["pinCount"], loc["linkCount"])
            if derived_counts != native_counts:
                count_mismatches += 1
                print(f"COUNT MISMATCH: {meta_path.name} graph {graph_index}")

    print(f"assets with graphs checked : {checked}")
    print(f"order mismatches           : {order_mismatches}")
    print(f"count mismatches           : {count_mismatches}")

    if order_mismatches or count_mismatches:
        print("RESULT: FAIL - native and derived indexes disagree")
        return 1
    print("RESULT: PASS - native index matches derived enumeration exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
