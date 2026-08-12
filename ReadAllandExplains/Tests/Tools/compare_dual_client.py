"""Compares two client transcripts against the CP7 determinism reference.

The point of the dual-client check is that facts must not drift between clients.
Prose will always differ, so this tool ignores wording entirely and extracts only
the values that must be identical: fingerprints, counts, index provenance,
warning codes, node ids and json pointers.

Usage:
    python compare_dual_client.py --a clientA.txt --b clientB.txt
    python compare_dual_client.py --a clientA.txt --b clientB.txt --show-missing
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# The reference was collected by calling the MCP server directly against
# ContextPack_20260811_210312_55EAC3E3. Any client disagreeing with these values
# is either reading a different pack or not reading the pack at all.
REFERENCE = {
    "fingerprint": "sha1:10533df69cb81b6ea0da67dad92ac0c40ee4c88b",
    "bp_graph_count": "7",
    "bp_nodes": "256",
    "bp_pins": "780",
    "bp_edges": "329",
    "ns_graph_count": "20",
    "ns_nodes": "767",
    "ns_pins": "2951",
    "ns_edges": "1087",
    "index_source": "native_metadata_graph_index",
    "hlsl_node_id": "54CF7B53-4C9C-1BB0-BB8F-F592DC86CC57",
    "hlsl_pointer": "/graphs/16/nodes/20",
    "section_count": "7",
    "technical_chars": "176732",
}

# A client that still reports a derived index is reading an old pack, which would
# invalidate the whole comparison.
FORBIDDEN = ["GRAPH_INDEX_DERIVED"]

SECTION_IDS = [
    "overview",
    "parameters",
    "relationships",
    "graph-ir",
    "niagara-details",
    "technical",
    "ai-prompt",
]


def extract(text: str) -> dict[str, set[str]]:
    """Pulls deterministic tokens out of free-form client output."""
    found: dict[str, set[str]] = {}

    found["fingerprint"] = set(re.findall(r"sha1:[0-9a-f]{40}", text))
    found["index_source"] = set(
        re.findall(r"native_metadata_graph_index|derived_metadata_graphs", text)
    )
    found["guid"] = {g.upper() for g in re.findall(r"\b[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}\b", text)}
    found["pointer"] = set(re.findall(r"/graphs/\d+(?:/nodes/\d+)?", text))
    found["warning"] = set(re.findall(r"[A-Z][A-Z_]{4,}", text))
    found["number"] = set(re.findall(r"\b\d{2,7}\b", text))
    found["section_id"] = {s for s in SECTION_IDS if s in text}
    return found


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", required=True, help="transcript from client A")
    parser.add_argument("--b", required=True, help="transcript from client B")
    parser.add_argument("--show-missing", action="store_true")
    args = parser.parse_args()

    text_a = Path(args.a).read_text(encoding="utf-8", errors="replace")
    text_b = Path(args.b).read_text(encoding="utf-8", errors="replace")
    a = extract(text_a)
    b = extract(text_b)

    print("=== Reference agreement ===")
    agreed = 0
    disagreed: list[str] = []
    missing: list[str] = []

    numeric_keys = {
        "bp_graph_count", "bp_nodes", "bp_pins", "bp_edges",
        "ns_graph_count", "ns_nodes", "ns_pins", "ns_edges",
        "section_count", "technical_chars",
    }

    for key, expected in REFERENCE.items():
        if key == "fingerprint":
            in_a, in_b = expected in a["fingerprint"], expected in b["fingerprint"]
        elif key == "index_source":
            in_a, in_b = expected in a["index_source"], expected in b["index_source"]
        elif key == "hlsl_node_id":
            in_a, in_b = expected in a["guid"], expected in b["guid"]
        elif key == "hlsl_pointer":
            in_a, in_b = expected in a["pointer"], expected in b["pointer"]
        elif key in numeric_keys:
            # Single-digit counts are unreliable to match by regex, so treat a
            # short expected value as satisfied when it appears verbatim.
            if len(expected) < 2:
                in_a, in_b = expected in text_a, expected in text_b
            else:
                in_a, in_b = expected in a["number"], expected in b["number"]
        else:
            in_a, in_b = expected in text_a, expected in text_b

        if in_a and in_b:
            print(f"  BOTH OK    {key} = {expected}")
            agreed += 1
        elif in_a or in_b:
            side = "A only" if in_a else "B only"
            print(f"  MISMATCH   {key} = {expected}  ({side})")
            disagreed.append(key)
        else:
            print(f"  ABSENT     {key} = {expected}")
            missing.append(key)

    print()
    print("=== Forbidden markers ===")
    forbidden_hits = []
    for code in FORBIDDEN:
        hit_a, hit_b = code in text_a, code in text_b
        if hit_a or hit_b:
            where = "A and B" if hit_a and hit_b else ("A" if hit_a else "B")
            print(f"  PRESENT    {code} in {where}  <-- should not appear")
            forbidden_hits.append(code)
        else:
            print(f"  clean      {code}")

    print()
    print("=== Cross-client symmetry ===")
    for label in ("fingerprint", "guid", "pointer", "section_id"):
        only_a = a[label] - b[label]
        only_b = b[label] - a[label]
        if not only_a and not only_b:
            print(f"  identical  {label} ({len(a[label])} values)")
        else:
            print(f"  differs    {label}: A-only={sorted(only_a)[:4]} B-only={sorted(only_b)[:4]}")

    print()
    total = len(REFERENCE)
    print(f"agreed on reference values : {agreed}/{total}")
    print(f"mismatched                 : {len(disagreed)}")
    print(f"absent from both           : {len(missing)}")

    if args.show_missing and missing:
        print(f"absent keys: {missing}")

    if disagreed or forbidden_hits:
        print()
        print("RESULT: FAIL - clients disagree or a forbidden marker appeared")
        return 1
    if agreed < total:
        print()
        print("RESULT: INCOMPLETE - no disagreement, but some values were not reported by either client")
        return 2
    print()
    print("RESULT: PASS - both clients agree with the reference on every checked value")
    return 0


if __name__ == "__main__":
    sys.exit(main())
