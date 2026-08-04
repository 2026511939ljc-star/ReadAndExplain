from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any

SERVER_NAME = "readallandexplains"
SERVER_VERSION = "0.1.0"
DEFAULT_PROTOCOL_VERSION = "2025-06-18"
MAX_OUTPUT_CHARS = 60000


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def json_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2)


def clipped(text: str, limit: int = MAX_OUTPUT_CHARS) -> str:
    if len(text) <= limit:
        return text
    return text[:limit] + f"\n\n[truncated: {len(text) - limit} characters omitted]"


def default_export_root() -> Path:
    configured = os.environ.get("READALL_EXPORT_ROOT", "").strip()
    if configured:
        return Path(configured).expanduser()
    working_candidate = Path.cwd() / "Saved" / "ReadAllandExplainsExports"
    candidates = [working_candidate]
    script_path = Path(__file__).resolve()
    for parent in script_path.parents:
        candidates.append(parent / "Saved" / "ReadAllandExplainsExports")
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return working_candidate


class ContextPackStore:
    def __init__(self, export_root: Path):
        root = export_root.expanduser().resolve()
        self.export_root = root
        self.packs_root = root if root.name.lower() == "contextpacks" else root / "ContextPacks"
        self._json_cache: dict[Path, tuple[int, int, dict[str, Any]]] = {}

    def _cached_json(self, path: Path) -> dict[str, Any]:
        stat = path.stat()
        cached = self._json_cache.get(path)
        signature = (stat.st_mtime_ns, stat.st_size)
        if cached and cached[:2] == signature:
            return cached[2]
        value = load_json(path)
        self._json_cache[path] = (signature[0], signature[1], value)
        return value

    def _ensure_pack(self, path: Path) -> Path:
        resolved = path.expanduser().resolve()
        try:
            resolved.relative_to(self.packs_root.resolve())
        except ValueError as exc:
            raise ValueError("pack_path must be inside the configured ContextPacks directory") from exc
        if not (resolved / "context-pack.json").is_file():
            raise FileNotFoundError(f"context-pack.json not found in {resolved}")
        return resolved

    def list_packs(self, limit: int = 20) -> list[dict[str, Any]]:
        if not self.packs_root.exists():
            return []
        packs: list[dict[str, Any]] = []
        candidates = [path for path in self.packs_root.iterdir() if path.is_dir() and (path / "context-pack.json").is_file()]
        candidates.sort(key=lambda path: path.stat().st_mtime_ns, reverse=True)
        for path in candidates[: max(1, min(limit, 100))]:
            manifest = self._cached_json(path / "context-pack.json")
            packs.append(
                {
                    "name": path.name,
                    "path": str(path),
                    "createdUtc": manifest.get("createdUtc", ""),
                    "rootAssets": manifest.get("rootAssets", []),
                    "exportedAssetCount": manifest.get("exportedAssetCount", 0),
                    "dependencyDepth": manifest.get("dependencyDepth", 0),
                }
            )
        return packs

    def resolve_pack(self, pack_path: str | None = None) -> Path:
        if pack_path:
            return self._ensure_pack(Path(pack_path))
        packs = self.list_packs(1)
        if not packs:
            raise FileNotFoundError(f"No Context Pack found under {self.packs_root}")
        return Path(packs[0]["path"])

    def _metadata_map(self, pack: Path) -> dict[str, tuple[Path, dict[str, Any]]]:
        result: dict[str, tuple[Path, dict[str, Any]]] = {}
        for path in pack.rglob("*.meta.json"):
            try:
                metadata = self._cached_json(path)
            except (OSError, ValueError, json.JSONDecodeError):
                continue
            for key in (metadata.get("objectPath"), metadata.get("assetName"), path.stem.removesuffix(".meta")):
                if key:
                    result[str(key).casefold()] = (path, metadata)
        return result

    def assets(self, pack_path: str | None = None) -> tuple[Path, list[dict[str, Any]]]:
        pack = self.resolve_pack(pack_path)
        index_path = pack / "index.json"
        index = self._cached_json(index_path) if index_path.is_file() else {"assets": []}
        records = index.get("assets", [])
        if not isinstance(records, list):
            records = []
        metadata_map = self._metadata_map(pack)
        output: list[dict[str, Any]] = []
        for raw in records:
            if not isinstance(raw, dict):
                continue
            record = dict(raw)
            match = metadata_map.get(str(record.get("objectPath", "")).casefold()) or metadata_map.get(str(record.get("name", "")).casefold())
            if match:
                record["metadataFile"] = str(match[0])
            readable = self._resolve_readable_file(pack, record)
            if readable:
                record["readableFile"] = str(readable)
            output.append(record)
        return pack, output

    def _resolve_readable_file(self, pack: Path, record: dict[str, Any]) -> Path | None:
        declared = str(record.get("exportFile", ""))
        if declared:
            path = Path(declared).expanduser().resolve()
            try:
                path.relative_to(pack.resolve())
            except ValueError:
                path = Path()
            if path.is_file():
                return path
        name = str(record.get("name", ""))
        if name:
            matches = [path for path in pack.rglob(f"{name}_Readable*") if path.is_file() and path.suffix.lower() in {".md", ".txt"}]
            if matches:
                return matches[0]
        return None

    def find_asset(self, asset: str, pack_path: str | None = None) -> tuple[Path, dict[str, Any], dict[str, Any]]:
        pack, records = self.assets(pack_path)
        needle = asset.casefold().strip()
        exact = [record for record in records if needle in {str(record.get("name", "")).casefold(), str(record.get("objectPath", "")).casefold()}]
        candidates = exact or [record for record in records if needle in str(record.get("name", "")).casefold() or needle in str(record.get("objectPath", "")).casefold()]
        if not candidates:
            raise KeyError(f"Asset not found: {asset}")
        if len(candidates) > 1:
            names = [str(record.get("objectPath", record.get("name", ""))) for record in candidates[:20]]
            raise ValueError("Asset query is ambiguous: " + ", ".join(names))
        record = candidates[0]
        metadata_file = record.get("metadataFile")
        metadata = self._cached_json(Path(metadata_file)) if metadata_file else {}
        return pack, record, metadata

    def search_assets(self, query: str, pack_path: str | None = None, limit: int = 20) -> dict[str, Any]:
        pack, records = self.assets(pack_path)
        needle = query.casefold().strip()
        matches: list[dict[str, Any]] = []
        for record in records:
            searchable = json.dumps(
                {
                    "name": record.get("name"),
                    "objectPath": record.get("objectPath"),
                    "classPath": record.get("classPath"),
                    "assetKind": record.get("assetKind"),
                    "parameters": record.get("parameters", []),
                    "dependencies": record.get("dependencies", []),
                },
                ensure_ascii=False,
            ).casefold()
            if not needle or needle in searchable:
                matches.append(
                    {
                        "name": record.get("name", ""),
                        "objectPath": record.get("objectPath", ""),
                        "classPath": record.get("classPath", ""),
                        "assetKind": record.get("assetKind", ""),
                        "parameterCount": len(record.get("parameters", [])),
                        "dependencyCount": len(record.get("dependencies", [])),
                    }
                )
        return {"pack": str(pack), "matchCount": len(matches), "assets": matches[: max(1, min(limit, 100))]}

    def summary(self, asset: str, pack_path: str | None = None) -> dict[str, Any]:
        pack, record, metadata = self.find_asset(asset, pack_path)
        graphs = metadata.get("graphs", []) if isinstance(metadata.get("graphs", []), list) else []
        renderers = metadata.get("niagaraRenderers", []) if isinstance(metadata.get("niagaraRenderers", []), list) else []
        curves = metadata.get("niagaraCurves", []) if isinstance(metadata.get("niagaraCurves", []), list) else []
        graph_summary = []
        for graph in graphs:
            if isinstance(graph, dict):
                graph_summary.append(
                    {
                        "id": graph.get("id", ""),
                        "name": graph.get("name", ""),
                        "kind": graph.get("kind", ""),
                        "nodeCount": len(graph.get("nodes", [])),
                        "linkCount": len(graph.get("links", [])),
                    }
                )
        return {
            "pack": str(pack),
            "name": record.get("name", metadata.get("assetName", "")),
            "objectPath": record.get("objectPath", metadata.get("objectPath", "")),
            "classPath": record.get("classPath", metadata.get("classPath", "")),
            "assetKind": record.get("assetKind", metadata.get("assetKind", "")),
            "featureTags": metadata.get("featureTags", []),
            "artistFocus": metadata.get("artistFocus", ""),
            "suggestedPrompt": metadata.get("suggestedPrompt", ""),
            "parameters": metadata.get("parameters", record.get("parameters", [])),
            "dependencyCount": len(record.get("dependencies", metadata.get("dependencies", []))),
            "referencerCount": len(record.get("referencers", metadata.get("referencers", []))),
            "graphs": graph_summary,
            "rendererCount": len(renderers),
            "curveCount": len(curves),
        }

    def detail(self, asset: str, section: str, item_id: str | None, pack_path: str | None, offset: int, limit: int) -> Any:
        pack, record, metadata = self.find_asset(asset, pack_path)
        normalized = section.casefold()
        if normalized == "summary":
            return self.summary(asset, str(pack))
        if normalized in {"parameters", "dependencies", "referencers"}:
            return metadata.get(normalized, record.get(normalized, []))
        if normalized == "graphs":
            return [
                {"id": value.get("id"), "name": value.get("name"), "kind": value.get("kind"), "nodeCount": len(value.get("nodes", [])), "linkCount": len(value.get("links", []))}
                for value in metadata.get("graphs", [])
                if isinstance(value, dict)
            ]
        if normalized == "graph":
            values = metadata.get("graphs", [])
            return self._select_item(values, item_id, ("id", "name"), "graph")
        if normalized == "renderers":
            return metadata.get("niagaraRenderers", [])
        if normalized == "curves":
            values = metadata.get("niagaraCurves", [])
            if item_id:
                return self._select_item(values, item_id, ("id", "fingerprint", "exposedName", "objectPath"), "curve")
            return [
                {
                    "id": value.get("id"),
                    "fingerprint": value.get("fingerprint"),
                    "exposedName": value.get("exposedName"),
                    "usageCount": value.get("usageCount", len(value.get("usedBy", []))),
                    "usedBy": value.get("usedBy", []),
                    "minTime": value.get("minTime"),
                    "maxTime": value.get("maxTime"),
                    "channels": [{"name": channel.get("name"), "keyCount": len(channel.get("keys", []))} for channel in value.get("channels", []) if isinstance(channel, dict)],
                }
                for value in values
                if isinstance(value, dict)
            ]
        if normalized == "metadata":
            return metadata
        if normalized == "readable":
            readable_file = record.get("readableFile")
            if not readable_file:
                raise FileNotFoundError(f"Readable document not found for {asset}")
            text = Path(readable_file).read_text(encoding="utf-8-sig", errors="replace")
            start = max(0, offset)
            size = max(1, min(limit, MAX_OUTPUT_CHARS))
            return {"file": readable_file, "offset": start, "totalCharacters": len(text), "text": text[start : start + size]}
        raise ValueError("section must be summary, parameters, dependencies, referencers, graphs, graph, renderers, curves, metadata, or readable")

    @staticmethod
    def _select_item(values: Any, item_id: str | None, keys: tuple[str, ...], label: str) -> dict[str, Any]:
        if not item_id:
            raise ValueError(f"item_id is required for section {label}")
        needle = item_id.casefold()
        matches = [value for value in values if isinstance(value, dict) and any(needle == str(value.get(key, "")).casefold() for key in keys)]
        if not matches:
            matches = [value for value in values if isinstance(value, dict) and any(needle in str(value.get(key, "")).casefold() for key in keys)]
        if len(matches) != 1:
            raise KeyError(f"Expected one {label} for {item_id}, found {len(matches)}")
        return matches[0]

    def search_text(self, query: str, asset: str | None, pack_path: str | None, limit: int) -> dict[str, Any]:
        pack, records = self.assets(pack_path)
        selected = records
        if asset:
            _, selected_record, _ = self.find_asset(asset, str(pack))
            selected = [selected_record]
        needle = query.casefold()
        hits: list[dict[str, Any]] = []
        for record in selected:
            paths = [record.get("readableFile"), record.get("metadataFile")]
            for raw_path in paths:
                if not raw_path:
                    continue
                path = Path(str(raw_path))
                try:
                    lines = path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
                except OSError:
                    continue
                for line_number, line in enumerate(lines, 1):
                    position = line.casefold().find(needle)
                    if position < 0:
                        continue
                    start = max(0, position - 100)
                    end = min(len(line), position + len(query) + 180)
                    hits.append({"asset": record.get("name", ""), "file": str(path), "line": line_number, "snippet": line[start:end]})
                    if len(hits) >= max(1, min(limit, 100)):
                        return {"pack": str(pack), "hits": hits, "truncated": True}
        return {"pack": str(pack), "hits": hits, "truncated": False}


