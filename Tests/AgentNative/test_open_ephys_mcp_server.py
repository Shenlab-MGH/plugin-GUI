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
            for parameter_name in (
                "",
                "bad/name",
                "bad\\name",
                ".",
                "..",
                "bad\nname",
                "bad\u0085name",
            ):
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

        self.assertEqual(response["result"]["serverInfo"]["version"], "0.1.3")

    def test_mcp_exposes_strict_typed_stream_tools(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        response = server.handle({"jsonrpc": "2.0", "id": 7, "method": "tools/list"})
        tools = {tool["name"]: tool for tool in response["result"]["tools"]}

        self.assertEqual(
            tools["oe_list_streams"]["inputSchema"],
            {
                "type": "object",
                "required": ["processor_id"],
                "properties": {"processor_id": {"type": "integer", "minimum": 0}},
                "additionalProperties": False,
            },
        )
        self.assertEqual(
            tools["oe_get_stream"]["inputSchema"],
            {
                "type": "object",
                "required": ["processor_id", "stream_index"],
                "properties": {
                    "processor_id": {"type": "integer", "minimum": 0},
                    "stream_index": {"type": "integer", "minimum": 0},
                },
                "additionalProperties": False,
            },
        )

    def test_typed_stream_tools_use_declared_backends_and_readbacks(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        listed_stream = self._valid_stream(stream_index=0)
        fetched_stream = self._valid_stream(stream_index=1)

        with mock.patch.object(mcp, "call_http") as call_http:
            call_http.side_effect = [
                {"ok": True, "status": 200, "response": {"id": 101, "streams": [listed_stream]}},
                {"ok": True, "status": 200, "response": fetched_stream},
            ]
            listed = self._call_tool(server, "oe_list_streams", {"processor_id": 101})
            fetched = self._call_tool(
                server, "oe_get_stream", {"processor_id": 101, "stream_index": 1}
            )

        self.assertEqual(listed["tool"], "oe_list_streams")
        self.assertEqual(listed["readback"], "response.streams")
        self.assertEqual(fetched["tool"], "oe_get_stream")
        self.assertEqual(fetched["readback"], "response")
        self.assertEqual(
            [call.args[:3] for call in call_http.call_args_list],
            [
                ("GET", "/api/processors/101", None),
                ("GET", "/api/processors/101/streams/1", None),
            ],
        )

    def test_typed_stream_tools_reject_invalid_arguments_before_http(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        invalid_calls = (
            ("oe_list_streams", {}, "processor_id"),
            ("oe_list_streams", {"processor_id": True}, "processor_id"),
            ("oe_list_streams", {"processor_id": -1}, "processor_id"),
            ("oe_list_streams", {"processor_id": "101"}, "processor_id"),
            ("oe_list_streams", {"processor_id": 101, "extra": 1}, "Unexpected"),
            ("oe_get_stream", {"processor_id": 101}, "stream_index"),
            ("oe_get_stream", {"processor_id": 101, "stream_index": False}, "stream_index"),
            ("oe_get_stream", {"processor_id": 101, "stream_index": -1}, "stream_index"),
            ("oe_get_stream", {"processor_id": 101, "stream_index": "0"}, "stream_index"),
        )

        with mock.patch.object(mcp, "call_http") as call_http:
            for tool, arguments, expected_error in invalid_calls:
                with self.subTest(tool=tool, arguments=arguments):
                    response = server.handle(
                        {
                            "jsonrpc": "2.0",
                            "id": 8,
                            "method": "tools/call",
                            "params": {"name": tool, "arguments": arguments},
                        }
                    )
                    self.assertIn("error", response)
                    self.assertIn(expected_error, response["error"]["message"])
            call_http.assert_not_called()

    def test_typed_stream_tools_fail_closed_on_malformed_success_payloads(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        malformed = (
            ("oe_list_streams", {"processor_id": 101}, {"id": 101}),
            ("oe_list_streams", {"processor_id": 101}, {"streams": [{}]}),
            ("oe_get_stream", {"processor_id": 101, "stream_index": 0}, {}),
        )

        for tool, arguments, payload in malformed:
            with self.subTest(tool=tool, payload=payload):
                with mock.patch.object(
                    mcp,
                    "call_http",
                    return_value={"ok": True, "status": 200, "response": payload},
                ):
                    response = server.handle(
                        {
                            "jsonrpc": "2.0",
                            "id": 9,
                            "method": "tools/call",
                            "params": {"name": tool, "arguments": arguments},
                        }
                    )
                self.assertIn("error", response)

    def test_typed_stream_tools_strictly_validate_stream_semantics(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        mutations = []
        for field, invalid_value in (
            ("name", None),
            ("source_id", True),
            ("source_id", -1),
            ("sample_rate", True),
            ("sample_rate", 0),
            ("sample_rate", float("nan")),
            ("sample_rate", float("inf")),
            ("sample_rate", float("-inf")),
            ("channel_count", True),
            ("channel_count", -1),
            ("parameters", {}),
            ("stream_index", True),
            ("stream_index", -1),
            ("runtime_id", True),
            ("runtime_id", -1),
            ("source_name", None),
            ("description", None),
            ("identifier", None),
            ("generates_timestamps", 1),
        ):
            stream = self._valid_stream(stream_index=0)
            stream[field] = invalid_value
            mutations.append((f"{field}={invalid_value!r}", stream))

        for label, mutate in (
            ("identity.available", lambda s: s["identity"].update(available=False)),
            ("identity.available_type", lambda s: s["identity"].update(available=1)),
            ("identity.source_id_type", lambda s: s["identity"].update(source_id=True)),
            ("identity.scope", lambda s: s["identity"].update(scope="global")),
            ("identity.source_id", lambda s: s["identity"].update(source_id=202)),
            ("identity.identifier", lambda s: s["identity"].update(identifier="other")),
            ("uia.scope", lambda s: s["uia"].update(scope="global")),
            ("uia.fallback", lambda s: s["uia"].update(uses_display_name_fallback=True)),
            ("uia.collision_suffix_type", lambda s: s["uia"].update(uses_collision_suffix=1)),
            ("uia.processor", lambda s: s["uia"].update(automation_id=s["uia"]["automation_id"].replace("processor.101", "processor.202"))),
            ("uia.source", lambda s: s["uia"].update(automation_id=s["uia"]["automation_id"].replace("source_101", "source_202"))),
        ):
            stream = self._valid_stream(stream_index=0)
            mutate(stream)
            mutations.append((label, stream))

        for label, stream in mutations:
            with self.subTest(mutation=label):
                with mock.patch.object(
                    mcp,
                    "call_http",
                    return_value={
                        "ok": True,
                        "status": 200,
                        "response": {"id": 101, "streams": [stream]},
                    },
                ):
                    response = server.handle(
                        {
                            "jsonrpc": "2.0",
                            "id": 13,
                            "method": "tools/call",
                            "params": {
                                "name": "oe_list_streams",
                                "arguments": {"processor_id": 101},
                            },
                        }
                    )
                self.assertIn("error", response)

    def test_stream_semantic_sanitiser_matches_cpp_ascii_rules(self):
        cases = {
            "Probe.AP 01": "probe_ap_01",
            " A--B ": "a_b",
            "AP-é__LFP": "ap_lfp",
            "___": "unnamed",
            "神经": "unnamed",
            "": "unnamed",
        }
        for raw, expected in cases.items():
            with self.subTest(raw=raw):
                self.assertEqual(mcp.sanitise_stream_semantic_segment(raw), expected)

    def test_typed_stream_tools_derive_locator_segment_from_identifier_or_name(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        streams = (
            self._valid_stream(
                identifier="Probe.AP", name="Ignored fallback", semantic_segment="probe_ap"
            ),
            self._valid_stream(
                identifier="", name="Fallback Name", semantic_segment="fallback_name"
            ),
            self._valid_stream(
                identifier="神经", name="Ignored fallback", semantic_segment="unnamed"
            ),
        )

        for stream in streams:
            with self.subTest(identifier=stream["identifier"], name=stream["name"]):
                with mock.patch.object(
                    mcp,
                    "call_http",
                    return_value={"ok": True, "status": 200, "response": stream},
                ):
                    response = server.handle(
                        {
                            "jsonrpc": "2.0",
                            "id": 20,
                            "method": "tools/call",
                            "params": {
                                "name": "oe_get_stream",
                                "arguments": {"processor_id": 101, "stream_index": 0},
                            },
                        }
                    )
                self.assertNotIn("error", response)

    def test_typed_stream_tools_reject_a_self_reported_wrong_semantic_segment(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        stream = self._valid_stream(identifier="imec.ap", name="Probe AP")
        stream["uia"]["automation_id"] = (
            "oe.processor.101.streams.table.source_101.stream_probe_ap"
        )
        with mock.patch.object(
            mcp,
            "call_http",
            return_value={"ok": True, "status": 200, "response": stream},
        ):
            response = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 21,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_get_stream",
                        "arguments": {"processor_id": 101, "stream_index": 0},
                    },
                }
            )
        self.assertIn("error", response)

    def test_typed_stream_list_recomputes_collision_from_metadata(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        dotted = self._valid_stream(
            stream_index=0, identifier="probe.ap", semantic_segment="probe_ap"
        )
        underscored = self._valid_stream(
            stream_index=1, identifier="probe_ap", semantic_segment="probe_ap"
        )
        for stream in (dotted, underscored):
            stream["uia"]["automation_id"] = (
                "oe.processor.101.streams.table.source_101.stream_probe_ap."
                f"index_{stream['stream_index']}"
            )
            stream["uia"]["uses_collision_suffix"] = True

        with mock.patch.object(
            mcp,
            "call_http",
            return_value={
                "ok": True,
                "status": 200,
                "response": {"id": 101, "streams": [dotted, underscored]},
            },
        ):
            accepted = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 22,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_list_streams",
                        "arguments": {"processor_id": 101},
                    },
                }
            )
        self.assertNotIn("error", accepted)

        not_colliding = self._valid_stream(
            stream_index=1, identifier="probe_lfp", semantic_segment="probe_lfp"
        )
        not_colliding["uia"]["automation_id"] = (
            "oe.processor.101.streams.table.source_101.stream_probe_ap.index_1"
        )
        not_colliding["uia"]["uses_collision_suffix"] = True
        with mock.patch.object(
            mcp,
            "call_http",
            return_value={
                "ok": True,
                "status": 200,
                "response": {"id": 101, "streams": [dotted, not_colliding]},
            },
        ):
            rejected = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 23,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_list_streams",
                        "arguments": {"processor_id": 101},
                    },
                }
            )
        self.assertIn("error", rejected)
        self.assertIn("semantic segment", rejected["error"]["message"].lower())

    def test_typed_get_stream_requires_collision_flag_to_match_suffix_presence(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        invalid = []
        unique_with_suffix = self._valid_stream()
        unique_with_suffix["uia"]["automation_id"] += ".index_0"
        invalid.append(unique_with_suffix)
        collision_without_suffix = self._valid_stream()
        collision_without_suffix["uia"]["uses_collision_suffix"] = True
        invalid.append(collision_without_suffix)

        for stream in invalid:
            with self.subTest(uia=stream["uia"]):
                with mock.patch.object(
                    mcp,
                    "call_http",
                    return_value={"ok": True, "status": 200, "response": stream},
                ):
                    response = server.handle(
                        {
                            "jsonrpc": "2.0",
                            "id": 24,
                            "method": "tools/call",
                            "params": {
                                "name": "oe_get_stream",
                                "arguments": {"processor_id": 101, "stream_index": 0},
                            },
                        }
                    )
                self.assertIn("error", response)

    def test_typed_stream_tools_validate_request_response_identity(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        cases = (
            ("oe_list_streams", {"processor_id": 101}, {"id": 202, "streams": []}),
            (
                "oe_list_streams",
                {"processor_id": 101},
                {"id": 101, "streams": [self._valid_stream(stream_index=1)]},
            ),
            (
                "oe_get_stream",
                {"processor_id": 101, "stream_index": 1},
                self._valid_stream(stream_index=0),
            ),
        )
        for tool, arguments, payload in cases:
            with self.subTest(tool=tool, payload=payload):
                with mock.patch.object(
                    mcp,
                    "call_http",
                    return_value={"ok": True, "status": 200, "response": payload},
                ):
                    response = server.handle(
                        {
                            "jsonrpc": "2.0",
                            "id": 14,
                            "method": "tools/call",
                            "params": {"name": tool, "arguments": arguments},
                        }
                    )
                self.assertIn("error", response)

    def test_typed_stream_tools_preserve_http_failures(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        failure = {"ok": False, "status": 404, "response": {"error": "not found"}}
        with mock.patch.object(mcp, "call_http", return_value=failure):
            response = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 15,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_list_streams",
                        "arguments": {"processor_id": 999},
                    },
                }
            )
        self.assertTrue(response["result"]["isError"])
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["status"], 404)
        self.assertEqual(payload["response"], {"error": "not found"})

    def test_typed_stream_tools_mark_indeterminate_transport_results_as_errors(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        with mock.patch.object(
            mcp,
            "call_http",
            return_value={"status": None, "error": {"code": "transport_contract_error"}},
        ):
            response = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 19,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_list_streams",
                        "arguments": {"processor_id": 101},
                    },
                }
            )

        self.assertTrue(response["result"]["isError"])
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(payload["error"]["code"], "transport_contract_error")

    def test_typed_stream_list_allows_an_empty_success(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        with mock.patch.object(
            mcp,
            "call_http",
            return_value={"ok": True, "status": 200, "response": {"id": 101, "streams": []}},
        ):
            response = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 16,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_list_streams",
                        "arguments": {"processor_id": 101},
                    },
                }
            )
        self.assertNotIn("error", response)
        self.assertNotIn("isError", response["result"])
        payload = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(payload["response"]["streams"], [])

    def test_typed_stream_list_accepts_collision_suffixes_only_for_colliding_siblings(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        first = self._valid_stream(stream_index=0)
        second = self._valid_stream(stream_index=1)
        first["uia"]["automation_id"] += ".index_0"
        second["uia"]["automation_id"] += ".index_1"
        first["uia"]["uses_collision_suffix"] = True
        second["uia"]["uses_collision_suffix"] = True
        with mock.patch.object(
            mcp,
            "call_http",
            return_value={
                "ok": True,
                "status": 200,
                "response": {"id": 101, "streams": [first, second]},
            },
        ):
            accepted = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 17,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_list_streams",
                        "arguments": {"processor_id": 101},
                    },
                }
            )
        self.assertNotIn("error", accepted)

        unique = self._valid_stream(stream_index=0)
        unique["uia"]["automation_id"] += ".index_0"
        with mock.patch.object(
            mcp,
            "call_http",
            return_value={
                "ok": True,
                "status": 200,
                "response": {"id": 101, "streams": [unique]},
            },
        ):
            rejected = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 18,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_list_streams",
                        "arguments": {"processor_id": 101},
                    },
                }
            )
        self.assertIn("error", rejected)

    def test_stream_uia_locator_accepts_only_declared_compact_row_ids(self):
        server = mcp.McpServer(manifest_path=ROOT / "agent_native" / "open_ephys_agent_surface.json")
        valid_ids = (
            "oe.processor.100.streams.table.source_101.stream_imec_ap",
            "oe.processor.100.streams.table.source_101.stream_imec_ap.index_0",
            "oe.processor.100.streams.table.source_101.stream_imec_ap.index_17",
        )
        for automation_id in valid_ids:
            response = server.handle(
                {
                    "jsonrpc": "2.0",
                    "id": 10,
                    "method": "tools/call",
                    "params": {
                        "name": "oe_uia_locator",
                        "arguments": {"automation_id": automation_id},
                    },
                }
            )
            self.assertNotIn("error", response)
            payload = json.loads(response["result"]["content"][0]["text"])
            self.assertEqual(payload["rule"], server.manifest["uia"]["stream_row_automation_id_rule"])

        invalid = (
            "oe.processor.100.streams.expanded_table.source_101.stream_imec_ap",
            "oe.processor.A.streams.table.source_101.stream_imec_ap",
            "oe.processor.100.streams.table.source_x.stream_imec_ap",
            "oe.processor.100.streams.table.source_101.stream_",
            "oe.processor.100.streams.table.source_101.stream_Imec_AP",
            "oe.processor.100.streams.table.source_101.stream_imec-ap",
            "oe.processor.100.streams.table.source_101.stream_..",
            "oe.processor.100.streams.table.source_101.stream_imec_ap.index_",
            "oe.processor.100.streams.table.source_101.stream_imec_ap.index_-1",
            "oe.processor.100.streams.table.source_101.stream_imec_ap.index_true",
            "oe.processor.100.streams.table.source_101.stream_imec_ap.index_1.extra",
        )
        for automation_id in invalid:
            with self.subTest(automation_id=automation_id):
                response = server.handle(
                    {
                        "jsonrpc": "2.0",
                        "id": 11,
                        "method": "tools/call",
                        "params": {
                            "name": "oe_uia_locator",
                            "arguments": {"automation_id": automation_id},
                        },
                    }
                )
                self.assertIn("error", response)

    @staticmethod
    def _valid_stream(
        stream_index=0,
        *,
        identifier="imec.ap",
        name="Probe AP",
        semantic_segment="imec_ap",
    ):
        return {
            "name": name,
            "source_id": 101,
            "sample_rate": 30000.0,
            "channel_count": 0,
            "parameters": [],
            "stream_index": stream_index,
            "runtime_id": 7,
            "source_name": "Neuropixels PXI",
            "description": "AP stream",
            "identifier": identifier,
            "generates_timestamps": True,
            "identity": {
                "available": identifier != "",
                "scope": "configuration",
                "source_id": 101,
                "identifier": identifier,
            },
            "uia": {
                "automation_id": (
                    "oe.processor.101.streams.table.source_101.stream_"
                    + semantic_segment
                ),
                "scope": "configuration",
                "uses_display_name_fallback": identifier == "",
                "uses_collision_suffix": False,
            },
        }

    @staticmethod
    def _call_tool(server, name, arguments):
        response = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 12,
                "method": "tools/call",
                "params": {"name": name, "arguments": arguments},
            }
        )
        if "error" in response:
            raise AssertionError(response["error"])
        return json.loads(response["result"]["content"][0]["text"])

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
        self.assertEqual(contract["version"], "0.1.3")
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
            "source": "parameter_response.name",
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
        self.assertEqual(parameter_response["route_lookup_field"], "name")
        self.assertEqual(parameter_response["stable_identity_field"], "key")
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

    def test_contract_declares_synchronised_stream_surface(self):
        expected_response = {
            "required_fields": [
                "name",
                "source_id",
                "sample_rate",
                "channel_count",
                "parameters",
                "stream_index",
                "runtime_id",
                "source_name",
                "description",
                "identifier",
                "generates_timestamps",
                "identity.available",
                "identity.scope",
                "identity.source_id",
                "identity.identifier",
                "uia.automation_id",
                "uia.scope",
                "uia.uses_display_name_fallback",
                "uia.uses_collision_suffix",
            ],
            "route_lookup_field": "stream_index",
            "identity": {
                "fields": ["source_id", "identifier"],
                "available_when": "identifier_nonempty",
                "scope": "configuration",
            },
            "non_durable_fields": {
                "stream_index": "current_configuration_order",
                "runtime_id": "process_lifetime",
            },
        }
        expected_tools = {
            "oe_list_streams": {"backend_command": "get_processor", "readback": "response.streams"},
            "oe_get_stream": {"backend_command": "get_stream", "readback": "response"},
        }
        expected_uia_rule = (
            "oe.processor.<processor_id>.streams.table.source_<source_id>."
            "stream_<sanitised identifier-or-display-name>[.index_<nonnegative stream_index on collision>]"
        )

        self.assertEqual(self.manifest["stream_response"], expected_response)
        self.assertEqual(self.contract["api"]["stream_response"], expected_response)
        self.assertEqual(mcp.STREAM_RESPONSE_CONTRACT, expected_response)
        self.assertEqual(self.manifest["typed_tools"], expected_tools)
        self.assertEqual(self.contract["api"]["typed_tools"], expected_tools)
        self.assertEqual(mcp.TYPED_STREAM_TOOLS, expected_tools)
        self.assertEqual(self.manifest["uia"]["stream_row_automation_id_rule"], expected_uia_rule)
        self.assertEqual(self.contract["uia"]["stream_row_automation_id_rule"], expected_uia_rule)
        self.assertEqual(mcp.STREAM_ROW_AUTOMATION_ID_RULE, expected_uia_rule)
        self.assertEqual(
            self.manifest["uia"].get("processor_catalog_automation_id_rule"),
            "oe.processor_catalog.<sanitised processor slug>",
        )

    def test_skill_matches_parameter_contract_discovery_workflow(self):
        skill = " ".join(SKILL_PATH.read_text(encoding="utf-8").split())

        self.assertIn("`open-ephys-agent` contract `0.1.3`", skill)
        self.assertIn("`get_stream_parameters`, `get_parameter`, or `get_stream_parameter` first.", skill)
        self.assertIn("Pass that returned `uia.automation_id` to `oe_uia_locator` as `automation_id`.", skill)
        self.assertIn("do not construct or guess them from a parameter name or key", skill)
        self.assertIn("use its returned `name` as the raw `parameter_name`", skill)
        self.assertIn("`key` remains the stable identity", skill)
        self.assertNotIn("`key` as the raw `parameter_name`", skill)
        self.assertIn("one percent-encoded path segment", skill)
        self.assertIn("does not validate a real device", skill)
        self.assertIn("call `list_processors`, then `oe_list_streams`", skill)
        self.assertIn("same running configuration", skill)
        self.assertIn("discover the streams again", skill)
        self.assertIn("`runtime_id` is process-lifetime only", skill)
        self.assertIn("do not treat the display-name fallback as durable identity", skill)

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

    def test_contract_route_matrix_covers_stream_discovery_backends(self):
        route_names = {entry["command"] for entry in self.contract["api"]["required_routes"]}
        self.assertTrue({"get_processor", "get_stream"}.issubset(route_names))

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

    def test_manifest_rejects_an_independently_mutated_stream_response(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest, fixture, manifest_path = self._temporary_contract_pair(temp_dir)
            manifest["stream_response"]["identity"]["scope"] = "global"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self._fixture_path(manifest_path).write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "stream response"):
                mcp.load_manifest(manifest_path)

    def test_manifest_rejects_independently_mutated_typed_stream_tools(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest, fixture, manifest_path = self._temporary_contract_pair(temp_dir)
            manifest["typed_tools"]["oe_list_streams"]["backend_command"] = "list_processors"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self._fixture_path(manifest_path).write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "typed stream tools"):
                mcp.load_manifest(manifest_path)

    def test_manifest_rejects_an_independently_mutated_stream_uia_rule(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest, fixture, manifest_path = self._temporary_contract_pair(temp_dir)
            manifest["uia"]["stream_row_automation_id_rule"] = "oe.processor.<wrong>"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self._fixture_path(manifest_path).write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "stream UIA"):
                mcp.load_manifest(manifest_path)

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

    def test_manifest_rejects_a_stale_schema_version(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            fixture = json.loads(json.dumps(self.contract))
            manifest["contract"]["fixture"] = "contract.json"
            fixture["contract"]["fixture"] = "contract.json"
            manifest["schema_version"] = "0.1.1"
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "schema"):
                mcp.load_manifest(temp_root / "manifest.json")

    def test_manifest_rejects_a_matched_but_unsupported_contract_version(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest = json.loads(json.dumps(self.manifest))
            fixture = json.loads(json.dumps(self.contract))
            manifest["contract"]["fixture"] = "contract.json"
            fixture["contract"]["fixture"] = "contract.json"
            manifest["contract"]["version"] = "0.1.1"
            fixture["contract"]["version"] = "0.1.1"
            (temp_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (temp_root / "contract.json").write_text(json.dumps(fixture), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "supported contract"):
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

    def _temporary_contract_pair(self, temp_dir):
        temp_root = Path(temp_dir)
        manifest = json.loads(json.dumps(self.manifest))
        fixture = json.loads(json.dumps(self.contract))
        manifest["contract"]["fixture"] = "contract.json"
        fixture["contract"]["fixture"] = "contract.json"
        return manifest, fixture, temp_root / "manifest.json"

    @staticmethod
    def _fixture_path(manifest_path):
        return manifest_path.parent / "contract.json"


if __name__ == "__main__":
    unittest.main()
