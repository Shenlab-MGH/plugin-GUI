import importlib.util
import json
import math
import re
import tempfile
import threading
import unittest
from copy import deepcopy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
V003_CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_v0_0_3.json"
V004_CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_v0_0_4.json"
V004_PARITY_PATH = AGENT_DIR / "open_ephys_core_integration_parity_v0_0_4.json"
BUNDLE_PATH = AGENT_DIR / "open_ephys_agent_release_bundle.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"
README_PATH = AGENT_DIR / "README.md"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "agent-native-core.yml"

BASELINE = {
    "version": "1.1.0",
    "upstream_release_commit": "c2ce076f5b2d4182222f9d0fd9bb9b97a60582e7",
}

CAPABILITY_IDS = [
    "oe.control.acquisition",
    "oe.control.recording",
    "oe.control.recording.options",
    "oe.control.recording.filename",
    "oe.control.recording.directory",
    "oe.control.recording.new_directory",
    "oe.control.recording.force_new_directory",
    "oe.control.signal_chain.configuration",
    "oe.control.signal_chain.processors",
    "oe.status.cpu_usage",
    "oe.status.disk_usage",
    "oe.status.elapsed_time",
]

TOOL_NAMES = [
    "oe_get_capabilities", "oe_get_status", "oe_set_status",
    "oe_get_recording_options", "oe_set_recording_options",
    "oe_get_recording_filename", "oe_set_recording_filename",
    "oe_get_recording_directory", "oe_set_recording_directory",
    "oe_get_config", "oe_get_processors", "oe_get_cpu", "oe_get_disk", "oe_get_time",
]

PROCESSOR_CAPABILITY = {
    "id": "oe.control.signal_chain.processors",
    "name": "Loaded processors",
    "description": "Read the processors currently loaded in the signal chain.",
    "kind": "collection",
    "api": [{"operation": "read", "method": "GET", "path": "/api/processors",
             "request_fields": [], "response_fields": ["processors"]}],
}

PROCESSOR_TOOL = {
    "name": "oe_get_processors",
    "description": "Read loaded processors as id, current display name, and current-path predecessor.",
    "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
}


def documented_tools(text):
    section = text.split("## Tool surface", 1)
    if len(section) != 2:
        return []
    section_body = section[1].split("\n## ", 1)[0]
    return re.findall(r"^- `(oe_[a-z0-9_]+)`", section_body, flags=re.MULTILINE)


def flatten_test_ids(suite):
    ids = []
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            ids.extend(flatten_test_ids(item))
        else:
            ids.append(item.id())
    return ids