TOOLS = [
    {
        "name": "list_context_packs",
        "description": "List recent ReadAllandExplains Context Packs. Use this first only when no pack has been selected.",
        "inputSchema": {"type": "object", "properties": {"limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}}},
    },
    {
        "name": "search_assets",
        "description": "Search the latest or selected Context Pack by asset name, UE object path, type, parameter, or dependency. Returns small summaries only.",
        "inputSchema": {
            "type": "object",
            "properties": {"query": {"type": "string", "default": ""}, "pack_path": {"type": "string"}, "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}},
        },
    },
    {
        "name": "get_asset_summary",
        "description": "Get a compact asset overview with parameter clues and graph/renderer/curve counts. Prefer this before requesting detail.",
        "inputSchema": {"type": "object", "required": ["asset"], "properties": {"asset": {"type": "string"}, "pack_path": {"type": "string"}}},
    },
    {
        "name": "get_asset_detail",
        "description": "Read one precise asset section on demand. Curves without item_id return compact curve summaries; graph or a specific curve requires item_id.",
        "inputSchema": {
            "type": "object",
            "required": ["asset", "section"],
            "properties": {
                "asset": {"type": "string"},
                "section": {"type": "string", "enum": ["summary", "parameters", "dependencies", "referencers", "graphs", "graph", "renderers", "curves", "metadata", "readable"]},
                "item_id": {"type": "string"},
                "pack_path": {"type": "string"},
                "offset": {"type": "integer", "minimum": 0, "default": 0},
                "limit": {"type": "integer", "minimum": 1, "maximum": 60000, "default": 12000},
            },
        },
    },
    {
        "name": "search_export_text",
        "description": "Search readable Markdown and metadata JSON without loading complete files. Optionally restrict to one asset.",
        "inputSchema": {
            "type": "object",
            "required": ["query"],
            "properties": {"query": {"type": "string", "minLength": 1}, "asset": {"type": "string"}, "pack_path": {"type": "string"}, "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}},
        },
    },
]


def text_result(value: Any) -> dict[str, Any]:
    text = value if isinstance(value, str) else json_text(value)
    return {"content": [{"type": "text", "text": clipped(text)}]}


def call_tool(store: ContextPackStore, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "list_context_packs":
        return text_result(store.list_packs(int(arguments.get("limit", 20))))
    if name == "search_assets":
        return text_result(store.search_assets(str(arguments.get("query", "")), arguments.get("pack_path"), int(arguments.get("limit", 20))))
    if name == "get_asset_summary":
        return text_result(store.summary(str(arguments["asset"]), arguments.get("pack_path")))
    if name == "get_asset_detail":
        value = store.detail(
            str(arguments["asset"]),
            str(arguments["section"]),
            arguments.get("item_id"),
            arguments.get("pack_path"),
            int(arguments.get("offset", 0)),
            int(arguments.get("limit", 12000)),
        )
        return text_result(value)
    if name == "search_export_text":
        return text_result(store.search_text(str(arguments["query"]), arguments.get("asset"), arguments.get("pack_path"), int(arguments.get("limit", 20))))
    raise KeyError(f"Unknown tool: {name}")


def response(request_id: Any, result: Any = None, error: dict[str, Any] | None = None) -> dict[str, Any]:
    message: dict[str, Any] = {"jsonrpc": "2.0", "id": request_id}
    if error is not None:
        message["error"] = error
    else:
        message["result"] = result
    return message


def handle_request(store: ContextPackStore, request: dict[str, Any]) -> dict[str, Any] | None:
    method = request.get("method")
    request_id = request.get("id")
    if request_id is None:
        return None
    if method == "initialize":
        requested = request.get("params", {}).get("protocolVersion")
        return response(
            request_id,
            {
                "protocolVersion": requested or DEFAULT_PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "instructions": "Use search_assets, then get_asset_summary, then request only the required detail section. Avoid loading full metadata unless explicitly needed.",
            },
        )
    if method == "ping":
        return response(request_id, {})
    if method == "tools/list":
        return response(request_id, {"tools": TOOLS})
    if method == "tools/call":
        params = request.get("params", {})
        try:
            return response(request_id, call_tool(store, str(params.get("name", "")), params.get("arguments") or {}))
        except Exception as exc:
            return response(request_id, {"content": [{"type": "text", "text": f"{type(exc).__name__}: {exc}"}], "isError": True})
    return response(request_id, error={"code": -32601, "message": f"Method not found: {method}"})


def serve(store: ContextPackStore) -> None:
    for raw_line in sys.stdin.buffer:
        if not raw_line.strip():
            continue
        try:
            request = json.loads(raw_line.decode("utf-8-sig"))
            if not isinstance(request, dict):
                raise ValueError("JSON-RPC message must be an object")
            outgoing = handle_request(store, request)
            if outgoing is not None:
                sys.stdout.write(json.dumps(outgoing, ensure_ascii=False, separators=(",", ":")) + "\n")
                sys.stdout.flush()
        except Exception as exc:
            sys.stderr.write(f"{SERVER_NAME}: {type(exc).__name__}: {exc}\n")
            sys.stderr.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description="Read-only MCP server for ReadAllandExplains export caches")
    parser.add_argument("--root", type=Path, default=default_export_root(), help="ReadAllandExplainsExports directory or its ContextPacks child")
    parser.add_argument("--self-test", action="store_true", help="Print a compact latest-pack summary and exit")
    args = parser.parse_args()
    store = ContextPackStore(args.root)
    if args.self_test:
        packs = store.list_packs(1)
        result: dict[str, Any] = {"server": SERVER_NAME, "version": SERVER_VERSION, "exportRoot": str(store.export_root), "packs": packs}
        if packs:
            result["assets"] = store.search_assets("", packs[0]["path"], 100)
        print(json_text(result))
        return
    serve(store)


if __name__ == "__main__":
    main()
