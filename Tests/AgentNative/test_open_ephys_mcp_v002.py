import importlib.util
import io
import json
import math
import ntpath
import subprocess
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
BASE_CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_v0_0_1.json"
CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_v0_0_2.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
RELEASE_BASE_SHA = "e4d766fcec4664361cdd72ffdf7c35a5dc23066e"

TOOL_NAMES = [
    "oe_get_capabilities", "oe_get_status", "oe_set_status",
    "oe_get_recording_options", "oe_set_recording_options",
    "oe_get_recording_filename", "oe_set_recording_filename",
    "oe_get_recording_directory", "oe_set_recording_directory",
    "oe_get_cpu", "oe_get_disk", "oe_get_time",
]
CAPABILITY_IDS = [
    "oe.control.acquisition", "oe.control.recording", "oe.control.recording.options",
    "oe.control.recording.filename", "oe.control.recording.directory",
    "oe.control.recording.new_directory",
    "oe.control.recording.force_new_directory", "oe.status.cpu_usage",
    "oe.status.disk_usage", "oe.status.elapsed_time",
]


def load_server_module():
    spec = importlib.util.spec_from_file_location("open_ephys_mcp_server_v002", SERVER_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class FakeOpenEphysApi:
    def __init__(self, capabilities):
        self.capabilities = capabilities
        self.mode = "IDLE"
        self.options = {
            "capability": "oe.control.recording.options", "expanded": False,
            "force_new_directory": False, "new_directory_requested": False, "recording": False,
        }
        self.recording = {
            "parent_directory": "C:/data", "prepend_text": "", "base_text": "Record Node",
            "append_text": "", "default_record_engine": "Binary", "record_nodes": [],
        }
        self.existing_directories = {
            ntpath.normcase(ntpath.normpath(path))
            for path in ("C:/data", "D:/Open Ephys", "D:/data", "E:/data")
        }
        self.cpu = {"usage": 0.25}
        self.disk = {"capability": "oe.status.disk_usage", "usage": 0.5, "minimum": 0.0, "maximum": 1.0, "read_only": True}
        self.time = {"capability": "oe.status.elapsed_time", "display": "00:00:00", "elapsed_milliseconds": 0, "mode": "IDLE", "reference": "acquisition", "running": False, "recording": False, "read_only": True}
        self.status_put_response = None
        self.options_put_response = None
        self.recording_put_response = None
        self.capabilities_redirect = None
        self.requests = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def send_json(self, status, payload):
                data = json.dumps(payload, allow_nan=False).encode("utf-8")
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
                    else:
                        self.send_json(200, owner.capabilities)
                    return
                payloads = {"/api/status": {"mode": owner.mode}, "/api/recording/options": owner.options,
                            "/api/recording": owner.recording, "/api/cpu": owner.cpu,
                            "/api/disk": owner.disk, "/api/time": owner.time}
                self.send_json(200, payloads[self.path]) if self.path in payloads else self.send_json(404, {"error": "not found"})

            def do_PUT(self):
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
                owner.requests.append(("PUT", self.path, body))
                configured = {"/api/status": owner.status_put_response, "/api/recording/options": owner.options_put_response, "/api/recording": owner.recording_put_response}.get(self.path)
                if configured is not None:
                    self.send_json(*configured)
                    return
                if self.path == "/api/status":
                    owner.mode = body["mode"]
                    self.send_json(200, {"mode": owner.mode})
                elif self.path == "/api/recording/options":
                    owner.options.update(body)
                    self.send_json(200, {"ok": True, **owner.options})
                elif self.path == "/api/recording":
                    if set(body) == {"parent_directory"}:
                        normalized = ntpath.normpath(body["parent_directory"])
                        if ntpath.normcase(normalized) in owner.existing_directories:
                            owner.recording["parent_directory"] = normalized
                    else:
                        owner.recording.update(body)
                    self.send_json(200, owner.recording)
                else:
                    self.send_json(404, {"error": "not found"})

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def base_url(self):
        return f"http://127.0.0.1:{self.server.server_port}"

    def start(self): self.thread.start()
    def close(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()


class ContractTests(unittest.TestCase):
    def test_v002_extends_the_exact_v001_core_by_recording_directory_only(self):
        self.assertEqual(subprocess.run(
            ["git", "merge-base", "--is-ancestor", RELEASE_BASE_SHA, "HEAD"],
            cwd=ROOT,
        ).returncode, 0)
        base_contract = json.loads(BASE_CONTRACT_PATH.read_text(encoding="utf-8"))
        contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], "0.0.2")
        self.assertEqual(contract["contract"]["version"], "0.0.2")
        self.assertEqual(contract["bundle"]["version"], "0.0.2")
        self.assertEqual(contract["baseline"], {"upstream": "open-ephys/plugin-GUI", "version": "1.0.2", "commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f"})
        self.assertEqual(contract["mcp"], {"protocol_version": "2024-11-05", "modern_protocol_supported": False, "server_name": "open-ephys-agent-native"})
        self.assertEqual([tool["name"] for tool in contract["tools"]], TOOL_NAMES)
        self.assertEqual([item["id"] for item in contract["api"]["expected_capabilities_response"]["capabilities"]], CAPABILITY_IDS)
        self.assertEqual(contract["api"]["expected_capabilities_response"]["contract_version"], "0.0.2")
        self.assertEqual(contract["api"]["capabilities_contract_version"], "0.0.2")
        directory_capability = next(
            item for item in contract["api"]["expected_capabilities_response"]["capabilities"]
            if item["id"] == "oe.control.recording.directory"
        )
        self.assertEqual(directory_capability, {
            "id": "oe.control.recording.directory",
            "name": "Recording directory",
            "description": "Read or edit the recording parent directory.",
            "kind": "value",
            "uia": {"automation_id": "oe.control.recording.directory"},
            "api": [
                {"operation": "read", "method": "GET", "path": "/api/recording", "request_fields": [], "response_fields": ["parent_directory"]},
                {"operation": "set", "method": "PUT", "path": "/api/recording", "request_fields": ["parent_directory"], "response_fields": ["parent_directory"]},
            ],
        })
        base_capabilities = base_contract["api"]["expected_capabilities_response"]["capabilities"]
        self.assertEqual(
            [item for item in contract["api"]["expected_capabilities_response"]["capabilities"]
             if item["id"] != "oe.control.recording.directory"],
            base_capabilities,
        )
        self.assertEqual(
            [tool for tool in contract["tools"]
             if tool["name"] not in {"oe_get_recording_directory", "oe_set_recording_directory"}],
            base_contract["tools"],
        )
        self.assertFalse(contract["verification"]["hardware_verified"])
        self.assertFalse(contract["verification"]["scientific_verified"])
        tool_names = [tool["name"] for tool in contract["tools"]]
        for forbidden in ("oe_api_request", "oe_post_command", "processor", "parameter", "stream", "uia_locator", "audio"):
            self.assertNotIn(forbidden, tool_names)
        for tool in contract["tools"]:
            self.assertFalse(tool["inputSchema"].get("additionalProperties", True), tool["name"])
        directory_setter = next(tool for tool in contract["tools"] if tool["name"] == "oe_set_recording_directory")
        self.assertEqual(directory_setter["inputSchema"], {
            "type": "object",
            "required": ["parent_directory"],
            "properties": {"parent_directory": {"type": "string", "minLength": 1}},
            "additionalProperties": False,
        })