def load_server_module():
    spec = importlib.util.spec_from_file_location("open_ephys_mcp_server_v004", SERVER_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def wished_v004_contract():
    contract = deepcopy(json.loads(V003_CONTRACT_PATH.read_text(encoding="utf-8")))
    contract["schema_version"] = "0.0.4"
    contract["contract"]["version"] = "0.0.4"
    contract["bundle"]["version"] = "0.0.4"
    contract["api"]["capabilities_contract_version"] = "0.0.4"
    capabilities = contract["api"]["expected_capabilities_response"]
    capabilities["contract_version"] = "0.0.4"
    capability_index = next(
        index for index, item in enumerate(capabilities["capabilities"])
        if item["id"] == "oe.status.cpu_usage"
    )
    capabilities["capabilities"].insert(capability_index, PROCESSOR_CAPABILITY)
    tool_index = next(
        index for index, item in enumerate(contract["tools"])
        if item["name"] == "oe_get_cpu"
    )
    contract["tools"].insert(tool_index, PROCESSOR_TOOL)
    for tool in contract["tools"]:
        if tool.get("name") == "oe_get_capabilities":
            tool["description"] = (
                "Read and verify the exact Open Ephys 0.0.4 core capability contract."
            )
    return contract


class ProcessorApi:
    def __init__(self, capabilities):
        self.capabilities = capabilities
        self.processors = {"processors": []}
        self.requests = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_GET(self):
                owner.requests.append(("GET", self.path))
                payload = {"/api/capabilities": owner.capabilities,
                           "/api/processors": owner.processors}.get(self.path)
                status = 200 if payload is not None else 404
                data = json.dumps(payload if payload is not None else {"error": "not found"},
                                  allow_nan=False).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

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


class ContractV004Tests(unittest.TestCase):
    def test_v004_contract_keeps_v11_discovery_only_and_moves_uia_to_parity(self):
        self.assertTrue(V004_CONTRACT_PATH.is_file(), "0.0.4 v1.1 contract is missing")
        if not V004_CONTRACT_PATH.is_file():
            return
        contract = json.loads(V004_CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], "0.0.4")
        self.assertEqual(contract["contract"]["version"], "0.0.4")
        self.assertEqual(contract["bundle"]["version"], "0.0.4")
        self.assertEqual(contract["baseline"]["version"], BASELINE["version"])
        self.assertEqual(contract["baseline"]["upstream_release_commit"],
                         BASELINE["upstream_release_commit"])
        self.assertEqual(contract["api"]["capabilities_contract_version"], "0.0.4")
        response = contract["api"]["expected_capabilities_response"]
        self.assertEqual(response["contract_version"], "0.0.4")
        self.assertNotIn("surface", response)
        self.assertEqual([item["id"] for item in response["capabilities"]], CAPABILITY_IDS)
        processors = next(item for item in response["capabilities"]
                          if item["id"] == "oe.control.signal_chain.processors")
        self.assertEqual(processors, PROCESSOR_CAPABILITY)
        self.assertNotIn("uia", processors)
        self.assertEqual([tool["name"] for tool in contract["tools"]], TOOL_NAMES)
        tools_by_name = {tool["name"]: tool for tool in contract["tools"]}
        self.assertEqual(
            tools_by_name["oe_get_capabilities"]["description"],
            "Read and verify the exact Open Ephys 0.0.4 core capability contract.",
        )
        for tool in contract["tools"]:
            self.assertNotIn("r0.1.2", tool["description"])
            self.assertNotIn("r0.1.3", tool["description"])
        self.assertFalse(contract["verification"]["hardware_verified"])
        self.assertFalse(contract["verification"]["scientific_verified"])

    def test_v004_parity_pins_global_invariants_complete_surface_and_pending_gates(self):
        self.assertTrue(V004_PARITY_PATH.is_file(), "0.0.4 v1.1 parity artifact is missing")
        if not V004_PARITY_PATH.is_file():
            return
        parity = json.loads(V004_PARITY_PATH.read_text(encoding="utf-8"))
        self.assertEqual(parity["schema_version"], "0.0.4")
        self.assertEqual(parity["baseline"], BASELINE)
        self.assertFalse(parity["new_open_ephys_functionality"])
        self.assertEqual([item["id"] for item in parity["capabilities"]], CAPABILITY_IDS)
        parity_processor = next(item for item in parity["capabilities"]
                                if item["id"] == "oe.control.signal_chain.processors")
        self.assertEqual(parity_processor["parity"], "BOTH")
        self.assertEqual(parity_processor["api"], {
            "read": {"method": "GET", "path": "/api/processors", "field": "processors"}})
        self.assertEqual(parity_processor["uia"], {
            "platform": "windows",
            "automation_id": "oe.control.signal_chain.processors",
            "item_automation_id": "oe.processor.<node_id>",
        })
        self.assertEqual(parity_processor["mcp_tools"], ["oe_get_processors"])
        verification = parity["verification"]
        self.assertEqual(verification["processor_inventory"], {
            "windows_uia_runtime": "pending",
            "mcp_local_interop": "pending",
        })
        self.assertEqual(verification["official_mcp_v2_exact_commit"], "pending")
        self.assertEqual(verification["hardware"], "not_verified")

    def test_successor_bundle_preserves_v004_upstream_and_mcp_v2_provenance(self):
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], "0.0.5")
        self.assertEqual(bundle["bundle"]["version"], "0.0.5")
        self.assertIn("processor-inventory", bundle["bundle"]["coverage"])
        self.assertEqual(bundle["official_upstream"], {
            "repository": "open-ephys/plugin-GUI",
            "tag": "v1.1.0",
            "commit": "c2ce076f5b2d4182222f9d0fd9bb9b97a60582e7",
            "gui_version": "1.1.0",
        })
        self.assertEqual(bundle["contract"]["fixture"],
                         "agent_native/open_ephys_agent_contract_v1_1_0_v0_0_5.json")
        self.assertEqual(bundle["components"]["mcp"]["protocol_version"], "2024-11-05")
        self.assertEqual(bundle["components"]["mcp"]["protocol_era"], "legacy")
        self.assertIn("official_mcp_v2_auto_fallback", bundle["verification"])
        self.assertIn("official_mcp_v2_blocker", bundle["verification"])
        self.assertFalse(bundle["verification"]["hardware_verified"])

    def test_v004_operator_docs_pin_exact_tools_boundaries_and_nonclaims(self):
        for path in (README_PATH, SKILL_PATH):
            with self.subTest(path=path):
                text = path.read_text(encoding="utf-8")
                normalized = " ".join(text.lower().split())
                self.assertIn("0.0.4", text)
                self.assertIn("open ephys baseline:", normalized)
                self.assertIn("1.1.0", normalized)
                self.assertEqual(
                    documented_tools(text),
                    [*TOOL_NAMES, "oe_get_electrode_presets", "oe_set_electrode_preset"],
                )
                for phrase in (
                    "read-only processor inventory",
                    "id, current display name, and current-path predecessor",
                    "session/configuration scoped",
                    "not full topology",
                    "parameters or streams",
                    "no new open ephys functionality",
                    "no add, delete, load, save, probe, shank, or timer controls",
                    "hardware_verified=false",
                    "scientific_verified=false",
                ):
                    self.assertIn(phrase, normalized)

    def test_windows_workflow_discovers_both_contract_slices_and_parity(self):
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("runs-on: windows-2022", workflow)
        self.assertIn(
            'python -m unittest discover -s Tests/AgentNative -p "test_*.py" -v',
            workflow,
        )
        discovered = flatten_test_ids(unittest.defaultTestLoader.discover(
            str(ROOT / "Tests" / "AgentNative"),
            pattern="test_*.py",
        ))
        for expected in (
            "test_open_ephys_mcp_v003.ContractTests.test_v003_extends_exact_v002_core_by_configuration_snapshot_only",
            "test_open_ephys_mcp_v0_0_4.ContractV004Tests.test_v004_contract_keeps_v11_discovery_only_and_moves_uia_to_parity",
            "test_core_integration_parity.CoreIntegrationParityTests.test_report_matches_existing_core_contract_without_new_oe_features",
        ):
            self.assertIn(expected, discovered)


