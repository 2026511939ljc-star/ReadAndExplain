"""Compares two endpoint answer files field by field.

The earlier comparator scraped tokens out of chat prose, which was appropriate for
comparing what two assistants wrote. This one compares structured answers collected
directly from two MCP deployments, so it can be strict: every leaf value is compared
at its exact path, and any divergence is reported rather than summarised.

Strictness matters here. A comparator that only checks whether expected values are
present somewhere can pass while the two sides disagree elsewhere. This walks both
trees and reports the first-class fact: are they identical, and if not, exactly where.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# Values that must hold regardless of which endpoint answered, so a mutually
# consistent but wrong pair cannot pass silently.
EXPECTED = {
    "q1_identity.state": "complete",
    "q1_identity.fingerprint": "sha1:10533df69cb81b6ea0da67dad92ac0c40ee4c88b",
    "q2_blueprint.graph_index_source": "native_metadata_graph_index",
    "q2_blueprint.graph_count": 7,
    "q2_blueprint.total_nodes": 256,
    "q2_blueprint.total_pins": 780,
    "q2_blueprint.total_edges": 329,
    "q3_niagara.graph_index_source": "native_metadata_graph_index",
    "q3_niagara.graph_count": 20,
    "q3_niagara.total_nodes": 767,
    "q3_niagara.total_pins": 2951,
    "q3_niagara.total_edges": 1087,
    "q4_custom_hlsl.resolution": "listed",
    "q4_custom_hlsl.graph_index_source": "native_metadata_graph_index",
    "q4_custom_hlsl.candidate_count": 1,
    "q5_sections.section_count": 7,
}

FORBIDDEN_WARNING = "GRAPH_INDEX_DERIVED"


def flatten(node, prefix: str = "") -> dict:
    flat = {}
    if isinstance(node, dict):
        for key, value in node.items():
            flat.update(flatten(value, f"{prefix}.{key}" if prefix else key))
    elif isinstance(node, list):
        for index, value in enumerate(node):
            flat.update(flatten(value, f"{prefix}[{index}]"))
    else:
        flat[prefix] = node
    return flat


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", required=True)
    parser.add_argument("--b", required=True)
    args = parser.parse_args()

    # Files produced by Windows tooling frequently carry a UTF-8 BOM. That is an
    # encoding detail, not a data difference, so it must not look like a failure.
    a_doc = json.loads(Path(args.a).read_text(encoding="utf-8-sig"))
    b_doc = json.loads(Path(args.b).read_text(encoding="utf-8-sig"))

    print(f"endpoint A : {a_doc['endpoint']}")
    print(f"  script   : {a_doc['script']}")
    print(f"endpoint B : {b_doc['endpoint']}")
    print(f"  script   : {b_doc['script']}")
    print()

    a_flat = flatten(a_doc["q"])
    b_flat = flatten(b_doc["q"])

    print("=== Field-by-field equality ===")
    all_paths = sorted(set(a_flat) | set(b_flat))
    mismatches = []
    for path in all_paths:
        av = a_flat.get(path, "<absent>")
        bv = b_flat.get(path, "<absent>")
        if av != bv:
            mismatches.append((path, av, bv))
    print(f"  compared leaf values : {len(all_paths)}")
    print(f"  divergent            : {len(mismatches)}")
    for path, av, bv in mismatches:
        print(f"    DIFF {path}: A={av!r} B={bv!r}")

    print()
    print("=== Absolute expectations ===")
    expectation_failures = []
    for path, want in EXPECTED.items():
        got_a, got_b = a_flat.get(path), b_flat.get(path)
        if got_a == want and got_b == want:
            print(f"  ok        {path} = {want}")
        else:
            print(f"  FAIL      {path}: want={want!r} A={got_a!r} B={got_b!r}")
            expectation_failures.append(path)

    print()
    print("=== Forbidden warning ===")
    warning_hits = [
        path for path, value in list(a_flat.items()) + list(b_flat.items())
        if value == FORBIDDEN_WARNING
    ]
    if warning_hits:
        print(f"  FAIL      {FORBIDDEN_WARNING} present at {warning_hits}")
    else:
        print(f"  ok        {FORBIDDEN_WARNING} absent from both endpoints")

    print()
    if mismatches or expectation_failures or warning_hits:
        print("RESULT: FAIL")
        return 1
    print(f"RESULT: PASS - {len(all_paths)} leaf values identical across both endpoints")
    return 0


if __name__ == "__main__":
    sys.exit(main())
