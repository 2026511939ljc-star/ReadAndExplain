"""Collects the same deterministic answers from an explicitly chosen MCP script.

Why this exists separately from collect_dual_client_reference.py
---------------------------------------------------------------
The original collector derives the MCP path from its own location, which is fine
when it lives inside the plugin it tests. The dual-client check needs the opposite
property: one collector, two different MCP deployments, so the script under test
must be passed in explicitly rather than inferred.

Endpoint A and endpoint B are separate processes started from separate install
locations with separate working directories. If their answers agree byte for byte,
the reads are reproducible across deployments rather than merely repeatable inside
one process.

This does not prove two independent implementations agree. Both endpoints run the
same source file, verified by hash. What it rules out is deployment drift, process
state leakage and configuration-dependent behaviour.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

PACK = "ContextPack_20260811_210312_55EAC3E3"
BLUEPRINT = "BP_FluxAllOne"
NIAGARA = "NS_InfiniteSurfaceMesh"


def call(script: Path, root: str, cwd: Path, name: str, arguments: dict) -> dict:
    request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"name": name, "arguments": arguments},
    }
    proc = subprocess.run(
        [sys.executable, str(script), "--root", root],
        input=json.dumps(request),
        capture_output=True,
        text=True,
        encoding="utf-8",
        cwd=str(cwd),
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


def codes(payload: dict) -> list[str]:
    return sorted(w["code"] for w in payload.get("warnings", []))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True, help="MCP script to exercise")
    parser.add_argument("--root", required=True, help="export root to read")
    parser.add_argument("--label", required=True, help="endpoint name for the report")
    parser.add_argument("--out", required=True, help="where to write the answers")
    args = parser.parse_args()

    script = Path(args.script).resolve()
    cwd = script.parent
    answers: dict = {"endpoint": args.label, "script": str(script), "pack": PACK, "q": {}}

    packs = call(script, args.root, cwd, "list_context_packs", {})
    entry = next(
        (p for p in packs.get("data", {}).get("packs", []) if p.get("pack_id") == PACK),
        {},
    )
    answers["q"]["q1_identity"] = {
        "state": entry.get("state"),
        "fingerprint": entry.get("fingerprint"),
    }

    for key, asset in (("q2_blueprint", BLUEPRINT), ("q3_niagara", NIAGARA)):
        outline = call(script, args.root, cwd, "get_asset_outline", {"asset": asset, "pack_path": PACK})
        data = outline.get("data", {})
        graphs = data.get("graphs", [])
        answers["q"][key] = {
            "graph_index_source": data.get("graph_index_source"),
            "warnings": codes(outline),
            "graph_count": len(graphs),
            "total_nodes": sum(g.get("node_count", 0) for g in graphs),
            "total_pins": sum(g.get("pin_count", 0) for g in graphs),
            "total_edges": sum(g.get("edge_count", 0) for g in graphs),
            "json_pointers": [g.get("json_pointer") for g in graphs],
            "first_graph_id": graphs[0].get("graph_id") if graphs else None,
        }

    located = call(
        script,
        args.root,
        cwd,
        "locate_graph_target",
        {"asset": NIAGARA, "pack_path": PACK, "kind_filter": "NiagaraNodeCustomHlsl"},
    )
    ldata = located.get("data", {})
    candidates = ldata.get("candidates", [])
    answers["q"]["q4_custom_hlsl"] = {
        "resolution": ldata.get("resolution"),
        # This field is the whole reason the fix exists, so it is captured explicitly.
        "graph_index_source": ldata.get("graph_index_source"),
        "candidate_count": len(candidates),
        "node_ids": [c.get("node_id") for c in candidates],
        "json_pointers": [c.get("json_pointer") for c in candidates],
    }

    sections = call(script, args.root, cwd, "get_readable_sections", {"asset": NIAGARA, "pack_path": PACK})
    sdata = sections.get("data", {})
    section_list = sdata.get("sections", [])
    answers["q"]["q5_sections"] = {
        "section_count": len(section_list),
        "section_ids": [s.get("section_id") for s in section_list],
        "character_counts": [s.get("character_count") for s in section_list],
    }

    Path(args.out).write_text(json.dumps(answers, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"{args.label}: written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
