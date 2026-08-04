from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any

SERVER_NAME = "readallandexplains"
SERVER_VERSION = "0.2.0"
CONTRACT_VERSION = "rae.mcp/1.0"
SUPPORTED_PROTOCOL_VERSIONS = ("2024-11-05", "2025-03-26", "2025-06-18")
DEFAULT_PROTOCOL_VERSION = SUPPORTED_PROTOCOL_VERSIONS[-1]
SUPPORTED_PACK_SCHEMAS = {1}
MAX_OUTPUT_CHARS = 60000


class RaeError(Exception):
    def __init__(self, code: str, message: str, details: dict[str, Any] | None = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = details or {}


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8-sig") as handle:
            value = json.load(handle)
    except FileNotFoundError as exc:
        raise RaeError("FILE_NOT_FOUND", f"JSON file not found: {path}", {"path": str(path)}) from exc
    except json.JSONDecodeError as exc:
        raise RaeError("INVALID_JSON", f"Invalid JSON: {path}", {"path": str(path), "line": exc.lineno}) from exc
    if not isinstance(value, dict):
        raise RaeError("INVALID_JSON", f"Expected a JSON object: {path}", {"path": str(path)})
    return value


def json_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2)


def default_export_root() -> Path:
    configured = os.environ.get("READALL_EXPORT_ROOT", "").strip()
    if configured:
        return Path(configured).expanduser()
    working_candidate = Path.cwd() / "Saved" / "ReadAllandExplainsExports"
    candidates = [working_candidate]
    for parent in Path(__file__).resolve().parents:
        candidates.append(parent / "Saved" / "ReadAllandExplainsExports")
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return working_candidate


def page_info(offset: int = 0, limit: int = 0, returned: int = 0, total: int = 0) -> dict[str, Any]:
    next_offset = offset + returned
    truncated = next_offset < total
    return {
        "cursor": str(offset),
        "next_cursor": str(next_offset) if truncated else None,
        "offset": offset,
        "limit": limit,
        "returned": returned,
        "total": total,
        "truncated": truncated,
    }


def paginate(values: list[Any], offset: int, limit: int, maximum: int = 1000) -> tuple[list[Any], dict[str, Any]]:
    start = max(0, offset)
    size = max(1, min(limit, maximum))
    selected = values[start : start + size]
    return selected, page_info(start, size, len(selected), len(values))