class McpV004Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_server_module()

    def setUp(self):
        self.contract = wished_v004_contract()
        self.temp_dir = tempfile.TemporaryDirectory()
        self.contract_path = Path(self.temp_dir.name) / "v004.json"
        self.contract_path.write_text(json.dumps(self.contract), encoding="utf-8")
        self.api = ProcessorApi(self.contract["api"]["expected_capabilities_response"])
        try:
            self.server = self.module.McpServer(self.contract_path, self.api.base_url)
        except ValueError as exc:
            self.api.server.server_close()
            self.temp_dir.cleanup()
            self.fail(f"0.0.4 contract rejected by MCP server: {exc}")
        self.api.start()
        initialized = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2024-11-05", "capabilities": {},
                "clientInfo": {"name": "v004", "version": "1"}}})
        self.assertEqual(initialized["result"]["serverInfo"]["version"], "0.0.4")
        self.assertIsNone(self.server.handle({
            "jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}))

    def tearDown(self):
        self.api.close()
        self.temp_dir.cleanup()

    def call(self, arguments=None):
        response = self.server.handle({
            "jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
                "name": "oe_get_processors", "arguments": {} if arguments is None else arguments}})
        result = response["result"]
        return result, json.loads(result["content"][0]["text"])

    def test_no_argument_inventory_validates_full_schema_then_returns_closed_projection(self):
        self.api.processors = {"processors": [
            {"id": 11, "name": "File Reader",
             "parameters": [{"name": "path", "type": "String", "value": "C:/data"}],
             "streams": [{"name": "Neuropixels AP", "source_id": 11,
                          "sample_rate": 30000.0, "channel_count": 384, "parameters": []}],
             "predecessor": None},
            {"id": 22, "name": "Mouse probe filter", "parameters": [], "streams": [],
             "predecessor": 11},
        ]}
        result, payload = self.call()
        self.assertNotIn("isError", result)
        self.assertEqual(payload, {"processors": [
            {"id": 11, "name": "File Reader", "predecessor": None},
            {"id": 22, "name": "Mouse probe filter", "predecessor": 11},
        ]})
        self.assertEqual(self.api.requests[-2:],
                         [("GET", "/api/capabilities"), ("GET", "/api/processors")])

    def test_inventory_rejects_arguments_before_http(self):
        result, payload = self.call({"id": 22})
        self.assertTrue(result["isError"])
        self.assertEqual(payload["error"]["code"], "invalid_arguments")
        self.assertEqual(self.api.requests, [])

    def test_inventory_rejects_complete_raw_schema_drift_matrix(self):
        valid_parameter = {"name": "gain", "type": "Float", "value": "500"}
        valid_stream = {
            "name": "Neuropixels AP", "source_id": 1, "sample_rate": 30000.0,
            "channel_count": 384, "parameters": [valid_parameter],
        }
        valid_processor = {
            "id": 1, "name": "Probe", "parameters": [valid_parameter],
            "streams": [valid_stream], "predecessor": None,
        }
        invalid_payloads = (
            ("top-level missing processors", {}),
            ("top-level processors is not a list", {"processors": {}}),
            ("top-level extra field", {"processors": [], "extra": True}),
            ("processor is not an object", {"processors": [1]}),
            ("processor missing id", {"processors": [{"name": "Probe", "parameters": [valid_parameter], "streams": [valid_stream], "predecessor": None}]}),
            ("processor missing name", {"processors": [{"id": 1, "parameters": [valid_parameter], "streams": [valid_stream], "predecessor": None}]}),
            ("processor missing parameters", {"processors": [{"id": 1, "name": "Probe", "streams": [valid_stream], "predecessor": None}]}),
            ("processor missing streams", {"processors": [{"id": 1, "name": "Probe", "parameters": [valid_parameter], "predecessor": None}]}),
            ("processor missing predecessor", {"processors": [{"id": 1, "name": "Probe", "parameters": [valid_parameter], "streams": [valid_stream]}]}),
            ("processor extra field", {"processors": [{**valid_processor, "extra": True}]}),
            ("processor bool id", {"processors": [{**valid_processor, "id": True}]}),
            ("processor non-integer id", {"processors": [{**valid_processor, "id": 1.0}]}),
            ("processor negative id", {"processors": [{**valid_processor, "id": -1}]}),
            ("processor non-string name", {"processors": [{**valid_processor, "name": 1}]}),
            ("parameters is not a list", {"processors": [{**valid_processor, "parameters": {}}]}),
            ("parameter is not an object", {"processors": [{**valid_processor, "parameters": [1]}]}),
            ("parameter missing name", {"processors": [{**valid_processor, "parameters": [{"type": "Float", "value": "500"}]}]}),
            ("parameter missing type", {"processors": [{**valid_processor, "parameters": [{"name": "gain", "value": "500"}]}]}),
            ("parameter missing value", {"processors": [{**valid_processor, "parameters": [{"name": "gain", "type": "Float"}]}]}),
            ("parameter extra field", {"processors": [{**valid_processor, "parameters": [{**valid_parameter, "extra": True}]}]}),
            ("parameter non-string name", {"processors": [{**valid_processor, "parameters": [{**valid_parameter, "name": 1}]}]}),
            ("parameter non-string type", {"processors": [{**valid_processor, "parameters": [{**valid_parameter, "type": 1}]}]}),
            ("parameter non-string value", {"processors": [{**valid_processor, "parameters": [{**valid_parameter, "value": 500}]}]}),
            ("streams is not a list", {"processors": [{**valid_processor, "streams": {}}]}),
            ("stream is not an object", {"processors": [{**valid_processor, "streams": [1]}]}),
            ("stream missing name", {"processors": [{**valid_processor, "streams": [{"source_id": 1, "sample_rate": 30000.0, "channel_count": 384, "parameters": [valid_parameter]}]}]}),
            ("stream missing source_id", {"processors": [{**valid_processor, "streams": [{"name": "AP", "sample_rate": 30000.0, "channel_count": 384, "parameters": [valid_parameter]}]}]}),
            ("stream missing sample_rate", {"processors": [{**valid_processor, "streams": [{"name": "AP", "source_id": 1, "channel_count": 384, "parameters": [valid_parameter]}]}]}),
            ("stream missing channel_count", {"processors": [{**valid_processor, "streams": [{"name": "AP", "source_id": 1, "sample_rate": 30000.0, "parameters": [valid_parameter]}]}]}),
            ("stream missing parameters", {"processors": [{**valid_processor, "streams": [{"name": "AP", "source_id": 1, "sample_rate": 30000.0, "channel_count": 384}]}]}),
            ("stream extra field", {"processors": [{**valid_processor, "streams": [{**valid_stream, "extra": True}]}]}),
            ("stream non-string name", {"processors": [{**valid_processor, "streams": [{**valid_stream, "name": 1}]}]}),
            ("stream bool source id", {"processors": [{**valid_processor, "streams": [{**valid_stream, "source_id": True}]}]}),
            ("stream non-integer source id", {"processors": [{**valid_processor, "streams": [{**valid_stream, "source_id": 1.0}]}]}),
            ("stream negative source id", {"processors": [{**valid_processor, "streams": [{**valid_stream, "source_id": -1}]}]}),
            ("stream bool sample rate", {"processors": [{**valid_processor, "streams": [{**valid_stream, "sample_rate": True}]}]}),
            ("stream non-numeric sample rate", {"processors": [{**valid_processor, "streams": [{**valid_stream, "sample_rate": "30000"}]}]}),
            ("stream bool channel count", {"processors": [{**valid_processor, "streams": [{**valid_stream, "channel_count": True}]}]}),
            ("stream non-integer channel count", {"processors": [{**valid_processor, "streams": [{**valid_stream, "channel_count": 384.0}]}]}),
            ("stream negative channel count", {"processors": [{**valid_processor, "streams": [{**valid_stream, "channel_count": -1}]}]}),
            ("stream parameters is not a list", {"processors": [{**valid_processor, "streams": [{**valid_stream, "parameters": {}}]}]}),
            ("stream parameter is not an object", {"processors": [{**valid_processor, "streams": [{**valid_stream, "parameters": [1]}]}]}),
            ("stream parameter missing field", {"processors": [{**valid_processor, "streams": [{**valid_stream, "parameters": [{"name": "gain", "type": "Float"}]}]}]}),
            ("stream parameter extra field", {"processors": [{**valid_processor, "streams": [{**valid_stream, "parameters": [{**valid_parameter, "extra": True}]}]}]}),
            ("stream parameter non-string name", {"processors": [{**valid_processor, "streams": [{**valid_stream, "parameters": [{**valid_parameter, "name": 1}]}]}]}),
            ("stream parameter non-string type", {"processors": [{**valid_processor, "streams": [{**valid_stream, "parameters": [{**valid_parameter, "type": 1}]}]}]}),
            ("stream parameter non-string value", {"processors": [{**valid_processor, "streams": [{**valid_stream, "parameters": [{**valid_parameter, "value": 500}]}]}]}),
            ("predecessor bool", {"processors": [{**valid_processor, "predecessor": True}]}),
            ("predecessor non-integer", {"processors": [{**valid_processor, "predecessor": 1.0}]}),
            ("predecessor string", {"processors": [{**valid_processor, "predecessor": "1"}]}),
            ("predecessor negative", {"processors": [{**valid_processor, "predecessor": -1}]}),
            ("duplicate processor ids", {"processors": [valid_processor, {**valid_processor, "name": "Duplicate"}]}),
        )
        for label, invalid in invalid_payloads:
            with self.subTest(case=label):
                self.api.processors = invalid
                result, payload = self.call()
                self.assertTrue(result["isError"])
                self.assertEqual(payload["error"]["code"], "response_schema_mismatch")

    def test_inventory_rejects_nonfinite_stream_rates(self):
        valid_parameter = {"name": "gain", "type": "Float", "value": "500"}
        valid_stream = {
            "name": "Neuropixels AP", "source_id": 1, "sample_rate": 30000.0,
            "channel_count": 384, "parameters": [valid_parameter],
        }
        valid_processor = {
            "id": 1, "name": "Probe", "parameters": [valid_parameter],
            "streams": [valid_stream], "predecessor": None,
        }
        for sample_rate in (math.nan, math.inf, -math.inf):
            with self.subTest(sample_rate=sample_rate):
                payload = {"processors": [{
                    **valid_processor,
                    "streams": [{**valid_stream, "sample_rate": sample_rate}],
                }]}
                with self.assertRaises(self.module.ToolError) as rejected:
                    self.module.processors_projection(payload)
                self.assertEqual(rejected.exception.code, "response_schema_mismatch")

    def test_empty_inventory_and_empty_official_display_names_are_valid(self):
        self.api.processors = {"processors": []}
        self.assertEqual(self.call()[1], {"processors": []})
        empty_parameter = {"name": "", "type": "", "value": ""}
        self.api.processors = {"processors": [
            {"id": 1, "name": "", "parameters": [empty_parameter],
             "streams": [{"name": "", "source_id": 0, "sample_rate": 0.0,
                          "channel_count": 0, "parameters": [empty_parameter]}],
             "predecessor": None}]}
        self.assertEqual(self.call()[1], {
            "processors": [{"id": 1, "name": "", "predecessor": None}]})


if __name__ == "__main__":
    unittest.main()
