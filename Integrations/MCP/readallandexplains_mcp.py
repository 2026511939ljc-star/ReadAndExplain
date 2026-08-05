from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SERVER_NAME = "readallandexplains"
SERVER_VERSION = "0.5.0"
CONTRACT_VERSION = "rae.mcp/1.0"
SUPPORTED_PROTOCOL_VERSIONS = ("2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25")
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


def load_text(path: Path) -> str:
    try:
        return path.read_bytes().decode("utf-8-sig")
    except FileNotFoundError as exc:
        raise RaeError("FILE_NOT_FOUND", f"Text file not found: {path}", {"path": str(path)}) from exc
    except UnicodeDecodeError as exc:
        raise RaeError(
            "TEXT_ENCODING_INVALID",
            f"Text file is not valid UTF-8: {path}",
            {"path": str(path), "start": exc.start, "end": exc.end},
        ) from exc


def json_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2)


def _workspace_roots() -> list[Path]:
    roots: list[Path] = []
    configured_keys = (
        "CODEBUDDY_PROJECT_DIR",
        "CODEBUDDY_WORKSPACE_ROOT",
        "CLAUDE_PROJECT_DIR",
        "CODEBUDDY_WORKSPACE",
        "CODEBUDDY_WORKSPACE_FOLDER",
        "WORKSPACE_FOLDER",
        "INIT_CWD",
    )
    for key in configured_keys:
        configured = os.environ.get(key, "").strip()
        if configured:
            roots.append(Path(configured).expanduser())
    current = Path.cwd()
    roots.extend([current, *current.parents])
    unique: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        try:
            resolved = root.resolve()
        except OSError:
            continue
        marker = str(resolved).casefold()
        if marker not in seen:
            seen.add(marker)
            unique.append(resolved)
    return unique


def _nested_ue_export_roots(workspace: Path, maximum_depth: int = 4) -> list[Path]:
    if not workspace.is_dir():
        return []
    ignored = {".git", ".codebuddy", "binaries", "deriveddatacache", "intermediate", "node_modules", "saved"}
    output: list[Path] = []
    pending: list[tuple[Path, int]] = [(workspace, 0)]
    visited = 0
    while pending and visited < 2000:
        directory, depth = pending.pop(0)
        visited += 1
        try:
            children = list(directory.iterdir())
        except OSError:
            continue
        if any(child.is_file() and child.suffix.casefold() == ".uproject" for child in children):
            output.append(directory / "Saved" / "ReadAllandExplainsExports")
        if depth >= maximum_depth:
            continue
        for child in children:
            if child.is_dir() and child.name.casefold() not in ignored:
                pending.append((child, depth + 1))
    return output


def export_root_candidates() -> list[Path]:
    configured = os.environ.get("READALL_EXPORT_ROOT", "").strip()
    if configured:
        return [Path(configured).expanduser()]
    candidates: list[Path] = []
    workspaces = _workspace_roots()
    for workspace in workspaces:
        candidates.append(workspace / "Saved" / "ReadAllandExplainsExports")
    for workspace in workspaces[:2]:
        candidates.extend(_nested_ue_export_roots(workspace))
    for parent in Path(__file__).resolve().parents:
        candidates.append(parent / "Saved" / "ReadAllandExplainsExports")
    unique: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        marker = str(resolved).casefold()
        if marker not in seen:
            seen.add(marker)
            unique.append(resolved)
    return unique


