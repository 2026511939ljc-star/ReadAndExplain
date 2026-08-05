from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence, Tuple

EXIT_OK = 0
EXIT_DIFFERENT = 1
EXIT_ERROR = 2
REPORT_SCHEMA_VERSION = 1

MANIFEST_FILE = "context-pack.json"
BASELINE_MARKER_FILE = ".rae-golden-baseline.json"
BASELINE_MARKER_SCHEMA_VERSION = 1
CONTEXT_PACK_DOCUMENT_TYPE = "ReadAllandExplainsContextPack"
SAFE_CASE_NAME = re.compile(r"^[A-Za-z0-9._-]+$")
MANIFEST_RUNTIME_FIELDS = frozenset(
    {
        "packId",
        "createdUtc",
        "fingerprint",
        "originRequestId",
        "basePackId",
        "basePackFingerprint",
        "generatedUtc",
        "updatedUtc",
    }
)
MANIFEST_FILE_RUNTIME_FIELDS = frozenset({"size", "fingerprint"})
SET_LIKE_ARRAY_KEYS = frozenset(
    {
        "rootAssets",
        "assets",
        "files",
        "featureTags",
        "dependencies",
        "referencers",
        "parameters",
        "graphs",
        "nodes",
        "links",
        "pins",
        "materials",
        "bindings",
        "properties",
        "usedBy",
        "channels",
        "niagaraRenderers",
        "niagaraCurves",
        "includeFilePaths",
        "defines",
        "enumEntries",
    }
)


class GoldenError(Exception):
    pass


@dataclass(frozen=True)
class NormalizedFile:
    kind: str
    content: bytes

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.content).hexdigest()


