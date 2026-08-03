import importlib.util
import json
import subprocess
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_0_0_1.json"
BUNDLE_PATH = AGENT_DIR / "open_ephys_agent_release_bundle.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "agent-native-core.yml"


def load_server_module():
    spec = importlib.util.spec_from_file_location("open_ephys_mcp_server", SERVER_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeOpenEphysApi:
    def __init__(self, capabilities):
        self.capabilities = capabilities
        self.mode = "IDLE"
        self.filename = {
            "parent_directory": "C:/data",
            "prepend_text": "",
            "base_text": "Record Node",
            "append_text": "",
            "default_record_engine": "Binary",
            "record_nodes": [],
        }
        self.cpu = 0.25
        self.status_put_response = None
        self.recording_put_response = None
        self.capabilities_redirect = None
        self.requests = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def send_json(self, status, payload):
                data = json.dumps(payload).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                owner.requests.append(("GET", self.path, None))
                if self.path == "/api/capabilities":
                    if owner.capabilities_redirect:
                        self.send_response(302)
                        self.send_header("Location", owner.capabilities_redirect)
                        self.end_headers()
                        return
                    self.send_json(200, owner.capabilities)
                elif self.path == "/api/status":
                    self.send_json(200, {"mode": owner.mode})
                elif self.path == "/api/recording":
                    self.send_json(200, owner.filename)
                elif self.path == "/api/cpu":
                    self.send_json(200, {"usage": owner.cpu})
                else:
                    self.send_json(404, {"error": "not found"})

            def do_PUT(self):
                length = int(self.headers.get("Content-Length", "0"))
                body = json.loads(self.rfile.read(length) or b"{}")
                owner.requests.append(("PUT", self.path, body))
                if self.path == "/api/status":
                    if owner.status_put_response is not None:
                        status, payload = owner.status_put_response
                        self.send_json(status, payload)
                        return
                    owner.mode = body["mode"]
                    self.send_json(200, {"mode": owner.mode})
                elif self.path == "/api/recording":
                    if owner.recording_put_response is not None:
                        status, payload = owner.recording_put_response
                        self.send_json(status, payload)
                        return
                    owner.filename.update(body)
                    self.send_json(200, owner.filename)
                else:
                    self.send_json(404, {"error": "not found"})

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def base_url(self):
        return f"http://127.0.0.1:{self.server.server_port}"

    def start(self):
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


class McpR010Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        cls.module = load_server_module()

    def setUp(self):
        capabilities = self.contract["api"]["expected_capabilities_response"]
        self.api = FakeOpenEphysApi(capabilities)
        self.api.start()
        self.server = self.module.McpServer(CONTRACT_PATH, self.api.base_url)

    def tearDown(self):
        self.api.close()

    def ready_server(self):
        response = self.server.handle({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "test", "version": "1"},
            },
        })
        self.assertEqual(response["result"]["protocolVersion"], "2024-11-05")
        self.assertIsNone(self.server.handle({
            "jsonrpc": "2.0", "method": "notifications/initialized", "params": {}
        }))

    def call_tool(self, name, arguments=None):
        response = self.server.handle({
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments or {}},
        })
        return response["result"], json.loads(response["result"]["content"][0]["text"])

    def test_contract_pins_release_protocol_and_exact_tools(self):
        self.assertEqual(self.contract["schema_version"], "0.0.1")
        self.assertEqual(self.contract["contract"]["version"], "0.0.1")
        self.assertEqual(self.contract["bundle"]["version"], "0.0.1")
        self.assertEqual(self.contract["baseline"]["version"], "1.1.0")
        self.assertEqual(
            self.contract["baseline"]["fork_source_commit"],
            "5808213806be355b889cc7e52bb01c570fdeee08",
        )
        self.assertEqual(self.contract["mcp"]["protocol_version"], "2024-11-05")
        self.assertFalse(self.contract["mcp"]["modern_protocol_supported"])
        filename_tool = next(tool for tool in self.contract["tools"] if tool["name"] == "oe_set_recording_filename")
        self.assertEqual(filename_tool["inputSchema"]["maxProperties"], 1)
        self.assertEqual(
            [tool["name"] for tool in self.contract["tools"]],
            [
                "oe_get_capabilities", "oe_get_status", "oe_set_status",
                "oe_get_recording_filename", "oe_set_recording_filename", "oe_get_cpu",
            ],
        )

    def test_release_bundle_pins_the_same_contract_and_legacy_boundary(self):
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], "0.0.1")
        self.assertEqual(bundle["bundle"]["version"], "0.0.1")
        self.assertEqual(bundle["official_upstream"]["tag"], "v1.1.0")
        self.assertEqual(
            bundle["fork_source_commit"],
            self.contract["baseline"]["fork_source_commit"],
        )
        self.assertEqual(bundle["contract"]["version"], self.contract["contract"]["version"])
        self.assertEqual(bundle["components"]["mcp"]["protocol_version"], "2024-11-05")
        self.assertFalse(bundle["components"]["mcp"]["modern_protocol_supported"])
        self.assertFalse(bundle["verification"]["official_mcp_v2_auto_fallback"])
        blocker = bundle["verification"]["official_mcp_v2_blocker"]
        self.assertIsInstance(blocker, str)
        self.assertIn("exact-commit Windows gate", blocker)
        self.assertIn("pending", blocker.lower())

    def test_legacy_discovery_fallback_and_lifecycle(self):
        discover = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "server/discover", "params": {}})
        self.assertEqual(discover["error"]["code"], -32601)
        self.ready_server()
        listed = self.server.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/list", "params": {}})
        self.assertEqual(listed["result"]["tools"], self.contract["tools"])

    def test_tools_are_unavailable_until_initialized_notification(self):
        before_initialize = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}
        })
        self.assertEqual(before_initialize["error"]["code"], -32002)
        initialize = self.server.handle({
            "jsonrpc": "2.0", "id": 2, "method": "initialize", "params": {
                "protocolVersion": "2024-11-05", "capabilities": {},
                "clientInfo": {"name": "test", "version": "1"},
            },
        })
        self.assertIn("result", initialize)
        before_notification = self.server.handle({
            "jsonrpc": "2.0", "id": 3, "method": "tools/list", "params": {}
        })
        self.assertEqual(before_notification["error"]["code"], -32002)

    def test_newer_legacy_client_can_negotiate_pinned_server_protocol(self):
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2025-06-18", "capabilities": {},
                "clientInfo": {"name": "official-sdk", "version": "2.0.0"},
            },
        })
        self.assertEqual(response["result"]["protocolVersion"], "2024-11-05")

    def test_request_only_notifications_never_respond_or_mutate(self):
        self.ready_server()
        notification = {
            "jsonrpc": "2.0", "method": "tools/call",
            "params": {"name": "oe_set_status", "arguments": {"mode": "ACQUIRE"}},
        }
        self.assertIsNone(self.server.handle(notification))
        self.assertEqual(self.api.requests, [])

    def test_initialized_must_be_notification_only_and_malformed_no_id_gets_null_error(self):
        initialized_request = self.server.handle({
            "jsonrpc": "2.0", "id": 9, "method": "notifications/initialized", "params": {}
        })
        self.assertEqual(initialized_request["error"]["code"], -32600)
        malformed = self.server.handle({"jsonrpc": "2.0", "params": {}})
        self.assertIsNone(malformed["id"])
        self.assertEqual(malformed["error"]["code"], -32600)

    def test_invalid_request_ids_are_never_reflected(self):
        for invalid_id in [True, False, None, [], {}, 1.5]:
            with self.subTest(invalid_id=invalid_id):
                response = self.server.handle({
                    "jsonrpc": "2.0", "id": invalid_id, "method": "initialize", "params": {
                        "protocolVersion": "2024-11-05", "capabilities": {},
                        "clientInfo": {"name": "test", "version": "1"},
                    },
                })
                self.assertIsNone(response["id"])
                self.assertEqual(response["error"]["code"], -32600)

    def test_capability_verification_is_fail_closed(self):
        self.ready_server()
        result, payload = self.call_tool("oe_get_capabilities")
        self.assertNotIn("isError", result)
        self.assertEqual(payload, self.api.capabilities)

        self.api.capabilities = dict(self.api.capabilities, contract_version="unexpected")
        result, payload = self.call_tool("oe_get_status")
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "capability_contract_mismatch")
        self.assertFalse(any(request[1] == "/api/status" for request in self.api.requests[-1:]))

    def test_http_redirects_are_rejected_before_following_location(self):
        self.ready_server()
        self.api.capabilities_redirect = "https://example.invalid/steal"
        result, payload = self.call_tool("oe_get_capabilities")
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "api_http_error")
        self.assertEqual(payload["error"]["status"], 302)

    def test_reads_status_recording_filename_and_cpu(self):
        self.ready_server()
        _, status = self.call_tool("oe_get_status")
        _, filename = self.call_tool("oe_get_recording_filename")
        _, cpu = self.call_tool("oe_get_cpu")
        self.assertEqual(status, {"mode": "IDLE"})
        self.assertEqual(filename, {"prepend_text": "", "base_text": "Record Node", "append_text": ""})
        self.assertEqual(cpu, {"usage": 0.25})

    def test_set_status_requires_record_approval_and_uses_pre_post_readback(self):
        self.ready_server()
        result, payload = self.call_tool("oe_set_status", {"mode": "RECORD"})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "recording_approval_required")
        self.assertFalse(any(request[0] == "PUT" for request in self.api.requests))

        result, payload = self.call_tool("oe_set_status", {"mode": "RECORD", "approve_recording": True})
        self.assertNotIn("isError", result)
        self.assertEqual(payload["before"], {"mode": "IDLE"})
        self.assertEqual(payload["after"], {"mode": "RECORD"})
        status_requests = [request[:2] for request in self.api.requests if request[1] == "/api/status"]
        self.assertEqual(status_requests[-3:], [
            ("GET", "/api/status"), ("PUT", "/api/status"), ("GET", "/api/status")
        ])

    def test_invalid_status_argument_is_a_tool_error_without_api_mutation(self):
        self.ready_server()
        result, payload = self.call_tool("oe_set_status", {"mode": ["RECORD"]})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "invalid_arguments")
        self.assertEqual(self.api.requests, [])

    def test_set_status_surfaces_api_conflict_as_tool_error(self):
        self.ready_server()
        self.api.status_put_response = (409, {
            "error": {"code": "transition_rejected", "message": "Open Ephys did not reach the requested mode."},
            "requested_mode": "ACQUIRE", "mode": "IDLE",
        })
        result, payload = self.call_tool("oe_set_status", {"mode": "ACQUIRE"})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "api_http_error")
        self.assertEqual(payload["error"]["status"], 409)

    def test_set_filename_surfaces_api_bad_request_as_tool_error(self):
        self.ready_server()
        self.api.recording_put_response = (400, {
            "error": {"code": "invalid_recording_request", "message": "Invalid filename."}
        })
        result, payload = self.call_tool("oe_set_recording_filename", {"base_text": "mouse"})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "api_http_error")
        self.assertEqual(payload["error"]["status"], 400)

    def test_set_filename_validates_arguments_and_uses_pre_post_readback(self):
        self.ready_server()
        result, payload = self.call_tool("oe_set_recording_filename", {"base_text": 5})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "invalid_arguments")
        self.assertFalse(any(request[0] == "PUT" for request in self.api.requests))

        result, payload = self.call_tool("oe_set_recording_filename", {"prepend_text": "mouse_"})
        self.assertNotIn("isError", result)
        self.assertEqual(payload["before"]["prepend_text"], "")
        self.assertEqual(payload["after"]["prepend_text"], "mouse_")
        recording_requests = [request[:2] for request in self.api.requests if request[1] == "/api/recording"]
        self.assertEqual(recording_requests[-3:], [
            ("GET", "/api/recording"), ("PUT", "/api/recording"), ("GET", "/api/recording")
        ])

    def test_set_filename_rejects_non_atomic_and_windows_invalid_components_before_http(self):
        self.ready_server()
        invalid_arguments = [
            {"prepend_text": "mouse", "append_text": "probe"},
            {"base_text": "C:\\data"}, {"base_text": "../data"},
            {"base_text": "mouse/probe"}, {"base_text": "mouse\\probe"},
            {"base_text": "mouse\x00probe"}, {"base_text": "mouse*probe"},
            {"base_text": "CON"}, {"base_text": "mouse."},
            {"base_text": "COM¹"}, {"base_text": "COM².txt"},
            {"base_text": "LPT³.log"},
        ]
        for arguments in invalid_arguments:
            with self.subTest(arguments=arguments):
                self.api.requests.clear()
                result, payload = self.call_tool("oe_set_recording_filename", arguments)
                self.assertTrue(result["isError"])
                self.assertEqual(payload["error"]["code"], "invalid_arguments")
                self.assertEqual(self.api.requests, [])

    def test_post_readback_mismatch_fails_closed(self):
        self.ready_server()
        self.api.recording_put_response = (200, self.api.filename)
        result, payload = self.call_tool("oe_set_recording_filename", {"base_text": "new"})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "postcondition_failed")

    def test_invalid_cpu_schema_is_a_tool_error(self):
        self.ready_server()
        self.api.cpu = 1.5
        result, payload = self.call_tool("oe_get_cpu")
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "response_schema_mismatch")

    def test_stdio_stdout_contains_only_json_rpc(self):
        requests = "\n".join(json.dumps(item) for item in [
            {"jsonrpc": "2.0", "id": 1, "method": "server/discover", "params": {}},
            {"jsonrpc": "2.0", "id": 2, "method": "initialize", "params": {
                "protocolVersion": "2024-11-05", "capabilities": {},
                "clientInfo": {"name": "test", "version": "1"},
            }},
            {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/list", "params": {}},
            {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {
                "name": "oe_get_capabilities", "arguments": {},
            }},
            {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {
                "name": "oe_get_status", "arguments": {},
            }},
            {"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {
                "name": "oe_get_recording_filename", "arguments": {},
            }},
            {"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {
                "name": "oe_get_cpu", "arguments": {},
            }},
            {"jsonrpc": "2.0", "id": 8, "method": "tools/call", "params": {
                "name": "oe_set_status", "arguments": {"mode": "RECORD"},
            }},
        ]) + "\n"
        completed = subprocess.run(
            [
                sys.executable, str(SERVER_PATH),
                "--contract", str(CONTRACT_PATH),
                "--base-url", self.api.base_url,
            ],
            input=requests, text=True, capture_output=True, timeout=10, check=True,
        )
        lines = completed.stdout.splitlines()
        responses = [json.loads(line) for line in lines]
        self.assertEqual([response["id"] for response in responses], list(range(1, 9)))
        self.assertEqual(responses[0]["error"]["code"], -32601)
        self.assertEqual(responses[1]["result"]["protocolVersion"], "2024-11-05")
        self.assertEqual(
            [tool["name"] for tool in responses[2]["result"]["tools"]],
            [tool["name"] for tool in self.contract["tools"]],
        )
        status = json.loads(responses[4]["result"]["content"][0]["text"])
        filename = json.loads(responses[5]["result"]["content"][0]["text"])
        cpu = json.loads(responses[6]["result"]["content"][0]["text"])
        refusal = json.loads(responses[7]["result"]["content"][0]["text"])
        self.assertEqual(status, {"mode": "IDLE"})
        self.assertEqual(filename["base_text"], "Record Node")
        self.assertEqual(cpu, {"usage": 0.25})
        self.assertTrue(responses[7]["result"]["isError"])
        self.assertEqual(refusal["error"]["code"], "recording_approval_required")
        self.assertFalse(any(request[0] == "PUT" for request in self.api.requests))
        self.assertEqual(completed.stderr, "")

    def test_stdio_no_id_tool_call_has_no_response_and_cannot_put(self):
        requests = "\n".join(json.dumps(item) for item in [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2024-11-05", "capabilities": {},
                "clientInfo": {"name": "test", "version": "1"},
            }},
            {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
            {"jsonrpc": "2.0", "method": "tools/call", "params": {
                "name": "oe_set_status", "arguments": {"mode": "ACQUIRE"},
            }},
            {"jsonrpc": "2.0", "params": {}},
        ]) + "\n"
        completed = subprocess.run(
            [sys.executable, str(SERVER_PATH), "--contract", str(CONTRACT_PATH),
             "--base-url", self.api.base_url],
            input=requests, text=True, capture_output=True, timeout=10, check=True,
        )
        responses = [json.loads(line) for line in completed.stdout.splitlines()]
        self.assertEqual(len(responses), 2)
        self.assertEqual(responses[0]["id"], 1)
        self.assertIsNone(responses[1]["id"])
        self.assertEqual(responses[1]["error"]["code"], -32600)
        self.assertFalse(any(request[0] == "PUT" for request in self.api.requests))

    def test_skill_is_pinned_to_the_same_minimal_surface(self):
        skill = SKILL_PATH.read_text(encoding="utf-8")
        self.assertIn("contract: 0.0.1", skill)
        self.assertIn("Open Ephys baseline: 1.1.0", skill)
        for tool in self.contract["tools"]:
            self.assertIn(f"`{tool['name']}`", skill)
        self.assertNotIn("Neuropixels", skill)
        self.assertNotIn("oe_list_processors", skill)

    def test_dedicated_windows_ci_runs_for_every_synchronized_surface(self):
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("runs-on: windows-2022", workflow)
        self.assertIn("'agent_native/**'", workflow)
        self.assertIn("'skills/open-ephys-agent-native/**'", workflow)
        self.assertIn("'Tests/AgentNative/**'", workflow)
        self.assertIn("python -m unittest discover -s Tests/AgentNative", workflow)


if __name__ == "__main__":
    unittest.main()
