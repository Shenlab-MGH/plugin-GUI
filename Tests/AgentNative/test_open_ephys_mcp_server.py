import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "agent_native"))

CONTRACT_PATH = ROOT / "agent_native" / "open_ephys_agent_contract_v1_0_2.json"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"

import open_ephys_mcp_server as mcp


class AgentNativeManifestTests(unittest.TestCase):
    def setUp(self):
        self.manifest = mcp.load_manifest(ROOT / "agent_native" / "open_ephys_agent_surface.json")

    def test_manifest_has_unique_commands_and_no_screenshot_control(self):
        names = [command["name"] for command in self.manifest["commands"]]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(self.manifest["transport"]["screenshot_control"], "not_required")

    def test_every_write_like_command_is_exposed_as_post_command(self):
        for command in self.manifest["commands"]:
            method = command["http"]["method"]
            if method in {"PUT", "POST"} or command["name"] in {"clear_processors", "undo", "redo"}:
                self.assertTrue(command.get("post_command"), command["name"])

    def test_parameter_routes_render_raw_names_as_one_percent_encoded_segment(self):
        commands = mcp.command_index(self.manifest)
        expected_paths = {
            "get_parameter": "/api/processors/101/parameters/gain%20mode",
            "set_parameter": "/api/processors/101/parameters/gain%20mode",
            "get_stream_parameter": "/api/processors/101/streams/7/parameters/gain%20mode",
            "set_stream_parameter": "/api/processors/101/streams/7/parameters/gain%20mode",
        }

        for name, expected_path in expected_paths.items():
            with self.subTest(command=name):
                self.assertEqual(
                    mcp.render_path(
                        commands[name],
                        {"processor_id": 101, "stream_index": 7, "parameter_name": "gain mode"},
                    ),
                    expected_path,
                )

        self.assertEqual(
            mcp.render_path(
                commands["get_parameter"],
                {"processor_id": 101, "parameter_name": "%20"},
            ),
            "/api/processors/101/parameters/%2520",
        )

    def test_parameter_routes_reject_unsafe_raw_parameter_name_segments(self):
        commands = mcp.command_index(self.manifest)
        for name in ("get_parameter", "set_parameter", "get_stream_parameter", "set_stream_parameter"):
            for parameter_name in ("", "bad/name", "bad\\name", ".", "..", "bad\nname"):
                with self.subTest(command=name, parameter_name=repr(parameter_name)):
                    with self.assertRaisesRegex(ValueError, "(?i)parameter name"):
                        mcp.render_path(
                            commands[name],
                            {
                                "processor_id": 101,
                                "stream_index": 7,
                                "parameter_name": parameter_name,
                            },
                        )

    def test_parameter_name_segment_policy_does_not_apply_to_other_path_fields(self):
        command = mcp.command_index(self.manifest)["get_processor"]
        self.assertEqual(
            mcp.render_path(command, {"processor_id": "101/diagnostic"}),
            "/api/processors/101%2Fdiagnostic",
        )

    def test_execute_command_maps_post_style_to_compatibility_http_route(self):
        with mock.patch.object(mcp, "call_http") as call_http:
            call_http.return_value = {"ok": True, "status": 200, "response": {"mode": "ACQUIRE"}}
            result = mcp.execute_command(
                "set_mode",
                {"mode": "ACQUIRE"},
                manifest=self.manifest,
            )

        call_http.assert_called_once_with(
            "PUT",
            "/api/status",
            {"mode": "ACQUIRE"},
            base_url=mcp.DEFAULT_BASE_URL,
            timeout=5.0,
        )
        self.assertEqual(result["agent_command_style"], "POST")
        self.assertEqual(result["capability"], "oe.control.acquisition")

    def test_recording_settings_do_not_claim_the_record_toggle_identity(self):
        settings_capabilities = {
            "oe.control.recording.directory",
            "oe.control.recording.filename",
            "oe.control.recording.engine",
        }
        for name in ("get_recording", "set_recording"):
            command = mcp.command_index(self.manifest)[name]
            self.assertNotEqual(command.get("capability"), "oe.control.recording")
            self.assertEqual(set(command["capabilities"]), settings_capabilities)

    def test_set_mode_rejects_invalid_mode_without_http(self):
        with mock.patch.object(mcp, "call_http") as call_http:
            with self.assertRaises(ValueError):
                mcp.execute_command(
                    "set_mode",
                    {"mode": "RUNNING"},
                    manifest=self.manifest,
                )

        call_http.assert_not_called()

    def test_set_mode_record_requires_explicit_confirmation(self):
        with mock.patch.object(mcp, "call_http") as call_http:
            with self.assertRaises(ValueError):
                mcp.execute_command(
                    "set_mode",
                    {"mode": "RECORD"},
                    manifest=self.manifest,
                )

            call_http.assert_not_called()
            call_http.return_value = {"ok": True, "status": 200, "response": {"mode": "RECORD"}}
            result = mcp.execute_command(
                "set_mode",
                {"mode": "RECORD", "confirm_recording": True},
                manifest=self.manifest,
            )

        call_http.assert_called_once_with(
            "PUT",
            "/api/status",
            {"mode": "RECORD"},
            base_url=mcp.DEFAULT_BASE_URL,
            timeout=5.0,
        )
        self.assertEqual(result["capability"], "oe.control.acquisition")

    def test_raw_api_request_blocks_quit_and_unconfirmed_record(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")

        with self.assertRaises(ValueError):
            server._call_tool(
                {
                    "name": "oe_api_request",
                    "arguments": {"method": "PUT", "path": "/api/quit"},
                }
            )

        with self.assertRaises(ValueError):
            server._call_tool(
                {
                    "name": "oe_api_request",
                    "arguments": {
                        "method": "PUT",
                        "path": "/api/status",
                        "body": {"mode": "RECORD"},
                    },
                }
            )

    def test_mcp_rejects_non_loopback_base_urls(self):
        with self.assertRaises(ValueError):
            mcp.validate_base_url("https://example.invalid")

    def test_mcp_lists_tools_and_resolves_uia_locator(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        response = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        tool_names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertIn("oe_post_command", tool_names)
        self.assertIn("oe_uia_locator", tool_names)
        uia_tool = next(tool for tool in response["result"]["tools"] if tool["name"] == "oe_uia_locator")
        self.assertIn("automation_id", uia_tool["inputSchema"]["properties"])
        self.assertNotIn("required", uia_tool["inputSchema"])
        self.assertEqual(
            uia_tool["inputSchema"]["oneOf"],
            [{"required": ["capability"]}, {"required": ["automation_id"]}],
        )
        self.assertNotIn("anyOf", uia_tool["inputSchema"])

        response = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {
                    "name": "oe_uia_locator",
                    "arguments": {"capability": "oe.control.recording"},
                },
            }
        )
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(payload["automation_id"], "oe.control.recording")
        self.assertEqual(payload["transport"], "windows_uia")
        self.assertEqual(payload["rule"], self.manifest["uia"]["automation_id_rule"])

        root_response = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "name": "oe_uia_locator",
                    "arguments": {"capability": self.manifest["uia"]["root_id"]},
                },
            }
        )
        self.assertNotIn("error", root_response)
        root_payload = json.loads(root_response["result"]["content"][0]["text"])
        self.assertEqual(root_payload["automation_id"], self.manifest["uia"]["root_id"])
        self.assertEqual(root_payload["rule"], self.manifest["uia"]["automation_id_rule"])

    def test_mcp_initialize_derives_server_version_from_loaded_contract(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")

        response = server.handle({"jsonrpc": "2.0", "id": 3, "method": "initialize"})

        self.assertEqual(response["result"]["serverInfo"]["version"], "0.1.2")

    def test_uia_locator_allows_only_declared_static_ids_or_parameter_automation_ids(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")

        dynamic_response = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "tools/call",
                "params": {
                    "name": "oe_uia_locator",
                    "arguments": {"automation_id": "oe.parameter.gain"},
                },
            }
        )
        self.assertNotIn("error", dynamic_response)
        dynamic_payload = json.loads(dynamic_response["result"]["content"][0]["text"])
        self.assertEqual(dynamic_payload["automation_id"], "oe.parameter.gain")
        self.assertEqual(
            dynamic_payload["rule"],
            server.manifest["uia"]["parameter_automation_id_rule"],
        )

        catalog_response = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 6,
                "method": "tools/call",
                "params": {
                    "name": "oe_uia_locator",
                    "arguments": {
                        "automation_id": "oe.processor_catalog.bandpass_filter",
                    },
                },
            }
        )
        self.assertNotIn("error", catalog_response)
        catalog_payload = json.loads(catalog_response["result"]["content"][0]["text"])
        self.assertEqual(
            catalog_payload["rule"],
            server.manifest["uia"]["processor_catalog_automation_id_rule"],
        )

        for invalid_arguments in (
            {"capability": "oe.processor.parameter"},
            {"automation_id": "oe.control.recording"},
            {"automation_id": "oe.parameter."},
            {"automation_id": "oe.processor_catalog."},
            {"automation_id": "oe.processor_catalog.Bandpass"},
            {"automation_id": "oe.processor_catalog.bandpass-filter"},
            {"automation_id": "anything-else"},
        ):
            with self.subTest(arguments=invalid_arguments):
                response = server.handle(
                    {
                        "jsonrpc": "2.0",
                        "id": 5,
                        "method": "tools/call",
                        "params": {"name": "oe_uia_locator", "arguments": invalid_arguments},
                    }
                )
                self.assertIn("error", response)


class AgentNativeContractParityTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(CONTRACT_PATH.is_file(), str(CONTRACT_PATH))
        self.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.manifest = mcp.load_manifest(ROOT / "agent_native" / "open_ephys_agent_surface.json")

    def test_contract_metadata_matches_manifest_baseline_and_version(self):
        contract = self.contract["contract"]
        self.assertEqual(contract["id"], "open-ephys-agent")
        self.assertEqual(contract["version"], "0.1.2")
        self.assertEqual(self.contract["scope"], "core")
        self.assertEqual(self.manifest["contract"], contract)
        self.assertEqual(self.manifest["baseline"], self.contract["baseline"])
        self.assertEqual(
            self.manifest["transport"]["http_base_url"],
            self.contract["transport"]["http_base_url"],
        )

    def test_contract_declares_canonical_parameter_name_segment_policy(self):
        policy = {
            "field": "parameter_name",
            "input": "raw",
            "render": "percent_encode_utf8_once",
            "reject": ["empty", "slash", "backslash", "dot_segment", "control_character"],
        }
        self.assertEqual(self.contract["api"]["parameter_name_segment_policy"], policy)
        self.assertEqual(self.manifest["parameter_name_segment_policy"], policy)

    def test_contract_declares_parameter_response_and_dynamic_uia_rule(self):
        self.assertIn("parameter_response", self.contract["api"])
        self.assertIn("parameter_response", self.manifest)
        parameter_response = self.contract["api"]["parameter_response"]
        self.assertEqual(self.manifest["parameter_response"], parameter_response)
        self.assertEqual(
            parameter_response["required_fields"],
            [
                "name",
                "type",
                "value",
                "key",
                "display_name",
                "description",
                "enabled",
                "deactivate_during_acquisition",
                "uia.automation_id",
            ],
        )
        self.assertEqual(
            self.contract["uia"]["parameter_automation_id_rule"],
            "oe.parameter.<sanitised parameter key>",
        )
        self.assertEqual(self.manifest["uia"]["parameter_automation_id_rule"], "oe.parameter.<sanitised parameter key>")
        self.assertIn("processor_catalog_automation_id_rule", self.contract["uia"])
        self.assertIn("processor_catalog_automation_id_rule", self.manifest["uia"])
        self.assertEqual(
            self.contract["uia"].get("processor_catalog_automation_id_rule"),
            "oe.processor_catalog.<sanitised processor slug>",
        )
        self.assertEqual(
            self.manifest["uia"].get("processor_catalog_automation_id_rule"),
            "oe.processor_catalog.<sanitised processor slug>",
        )

    def test_skill_matches_parameter_contract_discovery_workflow(self):
        skill = SKILL_PATH.read_text(encoding="utf-8")

        self.assertIn("`open-ephys-agent` contract `0.1.2`", skill)
        self.assertIn("`get_stream_parameters`, `get_parameter`, or `get_stream_parameter` first.", skill)
        self.assertIn("Pass that returned\n`uia.automation_id` to `oe_uia_locator` as `automation_id`.", skill)
        self.assertIn("do not construct or guess them from a parameter name or key", skill)
        self.assertIn("use its returned\n`key` as the raw `parameter_name`", skill)
        self.assertIn("one percent-encoded path segment", skill)
        self.assertIn("does not validate a real\ndevice", skill)

    def test_contract_route_matrix_matches_manifest_for_required_commands(self):
        commands = mcp.command_index(self.manifest)
        route_matrix = self.contract["api"]["required_routes"]
        route_names = [entry["command"] for entry in route_matrix]
        self.assertEqual(len(route_names), len(set(route_names)))
        route_keys = [(entry["method"], entry["path"]) for entry in route_matrix]
        self.assertEqual(len(route_keys), len(set(route_keys)))

        for entry in route_matrix:
            self.assertIn(entry["command"], commands)
            command = commands[entry["command"]]
            http = command["http"]
            self.assertEqual(http["method"], entry["method"], entry["command"])
            self.assertEqual(
                http.get("path", http.get("path_template")),
                entry["path"],
                entry["command"],
            )
            declared = command.get("capabilities") or [command.get("capability")]
            self.assertIn(entry["capability"], declared, entry["command"])

    def test_contract_route_matrix_covers_all_parameter_get_and_put_routes(self):
        route_names = {
            entry["command"] for entry in self.contract["api"]["required_routes"]
        }
        self.assertTrue(
            {
                "get_parameter",
                "set_parameter",
                "get_stream_parameter",
                "set_stream_parameter",
            }.issubset(route_names)
        )

    def test_contract_declares_unique_manifest_routes_and_command_count(self):
        routes = []
        for command in self.manifest["commands"]:
            http = command["http"]
            path = http.get("path", http.get("path_template"))
            routes.append((http["method"], path))

        self.assertEqual(len(routes), len(set(routes)))
        self.assertEqual(self.contract["api"]["command_count"], len(self.manifest["commands"]))

    def test_contract_uia_ids_match_manifest_and_source(self):
        uia = self.contract["uia"]
        self.assertEqual(self.manifest["uia"]["root_id"], uia["root_id"])
        self.assertEqual(
            set(self.manifest["uia"]["capability_ids"]),
            {
                entry["id"]
                for entry in uia["required_ids"]
                if entry["id"] != uia["root_id"]
            },
        )

        for entry in uia["required_ids"]:
            source = ROOT / entry["source"]
            self.assertTrue(source.is_file(), entry["source"])
            self.assertIn(entry["id"], source.read_text(encoding="utf-8"), entry["id"])

    def test_default_manifest_loads_its_versioned_contract(self):
        loaded = mcp.load_manifest(ROOT / "agent_native" / "open_ephys_agent_surface.json")
        self.assertEqual(loaded["contract"], self.contract["contract"])

    def test_manifest_rejects_a_mismatched_contract_fixture(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            manifest["contract"]["fixture"] = "contract.json"
            fixture = json.loads(json.dumps(self.contract))
            fixture["contract"]["version"] = "9.9.9"
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "contract"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_mismatched_parameter_uia_contract(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            fixture = json.loads(json.dumps(self.contract))
            manifest["contract"]["fixture"] = "contract.json"
            fixture["contract"]["fixture"] = "contract.json"
            manifest["uia"]["parameter_automation_id_rule"] = "oe.parameter.<wrong key>"
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "parameter UIA"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_mismatched_processor_catalog_uia_contract(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            fixture = json.loads(json.dumps(self.contract))
            manifest["contract"]["fixture"] = "contract.json"
            fixture["contract"]["fixture"] = "contract.json"
            manifest["uia"]["processor_catalog_automation_id_rule"] = (
                "oe.processor_catalog.<wrong slug>"
            )
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "processor catalog UIA"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_mismatched_parameter_response_contract(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            fixture = json.loads(json.dumps(self.contract))
            manifest["contract"]["fixture"] = "contract.json"
            fixture["contract"]["fixture"] = "contract.json"
            manifest["parameter_response"] = {"required_fields": ["name"]}
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "parameter response"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_mismatched_parameter_name_segment_policy(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            fixture = json.loads(json.dumps(self.contract))
            manifest["contract"]["fixture"] = "contract.json"
            fixture["contract"]["fixture"] = "contract.json"
            manifest["parameter_name_segment_policy"]["input"] = "decoded"
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "parameter name segment policy"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_noncanonical_parameter_name_segment_policy(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            fixture = json.loads(json.dumps(self.contract))
            manifest["contract"]["fixture"] = "contract.json"
            fixture["contract"]["fixture"] = "contract.json"
            manifest["parameter_name_segment_policy"]["input"] = "decoded"
            fixture["api"]["parameter_name_segment_policy"]["input"] = "decoded"
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "parameter name segment policy is not canonical"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_missing_contract_fixture(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            manifest["contract"]["fixture"] = "missing-contract.json"
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "fixture"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_contract_fixture_outside_manifest_directory(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest_dir = temp_root / "manifest"
            manifest_dir.mkdir()
            manifest = json.loads(json.dumps(self.manifest))
            manifest["contract"]["fixture"] = "../outside-contract.json"
            fixture = json.loads(json.dumps(self.contract))
            fixture["contract"]["fixture"] = "../outside-contract.json"
            (manifest_dir / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "outside-contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "manifest directory"):
                mcp.load_manifest(manifest_dir / "manifest.json")


if __name__ == "__main__":
    unittest.main()