class ContextPackStore:
    def __init__(self, export_root: Path):
        root = export_root.expanduser().resolve()
        self.export_root = root
        self.packs_root = root if root.name.casefold() == "contextpacks" else root / "ContextPacks"
        self._json_cache: dict[Path, tuple[int, int, dict[str, Any]]] = {}

    def _cached_json(self, path: Path) -> dict[str, Any]:
        stat = path.stat()
        signature = (stat.st_mtime_ns, stat.st_size)
        cached = self._json_cache.get(path)
        if cached and cached[:2] == signature:
            return cached[2]
        value = load_json(path)
        self._json_cache[path] = (signature[0], signature[1], value)
        return value

    def _ensure_pack_path(self, path: Path) -> Path:
        resolved = path.expanduser().resolve()
        try:
            resolved.relative_to(self.packs_root.resolve())
        except ValueError as exc:
            raise RaeError("PACK_OUTSIDE_ROOT", "Pack must be inside the configured ContextPacks directory") from exc
        if not resolved.is_dir():
            raise RaeError("PACK_NOT_FOUND", f"Context Pack not found: {resolved}", {"pack_path": str(resolved)})
        if not (resolved / "context-pack.json").is_file():
            raise RaeError("PACK_INCOMPLETE", f"context-pack.json not found in {resolved}", {"pack_path": str(resolved)})
        return resolved

    def _manifest(self, pack: Path, validate: bool = True) -> dict[str, Any]:
        manifest = self._cached_json(pack / "context-pack.json")
        if validate:
            state = manifest.get("state")
            if state not in (None, "complete"):
                raise RaeError("PACK_INCOMPLETE", f"Context Pack state is {state!r}", {"pack_id": pack.name, "state": state})
            schema = manifest.get("schemaVersion")
            if schema not in SUPPORTED_PACK_SCHEMAS:
                raise RaeError(
                    "SCHEMA_UNSUPPORTED",
                    f"Unsupported Context Pack schema: {schema}",
                    {"pack_id": pack.name, "supported": sorted(SUPPORTED_PACK_SCHEMAS)},
                )
            files = manifest.get("files")
            if isinstance(files, list):
                pack_root = pack.resolve()
                for entry in files:
                    if not isinstance(entry, dict) or not entry.get("path"):
                        raise RaeError("PACK_INCOMPLETE", "Manifest contains an invalid file entry", {"pack_id": pack.name})
                    file_path = (pack / str(entry["path"])).resolve()
                    try:
                        file_path.relative_to(pack_root)
                    except ValueError as exc:
                        raise RaeError("PACK_INCOMPLETE", "Manifest file escapes the Context Pack", {"path": str(entry["path"])}) from exc
                    if not file_path.is_file():
                        raise RaeError("PACK_INCOMPLETE", "Manifest file is missing", {"path": str(entry["path"])})
                    expected_size = entry.get("size")
                    if isinstance(expected_size, (int, float)) and file_path.stat().st_size != int(expected_size):
                        raise RaeError(
                            "PACK_INCOMPLETE",
                            "Manifest file size does not match",
                            {"path": str(entry["path"]), "expected": int(expected_size), "actual": file_path.stat().st_size},
                        )
        return manifest

    def pack_info(self, pack: Path, validate: bool = True) -> tuple[dict[str, Any], list[dict[str, str]]]:
        manifest_path = pack / "context-pack.json"
        manifest = self._manifest(pack, validate)
        state = manifest.get("state") or "legacy"
        warnings: list[dict[str, str]] = []
        if state == "legacy":
            warnings.append({"code": "PACK_STATE_MISSING", "message": "Pack has no state field; accepted as a legacy complete pack."})
        digest = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        fingerprint = str(manifest.get("fingerprint") or f"sha256:{digest}")
        return (
            {
                "pack_id": str(manifest.get("packId") or pack.name),
                "path": str(pack),
                "schema_version": manifest.get("schemaVersion"),
                "created_at": manifest.get("createdUtc", ""),
                "state": state,
                "fingerprint": fingerprint,
                "root_assets": manifest.get("rootAssets", []),
                "exported_asset_count": manifest.get("exportedAssetCount", 0),
                "dependency_depth": manifest.get("dependencyDepth", 0),
            },
            warnings,
        )

    def list_packs(self, offset: int = 0, limit: int = 20) -> dict[str, Any]:
        if not self.packs_root.exists():
            return {"pack": None, "data": {"packs": []}, "page": page_info(offset, limit, 0, 0)}
        candidates = [
            path
            for path in self.packs_root.iterdir()
            if path.is_dir() and not path.name.casefold().endswith(".tmp") and (path / "context-pack.json").is_file()
        ]
        candidates.sort(key=lambda path: path.stat().st_mtime_ns, reverse=True)
        packs: list[dict[str, Any]] = []
        warnings: list[dict[str, str]] = []
        for path in candidates:
            try:
                info, item_warnings = self.pack_info(path, validate=True)
                packs.append(info)
                warnings.extend(item_warnings)
            except (OSError, RaeError) as exc:
                warnings.append({"code": "PACK_SKIPPED", "message": f"Skipped {path.name}: {exc}"})
        selected, page = paginate(packs, offset, limit, 100)
        evidence = [{"pack_id": item["pack_id"], "source_file": "context-pack.json", "json_pointer": ""} for item in selected]
        return {"pack": None, "data": {"packs": selected}, "evidence": evidence, "page": page, "warnings": warnings}

    def resolve_pack(self, pack_path: str | None = None) -> Path:
        if pack_path:
            raw = Path(pack_path)
            candidate = raw if raw.is_absolute() else self.packs_root / raw
            pack = self._ensure_pack_path(candidate)
            self._manifest(pack, validate=True)
            return pack
        if not self.packs_root.exists():
            raise RaeError("PACK_NOT_FOUND", f"No Context Pack found under {self.packs_root}")
        candidates = [path for path in self.packs_root.iterdir() if path.is_dir() and (path / "context-pack.json").is_file()]
        candidates.sort(key=lambda path: path.stat().st_mtime_ns, reverse=True)
        rejected: list[dict[str, str]] = []
        for candidate in candidates:
            try:
                pack = self._ensure_pack_path(candidate)
                self._manifest(pack, validate=True)
                return pack
            except (OSError, RaeError) as exc:
                rejected.append({"pack_id": candidate.name, "reason": str(exc)})
        raise RaeError("PACK_NOT_FOUND", f"No complete supported Context Pack found under {self.packs_root}", {"rejected": rejected})

    def _relative(self, pack: Path, path: Path) -> str:
        return path.resolve().relative_to(pack.resolve()).as_posix()

    def _metadata_map(self, pack: Path) -> dict[str, tuple[Path, dict[str, Any]]]:
        result: dict[str, tuple[Path, dict[str, Any]]] = {}
        for path in pack.rglob("*.meta.json"):
            try:
                metadata = self._cached_json(path)
            except (OSError, RaeError):
                continue
            keys = (metadata.get("objectPath"), metadata.get("assetName"), path.stem.removesuffix(".meta"))
            for key in keys:
                if key:
                    result[str(key).casefold()] = (path, metadata)
        return result

    def _resolve_readable_file(self, pack: Path, record: dict[str, Any]) -> Path | None:
        declared = str(record.get("exportFile", ""))
        if declared:
            source = Path(declared).expanduser()
            path = source.resolve() if source.is_absolute() else (pack / source).resolve()
            try:
                path.relative_to(pack.resolve())
            except ValueError:
                path = Path()
            if path.is_file():
                return path
        name = str(record.get("name", ""))
        matches = [path for path in pack.rglob(f"{name}_Readable*") if path.suffix.casefold() in {".md", ".txt"}]
        return matches[0] if matches else None

    def assets(self, pack_path: str | None = None) -> tuple[Path, list[dict[str, Any]]]:
        pack = self.resolve_pack(pack_path)
        index_path = pack / "index.json"
        index = self._cached_json(index_path) if index_path.is_file() else {"assets": []}
        records = index.get("assets", [])
        if not isinstance(records, list):
            raise RaeError("INVALID_INDEX", "index.json assets must be an array", {"pack_id": pack.name})
        metadata_map = self._metadata_map(pack)
        output: list[dict[str, Any]] = []
        for raw in records:
            if not isinstance(raw, dict):
                continue
            record = dict(raw)
            match = metadata_map.get(str(record.get("objectPath", "")).casefold()) or metadata_map.get(str(record.get("name", "")).casefold())
            if match:
                record["_metadata_path"] = str(match[0])
                record["_metadata"] = match[1]
            readable = self._resolve_readable_file(pack, record)
            if readable:
                record["_readable_path"] = str(readable)
            output.append(record)
        return pack, output

    @staticmethod
    def asset_descriptor(record: dict[str, Any], metadata: dict[str, Any]) -> dict[str, Any]:
        return {
            "name": record.get("name", metadata.get("assetName", "")),
            "object_path": record.get("objectPath", metadata.get("objectPath", "")),
            "class_path": record.get("classPath", metadata.get("classPath", "")),
            "asset_kind": record.get("assetKind", metadata.get("assetKind", "")),
        }

    def find_asset(self, asset: str, pack_path: str | None = None) -> tuple[Path, dict[str, Any], dict[str, Any]]:
        pack, records = self.assets(pack_path)
        needle = asset.casefold().strip()
        exact = [record for record in records if needle in {str(record.get("name", "")).casefold(), str(record.get("objectPath", "")).casefold()}]
        candidates = exact or [record for record in records if needle in str(record.get("name", "")).casefold() or needle in str(record.get("objectPath", "")).casefold()]
        if not candidates:
            raise RaeError("ASSET_NOT_FOUND", f"Asset not found: {asset}", {"asset": asset})
        if len(candidates) > 1:
            names = [str(record.get("objectPath", record.get("name", ""))) for record in candidates[:20]]
            raise RaeError("ASSET_AMBIGUOUS", f"Asset query is ambiguous: {asset}", {"matches": names})
        record = candidates[0]
        return pack, record, record.get("_metadata", {})

    def evidence(self, pack: Path, path: Path, pointer: str = "", asset_path: str = "", item_id: str = "", line: int | None = None) -> dict[str, Any]:
        value: dict[str, Any] = {"source_file": self._relative(pack, path), "json_pointer": pointer}
        if asset_path:
            value["asset_path"] = asset_path
        if item_id:
            value["item_id"] = item_id
        if line is not None:
            value["line"] = line
        return value

    def search_assets(self, query: str, pack_path: str | None, offset: int, limit: int) -> dict[str, Any]:
        pack, records = self.assets(pack_path)
        needle = query.casefold().strip()
        matches: list[dict[str, Any]] = []
        for record in records:
            searchable = json.dumps({key: record.get(key) for key in ("name", "objectPath", "classPath", "assetKind", "parameters", "dependencies")}, ensure_ascii=False).casefold()
            if not needle or needle in searchable:
                metadata = record.get("_metadata", {})
                item = self.asset_descriptor(record, metadata)
                item.update({"parameter_count": len(record.get("parameters", [])), "dependency_count": len(record.get("dependencies", []))})
                matches.append(item)
        selected, page = paginate(matches, offset, limit, 100)
        return {
            "pack": pack,
            "data": {"assets": selected},
            "evidence": [self.evidence(pack, pack / "index.json", "/assets")],
            "page": page,
        }

    def summary(self, asset: str, pack_path: str | None = None) -> dict[str, Any]:
        pack, record, metadata = self.find_asset(asset, pack_path)
        graphs = metadata.get("graphs", []) if isinstance(metadata.get("graphs", []), list) else []
        renderers = metadata.get("niagaraRenderers", []) if isinstance(metadata.get("niagaraRenderers", []), list) else []
        curves = metadata.get("niagaraCurves", []) if isinstance(metadata.get("niagaraCurves", []), list) else []
        graph_summary = [
            {"id": graph.get("id", ""), "name": graph.get("name", ""), "kind": graph.get("kind", ""), "node_count": len(graph.get("nodes", [])), "link_count": len(graph.get("links", []))}
            for graph in graphs
            if isinstance(graph, dict)
        ]
        descriptor = self.asset_descriptor(record, metadata)
        data = {
            "feature_tags": metadata.get("featureTags", []),
            "artist_focus": metadata.get("artistFocus", ""),
            "suggested_prompt": metadata.get("suggestedPrompt", ""),
            "parameters": metadata.get("parameters", record.get("parameters", [])),
            "dependency_count": len(record.get("dependencies", metadata.get("dependencies", []))),
            "referencer_count": len(record.get("referencers", metadata.get("referencers", []))),
            "graphs": graph_summary,
            "renderer_count": len(renderers),
            "curve_count": len(curves),
        }
        metadata_path = Path(record["_metadata_path"]) if record.get("_metadata_path") else pack / "index.json"
        pointer = "" if record.get("_metadata_path") else "/assets"
        return {
            "pack": pack,
            "asset": descriptor,
            "data": data,
            "evidence": [self.evidence(pack, metadata_path, pointer, descriptor["object_path"])],
            "missing_fields": [] if metadata else ["metadata"],
        }

    @staticmethod
    def _select_item(values: Any, item_id: str | None, keys: tuple[str, ...], label: str) -> tuple[dict[str, Any], int]:
        if not item_id:
            raise RaeError("ITEM_ID_REQUIRED", f"item_id is required for section {label}", {"section": label})
        needle = item_id.casefold()
        indexed = [(index, value) for index, value in enumerate(values) if isinstance(value, dict)]
        matches = [(index, value) for index, value in indexed if any(needle == str(value.get(key, "")).casefold() for key in keys)]
        if not matches:
            matches = [(index, value) for index, value in indexed if any(needle in str(value.get(key, "")).casefold() for key in keys)]
        if len(matches) != 1:
            raise RaeError("ITEM_NOT_FOUND", f"Expected one {label} for {item_id}, found {len(matches)}", {"item_id": item_id, "matches": len(matches)})
        return matches[0][1], matches[0][0]

    def detail(self, asset: str, section: str, item_id: str | None, pack_path: str | None, offset: int, limit: int) -> dict[str, Any]:
        if section.casefold() == "summary":
            return self.summary(asset, pack_path)
        pack, record, metadata = self.find_asset(asset, pack_path)
        descriptor = self.asset_descriptor(record, metadata)
        normalized = section.casefold()
        metadata_path = Path(record["_metadata_path"]) if record.get("_metadata_path") else pack / "index.json"
        pointer = ""
        page = page_info()
        missing_fields: list[str] = []
        if normalized in {"parameters", "dependencies", "referencers"}:
            values = metadata.get(normalized, record.get(normalized, []))
            values = values if isinstance(values, list) else []
            data, page = paginate(values, offset, limit)
            pointer = f"/{normalized}"
            if normalized not in metadata and normalized not in record:
                missing_fields.append(normalized)
        elif normalized == "graphs":
            values = [
                {"id": value.get("id"), "name": value.get("name"), "kind": value.get("kind"), "node_count": len(value.get("nodes", [])), "link_count": len(value.get("links", []))}
                for value in metadata.get("graphs", [])
                if isinstance(value, dict)
            ]
            data, page = paginate(values, offset, limit)
            pointer = "/graphs"
        elif normalized == "graph":
            data, index = self._select_item(metadata.get("graphs", []), item_id, ("id", "name"), "graph")
            pointer = f"/graphs/{index}"
        elif normalized == "renderers":
            values = metadata.get("niagaraRenderers", [])
            values = values if isinstance(values, list) else []
            data, page = paginate(values, offset, limit)
            pointer = "/niagaraRenderers"
        elif normalized == "curves":
            values = metadata.get("niagaraCurves", [])
            values = values if isinstance(values, list) else []
            if item_id:
                data, index = self._select_item(values, item_id, ("id", "fingerprint", "exposedName", "objectPath"), "curve")
                pointer = f"/niagaraCurves/{index}"
            else:
                compact = [
                    {
                        "id": value.get("id"),
                        "fingerprint": value.get("fingerprint"),
                        "exposed_name": value.get("exposedName"),
                        "usage_count": value.get("usageCount", len(value.get("usedBy", []))),
                        "used_by": value.get("usedBy", []),
                        "min_time": value.get("minTime"),
                        "max_time": value.get("maxTime"),
                        "channels": [{"name": channel.get("name"), "key_count": len(channel.get("keys", []))} for channel in value.get("channels", []) if isinstance(channel, dict)],
                    }
                    for value in values
                    if isinstance(value, dict)
                ]
                data, page = paginate(compact, offset, limit)
                pointer = "/niagaraCurves"
        elif normalized == "metadata":
            data = metadata
            if not metadata:
                missing_fields.append("metadata")
        elif normalized == "readable":
            readable_path = record.get("_readable_path")
            if not readable_path:
                raise RaeError("READABLE_NOT_FOUND", f"Readable document not found for {asset}", {"asset": asset})
            path = Path(readable_path)
            text = path.read_text(encoding="utf-8-sig", errors="replace")
            start = max(0, offset)
            size = max(1, min(limit, MAX_OUTPUT_CHARS // 2))
            data = {"text": text[start : start + size]}
            page = page_info(start, size, len(data["text"]), len(text))
            metadata_path = path
        else:
            raise RaeError("INVALID_SECTION", f"Unknown detail section: {section}", {"section": section})
        return {
            "pack": pack,
            "asset": descriptor,
            "data": data,
            "evidence": [self.evidence(pack, metadata_path, pointer, descriptor["object_path"], item_id or "")],
            "page": page,
            "missing_fields": missing_fields,
        }

    def search_text(self, query: str, asset: str | None, pack_path: str | None, offset: int, limit: int) -> dict[str, Any]:
        if not query:
            raise RaeError("INVALID_ARGUMENT", "query must not be empty", {"argument": "query"})
        pack, records = self.assets(pack_path)
        if asset:
            _, selected_record, _ = self.find_asset(asset, str(pack))
            records = [selected_record]
        needle = query.casefold()
        hits: list[dict[str, Any]] = []
        evidence: list[dict[str, Any]] = []
        for record in records:
            descriptor = self.asset_descriptor(record, record.get("_metadata", {}))
            for raw_path in (record.get("_readable_path"), record.get("_metadata_path")):
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
                    hits.append({"asset": descriptor["name"], "file": self._relative(pack, path), "line": line_number, "snippet": line[start:end]})
                    evidence.append(self.evidence(pack, path, "", descriptor["object_path"], line=line_number))
        selected, page = paginate(hits, offset, limit, 100)
        selected_evidence = evidence[offset : offset + len(selected)]
        return {"pack": pack, "data": {"hits": selected}, "evidence": selected_evidence, "page": page}


OUTPUT_SCHEMA = {
    "type": "object",
    "required": ["protocol_version", "server", "pack", "asset", "data", "evidence", "page", "warnings", "missing_fields", "error"],
    "properties": {
        "protocol_version": {"type": "string"},
        "server": {"type": "object"},
        "pack": {"type": ["object", "null"]},
        "asset": {"type": ["object", "null"]},
        "data": {},
        "evidence": {"type": "array"},
        "page": {"type": "object"},
        "warnings": {"type": "array"},
        "missing_fields": {"type": "array"},
        "error": {"type": ["object", "null"]},
    },
}


def tool(name: str, description: str, properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    return {
        "name": name,
        "description": description,
        "inputSchema": {"type": "object", "properties": properties, **({"required": required} if required else {})},
        "outputSchema": OUTPUT_SCHEMA,
        "annotations": {"readOnlyHint": True, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False},
    }


PACK_ARG = {"type": "string", "description": "Context Pack path or pack_id. Omit to use the latest complete pack."}
OFFSET_ARG = {"type": "integer", "minimum": 0, "default": 0}
TOOLS = [
    tool("list_context_packs", "List recent Context Packs with manifest fingerprints and completion state.", {"offset": OFFSET_ARG, "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}}),
    tool("search_assets", "Search assets and return compact results with index evidence.", {"query": {"type": "string", "default": ""}, "pack_path": PACK_ARG, "offset": OFFSET_ARG, "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}}),
    tool("get_asset_summary", "Get a compact asset overview before requesting detail.", {"asset": {"type": "string"}, "pack_path": PACK_ARG}, ["asset"]),
    tool(
        "get_asset_detail",
        "Read one precise asset section. List and text sections support offset/limit pagination.",
        {
            "asset": {"type": "string"},
            "section": {"type": "string", "enum": ["summary", "parameters", "dependencies", "referencers", "graphs", "graph", "renderers", "curves", "metadata", "readable"]},
            "item_id": {"type": "string"},
            "pack_path": PACK_ARG,
            "offset": OFFSET_ARG,
            "limit": {"type": "integer", "minimum": 1, "maximum": 60000, "default": 100},
        },
        ["asset", "section"],
    ),
    tool("search_export_text", "Search readable documents and metadata without loading complete files.", {"query": {"type": "string", "minLength": 1}, "asset": {"type": "string"}, "pack_path": PACK_ARG, "offset": OFFSET_ARG, "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}}, ["query"]),
]


def envelope(store: ContextPackStore, payload: dict[str, Any], error: dict[str, Any] | None = None) -> dict[str, Any]:
    pack = payload.get("pack")
    pack_value = None
    warnings = list(payload.get("warnings", []))
    if isinstance(pack, Path):
        pack_value, pack_warnings = store.pack_info(pack)
        warnings.extend(pack_warnings)
    return {
        "protocol_version": CONTRACT_VERSION,
        "server": {"name": SERVER_NAME, "version": SERVER_VERSION},
        "pack": pack_value,
        "asset": payload.get("asset"),
        "data": payload.get("data"),
        "evidence": payload.get("evidence", []),
        "page": payload.get("page", page_info()),
        "warnings": warnings,
        "missing_fields": payload.get("missing_fields", []),
        "error": error,
    }


def error_envelope(store: ContextPackStore, exc: Exception) -> dict[str, Any]:
    if isinstance(exc, RaeError):
        error = {"code": exc.code, "message": exc.message, "details": exc.details}
    else:
        error = {"code": "INTERNAL_ERROR", "message": str(exc), "details": {"type": type(exc).__name__}}
    return envelope(store, {"data": None}, error)


def mcp_result(value: dict[str, Any], is_error: bool = False) -> dict[str, Any]:
    text = json_text(value)
    if len(text) > MAX_OUTPUT_CHARS:
        error = {
            "protocol_version": CONTRACT_VERSION,
            "server": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "pack": value.get("pack"),
            "asset": value.get("asset"),
            "data": None,
            "evidence": [],
            "page": value.get("page", page_info()),
            "warnings": value.get("warnings", []),
            "missing_fields": value.get("missing_fields", []),
            "error": {"code": "RESULT_TOO_LARGE", "message": "Result exceeds the output limit; request a smaller page or narrower section.", "details": {"characters": len(text), "limit": MAX_OUTPUT_CHARS}},
        }
        return mcp_result(error, True)
    result: dict[str, Any] = {"content": [{"type": "text", "text": text}], "structuredContent": value}
    if is_error:
        result["isError"] = True
    return result


def call_tool(store: ContextPackStore, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    offset = int(arguments.get("offset", 0))
    limit = int(arguments.get("limit", 20))
    if name == "list_context_packs":
        payload = store.list_packs(offset, limit)
    elif name == "search_assets":
        payload = store.search_assets(str(arguments.get("query", "")), arguments.get("pack_path"), offset, limit)
    elif name == "get_asset_summary":
        payload = store.summary(str(arguments["asset"]), arguments.get("pack_path"))
    elif name == "get_asset_detail":
        payload = store.detail(str(arguments["asset"]), str(arguments["section"]), arguments.get("item_id"), arguments.get("pack_path"), offset, int(arguments.get("limit", 100)))
    elif name == "search_export_text":
        payload = store.search_text(str(arguments["query"]), arguments.get("asset"), arguments.get("pack_path"), offset, limit)
    else:
        raise RaeError("TOOL_NOT_FOUND", f"Unknown tool: {name}", {"tool": name})
    return envelope(store, payload)


def response(request_id: Any, result: Any = None, error: dict[str, Any] | None = None) -> dict[str, Any]:
    message: dict[str, Any] = {"jsonrpc": "2.0", "id": request_id}
    message["error" if error is not None else "result"] = error if error is not None else result
    return message


def handle_request(store: ContextPackStore, request: dict[str, Any]) -> dict[str, Any] | None:
    method = request.get("method")
    request_id = request.get("id")
    if request_id is None:
        return None
    if method == "initialize":
        requested = request.get("params", {}).get("protocolVersion") or DEFAULT_PROTOCOL_VERSION
        if requested not in SUPPORTED_PROTOCOL_VERSIONS:
            return response(request_id, error={"code": -32602, "message": "Unsupported MCP protocol version", "data": {"errorCode": "PROTOCOL_UNSUPPORTED", "requested": requested, "supported": list(SUPPORTED_PROTOCOL_VERSIONS)}})
        return response(
            request_id,
            {
                "protocolVersion": requested,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "instructions": "Use search_assets, then get_asset_summary, then request only the required detail. Cite evidence and inspect missing_fields before drawing conclusions.",
            },
        )
    if method == "ping":
        return response(request_id, {})
    if method == "tools/list":
        return response(request_id, {"tools": TOOLS})
    if method == "tools/call":
        params = request.get("params", {})
        arguments = params.get("arguments") or {}
        if not isinstance(arguments, dict):
            return response(request_id, mcp_result(error_envelope(store, RaeError("INVALID_ARGUMENT", "arguments must be an object")), True))
        try:
            return response(request_id, mcp_result(call_tool(store, str(params.get("name", "")), arguments)))
        except Exception as exc:
            return response(request_id, mcp_result(error_envelope(store, exc), True))
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
    parser = argparse.ArgumentParser(description="Read-only MCP server for ReadAllandExplains Context Packs")
    parser.add_argument("--root", type=Path, default=default_export_root(), help="ReadAllandExplainsExports directory or its ContextPacks child")
    parser.add_argument("--self-test", action="store_true", help="Print the latest-pack envelope and exit")
    args = parser.parse_args()
    store = ContextPackStore(args.root)
    if args.self_test:
        print(json_text(envelope(store, store.list_packs(0, 1))))
        return
    serve(store)


if __name__ == "__main__":
    main()
