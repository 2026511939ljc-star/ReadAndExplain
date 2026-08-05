from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().with_name("golden_regression.py")
SPEC = importlib.util.spec_from_file_location("golden_regression", MODULE_PATH)
assert SPEC and SPEC.loader
golden = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = golden
SPEC.loader.exec_module(golden)


class GoldenRegressionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.actual = self.root / "ContextPack_First"
        self.baseline = self.root / "baseline"
        self._write_pack(self.actual)

    def tearDown(self) -> None:
        self.temp.cleanup()

    @staticmethod
    def _dump(path: Path, value: object, bom: bool = False) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        encoding = "utf-8-sig" if bom else "utf-8"
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding=encoding)

    def _refresh_manifest_files(self, root: Path) -> None:
        manifest_path = root / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        relative_files = sorted(
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file() and path != manifest_path
        )
        manifest["files"] = [
            {
                "path": relative_path,
                "size": (root / relative_path).stat().st_size,
                "fingerprint": "sha1:" + hashlib.sha1((root / relative_path).read_bytes()).hexdigest(),
            }
            for relative_path in relative_files
        ]
        fingerprint_source = "".join(
            f"{item['path']}:{item['size']}:{item['fingerprint'].split(':', 1)[1]}\n"
            for item in manifest["files"]
        )
        manifest["fingerprint"] = "sha1:" + hashlib.sha1(fingerprint_source.encode("utf-8")).hexdigest()
        self._dump(manifest_path, manifest)

    def _write_pack(
        self,
        root: Path,
        pack_id: str = "ContextPack_First",
        created: str = "2026-08-05T00:00:00Z",
        reverse_sets: bool = False,
    ) -> None:
        dependencies = ["/Game/Test/M_Water.M_Water", "/Game/Test/M_Foam.M_Foam"]
        assets = [
            {
                "name": "NS_Water",
                "objectPath": "/Game/Test/NS_Water.NS_Water",
                "exportFile": "Niagara/NS_Water_ReadableNiagara.md",
                "metadataFile": "Niagara/NS_Water.meta.json",
                "dependencies": dependencies,
                "referencers": [],
            }
        ]
        roots = ["/Game/Test/NS_Water.NS_Water"]
        if reverse_sets:
            dependencies.reverse()
            assets.reverse()
            roots.reverse()
        self._dump(root / "index.json", {"schemaVersion": 1, "assetCount": len(assets), "assets": assets})
        self._dump(
            root / "Niagara" / "NS_Water.meta.json",
            {
                "schemaVersion": 2,
                "assetName": "NS_Water",
                "objectPath": "/Game/Test/NS_Water.NS_Water",
                "featureTags": ["water", "foam"] if not reverse_sets else ["foam", "water"],
                "dependencies": dependencies,
                "referencers": [],
                "parameters": [
                    {"name": "User.Intensity", "kind": "float", "value": "1.0"},
                    {"name": "User.Color", "kind": "color", "value": "white"},
                ][:: -1 if reverse_sets else 1],
                "graphs": [
                    {
                        "id": "graph-update",
                        "nodes": [{"id": "node-b"}, {"id": "node-a"}][:: -1 if reverse_sets else 1],
                        "links": [],
                    }
                ],
                "niagaraCurves": [
                    {
                        "id": "curve-scale",
                        "fingerprint": "semantic-curve-fingerprint",
                        "usedBy": ["Update", "Spawn"] if not reverse_sets else ["Spawn", "Update"],
                        "channels": [
                            {
                                "name": "X",
                                "keys": [
                                    {"time": 0, "value": 0},
                                    {"time": 1, "value": 1},
                                ],
                            }
                        ],
                    }
                ],
            },
            bom=True,
        )
        readable = root / "Niagara" / "NS_Water_ReadableNiagara.md"
        readable.parent.mkdir(parents=True, exist_ok=True)
        readable.write_text("# NS_Water\n\nRequired Golden Marker\n", encoding="utf-8")
        (root / "README.md").write_text("# Context Pack\n", encoding="utf-8")
        (root / "index.md").write_text("# Index\n", encoding="utf-8")
        relative_files = [
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file() and path != root / "context-pack.json"
        ]
        if reverse_sets:
            relative_files.reverse()
        files = [
            {
                "path": relative_path,
                "size": (root / relative_path).stat().st_size,
                "fingerprint": "sha1:" + hashlib.sha1((root / relative_path).read_bytes()).hexdigest(),
            }
            for relative_path in relative_files
        ]
        fingerprint_source = "".join(
            f"{item['path']}:{item['size']}:{item['fingerprint'].split(':', 1)[1]}\n"
            for item in files
        )
        manifest = {
            "schemaVersion": 1,
            "documentType": "ReadAllandExplainsContextPack",
            "packId": pack_id,
            "state": "complete",
            "createdUtc": created,
            "fingerprint": "sha1:" + hashlib.sha1(fingerprint_source.encode("utf-8")).hexdigest(),
            "originRequestId": f"request-{pack_id}",
            "basePackId": f"base-{pack_id}",
            "dependencyDepth": 1,
            "attemptedAssetCount": 1,
            "exportedAssetCount": 1,
            "failedCount": 0,
            "skippedCount": 0,
            "files": files,
            "rootAssets": roots,
            "assets": [
                {
                    "objectPath": "/Game/Test/NS_Water.NS_Water",
                    "packageName": "/Game/Test/NS_Water",
                    "exportFile": "Niagara/NS_Water_ReadableNiagara.md",
                    "metadataFile": "Niagara/NS_Water.meta.json",
                }
            ],
        }
        self._dump(root / "context-pack.json", manifest)

    def test_manifest_dynamic_fields_and_set_arrays_are_normalized(self) -> None:
        golden.create_baseline(self.actual, self.baseline)
        second = self.root / "ContextPack_Second"
        self._write_pack(
            second,
            pack_id="ContextPack_Second",
            created="2026-08-05T12:34:56Z",
            reverse_sets=True,
        )

        result = golden.compare_packs(second, self.baseline)

        self.assertTrue(result["ok"])
        normalized_manifest = json.loads((self.baseline / "context-pack.json").read_text(encoding="utf-8"))
        self.assertNotIn("packId", normalized_manifest)
        self.assertNotIn("createdUtc", normalized_manifest)
        self.assertNotIn("fingerprint", normalized_manifest)
        self.assertEqual(["path"], list(normalized_manifest["files"][0]))

    def test_semantic_json_change_fails(self) -> None:
        golden.create_baseline(self.actual, self.baseline)
        metadata = self.actual / "Niagara" / "NS_Water.meta.json"
        value = json.loads(metadata.read_text(encoding="utf-8-sig"))
        value["parameters"][0]["value"] = "2.0"
        self._dump(metadata, value)
        self._refresh_manifest_files(self.actual)

        result = golden.compare_packs(self.actual, self.baseline)

        self.assertFalse(result["ok"])
        self.assertEqual(golden.EXIT_DIFFERENT, result["exitCode"])
        self.assertEqual(["Niagara/NS_Water.meta.json"], [item["path"] for item in result["differences"]["changed"]])

    def test_added_and_missing_files_are_reported(self) -> None:
        optional = self.actual / "Diagnostics" / "optional.txt"
        optional.parent.mkdir(parents=True, exist_ok=True)
        optional.write_text("optional\n", encoding="utf-8")
        self._refresh_manifest_files(self.actual)
        golden.create_baseline(self.actual, self.baseline)
        optional.unlink()
        added = self.actual / "Materials" / "M_Added.md"
        added.parent.mkdir(parents=True, exist_ok=True)
        added.write_text("added\n", encoding="utf-8")
        self._refresh_manifest_files(self.actual)

        result = golden.compare_packs(self.actual, self.baseline)

        self.assertEqual(["Materials/M_Added.md"], result["differences"]["added"])
        self.assertEqual(["Diagnostics/optional.txt"], result["differences"]["missing"])
        self.assertEqual(1, result["exitCode"])

    def test_text_bom_line_endings_and_trailing_whitespace_are_normalized(self) -> None:
        text_path = self.actual / "Niagara" / "NS_Water_ReadableNiagara.md"
        text_path.write_bytes(b"\xef\xbb\xbf# NS_Water\r\n\r\nRequired Golden Marker  \r\n\r\n")
        self._refresh_manifest_files(self.actual)
        golden.create_baseline(self.actual, self.baseline)
        text_path.write_bytes(b"# NS_Water\n\nRequired Golden Marker\t\n")
        self._refresh_manifest_files(self.actual)

        result = golden.compare_packs(self.actual, self.baseline)

        self.assertTrue(result["ok"])
        self.assertEqual(b"# NS_Water\n\nRequired Golden Marker\n", (self.baseline / "Niagara" / "NS_Water_ReadableNiagara.md").read_bytes())

    def test_ordered_curve_keys_remain_semantic(self) -> None:
        golden.create_baseline(self.actual, self.baseline)
        metadata = self.actual / "Niagara" / "NS_Water.meta.json"
        value = json.loads(metadata.read_text(encoding="utf-8-sig"))
        value["niagaraCurves"][0]["channels"][0]["keys"].reverse()
        self._dump(metadata, value)
        self._refresh_manifest_files(self.actual)

        result = golden.compare_packs(self.actual, self.baseline)

        self.assertFalse(result["ok"])

    def test_partial_pack_is_rejected_as_baseline(self) -> None:
        manifest_path = self.actual / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["failedCount"] = 1
        manifest["skippedCount"] = 0
        self._dump(manifest_path, manifest)

        with self.assertRaises(golden.GoldenError) as raised:
            golden.create_baseline(self.actual, self.baseline)

        self.assertIn("failedCount=1", str(raised.exception))

    def test_duplicate_export_files_are_rejected_as_baseline(self) -> None:
        index_path = self.actual / "index.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        index["assets"][0]["exportFile"] = "Materials/Duplicate.md"
        index["assets"].append(
            {
                "name": "M_Duplicate",
                "objectPath": "/Game/Test/M_Duplicate.M_Duplicate",
                "exportFile": "Materials/Duplicate.md",
                "metadataFile": "Materials/M_Duplicate.meta.json",
                "dependencies": [],
                "referencers": [],
            }
        )
        (self.actual / "Materials").mkdir(parents=True, exist_ok=True)
        (self.actual / "Materials" / "Duplicate.md").write_text("duplicate\n", encoding="utf-8")
        self._dump(
            self.actual / "Materials" / "M_Duplicate.meta.json",
            {"assetName": "M_Duplicate", "objectPath": "/Game/Test/M_Duplicate.M_Duplicate"},
        )
        index["assetCount"] = len(index["assets"])
        self._dump(index_path, index)
        self._refresh_manifest_files(self.actual)

        with self.assertRaises(golden.GoldenError) as raised:
            golden.create_baseline(self.actual, self.baseline)

        self.assertIn("duplicate exportFile", str(raised.exception))

    def test_legacy_schema_one_pack_without_metadata_file_mapping_is_accepted(self) -> None:
        index_path = self.actual / "index.json"
        manifest_path = self.actual / "context-pack.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        index["assets"][0].pop("metadataFile")
        self._dump(index_path, index)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest.pop("attemptedAssetCount")
        manifest["assets"][0].pop("metadataFile")
        self._dump(manifest_path, manifest)
        self._refresh_manifest_files(self.actual)

        golden.create_baseline(self.actual, self.baseline)

        self.assertTrue((self.baseline / "Niagara" / "NS_Water.meta.json").is_file())

    def test_modern_pack_requires_metadata_mapping_and_valid_content_fingerprint(self) -> None:
        index_path = self.actual / "index.json"
        manifest_path = self.actual / "context-pack.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        index["assets"][0].pop("metadataFile")
        self._dump(index_path, index)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["assets"][0].pop("metadataFile")
        self._dump(manifest_path, manifest)
        self._refresh_manifest_files(self.actual)
        with self.assertRaises(golden.GoldenError) as missing_mapping:
            golden.create_baseline(self.actual, self.baseline)
        self.assertIn("requires an explicit metadataFile", str(missing_mapping.exception))

        self._write_pack(self.actual)
        readable = self.actual / "Niagara" / "NS_Water_ReadableNiagara.md"
        original = readable.read_bytes()
        readable.write_bytes(bytes([original[0] ^ 1]) + original[1:])
        with self.assertRaises(golden.GoldenError) as fingerprint_mismatch:
            golden.create_baseline(self.actual, self.baseline)
        self.assertIn("fingerprint does not match", str(fingerprint_mismatch.exception))

    def test_same_name_assets_with_unique_files_are_accepted(self) -> None:
        index_path = self.actual / "index.json"
        manifest_path = self.actual / "context-pack.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        second_object_path = "/Game/Other/NS_Water.NS_Water"
        second_export = "Niagara/NS_Water__second_ReadableNiagara.md"
        second_metadata = "Niagara/NS_Water__second.meta.json"
        (self.actual / second_export).write_text("# Other NS_Water\n", encoding="utf-8")
        self._dump(
            self.actual / second_metadata,
            {"assetName": "NS_Water", "objectPath": second_object_path},
        )
        second_record = {
            "name": "NS_Water",
            "objectPath": second_object_path,
            "exportFile": second_export,
            "metadataFile": second_metadata,
            "dependencies": [],
            "referencers": [],
        }
        index["assets"].append(second_record)
        index["assetCount"] = len(index["assets"])
        self._dump(index_path, index)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["assets"].append(
            {
                "objectPath": second_object_path,
                "packageName": "/Game/Other/NS_Water",
                "exportFile": second_export,
                "metadataFile": second_metadata,
            }
        )
        manifest["attemptedAssetCount"] = len(manifest["assets"])
        manifest["exportedAssetCount"] = len(manifest["assets"])
        self._dump(manifest_path, manifest)
        self._refresh_manifest_files(self.actual)

        golden.create_baseline(self.actual, self.baseline)

        self.assertTrue((self.baseline / second_export).is_file())
        self.assertTrue((self.baseline / second_metadata).is_file())

    def test_missing_required_file_and_metadata_identity_mismatch_are_rejected(self) -> None:
        (self.actual / "README.md").unlink()
        self._refresh_manifest_files(self.actual)
        with self.assertRaises(golden.GoldenError) as missing:
            golden.create_baseline(self.actual, self.baseline)
        self.assertIn("missing required files", str(missing.exception))

        (self.actual / "README.md").write_text("# Context Pack\n", encoding="utf-8")
        metadata_path = self.actual / "Niagara" / "NS_Water.meta.json"
        metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
        metadata["objectPath"] = "/Game/Wrong/NS_Water.NS_Water"
        self._dump(metadata_path, metadata, bom=True)
        self._refresh_manifest_files(self.actual)
        with self.assertRaises(golden.GoldenError) as mismatch:
            golden.create_baseline(self.actual, self.baseline)
        self.assertIn("metadata objectPath does not match", str(mismatch.exception))

    def test_force_refuses_unmanaged_baseline_directory(self) -> None:
        self.baseline.mkdir(parents=True)
        sentinel = self.baseline / "sentinel.txt"
        sentinel.write_text("keep\n", encoding="utf-8")

        with self.assertRaises(golden.GoldenError) as raised:
            golden.create_baseline(self.actual, self.baseline, force=True)

        self.assertIn("unmanaged baseline", str(raised.exception))
        self.assertEqual("keep\n", sentinel.read_text(encoding="utf-8"))

    def test_temporary_pack_is_rejected(self) -> None:
        temporary = self.root / "ContextPack_Temporary.tmp"
        self._write_pack(temporary, pack_id="ContextPack_Temporary.tmp")

        with self.assertRaises(golden.GoldenError) as raised:
            golden.create_baseline(temporary, self.baseline)

        self.assertIn("Temporary Context Pack", str(raised.exception))

    def test_manifest_size_mismatch_is_rejected(self) -> None:
        manifest_path = self.actual / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["files"][0]["size"] += 1
        self._dump(manifest_path, manifest)

        with self.assertRaises(golden.GoldenError) as raised:
            golden.create_baseline(self.actual, self.baseline)

        self.assertIn("wrong size", str(raised.exception))

    def test_required_asset_must_be_exported_not_only_a_dependency(self) -> None:
        config = self.root / "dependency-cases.json"
        self._dump(
            config,
            {
                "schemaVersion": 1,
                "cases": [
                    {
                        "name": "dependency-is-not-export",
                        "actualPack": str(self.actual),
                        "baseline": str(self.baseline),
                        "requiredAssetPaths": ["/Game/Test/M_Water.M_Water"],
                    }
                ],
            },
        )

        result = golden.run_cases(config, update_baselines=True)

        self.assertFalse(result["ok"])
        self.assertFalse(self.baseline.exists())
        self.assertEqual("missingAssetPath", result["cases"][0]["requirementFailures"][0]["type"])

    def test_cases_validate_required_assets_and_markers(self) -> None:
        golden.create_baseline(self.actual, self.baseline)
        config = self.root / "cases.json"
        self._dump(
            config,
            {
                "schemaVersion": 1,
                "cases": [
                    {
                        "name": "water-smoke",
                        "actualPack": str(self.actual),
                        "baseline": str(self.baseline),
                        "requiredAssetPaths": ["/Game/Test/NS_Water.NS_Water"],
                        "requiredMarkers": [
                            {
                                "file": "Niagara/NS_Water_ReadableNiagara.md",
                                "contains": "Required Golden Marker",
                            }
                        ],
                        "forbiddenMarkers": [
                            {
                                "file": "Niagara/NS_Water_ReadableNiagara.md",
                                "contains": "UnknownExpr(\"MaterialExpressionCustom\"",
                            }
                        ],
                    }
                ],
            },
        )

        result = golden.run_cases(config)

        self.assertTrue(result["ok"])
        self.assertEqual(1, result["summary"]["passed"])
        self.assertNotIn("provenance", result)

    def test_cases_provenance_is_json_validated_and_passed_through(self) -> None:
        golden.create_baseline(self.actual, self.baseline)
        provenance = {
            "project": {"path": "D:/UE5/Trans/Trans.uproject"},
            "engine": {"commandPath": "C:/UE/UnrealEditor-Cmd.exe", "version": "5.7.0"},
            "git": {"head": "a" * 40, "dirty": True},
            "pluginDescriptor": {"sha256": "b" * 64},
            "pluginModule": {"sha256": "c" * 64},
        }
        config = self.root / "provenance-cases.json"
        self._dump(
            config,
            {
                "schemaVersion": 1,
                "provenance": provenance,
                "cases": [
                    {
                        "name": "provenance-water-smoke",
                        "actualPack": str(self.actual),
                        "baseline": str(self.baseline),
                    }
                ],
            },
        )

        result = golden.run_cases(config)

        self.assertTrue(result["ok"])
        self.assertEqual(provenance, result["provenance"])
        human = golden.render_human_report(result)
        self.assertIn('Provenance: {"engine":', human)
        self.assertIn('"dirty":true', human)

    def test_cases_reject_non_object_provenance(self) -> None:
        config = self.root / "invalid-provenance-cases.json"
        self._dump(config, {"schemaVersion": 1, "provenance": ["not", "an", "object"], "cases": []})

        with self.assertRaises(golden.GoldenError) as raised:
            golden.run_cases(config)

        self.assertIn("provenance must be a JSON object", str(raised.exception))

    def test_cases_reject_non_standard_json_provenance_values(self) -> None:
        config = self.root / "nan-provenance-cases.json"
        config.write_text(
            '{"schemaVersion":1,"provenance":{"invalid":NaN},"cases":[]}',
            encoding="utf-8",
        )

        with self.assertRaises(golden.GoldenError) as raised:
            golden.run_cases(config)

        self.assertIn("provenance is not valid JSON", str(raised.exception))

    def test_cases_reject_forbidden_markers_before_baseline_update(self) -> None:
        config = self.root / "forbidden-cases.json"
        self._dump(
            config,
            {
                "schemaVersion": 1,
                "cases": [
                    {
                        "name": "forbidden-water-smoke",
                        "actualPack": str(self.actual),
                        "baseline": str(self.baseline),
                        "forbiddenMarkers": [
                            {
                                "file": "Niagara/NS_Water_ReadableNiagara.md",
                                "contains": "Required Golden Marker",
                            }
                        ],
                    }
                ],
            },
        )

        result = golden.run_cases(config, update_baselines=True)

        self.assertFalse(result["ok"])
        self.assertFalse(self.baseline.exists())
        failures = result["cases"][0]["requirementFailures"]
        self.assertEqual("forbiddenMarkerPresent", failures[0]["type"])

    def test_cli_create_compare_round_trip_and_reports(self) -> None:
        cli_baseline = self.root / "cli-baseline"
        create_json = self.root / "reports" / "create.json"
        created = subprocess.run(
            [
                sys.executable,
                str(MODULE_PATH),
                "create",
                str(self.actual),
                str(cli_baseline),
                "--format",
                "json",
                "--json-report",
                str(create_json),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(0, created.returncode, created.stderr.decode("utf-8", errors="replace"))
        create_result = json.loads(created.stdout.decode("utf-8"))
        self.assertTrue(create_result["ok"])
        self.assertEqual(create_result, json.loads(create_json.read_text(encoding="utf-8")))

        manifest_path = self.actual / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["createdUtc"] = "2099-01-01T00:00:00Z"
        self._dump(manifest_path, manifest)
        human_report = self.root / "reports" / "compare.txt"
        compared = subprocess.run(
            [
                sys.executable,
                str(MODULE_PATH),
                "compare",
                str(self.actual),
                str(cli_baseline),
                "--format",
                "json",
                "--human-report",
                str(human_report),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(0, compared.returncode, compared.stderr.decode("utf-8", errors="replace"))
        self.assertTrue(json.loads(compared.stdout.decode("utf-8"))["ok"])
        self.assertTrue(human_report.read_text(encoding="utf-8").startswith("PASS"))

        metadata = self.actual / "Niagara" / "NS_Water.meta.json"
        value = json.loads(metadata.read_text(encoding="utf-8-sig"))
        value["assetName"] = "NS_SemanticChange"
        self._dump(metadata, value)
        self._refresh_manifest_files(self.actual)
        failed = subprocess.run(
            [sys.executable, str(MODULE_PATH), "compare", str(self.actual), str(cli_baseline), "--format", "json"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(1, failed.returncode)
        self.assertFalse(json.loads(failed.stdout.decode("utf-8"))["ok"])

    def test_cli_rejects_incomplete_pack_with_operational_exit_code(self) -> None:
        manifest_path = self.actual / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["state"] = "writing"
        self._dump(manifest_path, manifest)

        completed = subprocess.run(
            [
                sys.executable,
                str(MODULE_PATH),
                "create",
                str(self.actual),
                str(self.baseline),
                "--format",
                "json",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        self.assertEqual(golden.EXIT_ERROR, completed.returncode)
        result = json.loads(completed.stdout.decode("utf-8"))
        self.assertFalse(result["ok"])
        self.assertEqual(golden.EXIT_ERROR, result["exitCode"])
        self.assertIn("not complete", result["error"])


if __name__ == "__main__":
    unittest.main()
