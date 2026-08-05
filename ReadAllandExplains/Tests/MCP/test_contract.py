from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

PLUGIN_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = PLUGIN_ROOT / "Integrations" / "MCP" / "readallandexplains_mcp.py"
SKILL_PATH = PLUGIN_ROOT / "skills" / "readallandexplains" / "SKILL.md"
UPLUGIN_PATH = PLUGIN_ROOT / "ReadAllandExplains.uplugin"
CODEBUDDY_PLUGIN_PATH = PLUGIN_ROOT / ".codebuddy-plugin" / "plugin.json"
MARKETPLACE_PATH = PLUGIN_ROOT / ".codebuddy-plugin" / "marketplace.json"
FILTER_PLUGIN_PATH = PLUGIN_ROOT / "Config" / "FilterPlugin.ini"
SPEC = importlib.util.spec_from_file_location("readallandexplains_mcp", MODULE_PATH)
assert SPEC and SPEC.loader
rae = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = rae
SPEC.loader.exec_module(rae)


class ContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "ReadAllandExplainsExports"
        self.packs = self.root / "ContextPacks"
        self.complete = self.packs / "ContextPack_Complete"
        self._write_pack(self.complete, state="complete", schema=1)
        self.store = rae.ContextPackStore(self.root)

    def tearDown(self) -> None:
        self.temp.cleanup()

    @staticmethod
    def _dump(path: Path, value: object) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")

    def _write_pack(self, pack: Path, state: str, schema: int) -> None:
        object_path = "/Game/Test/NS_Test.NS_Test"
        manifest = {
            "schemaVersion": schema,
            "documentType": "ReadAllandExplainsContextPack",
            "state": state,
            "createdUtc": "2026-08-04T00:00:00Z",
            "dependencyDepth": 2,
            "exportedAssetCount": 2,
            "rootAssets": [object_path],
        }
        self._dump(pack / "context-pack.json", manifest)
        self._dump(
            pack / "index.json",
            {
                "assets": [
                    {
                        "name": "NS_Test",
                        "objectPath": object_path,
                        "classPath": "/Script/Niagara.NiagaraSystem",
                        "assetKind": "Niagara",
                        "exportFile": "Niagara/NS_Test_ReadableNiagara.md",
                        "dependencies": [
                            "/Game/Test/M_Test.M_Test",
                            "/Game/Test/M_Missing.M_Missing",
                            "/Engine/Functions/EngineOnly.EngineOnly",
                        ],
                        "referencers": [],
                    },
                    {
                        "name": "M_Test",
                        "objectPath": "/Game/Test/M_Test.M_Test",
                        "classPath": "/Script/Engine.Material",
                        "assetKind": "Material",
                        "exportFile": "Materials/M_Test_ReadableMaterial.md",
                        "dependencies": [],
                        "referencers": [object_path],
                    },
                ]
            },
        )
        self._dump(
            pack / "Niagara" / "NS_Test.meta.json",
            {
                "assetName": "NS_Test",
                "objectPath": object_path,
                "classPath": "/Script/Niagara.NiagaraSystem",
                "assetKind": "Niagara",
                "parameters": [{"name": "User.Intensity", "value": "1.0"}, {"name": "泡沫强度", "value": "0.75"}],
                "dependencies": [
                    "/Game/Test/M_Test.M_Test",
                    "/Game/Test/M_Missing.M_Missing",
                    "/Engine/Functions/EngineOnly.EngineOnly",
                ],
                "referencers": [],
                "graphs": [
                    {"id": "graph-1", "name": "Spawn", "kind": "NiagaraGraph", "nodes": [{"id": "node-1"}], "links": []},
                    {"id": "graph-2", "name": "Update", "kind": "NiagaraGraph", "nodes": [], "links": []},
                    {"id": "graph-3", "name": "Render", "kind": "NiagaraGraph", "nodes": [], "links": []},
                ],
                "niagaraRenderers": [{"id": "renderer-1", "type": "Sprite"}],
                "niagaraCurves": [
                    {
                        "id": "curve-1",
                        "fingerprint": "abc",
                        "exposedName": "Scale",
                        "usedBy": ["Update"],
                        "usageCount": 1,
                        "minTime": 0,
                        "maxTime": 1,
                        "channels": [{"name": "X", "keys": [{"time": 0, "value": 0}, {"time": 1, "value": 1}]}],
                    }
                ],
            },
        )
        readable = pack / "Niagara" / "NS_Test_ReadableNiagara.md"
        readable.parent.mkdir(parents=True, exist_ok=True)
        readable.write_text("# NS_Test\n\nCustom HLSL marker\n泡沫强度控制瀑布浪花。\n", encoding="utf-8-sig")
        self._dump(
            pack / "Materials" / "M_Test.meta.json",
            {
                "assetName": "M_Test",
                "objectPath": "/Game/Test/M_Test.M_Test",
                "classPath": "/Script/Engine.Material",
                "assetKind": "Material",
                "parameters": [],
                "dependencies": [],
                "referencers": [object_path],
                "graphs": [],
            },
        )
        material_readable = pack / "Materials" / "M_Test_ReadableMaterial.md"
        material_readable.parent.mkdir(parents=True, exist_ok=True)
        material_readable.write_text("# M_Test\n", encoding="utf-8")

    def _make_modern_pack(self, pack: Path) -> None:
        (pack / "README.md").write_text("# Context Pack\n", encoding="utf-8")
        (pack / "index.md").write_text("# Index\n", encoding="utf-8")
        index_path = pack / "index.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        index["assetCount"] = len(index["assets"])
        index["assets"][0]["metadataFile"] = "Niagara/NS_Test.meta.json"
        index["assets"][1]["metadataFile"] = "Materials/M_Test.meta.json"
        self._dump(index_path, index)
        manifest_path = pack / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest.update(
            {
                "packId": pack.name,
                "attemptedAssetCount": len(index["assets"]),
                "failedCount": 0,
                "skippedCount": 0,
                "assets": [
                    {
                        "objectPath": item["objectPath"],
                        "exportFile": item["exportFile"],
                        "metadataFile": item["metadataFile"],
                    }
                    for item in index["assets"]
                ],
            }
        )
        manifest["files"] = [
            {
                "path": path.relative_to(pack).as_posix(),
                "size": path.stat().st_size,
                "fingerprint": "sha1:" + hashlib.sha1(path.read_bytes()).hexdigest(),
            }
            for path in sorted(pack.rglob("*"))
            if path.is_file() and path != manifest_path
        ]
        fingerprint_source = "".join(
            f"{item['path']}:{item['size']}:{item['fingerprint'].split(':', 1)[1]}\n"
            for item in manifest["files"]
        )
        manifest["fingerprint"] = "sha1:" + hashlib.sha1(fingerprint_source.encode("utf-8")).hexdigest()
        self._dump(manifest_path, manifest)

    def test_all_tools_publish_read_only_contract(self) -> None:
        self.assertEqual(7, len(rae.TOOLS))
        descriptors = {descriptor["name"]: descriptor for descriptor in rae.TOOLS}
        for name, descriptor in descriptors.items():
            self.assertEqual(name != "request_targeted_snapshot", descriptor["annotations"]["readOnlyHint"])
            self.assertFalse(descriptor["annotations"]["destructiveHint"])
            self.assertEqual(rae.OUTPUT_SCHEMA, descriptor["outputSchema"])
        self.assertFalse(descriptors["request_targeted_snapshot"]["annotations"]["idempotentHint"])
        self.assertTrue(descriptors["get_snapshot_request_status"]["annotations"]["idempotentHint"])

    def test_search_returns_envelope_and_evidence(self) -> None:
        result = rae.call_tool(self.store, "search_assets", {"query": "NS_Test", "limit": 20})
        self.assertEqual("rae.mcp/1.0", result["protocol_version"])
        self.assertEqual("ContextPack_Complete", result["pack"]["pack_id"])
        self.assertEqual("NS_Test", result["data"]["assets"][0]["name"])
        self.assertEqual("index.json", result["evidence"][0]["source_file"])
        self.assertIsNone(result["error"])

    def test_summary_cites_metadata(self) -> None:
        result = rae.call_tool(self.store, "get_asset_summary", {"asset": "NS_Test"})
        self.assertEqual("/Game/Test/NS_Test.NS_Test", result["asset"]["object_path"])
        self.assertEqual(3, len(result["data"]["graphs"]))
        self.assertEqual("Niagara/NS_Test.meta.json", result["evidence"][0]["source_file"])
        self.assertEqual([], result["missing_fields"])

    def test_detail_pagination_is_machine_readable(self) -> None:
        result = rae.call_tool(
            self.store,
            "get_asset_detail",
            {"asset": "NS_Test", "section": "graphs", "offset": 0, "limit": 2},
        )
        self.assertEqual(2, result["page"]["returned"])
        self.assertEqual(3, result["page"]["total"])
        self.assertTrue(result["page"]["truncated"])
        self.assertEqual("2", result["page"]["next_cursor"])

    def test_coverage_lists_targeted_missing_project_dependencies(self) -> None:
        result = rae.call_tool(
            self.store,
            "get_asset_detail",
            {"asset": "NS_Test", "section": "coverage"},
        )
        coverage = result["data"]
        self.assertEqual(1, coverage["exported_dependency_count"])
        self.assertEqual(1, coverage["missing_project_dependency_count"])
        self.assertEqual(1, coverage["external_dependency_count"])
        self.assertEqual("/Game/Test/M_Test.M_Test", coverage["exported_dependencies"][0]["path"])
        self.assertEqual("/Game/Test/M_Missing.M_Missing", coverage["suggested_capture"][0]["asset_path"])
        self.assertEqual(["dependency_assets"], result["missing_fields"])
        self.assertEqual("/dependencies", result["evidence"][0]["json_pointer"])

    def test_utf8_chinese_round_trip_has_no_replacement_characters(self) -> None:
        detail = rae.call_tool(
            self.store,
            "get_asset_detail",
            {"asset": "NS_Test", "section": "readable", "offset": 0, "limit": 1000},
        )
        self.assertIn("泡沫强度控制瀑布浪花", detail["data"]["text"])
        self.assertNotIn("�", detail["data"]["text"])
        search = rae.call_tool(self.store, "search_export_text", {"query": "瀑布浪花", "asset": "NS_Test"})
        self.assertEqual(1, search["page"]["total"])
        self.assertIn("泡沫强度", search["data"]["hits"][0]["snippet"])

    def test_invalid_readable_encoding_is_reported_instead_of_replaced(self) -> None:
        readable = self.complete / "Niagara" / "NS_Test_ReadableNiagara.md"
        readable.write_bytes(b"# NS_Test\n\xff\xfe")
        with self.assertRaises(rae.RaeError) as raised:
            self.store.detail("NS_Test", "readable", None, None, 0, 100)
        self.assertEqual("TEXT_ENCODING_INVALID", raised.exception.code)

    def test_targeted_snapshot_requires_explicit_permission(self) -> None:
        fingerprint = self.store.pack_info(self.complete)[0]["fingerprint"]
        with self.assertRaises(rae.RaeError) as raised:
            self.store.submit_synclive_request(
                ["/Game/Test/M_Missing.M_Missing"],
                fingerprint,
                False,
                request_id="permission-test",
            )
        self.assertEqual("PERMISSION_REQUIRED", raised.exception.code)
        self.assertFalse((self.root / "SyncLive" / "Pending" / "permission-test.json").exists())

    def test_targeted_snapshot_is_bounded_bound_to_pack_and_atomic(self) -> None:
        pack_info = self.store.pack_info(self.complete)[0]
        result = rae.call_tool(
            self.store,
            "request_targeted_snapshot",
            {
                "asset_paths": ["/Game/Test/M_Missing.M_Missing"],
                "base_pack_fingerprint": pack_info["fingerprint"],
                "permission_granted": True,
                "dependency_depth": 0,
                "pack_path": pack_info["pack_id"],
                "request_id": "capture-test",
                "reason": "补充缺失材质",
            },
        )
        self.assertEqual("pending", result["data"]["state"])
        self.assertEqual(pack_info["pack_id"], result["pack"]["pack_id"])
        pending = self.root / "SyncLive" / "Pending" / "capture-test.json"
        request = json.loads(pending.read_text(encoding="utf-8"))
        self.assertTrue(request["permissionGranted"])
        self.assertEqual(pack_info["fingerprint"], request["basePackFingerprint"])
        self.assertEqual(["/Game/Test/M_Missing.M_Missing"], request["assetPaths"])
        self.assertEqual("补充缺失材质", request["reason"])
        self.assertFalse(pending.with_name(pending.name + ".tmp").exists())

        status = rae.call_tool(self.store, "get_snapshot_request_status", {"request_id": "capture-test"})
        self.assertEqual("pending", status["data"]["state"])
        self.assertEqual("SyncLive/Pending/capture-test.json", status["evidence"][0]["source_file"])

    def test_targeted_snapshot_rejects_stale_fingerprint_and_unsafe_scope(self) -> None:
        with self.assertRaises(rae.RaeError) as stale:
            self.store.submit_synclive_request(
                ["/Game/Test/M_Missing.M_Missing"],
                "blake3-160:stale",
                True,
                request_id="stale-test",
            )
        self.assertEqual("BASE_PACK_FINGERPRINT_MISMATCH", stale.exception.code)

        fingerprint = self.store.pack_info(self.complete)[0]["fingerprint"]
        invalid_cases = [
            (["/Engine/Test.Asset"], 0, "ASSET_PATH_INVALID"),
            ([f"/Game/Test/M_{index}.M_{index}" for index in range(6)], 0, "ASSET_LIMIT_INVALID"),
            (["/Game/Test/M_Missing.M_Missing"], 2, "DEPENDENCY_DEPTH_INVALID"),
        ]
        for index, (assets, depth, expected_code) in enumerate(invalid_cases):
            with self.subTest(index=index), self.assertRaises(rae.RaeError) as raised:
                self.store.submit_synclive_request(assets, fingerprint, True, depth, request_id=f"invalid-{index}")
            self.assertEqual(expected_code, raised.exception.code)

    def test_targeted_snapshot_request_id_is_idempotent_or_conflicting(self) -> None:
        fingerprint = self.store.pack_info(self.complete)[0]["fingerprint"]
        arguments = (["/Game/Test/M_Missing.M_Missing"], fingerprint, True)
        first = self.store.submit_synclive_request(*arguments, request_id="stable-request", reason="same")
        second = self.store.submit_synclive_request(*arguments, request_id="stable-request", reason="same")
        self.assertEqual("pending", first["data"]["state"])
        self.assertTrue(second["data"]["idempotent"])
        with self.assertRaises(rae.RaeError) as raised:
            self.store.submit_synclive_request(
                ["/Game/Test/M_Other.M_Other"],
                fingerprint,
                True,
                request_id="stable-request",
                reason="different",
            )
        self.assertEqual("REQUEST_ID_CONFLICT", raised.exception.code)

    def test_completed_snapshot_status_resolves_new_complete_pack(self) -> None:
        completed_pack = self.packs / "ContextPack_Targeted"
        self._write_pack(completed_pack, state="complete", schema=1)
        result_path = self.root / "SyncLive" / "Results" / "completed-request.json"
        self._dump(
            result_path,
            {
                "schemaVersion": 1,
                "requestId": "completed-request",
                "state": "complete",
                "outputPackId": "ContextPack_Targeted",
                "basePackId": "ContextPack_Complete",
            },
        )
        result = rae.call_tool(self.store, "get_snapshot_request_status", {"request_id": "completed-request"})
        self.assertEqual("complete", result["data"]["request"]["state"])
        self.assertEqual("ContextPack_Targeted", result["pack"]["pack_id"])
        self.assertEqual("SyncLive/Results/completed-request.json", result["evidence"][0]["source_file"])

    def test_skill_requires_progressive_budgeted_missing_dependency_flow(self) -> None:
        skill = SKILL_PATH.read_text(encoding="utf-8-sig")
        for marker in ("拆分任务", "最多 5 个资产", "coverage", "阻塞结论", "补充计划", "明确许可", "本轮已确认", "美术含义与建议"):
            self.assertIn(marker, skill)

    def test_product_versions_and_release_package_are_consistent(self) -> None:
        uplugin = json.loads(UPLUGIN_PATH.read_text(encoding="utf-8"))
        codebuddy_plugin = json.loads(CODEBUDDY_PLUGIN_PATH.read_text(encoding="utf-8"))
        marketplace = json.loads(MARKETPLACE_PATH.read_text(encoding="utf-8"))
        version = uplugin["VersionName"]
        self.assertEqual(version, codebuddy_plugin["version"])
        self.assertEqual(version, marketplace["plugins"][0]["version"])
        self.assertIn(f"/RELEASE_NOTES_{version}.md", FILTER_PLUGIN_PATH.read_text(encoding="utf-8-sig"))
        self.assertTrue((PLUGIN_ROOT / f"RELEASE_NOTES_{version}.md").is_file())

    def test_repository_management_assets_exist(self) -> None:
        self.assertTrue((PLUGIN_ROOT / "docs" / "REPOSITORY_MANAGEMENT.md").is_file())
        self.assertTrue((PLUGIN_ROOT / "Scripts" / "SyncWorkspaceSkill.ps1").is_file())
        skill_header = SKILL_PATH.read_text(encoding="utf-8-sig").split("---", 2)[1]
        self.assertNotIn("allowed-tools:", skill_header)
        self.assertNotIn("disable:", skill_header)

    def test_error_uses_stable_code(self) -> None:
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "get_asset_summary", "arguments": {"asset": "Missing"}},
        }
        response = rae.handle_request(self.store, request)
        assert response
        result = response["result"]
        self.assertTrue(result["isError"])
        self.assertEqual("ASSET_NOT_FOUND", result["structuredContent"]["error"]["code"])

    def test_latest_resolution_skips_writing_pack(self) -> None:
        writing = self.packs / "ContextPack_Writing"
        self._write_pack(writing, state="writing", schema=1)
        timestamp = self.complete.stat().st_mtime + 10
        os.utime(writing, (timestamp, timestamp))
        resolved = self.store.resolve_pack()
        self.assertEqual(self.complete.resolve(), resolved)

    def test_list_excludes_writing_and_temporary_packs(self) -> None:
        writing = self.packs / "ContextPack_Writing"
        temporary = self.packs / "ContextPack_Temporary.tmp"
        self._write_pack(writing, state="writing", schema=1)
        self._write_pack(temporary, state="complete", schema=1)
        result = self.store.list_packs()
        names = [item["pack_id"] for item in result["data"]["packs"]]
        self.assertEqual(["ContextPack_Complete"], names)
        self.assertTrue(any(warning["code"] == "PACK_SKIPPED" for warning in result["warnings"]))

    def test_explicit_and_default_resolution_reject_temporary_pack(self) -> None:
        temporary = self.packs / "ContextPack_Temporary.tmp"
        self._write_pack(temporary, state="complete", schema=1)
        with self.assertRaises(rae.RaeError) as explicit:
            self.store.resolve_pack("ContextPack_Temporary.tmp")
        self.assertEqual("PACK_INCOMPLETE", explicit.exception.code)
        timestamp = self.complete.stat().st_mtime + 10
        os.utime(temporary, (timestamp, timestamp))
        self.assertEqual(self.complete.resolve(), self.store.resolve_pack())

    def test_complete_pack_with_failed_or_skipped_assets_is_rejected(self) -> None:
        manifest_path = self.complete / "context-pack.json"
        for field in ("failedCount", "skippedCount"):
            with self.subTest(field=field):
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                manifest[field] = 1
                self._dump(manifest_path, manifest)
                with self.assertRaises(rae.RaeError) as raised:
                    self.store.resolve_pack("ContextPack_Complete")
                self.assertEqual("PACK_INCOMPLETE", raised.exception.code)
                manifest[field] = 0
                self._dump(manifest_path, manifest)

    def test_modern_pack_strict_manifest_and_metadata_mapping_are_validated(self) -> None:
        self._make_modern_pack(self.complete)
        self.assertEqual(self.complete.resolve(), self.store.resolve_pack("ContextPack_Complete"))

        metadata_path = self.complete / "Niagara" / "NS_Test.meta.json"
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        metadata["objectPath"] = "/Game/Wrong/NS_Test.NS_Test"
        self._dump(metadata_path, metadata)
        manifest_path = self.complete / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for entry in manifest["files"]:
            if entry["path"] == "Niagara/NS_Test.meta.json":
                entry["size"] = metadata_path.stat().st_size
        self._dump(manifest_path, manifest)
        with self.assertRaises(rae.RaeError) as raised:
            self.store.resolve_pack("ContextPack_Complete")
        self.assertEqual("PACK_INCOMPLETE", raised.exception.code)

    def test_modern_pack_requires_metadata_mapping_and_valid_content_fingerprint(self) -> None:
        self._make_modern_pack(self.complete)
        index_path = self.complete / "index.json"
        manifest_path = self.complete / "context-pack.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        index["assets"][0].pop("metadataFile")
        self._dump(index_path, index)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["assets"][0].pop("metadataFile")
        for entry in manifest["files"]:
            path = self.complete / entry["path"]
            if entry["path"] == "index.json":
                entry["size"] = path.stat().st_size
                entry["fingerprint"] = "sha1:" + hashlib.sha1(path.read_bytes()).hexdigest()
        fingerprint_source = "".join(
            f"{item['path']}:{item['size']}:{item['fingerprint'].split(':', 1)[1]}\n"
            for item in manifest["files"]
        )
        manifest["fingerprint"] = "sha1:" + hashlib.sha1(fingerprint_source.encode("utf-8")).hexdigest()
        self._dump(manifest_path, manifest)
        with self.assertRaises(rae.RaeError) as missing_mapping:
            self.store.resolve_pack("ContextPack_Complete")
        self.assertEqual("PACK_INCOMPLETE", missing_mapping.exception.code)

        self._write_pack(self.complete, state="complete", schema=1)
        self._make_modern_pack(self.complete)
        readable = self.complete / "Materials" / "M_Test_ReadableMaterial.md"
        original = readable.read_bytes()
        readable.write_bytes(bytes([original[0] ^ 1]) + original[1:])
        with self.assertRaises(rae.RaeError) as fingerprint_mismatch:
            self.store.resolve_pack("ContextPack_Complete")
        self.assertEqual("PACK_INCOMPLETE", fingerprint_mismatch.exception.code)

    def test_manifest_fingerprint_is_preferred(self) -> None:
        manifest_path = self.complete / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["fingerprint"] = "blake3-160:fixture"
        self._dump(manifest_path, manifest)
        info, _ = self.store.pack_info(self.complete)
        self.assertEqual("blake3-160:fixture", info["fingerprint"])

    def test_manifest_file_size_mismatch_is_rejected(self) -> None:
        manifest_path = self.complete / "context-pack.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["files"] = [{"path": "index.json", "size": 1, "fingerprint": "blake3-160:fixture"}]
        self._dump(manifest_path, manifest)
        with self.assertRaises(rae.RaeError) as raised:
            self.store.resolve_pack("ContextPack_Complete")
        self.assertEqual("PACK_INCOMPLETE", raised.exception.code)

    def test_explicit_incomplete_pack_is_rejected(self) -> None:
        writing = self.packs / "ContextPack_Writing"
        self._write_pack(writing, state="writing", schema=1)
        with self.assertRaises(rae.RaeError) as raised:
            self.store.resolve_pack("ContextPack_Writing")
        self.assertEqual("PACK_INCOMPLETE", raised.exception.code)

    def test_unsupported_schema_is_rejected(self) -> None:
        unsupported = self.packs / "ContextPack_Unsupported"
        self._write_pack(unsupported, state="complete", schema=99)
        with self.assertRaises(rae.RaeError) as raised:
            self.store.resolve_pack("ContextPack_Unsupported")
        self.assertEqual("SCHEMA_UNSUPPORTED", raised.exception.code)

    def test_pack_path_cannot_escape_root(self) -> None:
        outside = Path(self.temp.name) / "Outside"
        self._write_pack(outside, state="complete", schema=1)
        with self.assertRaises(rae.RaeError) as raised:
            self.store.resolve_pack(str(outside))
        self.assertEqual("PACK_OUTSIDE_ROOT", raised.exception.code)

    def test_protocol_compatibility_is_validated(self) -> None:
        supported = rae.handle_request(
            self.store,
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25"}},
        )
        assert supported
        self.assertEqual("2025-11-25", supported["result"]["protocolVersion"])

        response = rae.handle_request(
            self.store,
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2099-01-01"}},
        )
        assert response
        self.assertEqual(-32602, response["error"]["code"])
        self.assertEqual("PROTOCOL_UNSUPPORTED", response["error"]["data"]["errorCode"])

    def test_text_search_returns_line_evidence(self) -> None:
        result = rae.call_tool(self.store, "search_export_text", {"query": "Custom HLSL", "asset": "NS_Test"})
        self.assertEqual(1, result["page"]["total"])
        self.assertEqual(3, result["evidence"][0]["line"])
        self.assertEqual("Niagara/NS_Test_ReadableNiagara.md", result["evidence"][0]["source_file"])

    def test_codebuddy_project_root_discovers_nested_unreal_project(self) -> None:
        workspace = Path(self.temp.name) / "Workspace"
        project = workspace / "Games" / "Trans"
        (project / "Trans.uproject").parent.mkdir(parents=True, exist_ok=True)
        (project / "Trans.uproject").write_text("{}", encoding="utf-8")
        discovered_pack = project / "Saved" / "ReadAllandExplainsExports" / "ContextPacks" / "ContextPack_CodeBuddy"
        self._write_pack(discovered_pack, state="complete", schema=1)
        with mock.patch.dict(
            os.environ,
            {"CODEBUDDY_PROJECT_DIR": str(workspace), "READALL_EXPORT_ROOT": ""},
            clear=False,
        ):
            self.assertEqual(
                (project / "Saved" / "ReadAllandExplainsExports").resolve(),
                rae.default_export_root(),
            )

    def test_export_root_environment_override_has_priority(self) -> None:
        explicit = Path(self.temp.name) / "ExplicitExports"
        with mock.patch.dict(os.environ, {"READALL_EXPORT_ROOT": str(explicit)}, clear=False):
            self.assertEqual(explicit.resolve(), rae.default_export_root().resolve())

    def test_stdio_emits_utf8_when_windows_text_encoding_is_cp936(self) -> None:
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "get_asset_summary", "arguments": {"asset": "NS_Test"}},
        }
        environment = os.environ.copy()
        environment["PYTHONIOENCODING"] = "cp936"
        completed = subprocess.run(
            [sys.executable, str(MODULE_PATH), "--root", str(self.root)],
            input=(json.dumps(request, ensure_ascii=False) + "\n").encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
            env=environment,
        )
        response = json.loads(completed.stdout.decode("utf-8"))
        data = response["result"]["structuredContent"]["data"]
        self.assertTrue(any(parameter.get("name") == "泡沫强度" for parameter in data["parameters"]))
        self.assertIn("泡沫强度".encode("utf-8"), completed.stdout)


if __name__ == "__main__":
    unittest.main()