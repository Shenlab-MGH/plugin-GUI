import importlib.util
import json
import unittest
from pathlib import Path

from Tests.AgentNative.test_open_ephys_mcp_r012 import FakeOpenEphysApi


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_r0_1_3.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"
README_PATH = AGENT_DIR / "README.md"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"

TOOL_NAMES = [
    "oe_get_capabilities", "oe_get_status", "oe_set_status",
    "oe_get_recording_options", "oe_set_recording_options",
    "oe_get_recording_filename", "oe_set_recording_filename",
    "oe_get_recording_directory", "oe_set_recording_directory",
    "oe_get_config", "oe_get_processors",
    "oe_get_cpu", "oe_get_disk", "oe_get_time",
]


def load_server_module():
    spec = importlib.util.spec_from_file_location("open_ephys_mcp_server_r013", SERVER_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ContractR013Tests(unittest.TestCase):
    def test_r013_contract_exists(self):
        self.assertTrue(CONTRACT_PATH.is_file(), "r0.1.3 contract is missing")

    def test_contract_pins_one_read_only_inventory_capability_and_tool(self):
        contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], "r0.1.3")
        self.assertEqual(contract["contract"]["version"], "r0.1.3")
        self.assertEqual(contract["bundle"]["version"], "r0.1.3")
        self.assertEqual(contract["api"]["capabilities_contract_version"], "0.1.4")
        self.assertEqual(contract["api"]["expected_capabilities_response"]["contract_version"], "0.1.4")
        self.assertEqual(len(contract["api"]["expected_capabilities_response"]["capabilities"]), 12)
        self.assertEqual([tool["name"] for tool in contract["tools"]], TOOL_NAMES)
        self.assertEqual(len(contract["tools"]), 14)

        capability = next(item for item in contract["api"]["expected_capabilities_response"]["capabilities"]
                          if item["id"] == "oe.control.signal_chain.processors")
        self.assertEqual(capability, {
            "id": "oe.control.signal_chain.processors",
            "name": "Loaded processors",
            "description": "Read the processors currently loaded in the signal chain.",
            "kind": "collection",
            "uia": {"automation_id": "oe.control.signal_chain.processors"},
            "api": [{"operation": "read", "method": "GET", "path": "/api/processors",
                     "request_fields": [], "response_fields": ["processors"]}],
        })
        tool = next(item for item in contract["tools"] if item["name"] == "oe_get_processors")
        self.assertEqual(tool["inputSchema"], {"type": "object", "properties": {}, "additionalProperties": False})
        for forbidden in ("parameter", "stream", "add", "delete", "load", "save", "probe", "shank", "timer"):
            self.assertNotIn(forbidden, tool["name"])
        self.assertEqual(contract["verification"], {"hardware_verified": False, "scientific_verified": False})


class McpR013Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        cls.module = load_server_module()

    def setUp(self):
        self.api = FakeOpenEphysApi(self.contract["api"]["expected_capabilities_response"])
        self.api.processors = {"processors": [
            {"id": 11, "name": "File Reader", "parameters": [{"name": "path"}],
             "streams": [{"name": "Data"}], "predecessor": None},
            {"id": 22, "name": "Mouse probe filter", "parameters": [],
             "streams": [], "predecessor": 11},
        ]}
        self.api.start()
        self.server = self.module.McpServer(CONTRACT_PATH, self.api.base_url)
        response = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2024-11-05", "capabilities": {},
            "clientInfo": {"name": "test", "version": "1"}}})
        self.assertEqual(response["result"]["serverInfo"]["version"], "r0.1.3")
        self.assertIsNone(self.server.handle({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}))

    def tearDown(self):
        self.api.close()

    def call(self, arguments=None):
        response = self.server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
            "name": "oe_get_processors", "arguments": arguments or {}}})
        return response["result"], json.loads(response["result"]["content"][0]["text"])

    def test_inventory_validates_raw_schema_then_returns_only_closed_projection(self):
        result, payload = self.call()
        self.assertNotIn("isError", result)
        self.assertEqual(payload, {"processors": [
            {"id": 11, "name": "File Reader", "predecessor": None},
            {"id": 22, "name": "Mouse probe filter", "predecessor": 11},
        ]})
        self.assertEqual([request[:2] for request in self.api.requests[-2:]],
                         [("GET", "/api/capabilities"), ("GET", "/api/processors")])

    def test_inventory_is_no_args_and_rejects_malformed_official_shapes(self):
        result, payload = self.call({"id": 22})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "invalid_arguments")

        invalid_payloads = (
            {}, {"processors": "not-a-list"}, {"processors": [], "extra": True},
            {"processors": [{"id": True, "name": "x", "parameters": [], "streams": [], "predecessor": None}]},
            {"processors": [{"id": 1, "name": "", "parameters": [], "streams": [], "predecessor": None}]},
            {"processors": [{"id": 1, "name": "x", "parameters": {}, "streams": [], "predecessor": None}]},
            {"processors": [{"id": 1, "name": "x", "parameters": [], "streams": [], "predecessor": "bad"}]},
            {"processors": [
                {"id": 1, "name": "x", "parameters": [], "streams": [], "predecessor": None},
                {"id": 1, "name": "y", "parameters": [], "streams": [], "predecessor": None},
            ]},
        )
        for payload_value in invalid_payloads:
            with self.subTest(payload=payload_value):
                self.api.processors = payload_value
                result, payload = self.call()
                self.assertTrue(result["isError"])
                self.assertEqual(payload["error"]["code"], "response_schema_mismatch")

    def test_empty_inventory_is_valid(self):
        self.api.processors = {"processors": []}
        self.assertEqual(self.call()[1], {"processors": []})


class OperatorDocsR013Tests(unittest.TestCase):
    def test_docs_state_exact_inventory_boundaries(self):
        for document in (README_PATH.read_text(encoding="utf-8"), SKILL_PATH.read_text(encoding="utf-8")):
            normalized = " ".join(document.split())
            for required in (
                "r0.1.3", "oe_get_processors", "id/name/predecessor",
                "session/configuration scoped", "current source-path field",
                "not full topology", "parameters or streams", "read-only",
                "hardware_verified:false", "scientific_verified:false",
            ):
                self.assertIn(required, normalized)


if __name__ == "__main__": unittest.main()