def default_export_root() -> Path:
    candidates = export_root_candidates()
    for candidate in candidates:
        packs_root = candidate if candidate.name.casefold() == "contextpacks" else candidate / "ContextPacks"
        if packs_root.is_dir() and any((path / "context-pack.json").is_file() for path in packs_root.iterdir() if path.is_dir()):
            return candidate
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def diagnostics(root: Path) -> dict[str, Any]:
    resolved = root.expanduser().resolve()
    packs_root = resolved if resolved.name.casefold() == "contextpacks" else resolved / "ContextPacks"
    packs = []
    if packs_root.is_dir():
        packs = sorted(path.name for path in packs_root.iterdir() if path.is_dir() and (path / "context-pack.json").is_file())
    return {
        "server": {"name": SERVER_NAME, "version": SERVER_VERSION},
        "python": sys.version.split()[0],
        "working_directory": str(Path.cwd()),
        "export_root": str(resolved),
        "export_root_exists": resolved.is_dir(),
        "context_packs_root": str(packs_root),
        "context_pack_count": len(packs),
        "context_packs": packs,
        "environment_override": os.environ.get("READALL_EXPORT_ROOT") or None,
        "candidate_roots": [str(path) for path in export_root_candidates()],
    }


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
        if resolved.name.casefold().endswith(".tmp"):
            raise RaeError("PACK_INCOMPLETE", "Temporary Context Pack directories are never readable", {"pack_path": str(resolved)})
        if not resolved.is_dir():
            raise RaeError("PACK_NOT_FOUND", f"Context Pack not found: {resolved}", {"pack_path": str(resolved)})
        if not (resolved / "context-pack.json").is_file():
            raise RaeError("PACK_INCOMPLETE", f"context-pack.json not found in {resolved}", {"pack_path": str(resolved)})
        return resolved

    def _manifest(self, pack: Path, validate: bool = True) -> dict[str, Any]:
        manifest = self._cached_json(pack / "context-pack.json")
        if validate:
            modern = "attemptedAssetCount" in manifest
            state = manifest.get("state")
            if (modern and state != "complete") or (not modern and state not in (None, "complete")):
                raise RaeError("PACK_INCOMPLETE", f"Context Pack state is {state!r}", {"pack_id": pack.name, "state": state})
            schema = manifest.get("schemaVersion")
            if schema not in SUPPORTED_PACK_SCHEMAS:
                raise RaeError(
                    "SCHEMA_UNSUPPORTED",
                    f"Unsupported Context Pack schema: {schema}",
                    {"pack_id": pack.name, "supported": sorted(SUPPORTED_PACK_SCHEMAS)},
                )
            if modern and manifest.get("documentType") != "ReadAllandExplainsContextPack":
                raise RaeError("PACK_INCOMPLETE", "Modern Context Pack documentType is invalid", {"pack_id": pack.name})
            declared_pack_id = manifest.get("packId")
            if (modern and declared_pack_id != pack.name) or (not modern and declared_pack_id is not None and declared_pack_id != pack.name):
                raise RaeError("PACK_INCOMPLETE", "Manifest packId does not match its directory", {"pack_id": pack.name})
            for count_field in ("failedCount", "skippedCount"):
                count = manifest.get(count_field)
                if (modern and (not isinstance(count, int) or isinstance(count, bool) or count != 0)) or (
                    not modern and count is not None and (not isinstance(count, (int, float)) or isinstance(count, bool) or count != 0)
                ):
                    raise RaeError(
                        "PACK_INCOMPLETE",
                        f"Context Pack has {count_field}={count!r}",
                        {"pack_id": pack.name, count_field: count},
                    )
            files = manifest.get("files")
            if modern and not isinstance(files, list):
                raise RaeError("PACK_INCOMPLETE", "Modern Context Pack files must be an array", {"pack_id": pack.name})
            if isinstance(files, list):
                pack_root = pack.resolve()
                declared_paths: set[str] = set()
                fingerprint_source: list[str] = []
                for entry in files:
                    if not isinstance(entry, dict) or not entry.get("path"):
                        raise RaeError("PACK_INCOMPLETE", "Manifest contains an invalid file entry", {"pack_id": pack.name})
                    relative_path = str(entry["path"]).replace("\\", "/")
                    path_key = relative_path.casefold()
                    if path_key in declared_paths:
                        raise RaeError("PACK_INCOMPLETE", "Manifest contains a duplicate file path", {"path": relative_path})
                    declared_paths.add(path_key)
                    file_path = (pack / relative_path).resolve()
                    try:
                        file_path.relative_to(pack_root)
                    except ValueError as exc:
                        raise RaeError("PACK_INCOMPLETE", "Manifest file escapes the Context Pack", {"path": relative_path}) from exc
                    if not file_path.is_file():
                        raise RaeError("PACK_INCOMPLETE", "Manifest file is missing", {"path": relative_path})
                    expected_size = entry.get("size")
                    if not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size < 0:
                        raise RaeError("PACK_INCOMPLETE", "Manifest file size is invalid", {"path": relative_path})
                    if file_path.stat().st_size != expected_size:
                        raise RaeError(
                            "PACK_INCOMPLETE",
                            "Manifest file size does not match",
                            {"path": relative_path, "expected": expected_size, "actual": file_path.stat().st_size},
                        )
                    fingerprint = entry.get("fingerprint")
                    if modern:
                        if not isinstance(fingerprint, str) or not fingerprint.startswith("sha1:"):
                            raise RaeError("PACK_INCOMPLETE", "Modern Manifest file fingerprint is invalid", {"path": relative_path})
                        actual_fingerprint = "sha1:" + hashlib.sha1(file_path.read_bytes()).hexdigest()
                        if fingerprint.casefold() != actual_fingerprint:
                            raise RaeError(
                                "PACK_INCOMPLETE",
                                "Manifest file fingerprint does not match",
                                {"path": relative_path, "expected": fingerprint, "actual": actual_fingerprint},
                            )
                        fingerprint_source.append(f"{relative_path}:{expected_size}:{actual_fingerprint.removeprefix('sha1:')}\n")
                disk_paths = {
                    path.relative_to(pack).as_posix().casefold()
                    for path in pack.rglob("*")
                    if path.is_file() and path.relative_to(pack).as_posix().casefold() != "context-pack.json"
                }
                if declared_paths != disk_paths:
                    raise RaeError("PACK_INCOMPLETE", "Manifest file list does not match the Context Pack", {"pack_id": pack.name})
                if modern:
                    required_files = {"readme.md", "index.md", "index.json"}
                    if not required_files.issubset(disk_paths):
                        raise RaeError("PACK_INCOMPLETE", "Modern Context Pack is missing required files", {"pack_id": pack.name})
                    expected_pack_fingerprint = "sha1:" + hashlib.sha1("".join(fingerprint_source).encode("utf-8")).hexdigest()
                    if str(manifest.get("fingerprint", "")).casefold() != expected_pack_fingerprint:
                        raise RaeError(
                            "PACK_INCOMPLETE",
                            "Context Pack fingerprint does not match its file manifest",
                            {"pack_id": pack.name, "expected": expected_pack_fingerprint, "actual": manifest.get("fingerprint")},
                        )
            manifest_assets = manifest.get("assets")
            index_path = pack / "index.json"
            if modern:
                attempted = manifest.get("attemptedAssetCount")
                exported = manifest.get("exportedAssetCount")
                roots = manifest.get("rootAssets")
                if not isinstance(manifest_assets, list) or not index_path.is_file():
                    raise RaeError("PACK_INCOMPLETE", "Modern Context Pack requires Manifest assets and index.json", {"pack_id": pack.name})
                if not isinstance(attempted, int) or isinstance(attempted, bool) or attempted <= 0:
                    raise RaeError("PACK_INCOMPLETE", "Modern Context Pack attemptedAssetCount is invalid", {"pack_id": pack.name})
                if not isinstance(exported, int) or isinstance(exported, bool) or exported != attempted or exported != len(manifest_assets):
                    raise RaeError("PACK_INCOMPLETE", "Modern Context Pack asset counts do not match", {"pack_id": pack.name})
                if not isinstance(roots, list) or not roots or not all(isinstance(item, str) and item.startswith("/Game/") for item in roots):
                    raise RaeError("PACK_INCOMPLETE", "Modern Context Pack rootAssets are invalid", {"pack_id": pack.name})
            if isinstance(manifest_assets, list) and index_path.is_file():
                index = self._cached_json(index_path)
                index_assets = index.get("assets")
                if not isinstance(index_assets, list):
                    raise RaeError("PACK_INCOMPLETE", "index.json assets must be an array", {"pack_id": pack.name})
                if modern:
                    asset_count = index.get("assetCount")
                    if not isinstance(asset_count, int) or isinstance(asset_count, bool) or asset_count != len(index_assets):
                        raise RaeError("PACK_INCOMPLETE", "Modern index assetCount does not match assets", {"pack_id": pack.name})

                def asset_map(items: list[Any], label: str) -> dict[str, tuple[str, str]]:
                    result: dict[str, tuple[str, str]] = {}
                    file_keys: set[str] = set()
                    metadata_keys: set[str] = set()
                    for item in items:
                        if not isinstance(item, dict):
                            raise RaeError("PACK_INCOMPLETE", f"{label} contains an invalid asset entry", {"pack_id": pack.name})
                        object_path = item.get("objectPath")
                        export_file = item.get("exportFile")
                        metadata_file = item.get("metadataFile", "")
                        if not isinstance(object_path, str) or not isinstance(export_file, str) or not export_file:
                            raise RaeError("PACK_INCOMPLETE", f"{label} contains an invalid asset mapping", {"pack_id": pack.name})
                        if modern and (not isinstance(metadata_file, str) or not metadata_file):
                            raise RaeError("PACK_INCOMPLETE", f"{label} requires an explicit metadataFile", {"object_path": object_path})
                        if object_path in result:
                            raise RaeError("PACK_INCOMPLETE", f"{label} contains duplicate objectPath values", {"object_path": object_path})
                        export_relative = export_file.replace("\\", "/")
                        metadata_relative = str(metadata_file).replace("\\", "/")
                        export_path = (pack / export_relative).resolve()
                        try:
                            export_path.relative_to(pack.resolve())
                        except ValueError as exc:
                            raise RaeError("PACK_INCOMPLETE", f"{label} exportFile escapes the Context Pack", {"path": export_file}) from exc
                        if not export_path.is_file():
                            raise RaeError("PACK_INCOMPLETE", f"{label} exportFile is missing", {"path": export_file})
                        export_key = export_relative.casefold()
                        if export_key in file_keys:
                            raise RaeError("PACK_INCOMPLETE", f"{label} contains duplicate exportFile paths", {"path": export_file})
                        file_keys.add(export_key)
                        if metadata_relative:
                            metadata_path = (pack / metadata_relative).resolve()
                            try:
                                metadata_path.relative_to(pack.resolve())
                            except ValueError as exc:
                                raise RaeError("PACK_INCOMPLETE", f"{label} metadataFile escapes the Context Pack", {"path": metadata_file}) from exc
                            if not metadata_path.is_file():
                                raise RaeError("PACK_INCOMPLETE", f"{label} metadataFile is missing", {"path": metadata_file})
                            metadata = self._cached_json(metadata_path)
                            if metadata.get("objectPath") != object_path:
                                raise RaeError("PACK_INCOMPLETE", f"{label} metadata objectPath does not match", {"path": metadata_file})
                            metadata_key = metadata_relative.casefold()
                            if metadata_key in metadata_keys:
                                raise RaeError("PACK_INCOMPLETE", f"{label} contains duplicate metadataFile paths", {"path": metadata_file})
                            metadata_keys.add(metadata_key)
                        result[object_path] = (export_relative, metadata_relative)
                    return result

                manifest_map = asset_map(manifest_assets, "Manifest")
                index_map = asset_map(index_assets, "Index")
                if manifest_map != index_map:
                    raise RaeError("PACK_INCOMPLETE", "Manifest and index asset mappings do not match", {"pack_id": pack.name})
                if modern:
                    missing_roots = sorted(set(manifest["rootAssets"]) - set(manifest_map))
                    if missing_roots:
                        raise RaeError("PACK_INCOMPLETE", "Modern Context Pack root assets were not exported", {"missing_roots": missing_roots})
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
                "origin_request_id": manifest.get("originRequestId"),
                "base_pack_id": manifest.get("basePackId"),
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

    @staticmethod
    def _safe_pack_member(pack: Path, path: Path) -> Path:
        try:
            attributes = path.lstat().st_file_attributes
        except AttributeError:
            attributes = 0
        except OSError as exc:
            raise RaeError("PACK_INCOMPLETE", "Pack member is unavailable", {"path": str(path)}) from exc
        if path.is_symlink() or attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise RaeError("PACK_INCOMPLETE", "Pack links and reparse points are not supported", {"path": str(path)})
        resolved = path.resolve()
        try:
            resolved.relative_to(pack.resolve())
        except ValueError as exc:
            raise RaeError("PACK_INCOMPLETE", "Pack file resolves outside its root", {"path": str(path)}) from exc
        if not resolved.is_file():
            raise RaeError("PACK_INCOMPLETE", "Pack member is not a regular file", {"path": str(path)})
        return resolved

    def _metadata_map(self, pack: Path) -> dict[str, tuple[Path, dict[str, Any]]]:
        result: dict[str, tuple[Path, dict[str, Any]]] = {}
        for path in pack.rglob("*.meta.json"):
            path = self._safe_pack_member(pack, path)
            try:
                metadata = self._cached_json(path)
            except (OSError, RaeError):
                continue
            keys = (metadata.get("objectPath"), metadata.get("assetName"), path.stem.removesuffix(".meta"))
            for key in keys:
                if key:
                    result[str(key).casefold()] = (path, metadata)
        return result

    def _resolve_metadata_file(self, pack: Path, record: dict[str, Any]) -> tuple[Path, dict[str, Any]] | None:
        declared = record.get("metadataFile")
        if not declared:
            return None
        path = self._safe_pack_member(pack, pack / str(declared))
        metadata = self._cached_json(path)
        object_path = str(record.get("objectPath", ""))
        if object_path and metadata.get("objectPath") != object_path:
            raise RaeError("PACK_INCOMPLETE", "Metadata objectPath does not match the index", {"path": str(declared)})
        return path, metadata

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
                return self._safe_pack_member(pack, path)
        name = str(record.get("name", ""))
        matches = [
            self._safe_pack_member(pack, path)
            for path in pack.rglob(f"{name}_Readable*")
            if path.suffix.casefold() in {".md", ".txt"}
        ]
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
            match = self._resolve_metadata_file(pack, record)
            if match is None:
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

    @staticmethod
    def _dependency_path(value: Any) -> str:
        if isinstance(value, dict):
            for key in ("objectPath", "assetPath", "path", "name"):
                if value.get(key):
                    return str(value[key]).strip()
            return ""
        return str(value).strip()

    @staticmethod
    def _asset_path_keys(value: str) -> set[str]:
        raw = value.strip().replace("\\", "/")
        if "'" in raw:
            quoted = [part for part in raw.split("'") if part.startswith("/")]
            if quoted:
                raw = quoted[-1]
        raw = raw.strip("\"'")
        if not raw:
            return set()
        keys = {raw.casefold()}
        tail = raw.rsplit("/", 1)[-1]
        if raw.startswith("/"):
            if "." in tail:
                keys.add(raw.rsplit(".", 1)[0].casefold())
        else:
            keys.add(tail.casefold())
        return keys

    def dependency_coverage(self, pack: Path, record: dict[str, Any], metadata: dict[str, Any]) -> dict[str, Any]:
        _, records = self.assets(str(pack))
        exported_lookup: dict[str, dict[str, Any]] = {}
        for candidate in records:
            candidate_metadata = candidate.get("_metadata", {})
            descriptor = self.asset_descriptor(candidate, candidate_metadata)
            for value in (descriptor["object_path"], descriptor["name"]):
                for key in self._asset_path_keys(str(value)):
                    exported_lookup[key] = descriptor

        raw_dependencies = metadata.get("dependencies", record.get("dependencies", []))
        dependencies = raw_dependencies if isinstance(raw_dependencies, list) else []
        exported: list[dict[str, Any]] = []
        missing_project: list[dict[str, Any]] = []
        external: list[dict[str, Any]] = []
        seen: set[str] = set()
        for value in dependencies:
            dependency = self._dependency_path(value)
            marker = dependency.casefold()
            if not dependency or marker in seen:
                continue
            seen.add(marker)
            match = next((exported_lookup[key] for key in self._asset_path_keys(dependency) if key in exported_lookup), None)
            if match:
                exported.append({"path": dependency, "asset": match})
            elif any(key.startswith("/game/") for key in self._asset_path_keys(dependency)):
                missing_project.append({"path": dependency, "reason": "not_present_in_context_pack"})
            else:
                external.append({"path": dependency, "reason": "outside_project_snapshot_scope"})

        return {
            "metadata_available": bool(metadata),
            "readable_available": bool(record.get("_readable_path")),
            "dependency_total": len(exported) + len(missing_project) + len(external),
            "exported_dependency_count": len(exported),
            "missing_project_dependency_count": len(missing_project),
            "external_dependency_count": len(external),
            "exported_dependencies": exported,
            "missing_project_dependencies": missing_project,
            "external_dependencies": external,
            "suggested_capture": [
                {
                    "asset_path": item["path"],
                    "reason": "Direct /Game/ dependency is not present in the selected Context Pack.",
                    "priority": "blocking_if_required_by_question",
                }
                for item in missing_project
            ],
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
        coverage = self.dependency_coverage(pack, record, metadata)
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
            "coverage": {
                "metadata_available": coverage["metadata_available"],
                "readable_available": coverage["readable_available"],
                "exported_dependency_count": coverage["exported_dependency_count"],
                "missing_project_dependency_count": coverage["missing_project_dependency_count"],
                "external_dependency_count": coverage["external_dependency_count"],
            },
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
        elif normalized == "coverage":
            data = self.dependency_coverage(pack, record, metadata)
            pointer = "/dependencies" if "dependencies" in metadata else "/assets"
            if data["missing_project_dependency_count"]:
                missing_fields.append("dependency_assets")
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
            text = load_text(path)
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
                    lines = load_text(path).splitlines()
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

    @property
    def synclive_root(self) -> Path:
        return self.export_root / "SyncLive"

    @staticmethod
    def _validate_request_id(request_id: str) -> str:
        value = request_id.strip()
        allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
        if not value or len(value) > 64 or any(character not in allowed for character in value):
            raise RaeError("REQUEST_ID_INVALID", "request_id must contain 1-64 ASCII letters, numbers, '-' or '_'.")
        return value

    def _synclive_request_locations(self, request_id: str) -> dict[str, Path]:
        filename = f"{request_id}.json"
        return {
            "pending": self.synclive_root / "Pending" / filename,
            "processing": self.synclive_root / "Processing" / filename,
            "result": self.synclive_root / "Results" / filename,
            "archive": self.synclive_root / "Archive" / filename,
        }

    @staticmethod
    def _write_json_atomic(path: Path, value: dict[str, Any]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(path.name + ".tmp")
        try:
            temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
            os.replace(temporary, path)
        except OSError as exc:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass
            raise RaeError("REQUEST_WRITE_FAILED", f"Could not write SyncLive request: {path}", {"path": str(path)}) from exc

    def synclive_status(self, request_id: str) -> dict[str, Any]:
        validated_id = self._validate_request_id(request_id)
        locations = self._synclive_request_locations(validated_id)
        if locations["result"].is_file():
            result = load_json(locations["result"])
            output_pack_id = str(result.get("outputPackId", ""))
            output_pack: Path | None = None
            warnings: list[dict[str, str]] = []
            if result.get("state") == "complete" and output_pack_id:
                try:
                    output_pack = self.resolve_pack(output_pack_id)
                except RaeError as exc:
                    warnings.append({"code": "OUTPUT_PACK_UNAVAILABLE", "message": exc.message})
            return {
                "pack": output_pack,
                "data": {"request": result, "queue": "results"},
                "evidence": [{"source_file": str(locations["result"].relative_to(self.export_root)).replace("\\", "/"), "json_pointer": ""}],
                "warnings": warnings,
            }
        for state in ("processing", "pending"):
            path = locations[state]
            if path.is_file():
                request = load_json(path)
                return {
                    "pack": None,
                    "data": {"request": request, "state": state, "queue": state},
                    "evidence": [{"source_file": str(path.relative_to(self.export_root)).replace("\\", "/"), "json_pointer": ""}],
                }
        if locations["archive"].is_file():
            request = load_json(locations["archive"])
            return {
                "pack": None,
                "data": {"request": request, "state": "archived_without_result", "queue": "archive"},
                "evidence": [{"source_file": str(locations["archive"].relative_to(self.export_root)).replace("\\", "/"), "json_pointer": ""}],
                "warnings": [{"code": "RESULT_MISSING", "message": "The request was archived but its result file is missing."}],
            }
        raise RaeError("REQUEST_NOT_FOUND", f"SyncLive request not found: {validated_id}", {"request_id": validated_id})

    def submit_synclive_request(
        self,
        asset_paths: Any,
        base_pack_fingerprint: str,
        permission_granted: bool,
        dependency_depth: int = 0,
        pack_path: str | None = None,
        request_id: str | None = None,
        reason: str = "",
    ) -> dict[str, Any]:
        if permission_granted is not True:
            raise RaeError("PERMISSION_REQUIRED", "Explicit user permission is required before submitting a targeted snapshot request.")
        if not isinstance(asset_paths, list) or not 1 <= len(asset_paths) <= 5:
            raise RaeError("ASSET_LIMIT_INVALID", "asset_paths must contain between 1 and 5 assets.")
        if isinstance(dependency_depth, bool) or not isinstance(dependency_depth, int) or not 0 <= dependency_depth <= 1:
            raise RaeError("DEPENDENCY_DEPTH_INVALID", "dependency_depth must be the integer 0 or 1.")

        normalized_assets: list[str] = []
        seen: set[str] = set()
        for raw in asset_paths:
            if not isinstance(raw, str):
                raise RaeError("ASSET_PATH_INVALID", "Every asset path must be a string.")
            asset_path = raw.strip()
            tail = asset_path.rsplit("/", 1)[-1]
            if not asset_path.startswith("/Game/") or ".." in asset_path or "\\" in asset_path or "\n" in asset_path or "\r" in asset_path or "." not in tail:
                raise RaeError("ASSET_PATH_INVALID", f"Only canonical /Game/ object paths are allowed: {asset_path!r}")
            marker = asset_path.casefold()
            if marker not in seen:
                seen.add(marker)
                normalized_assets.append(asset_path)
        if not normalized_assets:
            raise RaeError("ASSET_LIMIT_INVALID", "At least one unique asset path is required.")

        base_pack = self.resolve_pack(pack_path)
        base_info, warnings = self.pack_info(base_pack)
        expected_fingerprint = str(base_pack_fingerprint).strip()
        if not expected_fingerprint or expected_fingerprint != base_info["fingerprint"]:
            raise RaeError(
                "BASE_PACK_FINGERPRINT_MISMATCH",
                "The supplied base Pack fingerprint does not match the selected complete Pack.",
                {"pack_id": base_info["pack_id"], "expected": base_info["fingerprint"], "received": expected_fingerprint},
            )

        validated_id = self._validate_request_id(request_id or f"capture_{uuid.uuid4().hex}")
        locations = self._synclive_request_locations(validated_id)
        request = {
            "schemaVersion": 1,
            "requestId": validated_id,
            "createdUtc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "mode": "targeted_context_pack",
            "permissionGranted": True,
            "assetPaths": normalized_assets,
            "dependencyDepth": dependency_depth,
            "basePackId": base_info["pack_id"],
            "basePackFingerprint": base_info["fingerprint"],
            "reason": str(reason).strip()[:500],
        }

        for state in ("result", "processing", "pending", "archive"):
            path = locations[state]
            if not path.is_file():
                continue
            if state == "result":
                return self.synclive_status(validated_id)
            existing = load_json(path)
            comparable = {key: existing.get(key) for key in request if key != "createdUtc"}
            expected = {key: value for key, value in request.items() if key != "createdUtc"}
            if comparable == expected:
                return {
                    "pack": base_pack,
                    "data": {"request_id": validated_id, "state": state, "request": existing, "idempotent": True},
                    "warnings": warnings,
                }
            raise RaeError("REQUEST_ID_CONFLICT", f"request_id already exists with different content: {validated_id}", {"state": state})

        self._write_json_atomic(locations["pending"], request)
        return {
            "pack": base_pack,
            "data": {
                "request_id": validated_id,
                "state": "pending",
                "request_file": str(locations["pending"]),
                "asset_count": len(normalized_assets),
                "dependency_depth": dependency_depth,
                "limits": {"max_assets": 5, "max_dependency_depth": 1},
            },
            "evidence": [{"source_file": str(locations["pending"].relative_to(self.export_root)).replace("\\", "/"), "json_pointer": ""}],
            "warnings": warnings,
        }


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


def tool(
    name: str,
    description: str,
    properties: dict[str, Any],
    required: list[str] | None = None,
    *,
    read_only: bool = True,
    idempotent: bool = True,
) -> dict[str, Any]:
    return {
        "name": name,
        "description": description,
        "inputSchema": {"type": "object", "properties": properties, **({"required": required} if required else {})},
        "outputSchema": OUTPUT_SCHEMA,
        "annotations": {"readOnlyHint": read_only, "destructiveHint": False, "idempotentHint": idempotent, "openWorldHint": False},
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
            "section": {"type": "string", "enum": ["summary", "parameters", "coverage", "dependencies", "referencers", "graphs", "graph", "renderers", "curves", "metadata", "readable"]},
            "item_id": {"type": "string"},
            "pack_path": PACK_ARG,
            "offset": OFFSET_ARG,
            "limit": {"type": "integer", "minimum": 1, "maximum": 60000, "default": 100},
        },
        ["asset", "section"],
    ),
    tool("search_export_text", "Search readable documents and metadata without loading complete files.", {"query": {"type": "string", "minLength": 1}, "asset": {"type": "string"}, "pack_path": PACK_ARG, "offset": OFFSET_ARG, "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 20}}, ["query"]),
    tool(
        "request_targeted_snapshot",
        "After explicit user permission, atomically queue a bounded UE editor request for 1-5 /Game/ assets and dependency depth 0-1. The request is bound to an exact complete base Pack fingerprint.",
        {
            "asset_paths": {"type": "array", "items": {"type": "string", "pattern": "^/Game/"}, "minItems": 1, "maxItems": 5, "uniqueItems": True},
            "base_pack_fingerprint": {"type": "string", "minLength": 1},
            "permission_granted": {"type": "boolean", "const": True},
            "dependency_depth": {"type": "integer", "minimum": 0, "maximum": 1, "default": 0},
            "pack_path": PACK_ARG,
            "request_id": {"type": "string", "pattern": "^[A-Za-z0-9_-]{1,64}$"},
            "reason": {"type": "string", "maxLength": 500},
        },
        ["asset_paths", "base_pack_fingerprint", "permission_granted"],
        read_only=False,
        idempotent=False,
    ),
    tool("get_snapshot_request_status", "Read the pending, processing, complete, failed or rejected state of one SyncLive Lite request.", {"request_id": {"type": "string", "pattern": "^[A-Za-z0-9_-]{1,64}$"}}, ["request_id"]),
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
    elif name == "request_targeted_snapshot":
        payload = store.submit_synclive_request(
            arguments.get("asset_paths"),
            str(arguments.get("base_pack_fingerprint", "")),
            arguments.get("permission_granted") is True,
            arguments.get("dependency_depth", 0),
            arguments.get("pack_path"),
            arguments.get("request_id"),
            str(arguments.get("reason", "")),
        )
    elif name == "get_snapshot_request_status":
        payload = store.synclive_status(str(arguments.get("request_id", "")))
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
                "instructions": "Split broad requests into small asset questions. Use search_assets, get_asset_summary, then coverage before cross-asset drill-down. Cite evidence and inspect missing_fields. Only after explicit user permission, bind a bounded request_targeted_snapshot call to the current Pack fingerprint, poll get_snapshot_request_status, verify the new complete Pack, and resume the interrupted task.",
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
                payload = (json.dumps(outgoing, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
                sys.stdout.buffer.write(payload)
                sys.stdout.buffer.flush()
        except Exception as exc:
            sys.stderr.write(f"{SERVER_NAME}: {type(exc).__name__}: {exc}\n")
            sys.stderr.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description="Progressive Context Pack MCP server with bounded, permission-gated SyncLive Lite requests")
    parser.add_argument("--root", type=Path, default=None, help="ReadAllandExplainsExports directory or its ContextPacks child")
    parser.add_argument("--self-test", action="store_true", help="Print the latest-pack envelope and exit")
    parser.add_argument("--diagnose", action="store_true", help="Print CodeBuddy workspace discovery diagnostics and exit")
    args = parser.parse_args()
    export_root = args.root or default_export_root()
    store = ContextPackStore(export_root)
    if args.diagnose:
        print(json_text(diagnostics(export_root)))
        return
    if args.self_test:
        print(json_text(envelope(store, store.list_packs(0, 1))))
        return
    serve(store)


if __name__ == "__main__":
    main()