def _canonical_sort_key(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _strip_manifest_runtime_fields(value: Any) -> Any:
    if not isinstance(value, dict):
        return value
    stripped = {key: item for key, item in value.items() if key not in MANIFEST_RUNTIME_FIELDS}
    files = stripped.get("files")
    if isinstance(files, list):
        clean_files: List[Any] = []
        for item in files:
            if isinstance(item, dict):
                clean_files.append(
                    {key: child for key, child in item.items() if key not in MANIFEST_FILE_RUNTIME_FIELDS}
                )
            else:
                clean_files.append(item)
        stripped["files"] = clean_files
    return stripped


def _normalize_json_value(value: Any, parent_key: Optional[str] = None) -> Any:
    if isinstance(value, dict):
        return {
            key: _normalize_json_value(value[key], key)
            for key in sorted(value)
        }
    if isinstance(value, list):
        normalized = [_normalize_json_value(item) for item in value]
        if parent_key in SET_LIKE_ARRAY_KEYS:
            normalized.sort(key=_canonical_sort_key)
        return normalized
    return value


def normalize_json_data(value: Any, relative_path: str) -> Any:
    if relative_path.replace("\\", "/").lower() == MANIFEST_FILE:
        value = _strip_manifest_runtime_fields(value)
    return _normalize_json_value(value)


def normalize_text_bytes(raw: bytes, source: str = "<memory>") -> bytes:
    try:
        text = raw.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise GoldenError(f"Text file is not valid UTF-8: {source}: {error}") from error
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = [line.rstrip(" \t") for line in text.split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    canonical = "\n".join(lines)
    if canonical:
        canonical += "\n"
    return canonical.encode("utf-8")


def normalize_file(path: Path, relative_path: str) -> NormalizedFile:
    raw = path.read_bytes()
    if path.suffix.lower() == ".json":
        try:
            value = json.loads(raw.decode("utf-8-sig"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise GoldenError(f"Invalid UTF-8 JSON: {path}: {error}") from error
        value = normalize_json_data(value, relative_path)
        content = (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
        return NormalizedFile("json", content)
    return NormalizedFile("text", normalize_text_bytes(raw, str(path)))


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def _validate_directory(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_dir():
        raise GoldenError(f"{label} directory does not exist: {resolved}")
    return resolved


def _iter_files(root: Path) -> Iterable[Tuple[str, Path]]:
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix().casefold()):
        try:
            attributes = path.lstat().st_file_attributes
        except AttributeError:
            attributes = 0
        if path.is_symlink() or attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise GoldenError(f"Symbolic links and reparse points are not supported in Golden Packs: {path}")
        if path.is_file():
            resolved_path = path.resolve()
            if not _is_within(resolved_path, root):
                raise GoldenError(f"Pack file resolves outside its root: {path}")
            relative_path = path.relative_to(root).as_posix()
            if relative_path == BASELINE_MARKER_FILE:
                continue
            yield relative_path, path


def load_snapshot(root: Path) -> Dict[str, NormalizedFile]:
    resolved = _validate_directory(root, "Pack")
    snapshot: Dict[str, NormalizedFile] = {}
    for relative_path, path in _iter_files(resolved):
        snapshot[relative_path] = normalize_file(path, relative_path)
    if not snapshot:
        raise GoldenError(f"Pack directory contains no files: {resolved}")
    return snapshot


def _resolve_pack_file(root: Path, relative_path: Any, label: str) -> Tuple[str, Path]:
    if not isinstance(relative_path, str) or not relative_path.strip():
        raise GoldenError(f"{label} must be a non-empty relative path")
    normalized = relative_path.replace("\\", "/")
    parts = normalized.split("/")
    if normalized.startswith("/") or any(part in ("", ".", "..") for part in parts):
        raise GoldenError(f"Unsafe {label}: {relative_path}")
    resolved = root.joinpath(*parts).resolve()
    if not _is_within(resolved, root):
        raise GoldenError(f"{label} resolves outside the Context Pack: {relative_path}")
    return normalized, resolved


def _read_json_object(path: Path, label: str) -> Dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise GoldenError(f"Cannot read {label} {path}: {error}") from error
    if not isinstance(value, dict):
        raise GoldenError(f"{label} must be a JSON object: {path}")
    return value


def _validate_baseline(root: Path) -> Path:
    resolved = _validate_directory(root, "Golden baseline")
    if not _is_managed_baseline(resolved):
        raise GoldenError(f"Golden baseline is not managed by this tool: {resolved}")
    manifest_path = resolved / MANIFEST_FILE
    manifest = _read_json_object(manifest_path, "Golden baseline manifest")
    if manifest.get("documentType") != CONTEXT_PACK_DOCUMENT_TYPE or manifest.get("state") != "complete":
        raise GoldenError(f"Golden baseline manifest is invalid: {manifest_path}")
    return resolved


def _validate_context_pack(root: Path) -> Path:
    resolved = _validate_directory(root, "Context Pack")
    if resolved.name.casefold().endswith(".tmp"):
        raise GoldenError(f"Temporary Context Pack directories are not accepted: {resolved}")
    manifest_path = resolved / MANIFEST_FILE
    if not manifest_path.is_file():
        raise GoldenError(f"Context Pack manifest does not exist: {manifest_path}")
    manifest = _read_json_object(manifest_path, "Context Pack manifest")
    modern = "attemptedAssetCount" in manifest
    if manifest.get("documentType") != CONTEXT_PACK_DOCUMENT_TYPE:
        raise GoldenError(f"Not a ReadAllandExplains Context Pack: {manifest_path}")
    if manifest.get("state") != "complete":
        raise GoldenError(f"Context Pack is not complete: {manifest_path}")
    pack_id = manifest.get("packId")
    if not isinstance(pack_id, str) or pack_id != resolved.name:
        raise GoldenError(f"Context Pack directory must match manifest packId: {resolved}")
    for count_field in ("failedCount", "skippedCount"):
        count = manifest.get(count_field, 0)
        if not isinstance(count, (int, float)) or isinstance(count, bool):
            raise GoldenError(f"Context Pack {count_field} is invalid: {manifest_path}")
        if count != 0:
            raise GoldenError(f"Context Pack has {count_field}={count}; refusing a partial Golden baseline: {manifest_path}")

    manifest_files = manifest.get("files")
    if not isinstance(manifest_files, list):
        raise GoldenError(f"Context Pack manifest files must be an array: {manifest_path}")
    declared_files: Dict[str, Mapping[str, Any]] = {}
    declared_file_keys: Dict[str, str] = {}
    for file_entry in manifest_files:
        if not isinstance(file_entry, dict):
            raise GoldenError(f"Context Pack manifest contains an invalid file entry: {manifest_path}")
        relative_path, file_path = _resolve_pack_file(resolved, file_entry.get("path"), "manifest file path")
        path_key = relative_path.casefold()
        if path_key in declared_file_keys:
            raise GoldenError(f"Context Pack manifest contains duplicate file path: {relative_path}")
        declared_file_keys[path_key] = relative_path
        size = file_entry.get("size")
        fingerprint = file_entry.get("fingerprint")
        if not isinstance(size, int) or isinstance(size, bool) or size < 0:
            raise GoldenError(f"Context Pack manifest file size is invalid: {relative_path}")
        if not isinstance(fingerprint, str) or not fingerprint.startswith(("blake3-160:", "sha1:")):
            raise GoldenError(f"Context Pack manifest fingerprint is invalid: {relative_path}")
        if not file_path.is_file() or file_path.stat().st_size != size:
            raise GoldenError(f"Context Pack manifest file is missing or has the wrong size: {relative_path}")
        if modern:
            actual_fingerprint = "sha1:" + hashlib.sha1(file_path.read_bytes()).hexdigest()
            if fingerprint.casefold() != actual_fingerprint:
                raise GoldenError(f"Context Pack manifest file fingerprint does not match: {relative_path}")
        declared_files[relative_path] = file_entry
    if modern:
        fingerprint_source = "".join(
            f"{relative_path}:{declared_files[relative_path]['size']}:{str(declared_files[relative_path]['fingerprint']).split(':', 1)[1]}\n"
            for relative_path in declared_files
        )
        expected_pack_fingerprint = "sha1:" + hashlib.sha1(fingerprint_source.encode("utf-8")).hexdigest()
        if str(manifest.get("fingerprint", "")).casefold() != expected_pack_fingerprint:
            raise GoldenError(f"Context Pack fingerprint does not match its file manifest: {manifest_path}")
    disk_files: Dict[str, str] = {}
    for relative_path, _ in _iter_files(resolved):
        if relative_path == MANIFEST_FILE:
            continue
        path_key = relative_path.casefold()
        if path_key in disk_files:
            raise GoldenError(f"Context Pack contains case-insensitive duplicate file paths: {relative_path}")
        disk_files[path_key] = relative_path
    if set(declared_file_keys) != set(disk_files):
        missing = sorted(declared_file_keys[key] for key in set(declared_file_keys) - set(disk_files))
        undeclared = sorted(disk_files[key] for key in set(disk_files) - set(declared_file_keys))
        raise GoldenError(f"Context Pack file manifest mismatch; missing={missing}, undeclared={undeclared}")
    required_files = {"readme.md", "index.md", "index.json"}
    missing_required = sorted(required_files - set(disk_files))
    if missing_required:
        raise GoldenError(f"Context Pack is missing required files: {missing_required}")

    manifest_assets = manifest.get("assets")
    if not isinstance(manifest_assets, list):
        raise GoldenError(f"Context Pack manifest assets must be an array: {manifest_path}")
    exported_count = manifest.get("exportedAssetCount")
    if not isinstance(exported_count, int) or isinstance(exported_count, bool) or exported_count != len(manifest_assets):
        raise GoldenError(f"Context Pack exportedAssetCount does not match manifest assets: {manifest_path}")
    if modern:
        attempted_count = manifest.get("attemptedAssetCount")
        if not isinstance(attempted_count, int) or isinstance(attempted_count, bool) or attempted_count <= 0 or attempted_count != exported_count:
            raise GoldenError(f"Context Pack attemptedAssetCount does not match exported assets: {manifest_path}")

    index_path = resolved / "index.json"
    if not index_path.is_file():
        raise GoldenError(f"Context Pack index does not exist: {index_path}")
    index = _read_json_object(index_path, "Context Pack index")
    index_assets = index.get("assets")
    if not isinstance(index_assets, list):
        raise GoldenError(f"Context Pack index assets must be an array: {index_path}")
    asset_count = index.get("assetCount")
    if not isinstance(asset_count, int) or isinstance(asset_count, bool) or asset_count != len(index_assets):
        raise GoldenError(f"Context Pack index assetCount does not match assets: {index_path}")

    legacy_metadata_by_object: Dict[str, Tuple[str, Path]] = {}
    for relative_path, metadata_path in _iter_files(resolved):
        if not relative_path.casefold().endswith(".meta.json"):
            continue
        metadata = _read_json_object(metadata_path, "Context Pack metadata")
        metadata_object_path = metadata.get("objectPath")
        if isinstance(metadata_object_path, str) and metadata_object_path.startswith("/Game/"):
            if metadata_object_path in legacy_metadata_by_object:
                raise GoldenError(f"Context Pack contains duplicate Metadata objectPath: {metadata_object_path}")
            legacy_metadata_by_object[metadata_object_path] = (relative_path, metadata_path)

    def asset_map(items: Sequence[Any], label: str) -> Dict[str, Tuple[str, str]]:
        result: Dict[str, Tuple[str, str]] = {}
        export_keys: set[str] = set()
        metadata_keys: set[str] = set()
        for item in items:
            if not isinstance(item, dict):
                raise GoldenError(f"{label} contains an invalid asset entry")
            object_path = item.get("objectPath")
            export_file = item.get("exportFile")
            metadata_file = item.get("metadataFile")
            if not isinstance(object_path, str) or not object_path.startswith("/Game/"):
                raise GoldenError(f"{label} contains an invalid objectPath")
            relative_path, file_path = _resolve_pack_file(resolved, export_file, f"{label} exportFile")
            if modern and (not isinstance(metadata_file, str) or not metadata_file):
                raise GoldenError(f"{label} requires an explicit metadataFile for objectPath: {object_path}")
            if metadata_file is None:
                legacy_metadata = legacy_metadata_by_object.get(object_path)
                if not legacy_metadata:
                    raise GoldenError(f"{label} has no Metadata for objectPath: {object_path}")
                metadata_relative_path, metadata_path = legacy_metadata
            else:
                metadata_relative_path, metadata_path = _resolve_pack_file(resolved, metadata_file, f"{label} metadataFile")
            if not file_path.is_file():
                raise GoldenError(f"{label} exportFile does not exist: {relative_path}")
            if not metadata_path.is_file():
                raise GoldenError(f"{label} metadataFile does not exist: {metadata_relative_path}")
            metadata = _read_json_object(metadata_path, f"{label} metadata")
            if metadata.get("objectPath") != object_path:
                raise GoldenError(f"{label} metadata objectPath does not match: {metadata_relative_path}")
            if object_path in result:
                raise GoldenError(f"{label} contains duplicate objectPath: {object_path}")
            export_key = relative_path.casefold()
            metadata_key = metadata_relative_path.casefold()
            if export_key in export_keys:
                raise GoldenError(f"{label} contains duplicate exportFile paths")
            if metadata_key in metadata_keys:
                raise GoldenError(f"{label} contains duplicate metadataFile paths")
            export_keys.add(export_key)
            metadata_keys.add(metadata_key)
            result[object_path] = (relative_path, metadata_relative_path)
        return result

    manifest_asset_map = asset_map(manifest_assets, "Context Pack manifest")
    index_asset_map = asset_map(index_assets, "Context Pack index")
    if manifest_asset_map != index_asset_map:
        raise GoldenError(f"Context Pack manifest and index asset mappings do not match: {resolved}")
    root_assets = manifest.get("rootAssets")
    if not isinstance(root_assets, list) or not all(isinstance(item, str) for item in root_assets):
        raise GoldenError(f"Context Pack rootAssets must be an array of strings: {manifest_path}")
    missing_roots = sorted(set(root_assets) - set(manifest_asset_map))
    if missing_roots:
        raise GoldenError(f"Context Pack root assets were not exported: {missing_roots}")
    return resolved


def _write_normalized_tree(snapshot: Mapping[str, NormalizedFile], destination: Path) -> None:
    for relative_path in sorted(snapshot):
        output = destination.joinpath(*relative_path.split("/"))
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(snapshot[relative_path].content)


def _write_baseline_marker(destination: Path) -> None:
    marker = {
        "schemaVersion": BASELINE_MARKER_SCHEMA_VERSION,
        "documentType": "ReadAllandExplainsGoldenBaseline",
    }
    (destination / BASELINE_MARKER_FILE).write_text(
        json.dumps(marker, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _is_managed_baseline(destination: Path) -> bool:
    marker_path = destination / BASELINE_MARKER_FILE
    if not marker_path.is_file():
        return False
    try:
        marker = json.loads(marker_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return False
    return (
        isinstance(marker, dict)
        and marker.get("schemaVersion") == BASELINE_MARKER_SCHEMA_VERSION
        and marker.get("documentType") == "ReadAllandExplainsGoldenBaseline"
    )


def create_baseline(pack: Path, baseline: Path, force: bool = False) -> Dict[str, Any]:
    source = _validate_context_pack(pack)
    destination = baseline.expanduser().resolve()
    if source == destination or _is_within(destination, source) or _is_within(source, destination):
        raise GoldenError("Context Pack and baseline directories must not contain one another")
    if destination.exists():
        if not force:
            raise GoldenError(f"Baseline already exists; pass --force to replace it: {destination}")
        if not destination.is_dir() or not _is_managed_baseline(destination):
            raise GoldenError(f"Refusing to replace an unmanaged baseline path: {destination}")

    snapshot = load_snapshot(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{destination.name}.tmp-", dir=str(destination.parent)))
    backup = destination.with_name(f".{destination.name}.backup-{os.getpid()}")
    replaced_existing = False
    try:
        _write_normalized_tree(snapshot, temporary)
        _write_baseline_marker(temporary)
        if destination.exists():
            if backup.exists():
                raise GoldenError(f"Baseline backup path already exists: {backup}")
            os.replace(str(destination), str(backup))
            replaced_existing = True
        os.replace(str(temporary), str(destination))
        if replaced_existing:
            shutil.rmtree(backup)
    except Exception:
        if temporary.exists():
            shutil.rmtree(temporary, ignore_errors=True)
        if replaced_existing and backup.exists() and not destination.exists():
            os.replace(str(backup), str(destination))
        raise

    return {
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "tool": "golden_regression",
        "action": "create",
        "ok": True,
        "exitCode": EXIT_OK,
        "source": str(source),
        "baseline": str(destination),
        "fileCount": len(snapshot),
        "files": sorted(snapshot),
    }


def compare_packs(actual: Path, baseline: Path) -> Dict[str, Any]:
    actual_root = _validate_context_pack(actual)
    baseline_root = _validate_baseline(baseline)
    actual_snapshot = load_snapshot(actual_root)
    baseline_snapshot = load_snapshot(baseline_root)

    actual_paths = set(actual_snapshot)
    baseline_paths = set(baseline_snapshot)
    added = sorted(actual_paths - baseline_paths)
    missing = sorted(baseline_paths - actual_paths)
    changed: List[Dict[str, str]] = []
    unchanged = 0
    for relative_path in sorted(actual_paths & baseline_paths):
        actual_file = actual_snapshot[relative_path]
        expected_file = baseline_snapshot[relative_path]
        if actual_file.content == expected_file.content and actual_file.kind == expected_file.kind:
            unchanged += 1
            continue
        changed.append(
            {
                "path": relative_path,
                "expectedKind": expected_file.kind,
                "actualKind": actual_file.kind,
                "expectedSha256": expected_file.sha256,
                "actualSha256": actual_file.sha256,
            }
        )

    ok = not added and not missing and not changed
    return {
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "tool": "golden_regression",
        "action": "compare",
        "ok": ok,
        "exitCode": EXIT_OK if ok else EXIT_DIFFERENT,
        "actual": str(actual_root),
        "baseline": str(baseline_root),
        "summary": {
            "actualFiles": len(actual_snapshot),
            "baselineFiles": len(baseline_snapshot),
            "unchangedFiles": unchanged,
            "addedFiles": len(added),
            "missingFiles": len(missing),
            "changedFiles": len(changed),
        },
        "differences": {"added": added, "missing": missing, "changed": changed},
    }


def _case_requirements(actual: Path, case: Mapping[str, Any]) -> List[Dict[str, str]]:
    snapshot = load_snapshot(actual)
    index_item = snapshot.get("index.json")
    if index_item is None or index_item.kind != "json":
        raise GoldenError("Context Pack index.json is required for case validation")
    index = json.loads(index_item.content.decode("utf-8"))
    index_assets = index.get("assets", []) if isinstance(index, dict) else []
    if not isinstance(index_assets, list):
        raise GoldenError("Context Pack index assets must be an array")
    exported_asset_paths = {
        item["objectPath"]
        for item in index_assets
        if isinstance(item, dict)
        and isinstance(item.get("objectPath"), str)
        and isinstance(item.get("exportFile"), str)
        and item["exportFile"] in snapshot
    }

    required_asset_paths = case.get("requiredAssetPaths", [])
    required_markers = case.get("requiredMarkers", [])
    forbidden_markers = case.get("forbiddenMarkers", [])
    if not isinstance(required_asset_paths, list):
        raise GoldenError("requiredAssetPaths must be an array")
    if not isinstance(required_markers, list):
        raise GoldenError("requiredMarkers must be an array")
    if not isinstance(forbidden_markers, list):
        raise GoldenError("forbiddenMarkers must be an array")

    failures: List[Dict[str, str]] = []
    for asset_path in required_asset_paths:
        if not isinstance(asset_path, str):
            raise GoldenError("requiredAssetPaths entries must be strings")
        if asset_path not in exported_asset_paths:
            failures.append({"type": "missingAssetPath", "value": asset_path})

    for marker in required_markers:
        if not isinstance(marker, dict) or not isinstance(marker.get("file"), str) or not isinstance(marker.get("contains"), str):
            raise GoldenError("requiredMarkers entries must contain string file and contains fields")
        relative_path = marker["file"].replace("\\", "/")
        while relative_path.startswith("./"):
            relative_path = relative_path[2:]
        item = snapshot.get(relative_path)
        if item is None:
            failures.append({"type": "markerFileMissing", "file": relative_path, "value": marker["contains"]})
        elif marker["contains"] not in item.content.decode("utf-8"):
            failures.append({"type": "markerMissing", "file": relative_path, "value": marker["contains"]})

    for marker in forbidden_markers:
        if not isinstance(marker, dict) or not isinstance(marker.get("file"), str) or not isinstance(marker.get("contains"), str):
            raise GoldenError("forbiddenMarkers entries must contain string file and contains fields")
        relative_path = marker["file"].replace("\\", "/")
        while relative_path.startswith("./"):
            relative_path = relative_path[2:]
        item = snapshot.get(relative_path)
        if item is None:
            failures.append({"type": "markerFileMissing", "file": relative_path, "value": marker["contains"]})
        elif marker["contains"] in item.content.decode("utf-8"):
            failures.append({"type": "forbiddenMarkerPresent", "file": relative_path, "value": marker["contains"]})
    return failures


def _config_path(config_file: Path, value: Any, field: str) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise GoldenError(f"Case field {field} must be a non-empty path string")
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = config_file.parent / path
    return path.resolve()


def _validated_provenance(data: Mapping[str, Any]) -> Optional[Dict[str, Any]]:
    if "provenance" not in data:
        return None
    provenance = data["provenance"]
    if not isinstance(provenance, dict):
        raise GoldenError("Cases config provenance must be a JSON object")
    try:
        encoded = json.dumps(provenance, ensure_ascii=False, allow_nan=False)
        validated = json.loads(encoded)
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise GoldenError(f"Cases config provenance is not valid JSON: {error}") from error
    if not isinstance(validated, dict):
        raise GoldenError("Cases config provenance must be a JSON object")
    return validated


def run_cases(config: Path, update_baselines: bool = False, force: bool = False) -> Dict[str, Any]:
    config_file = config.expanduser().resolve()
    try:
        data = json.loads(config_file.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise GoldenError(f"Cannot read cases config {config_file}: {error}") from error
    if not isinstance(data, dict) or data.get("schemaVersion") != 1 or not isinstance(data.get("cases"), list):
        raise GoldenError("Cases config must be an object with schemaVersion 1 and a cases array")
    provenance = _validated_provenance(data)
    if update_baselines and len(data["cases"]) != 1:
        raise GoldenError("Baseline updates must contain exactly one case so approval is atomic")

    results: List[Dict[str, Any]] = []
    operational_error = False
    for index, case in enumerate(data["cases"]):
        if not isinstance(case, dict):
            raise GoldenError(f"Case at index {index} must be an object")
        name = case.get("name")
        if not isinstance(name, str) or not name.strip():
            raise GoldenError(f"Case at index {index} requires a non-empty name")
        actual = _config_path(config_file, case.get("actualPack"), "actualPack")
        baseline = _config_path(config_file, case.get("baseline"), "baseline")
        try:
            _validate_context_pack(actual)
            requirement_failures = _case_requirements(actual, case)
            if requirement_failures:
                results.append(
                    {
                        "name": name,
                        "ok": False,
                        "comparison": None,
                        "requirementFailures": requirement_failures,
                    }
                )
                continue
            if update_baselines:
                create_baseline(actual, baseline, force=force)
            comparison = compare_packs(actual, baseline)
            case_ok = comparison["ok"]
            results.append(
                {
                    "name": name,
                    "ok": case_ok,
                    "comparison": comparison,
                    "requirementFailures": [],
                }
            )
        except GoldenError as error:
            operational_error = True
            results.append({"name": name, "ok": False, "error": str(error)})

    ok = bool(results) and all(result["ok"] for result in results)
    if operational_error:
        exit_code = EXIT_ERROR
    else:
        exit_code = EXIT_OK if ok else EXIT_DIFFERENT
    report = {
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "tool": "golden_regression",
        "action": "cases",
        "ok": ok,
        "exitCode": exit_code,
        "config": str(config_file),
        "summary": {
            "cases": len(results),
            "passed": sum(1 for result in results if result["ok"]),
            "failed": sum(1 for result in results if not result["ok"]),
        },
        "cases": results,
    }
    if provenance is not None:
        report["provenance"] = provenance
    return report


def _list_section(lines: List[str], label: str, values: Sequence[Any]) -> None:
    if not values:
        return
    lines.append(f"{label} ({len(values)}):")
    for value in values:
        path = value.get("path") if isinstance(value, dict) else value
        lines.append(f"  {path}")


def render_human_report(result: Mapping[str, Any]) -> str:
    action = result.get("action")
    status = "PASS" if result.get("ok") else "FAIL"
    lines = [f"{status}: Golden Pack {action}"]
    if action == "create":
        lines.extend(
            [
                f"Source: {result['source']}",
                f"Baseline: {result['baseline']}",
                f"Normalized files: {result['fileCount']}",
            ]
        )
    elif action == "compare":
        summary = result["summary"]
        lines.extend(
            [
                f"Actual: {result['actual']}",
                f"Baseline: {result['baseline']}",
                "Files: actual={actualFiles}, baseline={baselineFiles}, unchanged={unchangedFiles}, "
                "added={addedFiles}, missing={missingFiles}, changed={changedFiles}".format(**summary),
            ]
        )
        differences = result["differences"]
        _list_section(lines, "Added", differences["added"])
        _list_section(lines, "Missing", differences["missing"])
        _list_section(lines, "Changed", differences["changed"])
    elif action == "cases":
        summary = result["summary"]
        lines.append(f"Cases: total={summary['cases']}, passed={summary['passed']}, failed={summary['failed']}")
        provenance = result.get("provenance")
        if isinstance(provenance, dict):
            lines.append("Provenance: " + json.dumps(provenance, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
        for case in result["cases"]:
            lines.append(f"  {'PASS' if case['ok'] else 'FAIL'} {case['name']}")
            for failure in case.get("requirementFailures", []):
                lines.append(f"    {failure['type']}: {failure.get('file', '')} {failure.get('value', '')}".rstrip())
            comparison = case.get("comparison")
            if isinstance(comparison, dict) and not comparison.get("ok"):
                differences = comparison.get("differences", {})
                for label, key in (("added", "added"), ("missing", "missing"), ("changed", "changed")):
                    for difference in differences.get(key, []):
                        path = difference.get("path") if isinstance(difference, dict) else difference
                        lines.append(f"    {label}: {path}")
            if "error" in case:
                lines.append(f"    error: {case['error']}")
    elif action == "error":
        lines.append(str(result.get("error", "Unknown error")))
    return "\n".join(lines) + "\n"


def _write_report(path: Optional[str], content: str) -> None:
    if not path:
        return
    output = Path(path).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(content)


def _emit(result: Mapping[str, Any], output_format: str, json_report: Optional[str], human_report: Optional[str]) -> None:
    json_text = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    human_text = render_human_report(result)
    _write_report(json_report, json_text)
    _write_report(human_report, human_text)
    sys.stdout.write(json_text if output_format == "json" else human_text)


def _add_output_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--format", choices=("human", "json"), default="human", help="stdout format")
    parser.add_argument("--json-report", help="also write the machine-readable JSON report")
    parser.add_argument("--human-report", help="also write the human-readable text report")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Normalize and regress ReadAllandExplains Context Packs")
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create a normalized baseline from a real Context Pack")
    create.add_argument("pack", help="source Context Pack directory")
    create.add_argument("baseline", help="destination baseline directory")
    create.add_argument("--force", action="store_true", help="replace an existing baseline")
    _add_output_arguments(create)

    compare = subparsers.add_parser("compare", help="compare an actual Context Pack with a baseline")
    compare.add_argument("actual", help="actual Context Pack directory")
    compare.add_argument("baseline", help="normalized baseline directory")
    _add_output_arguments(compare)

    cases = subparsers.add_parser("cases", help="run configured real-pack regression cases")
    cases.add_argument("config", help="cases JSON file")
    cases.add_argument("--update-baselines", action="store_true", help="create baselines before comparing")
    cases.add_argument("--force", action="store_true", help="replace baselines when updating")
    _add_output_arguments(cases)
    return parser


def _configure_stdio() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            reconfigure(encoding="utf-8", errors="strict")


def main(argv: Optional[Sequence[str]] = None) -> int:
    _configure_stdio()
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "create":
            result = create_baseline(Path(args.pack), Path(args.baseline), force=args.force)
        elif args.command == "compare":
            result = compare_packs(Path(args.actual), Path(args.baseline))
        else:
            result = run_cases(Path(args.config), update_baselines=args.update_baselines, force=args.force)
    except (GoldenError, OSError) as error:
        result = {
            "schemaVersion": REPORT_SCHEMA_VERSION,
            "tool": "golden_regression",
            "action": "error",
            "ok": False,
            "exitCode": EXIT_ERROR,
            "error": str(error),
        }
    try:
        _emit(result, args.format, args.json_report, args.human_report)
    except OSError as error:
        result = {
            "schemaVersion": REPORT_SCHEMA_VERSION,
            "tool": "golden_regression",
            "action": "error",
            "ok": False,
            "exitCode": EXIT_ERROR,
            "error": f"Cannot write regression report: {error}",
        }
        output = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        sys.stdout.write(output if args.format == "json" else render_human_report(result))
    return int(result["exitCode"])


if __name__ == "__main__":
    raise SystemExit(main())
