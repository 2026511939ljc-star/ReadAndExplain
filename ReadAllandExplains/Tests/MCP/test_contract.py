from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[2] / "Integrations" / "MCP" / "readallandexplains_mcp.py"
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
            "exportedAssetCount": 1,
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
                        "dependencies": ["/Game/Test/M_Test.M_Test"],
                        "referencers": [],
                    }
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
                "parameters": [{"name": "User.Intensity", "value": "1.0"}],
                "dependencies": ["/Game/Test/M_Test.M_Test"],
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
        readable.write_text("# NS_Test\n\nCustom HLSL marker\n", encoding="utf-8")

    def test_all_tools_publish_read_only_contract(self) -> None:
        self.assertEqual(5, len(rae.TOOLS))
        for descriptor in rae.TOOLS:
            self.assertTrue(descriptor["annotations"]["readOnlyHint"])
            self.assertFalse(descriptor["annotations"]["destructiveHint"])
            self.assertEqual(rae.OUTPUT_SCHEMA, descriptor["outputSchema"])

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


if __name__ == "__main__":
    unittest.main()