class McpV002Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        cls.module = load_server_module()

    def setUp(self):
        self.api = FakeOpenEphysApi(self.contract["api"]["expected_capabilities_response"])
        self.api.start()
        self.server = self.module.McpServer(CONTRACT_PATH, self.api.base_url)

    def tearDown(self): self.api.close()

    def ready_server(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}}})
        self.assertEqual(response["result"]["protocolVersion"], "2024-11-05")
        self.assertIsNone(self.server.handle({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}))

    def call(self, name, arguments=None):
        response = self.server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": name, "arguments": arguments or {}}})
        return response["result"], json.loads(response["result"]["content"][0]["text"])

    def assert_error(self, name, arguments, code):
        result, payload = self.call(name, arguments)
        self.assertTrue(result["isError"]); self.assertEqual(payload["error"]["code"], code)

    def test_lifecycle_and_explicit_tools_are_legacy_only(self):
        self.assertEqual(self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}})["error"]["code"], -32002)
        self.assertEqual(self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "server/discover", "params": {}})["error"]["code"], -32601)
        self.ready_server()
        listed = self.server.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/list", "params": {}})
        self.assertEqual([tool["name"] for tool in listed["result"]["tools"]], TOOL_NAMES)
        self.assertIsNone(self.server.handle({"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "oe_set_status", "arguments": {"mode": "ACQUIRE"}}}))
        self.assertEqual(self.api.requests, [])

    def test_tools_methods_accept_only_dictionary_outer_metadata(self):
        self.ready_server()
        listed = self.server.handle({"jsonrpc": "2.0", "id": 3, "method": "tools/list", "params": {"_meta": {}}})
        self.assertEqual([tool["name"] for tool in listed["result"]["tools"]], TOOL_NAMES)
        called = self.server.handle({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "oe_get_status", "arguments": {}, "_meta": {}}})
        self.assertEqual(json.loads(called["result"]["content"][0]["text"]), {"mode": "IDLE"})
        invalid_requests = (
            {"jsonrpc": "2.0", "id": 5, "method": "tools/list", "params": {"_meta": []}},
            {"jsonrpc": "2.0", "id": 6, "method": "tools/list", "params": {"unexpected": {}}},
            {"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": "oe_get_status", "arguments": {}, "_meta": []}},
            {"jsonrpc": "2.0", "id": 8, "method": "tools/call", "params": {"name": "oe_get_status", "arguments": {}, "unexpected": {}}},
        )
        for request in invalid_requests:
            with self.subTest(request=request):
                self.assertEqual(self.server.handle(request)["error"]["code"], -32602)

    def test_newer_legacy_client_can_negotiate_pinned_server_protocol(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "mcp", "version": "0.1.0", "title": "Official MCP Python SDK"}, "_meta": {}}})
        self.assertEqual(response["result"]["protocolVersion"], "2024-11-05")

    def test_initialize_rejects_nonobject_meta(self):
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "mcp", "version": "0.1.0"}, "_meta": []}})
        self.assertEqual(response["error"]["code"], -32602)

    def test_initialize_requires_nonempty_string_client_identity(self):
        invalid_client_info = ({}, {"name": "mcp"}, {"version": "0.1.0"}, {"name": "", "version": "0.1.0"}, {"name": "mcp", "version": ""}, {"name": 1, "version": "0.1.0"}, {"name": "mcp", "version": []})
        for index, client_info in enumerate(invalid_client_info):
            with self.subTest(client_info=client_info):
                server = self.module.McpServer(CONTRACT_PATH, self.api.base_url)
                response = server.handle({"jsonrpc": "2.0", "id": index, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": client_info}})
                self.assertEqual(response["error"]["code"], -32602)

    def test_loopback_only_and_redirects_are_rejected(self):
        with self.assertRaises(ValueError): self.module.ApiClient("https://example.invalid")
        with self.assertRaises(ValueError): self.module.ApiClient("http://127.0.0.1/api/status")
        self.ready_server(); self.api.capabilities_redirect = "https://example.invalid/"
        self.assert_error("oe_get_capabilities", {}, "api_http_error")

    def test_capability_drift_fails_before_a_tool_route(self):
        self.ready_server(); self.api.capabilities = {**self.api.capabilities, "contract_version": "drift"}
        self.assert_error("oe_get_status", {}, "capability_contract_mismatch")
        self.assertFalse(any(request[1] == "/api/status" for request in self.api.requests))

    def test_read_validators_and_strict_arguments(self):
        self.ready_server()
        self.assertEqual(self.call("oe_get_status")[1], {"mode": "IDLE"})
        self.assertEqual(self.call("oe_get_recording_options")[1]["expanded"], False)
        self.assertEqual(self.call("oe_get_recording_filename")[1]["base_text"], "Record Node")
        self.assertEqual(self.call("oe_get_recording_directory")[1], {"parent_directory": "C:/data"})
        self.assertEqual(self.call("oe_get_cpu")[1], {"usage": 0.25})
        self.assertEqual(self.call("oe_get_disk")[1]["usage"], 0.5)
        self.assertEqual(self.call("oe_get_time")[1]["display"], "00:00:00")
        for name, arguments in (("oe_get_cpu", {"x": 1}), ("oe_set_status", {"mode": "IDLE", "extra": True}), ("oe_set_recording_options", {"expanded": True, "force_new_directory": True}), ("oe_set_recording_filename", {"base_text": "x", "append_text": "y"}), ("oe_set_recording_directory", {}), ("oe_set_recording_directory", {"parent_directory": "C:/data", "extra": True})):
            with self.subTest(name=name): self.assert_error(name, arguments, "invalid_arguments")
        self.api.cpu = {"usage": 1.1}; self.assert_error("oe_get_cpu", {}, "response_schema_mismatch")

    def test_record_requires_same_call_approval_that_is_not_forwarded(self):
        self.ready_server(); self.assert_error("oe_set_status", {"mode": "RECORD"}, "recording_approval_required")
        self.assertFalse(any(request[0] == "PUT" for request in self.api.requests))
        result, payload = self.call("oe_set_status", {"mode": "RECORD", "approve_recording": True})
        self.assertNotIn("isError", result); self.assertEqual(payload["after"], {"mode": "RECORD"})
        status = [request for request in self.api.requests if request[1] == "/api/status"]
        self.assertEqual([request[0] for request in status[-3:]], ["GET", "PUT", "GET"])
        self.assertEqual(status[-2][2], {"mode": "RECORD"})

    def test_status_conflict_and_postcondition_failure_preserve_details(self):
        self.ready_server(); conflict = {"error": {"code": "record_nodes_not_synchronized", "message": "All Record Nodes must be synchronized before recording."}, "requested_mode": "RECORD", "mode": "ACQUIRE"}
        self.api.status_put_response = (409, conflict)
        result, payload = self.call("oe_set_status", {"mode": "RECORD", "approve_recording": True})
        self.assertTrue(result["isError"]); self.assertEqual(payload["error"]["status"], 409); self.assertEqual(payload["error"]["body"], conflict)
        self.api.status_put_response = (200, {"mode": "IDLE"})
        self.assert_error("oe_set_status", {"mode": "ACQUIRE"}, "postcondition_failed")

    def test_options_and_filename_use_atomic_pre_put_post_readback(self):
        self.ready_server()
        for name, arguments, path, field, value in (("oe_set_recording_options", {"expanded": True}, "/api/recording/options", "expanded", True), ("oe_set_recording_filename", {"base_text": "mouse"}, "/api/recording", "base_text", "mouse")):
            with self.subTest(name=name):
                result, payload = self.call(name, arguments)
                self.assertNotIn("isError", result); self.assertEqual(payload["after"][field], value)
                requests = [request for request in self.api.requests if request[1] == path]
                self.assertEqual([request[0] for request in requests[-3:]], ["GET", "PUT", "GET"])
        self.api.recording_put_response = (200, self.api.recording)
        self.assert_error("oe_set_recording_filename", {"base_text": "unreadback"}, "postcondition_failed")

    def test_directory_normalizes_put_requires_readback_and_refuses_record(self):
        self.ready_server()
        result, payload = self.call("oe_set_recording_directory", {"parent_directory": "D:/Open Ephys"})
        self.assertNotIn("isError", result)
        self.assertEqual(payload, {
            "before": {"parent_directory": "C:/data"},
            "requested": {"parent_directory": "D:/Open Ephys"},
            "submitted": {"parent_directory": "D:\\Open Ephys"},
            "after": {"parent_directory": "D:\\Open Ephys"},
        })
        requests = [request for request in self.api.requests if request[1] == "/api/recording"]
        self.assertEqual([request[0] for request in requests[-3:]], ["GET", "PUT", "GET"])
        self.assertEqual(requests[-2][2], {"parent_directory": "D:\\Open Ephys"})
        self.api.mode = "RECORD"
        self.api.requests.clear()
        self.assert_error("oe_set_recording_directory", {"parent_directory": "E:/data"}, "recording_active")
        self.assertFalse(any(request[0] == "PUT" and request[1] == "/api/recording" for request in self.api.requests))

    def test_directory_rejects_unsafe_paths_before_http_and_accepts_windows_equivalence(self):
        self.ready_server()
        for value in ("", "../relative", "D:relative", "/current-drive-rooted"):
            with self.subTest(value=value):
                self.api.requests.clear()
                self.assert_error("oe_set_recording_directory", {"parent_directory": value}, "invalid_arguments")
                self.assertEqual(self.api.requests, [])

        original = self.server.api.request
        def equivalent_readback(method, path, body=None):
            response = original(method, path, body)
            if path == "/api/recording" and method in {"PUT", "GET"} and response["parent_directory"] != "C:/data":
                response = {**response, "parent_directory": "d:\\OPEN EPHYS\\."}
                self.api.recording["parent_directory"] = response["parent_directory"]
            return response
        self.server.api.request = equivalent_readback
        result, payload = self.call("oe_set_recording_directory", {"parent_directory": "D:/Open Ephys"})
        self.assertNotIn("isError", result)
        self.assertEqual(payload["submitted"], {"parent_directory": "D:\\Open Ephys"})
        self.assertEqual(payload["after"], {"parent_directory": "d:\\OPEN EPHYS\\."})

    def test_directory_noop_and_authoritative_refusal_fail_closed_with_details(self):
        self.ready_server()
        result, payload = self.call("oe_set_recording_directory", {"parent_directory": "D:/does-not-exist"})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "postcondition_failed")
        self.assertEqual(payload["error"]["requested"], {"parent_directory": "D:/does-not-exist"})
        self.assertEqual(payload["error"]["submitted"], {"parent_directory": "D:\\does-not-exist"})
        self.api.recording_put_response = (409, {"error": "official refusal"})
        result, payload = self.call("oe_set_recording_directory", {"parent_directory": "D:/forbidden"})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["status"], 409)
        self.assertEqual(payload["error"]["requested"], {"parent_directory": "D:/forbidden"})
        self.assertEqual(payload["error"]["submitted"], {"parent_directory": "D:\\forbidden"})

    def test_committed_mutations_with_transport_failure_report_unknown_outcome(self):
        self.ready_server()
        cases = (
            ("oe_set_status", {"mode": "ACQUIRE"}, "/api/status", "mode", "ACQUIRE"),
            ("oe_set_recording_options", {"expanded": True}, "/api/recording/options", "expanded", True),
            ("oe_set_recording_filename", {"base_text": "mouse"}, "/api/recording", "base_text", "mouse"),
            ("oe_set_recording_directory", {"parent_directory": "D:/data"}, "/api/recording", "parent_directory", "D:\\data"),
        )
        for name, arguments, path, field, expected in cases:
            with self.subTest(name=name):
                original = self.server.api.request
                def uncertain_request(method, request_path, body=None, *, _original=original, _path=path):
                    result = _original(method, request_path, body)
                    if method == "PUT" and request_path == _path:
                        raise self.module.ApiHttpError(None, "connection lost after commit")
                    return result
                self.server.api.request = uncertain_request
                result, payload = self.call(name, arguments)
                self.server.api.request = original
                self.assertTrue(result["isError"])
                self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
                self.assertEqual(payload["error"]["requested"], arguments)
                self.assertEqual(payload["error"]["observed"][field], expected)
                if name == "oe_set_recording_directory":
                    self.assertEqual(payload["error"]["submitted"], {"parent_directory": "D:\\data"})

    def test_committed_mutations_with_http_504_report_unknown_outcome(self):
        self.ready_server()
        timeout_body = {"error": {"code": "operation_timeout", "message": "Timed out waiting for the operation."}}
        cases = (
            ("oe_set_status", {"mode": "ACQUIRE"}, "/api/status", "mode", "ACQUIRE"),
            ("oe_set_recording_options", {"expanded": True}, "/api/recording/options", "expanded", True),
            ("oe_set_recording_filename", {"base_text": "mouse"}, "/api/recording", "base_text", "mouse"),
            ("oe_set_recording_directory", {"parent_directory": "D:/data"}, "/api/recording", "parent_directory", "D:\\data"),
        )
        for name, arguments, path, field, expected in cases:
            with self.subTest(name=name):
                original = self.server.api.request
                def timed_out_after_commit(method, request_path, body=None, *, _original=original, _path=path):
                    result = _original(method, request_path, body)
                    if method == "PUT" and request_path == _path:
                        raise self.module.ApiHttpError(504, timeout_body)
                    return result
                self.server.api.request = timed_out_after_commit
                result, payload = self.call(name, arguments)
                self.server.api.request = original
                self.assertTrue(result["isError"])
                self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
                self.assertEqual(payload["error"]["put_error"]["status"], 504)
                self.assertEqual(payload["error"]["put_error"]["body"], timeout_body)
                self.assertEqual(payload["error"]["observed"][field], expected)
                if name == "oe_set_recording_directory":
                    self.assertEqual(payload["error"]["submitted"], {"parent_directory": "D:\\data"})

    def test_committed_mutations_with_invalid_put_response_report_unknown_outcome(self):
        self.ready_server()
        cases = (
            ("oe_set_status", {"mode": "ACQUIRE"}, "/api/status", "mode", "ACQUIRE"),
            ("oe_set_recording_options", {"expanded": True}, "/api/recording/options", "expanded", True),
            ("oe_set_recording_filename", {"base_text": "mouse"}, "/api/recording", "base_text", "mouse"),
            ("oe_set_recording_directory", {"parent_directory": "D:/data"}, "/api/recording", "parent_directory", "D:\\data"),
        )
        for name, arguments, path, field, expected in cases:
            with self.subTest(name=name):
                original = self.server.api.request
                def invalid_response(method, request_path, body=None, *, _original=original, _path=path):
                    result = _original(method, request_path, body)
                    return {} if method == "PUT" and request_path == _path else result
                self.server.api.request = invalid_response
                result, payload = self.call(name, arguments)
                self.server.api.request = original
                self.assertTrue(result["isError"])
                self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
                self.assertEqual(payload["error"]["requested"], arguments)
                self.assertEqual(payload["error"]["observed"][field], expected)
                if name == "oe_set_recording_directory":
                    self.assertEqual(payload["error"]["submitted"], {"parent_directory": "D:\\data"})

    def test_unknown_mutation_outcome_includes_readback_failure_details(self):
        self.ready_server()
        original = self.server.api.request
        committed = False
        def uncertain_and_unreadable(method, path, body=None):
            nonlocal committed
            if committed and method == "GET" and path == "/api/status":
                raise self.module.ApiHttpError(None, "readback unavailable")
            result = original(method, path, body)
            if method == "PUT" and path == "/api/status":
                committed = True
                raise self.module.ApiHttpError(None, "connection lost after commit")
            return result
        self.server.api.request = uncertain_and_unreadable
        result, payload = self.call("oe_set_status", {"mode": "ACQUIRE"})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
        self.assertEqual(payload["error"]["readback_error"]["code"], "api_http_error")
        self.assertNotIn("observed", payload["error"])

    def test_filename_validation_precedes_http_mutation(self):
        self.ready_server()
        for value in ("C:\\data", "../data", "mouse/probe", "mouse\x00probe", "mouse*probe", "CON", "COM\u00b9", "mouse."):
            with self.subTest(value=repr(value)):
                self.api.requests.clear(); self.assert_error("oe_set_recording_filename", {"base_text": value}, "invalid_arguments"); self.assertEqual(self.api.requests, [])

    def test_malformed_json_nonfinite_ids_and_stdio_recovery_are_safe(self):
        self.ready_server()
        for request_id in (True, None, [], {}, 1.5, math.nan, math.inf):
            response = self.server.handle({"jsonrpc": "2.0", "id": request_id, "method": "tools/list", "params": {}})
            self.assertIsNone(response["id"]); self.assertEqual(response["error"]["code"], -32600)
        stdin, stdout = io.StringIO("not json\n" + json.dumps({"jsonrpc": "2.0", "id": 9, "method": "initialize", "params": {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}}}) + "\n"), io.StringIO()
        old_stdin, old_stdout = self.module.sys.stdin, self.module.sys.stdout
        try:
            self.module.sys.stdin, self.module.sys.stdout = stdin, stdout; self.module.run_stdio(self.module.McpServer(CONTRACT_PATH, self.api.base_url))
        finally:
            self.module.sys.stdin, self.module.sys.stdout = old_stdin, old_stdout
        responses = [json.loads(line) for line in stdout.getvalue().splitlines()]
        self.assertEqual(responses[0]["error"]["code"], -32700); self.assertEqual(responses[1]["id"], 9)

    def test_skill_pins_contract_and_safety_boundary(self):
        skill = SKILL_PATH.read_text(encoding="utf-8")
        for required in ("0.0.2", "1.0.2", "2024-11-05", "approve_recording:true", "hardware_verified:false", "scientific_verified:false", "non-empty absolute Windows path", "equivalent PUT response and GET readback"):
            self.assertIn(required, skill)
        for forbidden in ("oe_api_request", "uia locator", "processor", "parameter", "stream"):
            self.assertNotIn(forbidden, skill.lower())


if __name__ == "__main__": unittest.main()
