import json
import unittest
from pathlib import Path
from unittest import mock

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "agent_native"))

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

    def test_path_rendering_quotes_dynamic_fields(self):
        command = mcp.command_index(self.manifest)["set_parameter"]
        path = mcp.render_path(
            command,
            {"processor_id": 101, "parameter_name": "gain mode", "value": 2},
        )
        self.assertEqual(path, "/api/processors/101/parameters/gain%20mode")

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


if __name__ == "__main__":
    unittest.main()
