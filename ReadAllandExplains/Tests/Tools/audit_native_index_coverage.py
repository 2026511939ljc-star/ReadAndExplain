"""Verifies the graphIndex the exporter actually wrote, across an entire pack.

Why this replaces the simulate-and-compare approach
---------------------------------------------------
The earlier harness rebuilt an index in Python using the same rules as the C++
exporter, then compared that reconstruction against the graphs it was derived
from. That checks the rules are self-consistent, but it cannot fail when the
exporter writes something different, because the exporter's output was never
read. It also pinned one hardcoded pack, so it silently stopped covering the
packs actually being shipped.

This version reads metadata.graphIndex as it exists on disk and holds it against
the graphs in the same file. If the exporter miscounts, emits stale pointers,
skips assets or disagrees on ordering, this fails and names the asset.

Coverage is reported explicitly, because "PASS" over two assets and "PASS" over
every graph-bearing asset in a pack are very different claims.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def check_asset(metadata: dict) -> tuple[list[str], bool]:
    """Returns (problems, had_native_index) for one asset."""
    problems: list[str] = []
    graphs = metadata.get("graphs") or []
    index = metadata.get("graphIndex")

    if not isinstance(index, dict):
        return ["no native graphIndex present"], False

    if index.get("source") != "native":
        problems.append(f"source is {index.get('source')!r}, expected 'native'")

    locators = index.get("graphs")
    if not isinstance(locators, list):
        return problems + ["graphIndex.graphs missing or not a list"], True

    if len(locators) != len(graphs):
        problems.append(f"graphCount {len(locators)} != actual graphs {len(graphs)}")
        return problems, True

    if index.get("graphCount") != len(graphs):
        problems.append(f"graphIndex.graphCount {index.get('graphCount')} != {len(graphs)}")

    roll_nodes = roll_pins = roll_links = 0

    for position, (locator, graph) in enumerate(zip(locators, graphs)):
        nodes = graph.get("nodes") or []
        links = graph.get("links") or []
        pins = sum(len(n.get("pins") or []) for n in nodes)
        roll_nodes += len(nodes)
        roll_pins += pins
        roll_links += len(links)

        if locator.get("graphId") != graph.get("id"):
            problems.append(f"graph {position}: graphId mismatch")

        actual = (len(nodes), pins, len(links))
        claimed = (locator.get("nodeCount"), locator.get("pinCount"), locator.get("linkCount"))
        if actual != claimed:
            problems.append(f"graph {position}: counts claimed {claimed} actual {actual}")

        # A pointer that does not resolve to the graph it describes is worse than
        # no pointer, because callers cite it as evidence.
        expected_pointer = f"/graphs/{position}"
        if locator.get("jsonPointer") != expected_pointer:
            problems.append(
                f"graph {position}: jsonPointer {locator.get('jsonPointer')!r} != {expected_pointer!r}"
            )

    for field, value in (("nodeCount", roll_nodes), ("pinCount", roll_pins), ("linkCount", roll_links)):
        if index.get(field) != value:
            problems.append(f"root {field} claimed {index.get(field)} actual {value}")

    return problems, True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", required=True, help="context pack directory to audit")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    pack = Path(args.pack)
    if not pack.is_dir():
        print(f"FAIL: pack directory not found: {pack}")
        return 1

    metas = sorted(pack.rglob("*.meta.json"))
    if not metas:
        print(f"FAIL: no .meta.json under {pack}")
        return 1

    total = with_graphs = native = failed = 0
    unreadable: list[str] = []

    for meta_path in metas:
        total += 1
        try:
            metadata = json.loads(meta_path.read_text(encoding="utf-8-sig"))
        except Exception as exc:
            unreadable.append(f"{meta_path.name}: {exc}")
            continue

        if not (metadata.get("graphs") or []):
            continue
        with_graphs += 1

        problems, had_index = check_asset(metadata)
        if had_index:
            native += 1
        if problems:
            failed += 1
            print(f"FAIL {meta_path.name}")
            for problem in problems:
                print(f"     - {problem}")
        elif args.verbose:
            print(f"ok   {meta_path.name}")

    print()
    print(f"pack                        : {pack.name}")
    print(f"metadata files              : {total}")
    print(f"assets with graphs          : {with_graphs}")
    print(f"carrying native graphIndex  : {native}")
    print(f"assets with problems        : {failed}")
    if unreadable:
        print(f"unreadable                  : {len(unreadable)}")
        for item in unreadable:
            print(f"     - {item}")

    coverage = (native / with_graphs * 100) if with_graphs else 0.0
    print(f"native index coverage       : {coverage:.1f}%")

    if failed or unreadable:
        print("RESULT: FAIL")
        return 1
    if with_graphs and native < with_graphs:
        print("RESULT: FAIL - some graph-bearing assets lack a native index")
        return 1
    print("RESULT: PASS - every graph-bearing asset carries a verified native index")
    return 0


if __name__ == "__main__":
    sys.exit(main())
