"""Collects the reference answers for the CP7 dual-client determinism check.

Why this exists
---------------
The strongest evidence this project has is that two unrelated clients, given the
same questions against the same immutable pack, return byte-identical field
values. That claim was previously established on a 4.7 pack whose graph index was
derived in MCP memory. CP7 moved index construction into the exporter, so the old
result does not automatically carry over to a natively indexed pack.

This script records the answers by calling the MCP server directly, so the
operator can compare each client's transcript against a fixed reference instead
of eyeballing two long chat logs.

Only fields that must be deterministic are captured. Free-form prose is
deliberately excluded, because natural language is expected to vary between
clients while facts are not.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

PLUGIN_ROOT = Path(__file__).resolve().parents[2]
MCP_SCRIPT = PLUGIN_ROOT / "Integrations" / "MCP" / "readallandexplains_mcp.py"
EXPORT_ROOT = r"D:\UE5\Trans\Saved\ReadAllandExplainsExports"

PACK = "ContextPack_20260811_210312_55EAC3E3"
BLUEPRINT = "BP_FluxAllOne"
NIAGARA = "NS_InfiniteSurfaceMesh"


def call(name: str, arguments: dict) -> dict:
    request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"name": name, "arguments": arguments},
    }
    proc = subprocess.run(
        [sys.executable, str(MCP_SCRIPT), "--root", EXPORT_ROOT],
        input=json.dumps(request),
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    for line in proc.stdout.splitlines():
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue
        result = message.get("result")
        if not result:
            continue
        if result.get("isError"):
            return {"error": result.get("structuredContent", {}).get("error")}
        return json.loads(result["content"][0]["text"])
    return {"error": "no response", "stderr": proc.stderr[-400:]}


def warning_codes(payload: dict) -> list[str]:
    return sorted(w["code"] for w in payload.get("warnings", []))


def main() -> int:
    reference: dict = {"pack": PACK, "checks": {}}

    packs = call("list_context_packs", {})
    entry = next(
        (p for p in packs.get("data", {}).get("packs", []) if p.get("pack_id") == PACK),
        {},
    )
    reference["checks"]["pack_identity"] = {
        "state": entry.get("state"),
        "fingerprint": entry.get("fingerprint"),
    }

    for label, asset in (("blueprint", BLUEPRINT), ("niagara", NIAGARA)):
        outline = call("get_asset_outline", {"asset": asset, "pack_path": PACK})
        data = outline.get("data", {})
        graphs = data.get("graphs", [])
        reference["checks"][f"{label}_outline"] = {
            "graph_index_source": data.get("graph_index_source"),
            "warnings": warning_codes(outline),
            "graph_count": len(graphs),
            "total_nodes": sum(g.get("node_count", 0) for g in graphs),
            "total_pins": sum(g.get("pin_count", 0) for g in graphs),
            "total_edges": sum(g.get("edge_count", 0) for g in graphs),
            # Pointers must be stable and ordered; this is what makes an answer citable.
            "json_pointers": [g.get("json_pointer") for g in graphs],
            "first_graph_id": graphs[0].get("graph_id") if graphs else None,
        }

    # Enumeration without a query is the discovery path; order must be reproducible.
    listed = call(
        "locate_graph_target",
        {"asset": NIAGARA, "pack_path": PACK, "kind_filter": "NiagaraNodeCustomHlsl"},
    )
    ldata = listed.get("data", {})
    candidates = ldata.get("candidates", [])
    reference["checks"]["custom_hlsl_locate"] = {
        "resolution": ldata.get("resolution"),
        "candidate_count": len(candidates),
        "node_ids": [c.get("node_id") for c in candidates],
        "json_pointers": [c.get("json_pointer") for c in candidates],
    }

    sections = call("get_readable_sections", {"asset": NIAGARA, "pack_path": PACK})
    sdata = sections.get("data", {})
    section_list = sdata.get("sections", [])
    reference["checks"]["readable_sections"] = {
        "section_count": len(section_list),
        "section_ids": [s.get("section_id") for s in section_list],
        "character_counts": [s.get("character_count") for s in section_list],
    }

    print(json.dumps(reference, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
