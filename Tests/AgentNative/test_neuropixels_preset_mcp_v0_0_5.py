"""Contract tests for the optional Neuropixels preset MCP adapter (0.0.5)."""

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


REAL_ROOT = Path(__file__).resolve().parents[2]
SERVER_PATH = REAL_ROOT / "agent_native" / "open_ephys_mcp_server.py"

spec = importlib.util.spec_from_file_location("open_ephys_mcp_server_real_v11_preset", SERVER_PATH)
real_server = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = real_server
spec.loader.exec_module(real_server)

HASH_A = "sha256:" + "a" * 64
HASH_B = "sha256:" + "b" * 64


def capability_document(exact: bool = True, *, top_level_version: str = "0.0.5") -> dict:
    descriptor = {
        "id": "oe.control.neuropixels.preset",
        "version": "0.0.5" if exact else "0.0.4",
        "operations": ["inventory", "set"],
    }
    capabilities = [{
        "id": item["id"],
        "name": item["id"],
        "description": item["id"],
        "kind": item["kind"],
        "api": list(item["api"]),
        **({"mode_semantics": item["mode_semantics"]} if "mode_semantics" in item else {}),
    } for item in real_server.REMOTE_BASE_CAPABILITIES]
    capabilities.append(descriptor)
    return {"contract_version": top_level_version, "surface": "discovery_only",
            "capabilities": capabilities}

def inventory(*, generation: str = "generation-1", selected_id: str = "preset-a") -> dict:
    presets = [
        {"preset_id": "preset-a", "preset_label": "Preset A", "electrode_map_hash": HASH_A},
        {"preset_id": "preset-b", "preset_label": "Preset B", "electrode_map_hash": HASH_B},
    ]
    selected = next(item for item in presets if item["preset_id"] == selected_id)
    return {
        "inventory_generation": generation,
        "targets": [{
            "processor_id": 100,
            "probe_id": "opaque-probe-1",
            "probe_serial": "NP2-SERIAL",
            "slot": 0,
            "port": 1,
            "dock": 0,
            "available_presets": copy.deepcopy(presets),
            "selected": {"selection_kind": "preset", **copy.deepcopy(selected)},
        }],
    }


def mutation_result(arguments: dict | None = None, *, before_id: str = "preset-a",
                    after_id: str = "preset-b", changed: bool = True) -> dict:
    arguments = arguments or PresetMcpTestCase.set_arguments()
    def selected_readback(preset_id: str) -> dict:
        target = inventory(selected_id=preset_id)["targets"][0]
        return {
            "processor_id": target["processor_id"],
            "probe_id": target["probe_id"],
            "probe_serial": target["probe_serial"],
            "preset_id": target["selected"]["preset_id"],
            "preset_label": target["selected"]["preset_label"],
            "electrode_map_hash": target["selected"]["electrode_map_hash"],
        }
    return {
        "changed": changed,
        "before": selected_readback(before_id),
        "requested": copy.deepcopy(arguments),
        "after": selected_readback(after_id),
    }


class FakeApi:
    def __init__(self, *, exact_capability: bool = True,
                 top_level_version: str = "0.0.5"):
        self.exact_capability = exact_capability
        self.top_level_version = top_level_version
        self.mode = "IDLE"
        self.inventory_reads = [inventory()]
        self.requests = []
        self.put_error = None
        self.put_response = mutation_result()

    def request(self, method: str, path: str, body=None):
        self.requests.append((method, path, copy.deepcopy(body)))
        if method == "GET" and path == "/api/capabilities":
            return capability_document(
                self.exact_capability,
                top_level_version=self.top_level_version,
            )
        if method == "GET" and path == "/api/status":
            return {"mode": self.mode}
        if method == "GET" and path == "/api/plugins/neuropixels/presets":
            if len(self.inventory_reads) > 1:
                return copy.deepcopy(self.inventory_reads.pop(0))
            return copy.deepcopy(self.inventory_reads[0])
        if method == "PUT" and path == "/api/plugins/neuropixels/presets/selected":
            if self.put_error is not None:
                raise self.put_error
            return copy.deepcopy(self.put_response)
        raise AssertionError(f"unexpected request: {method} {path}")


class PresetMcpTestCase(unittest.TestCase):
    def make_server(self, api: FakeApi):
        server = real_server.McpServer(api_client=api)
        server.connection_state = "ready"
        return server

    def call_tool(self, server, name, arguments=None):
        response = server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": name, "arguments": arguments or {}},
        })
        result = response["result"]
        return json.loads(result["content"][0]["text"]), result.get("isError", False)

    @staticmethod
    def set_arguments(**overrides):
        value = {
            "processor_id": 100,
            "probe_id": "opaque-probe-1",
            "expected_probe_serial": "NP2-SERIAL",
            "expected_inventory_generation": "generation-1",
            "preset_id": "preset-b",
            "expected_electrode_map_hash": HASH_B,
        }
        value.update(overrides)
        return value


class DiscoveryTests(PresetMcpTestCase):
    def test_tools_are_stably_published_but_fail_closed_without_exact_remote_capability(self):
        server = self.make_server(FakeApi(exact_capability=False))
        listed = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        names = [tool["name"] for tool in listed["result"]["tools"]]
        self.assertIn("oe_get_electrode_presets", names)
        self.assertIn("oe_set_electrode_preset", names)
        payload, is_error = self.call_tool(server, "oe_get_electrode_presets")
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "capability_contract_mismatch")

    def test_exact_remote_capability_publishes_legacy_compatible_tool_schemas(self):
        server = self.make_server(FakeApi())
        listed = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        tools = {tool["name"]: tool for tool in listed["result"]["tools"]}
        self.assertIn("oe_get_electrode_presets", tools)
        self.assertIn("oe_set_electrode_preset", tools)
        requirement = real_server.load_preset_contract(real_server.DEFAULT_PRESET_CONTRACT)
        expected = {tool["name"]: tool for tool in requirement["tools"]}
        for name in ("oe_get_electrode_presets", "oe_set_electrode_preset"):
            self.assertEqual(tools[name]["inputSchema"], expected[name]["inputSchema"])
            self.assertNotIn("outputSchema", tools[name])
        payload, is_error = self.call_tool(server, "oe_get_capabilities")
        self.assertFalse(is_error)
        self.assertEqual(payload["contract_version"], "0.0.5")
        self.assertEqual(payload["capabilities"][-1], capability_document()["capabilities"][-1])

    def test_descriptor_with_extra_fields_is_not_an_exact_match(self):
        api = FakeApi()
        original_request = api.request

        def request(method, path, body=None):
            response = original_request(method, path, body)
            if method == "GET" and path == "/api/capabilities":
                response["capabilities"][-1]["extra"] = True
            return response

        api.request = request
        server = self.make_server(api)
        listed = server.handle(
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        names = [tool["name"] for tool in listed["result"]["tools"]]
        self.assertIn("oe_set_electrode_preset", names)
        payload, is_error = self.call_tool(server, "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "capability_contract_mismatch")

    def test_exact_descriptor_under_wrong_top_level_contract_is_unavailable(self):
        server = self.make_server(FakeApi(top_level_version="0.0.4"))
        payload, is_error = self.call_tool(server, "oe_get_electrode_presets")
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "capability_contract_mismatch")


class InventoryTests(PresetMcpTestCase):
    def test_inventory_is_validated_against_output_schema(self):
        api = FakeApi()
        server = self.make_server(api)
        payload, is_error = self.call_tool(server, "oe_get_electrode_presets")
        self.assertFalse(is_error)
        self.assertEqual(payload, inventory())

        api.inventory_reads = [{**inventory(), "unexpected": True}]
        payload, is_error = self.call_tool(server, "oe_get_electrode_presets")
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "response_schema_mismatch")

    def test_probe_id_must_be_unique_across_entire_inventory(self):
        api = FakeApi()
        value = inventory()
        duplicate = copy.deepcopy(value["targets"][0])
        duplicate["processor_id"] = 200
        value["targets"].append(duplicate)
        api.inventory_reads = [value]
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_get_electrode_presets")
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "response_schema_mismatch")


class MutationTests(PresetMcpTestCase):
    def test_set_arguments_use_closed_contract_schema(self):
        api = FakeApi()
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset",
            self.set_arguments(unexpected=True),
        )
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "invalid_arguments")
        self.assertFalse(any(method == "PUT" for method, _path, _body in api.requests))

    def test_set_requires_idle_before_dispatch(self):
        api = FakeApi()
        api.mode = "ACQUIRE"
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "preset_change_requires_idle")
        self.assertFalse(any(method == "PUT" for method, _path, _body in api.requests))

    def test_generation_hash_and_probe_identity_are_cas_preconditions(self):
        cases = [
            ({"expected_inventory_generation": "stale"}, "inventory_generation_mismatch"),
            ({"expected_probe_serial": "WRONG"}, "probe_identity_mismatch"),
        ]
        for overrides, expected_code in cases:
            with self.subTest(expected_code=expected_code):
                api = FakeApi()
                payload, is_error = self.call_tool(
                    self.make_server(api), "oe_set_electrode_preset",
                    self.set_arguments(**overrides),
                )
                self.assertTrue(is_error)
                self.assertEqual(payload["error"]["code"], expected_code)
                self.assertFalse(any(method == "PUT" for method, _path, _body in api.requests))

    def test_requested_preset_row_hash_mismatch_fails_before_put(self):
        api = FakeApi()
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset",
            self.set_arguments(expected_electrode_map_hash=HASH_A),
        )
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "electrode_map_expectation_mismatch")
        self.assertFalse(any(method == "PUT" for method, _path, _body in api.requests))

    def test_same_target_same_preset_is_idempotent_without_put(self):
        api = FakeApi()
        args = self.set_arguments(preset_id="preset-a", expected_electrode_map_hash=HASH_A)
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", args)
        self.assertFalse(is_error)
        self.assertFalse(payload["changed"])
        self.assertEqual(payload["before"], payload["after"])
        self.assertEqual(payload["requested"], args)
        self.assertFalse(any(method == "PUT" for method, _path, _body in api.requests))

    def test_success_uses_exact_put_then_independent_authoritative_getter(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        args = self.set_arguments()
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", args)
        self.assertFalse(is_error)
        self.assertTrue(payload["changed"])
        self.assertEqual(payload["requested"], args)
        self.assertEqual(payload["after"]["preset_id"], "preset-b")
        puts = [request for request in api.requests if request[0] == "PUT"]
        self.assertEqual(puts, [("PUT", "/api/plugins/neuropixels/presets/selected", args)])
        put_index = api.requests.index(puts[0])
        self.assertEqual(api.requests[put_index + 1][:2],
                         ("GET", "/api/plugins/neuropixels/presets"))

    def test_malformed_success_body_is_unknown_and_cannot_report_success(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        api.put_response = {}
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
        self.assertEqual(len([r for r in api.requests
                              if r[:2] == ("GET", "/api/plugins/neuropixels/presets")]), 2)

    def test_success_body_with_wrong_requested_echo_is_unknown(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        wrong = self.set_arguments(preset_id="preset-a", expected_electrode_map_hash=HASH_A)
        api.put_response = mutation_result(wrong)
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")

    def test_core_noop_race_is_validated_then_confirmed_by_fresh_getter(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        api.put_response = mutation_result(before_id="preset-b", after_id="preset-b", changed=False)
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertFalse(is_error)
        self.assertTrue(payload["changed"])
        self.assertEqual(payload["before"]["preset_id"], "preset-a")
        self.assertEqual(payload["after"]["preset_id"], "preset-b")

    def test_valid_success_body_does_not_override_conflicting_fresh_getter(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory()]
        old_seconds = real_server.PRESET_CONVERGENCE_SECONDS
        real_server.PRESET_CONVERGENCE_SECONDS = 0
        try:
            payload, is_error = self.call_tool(
                self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        finally:
            real_server.PRESET_CONVERGENCE_SECONDS = old_seconds
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "postcondition_failed")

    def test_unknown_write_outcome_reads_fresh_state_once_and_never_retries(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        api.put_error = real_server.ToolError("open_ephys_unreachable", "disconnect")
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
        self.assertEqual(len([r for r in api.requests if r[0] == "PUT"]), 1)
        self.assertEqual(payload["error"]["observed"]["selected"]["preset_id"], "preset-b")

    def test_http_408_is_unknown_and_reads_once_without_retry(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        api.put_error = real_server.ApiHttpError(408, {
            "error": {"code": "request_timeout", "message": "timed out"},
        })
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
        self.assertEqual(len([r for r in api.requests if r[0] == "PUT"]), 1)
        self.assertEqual(len([r for r in api.requests
                              if r[:2] == ("GET", "/api/plugins/neuropixels/presets")]), 2)

    def test_definitive_typed_http_error_preserves_backend_code(self):
        api = FakeApi()
        api.put_error = real_server.ApiHttpError(409, {
            "error": {"code": "preset_apply_in_progress", "message": "busy"},
        })
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "preset_apply_in_progress")
        self.assertEqual(len([r for r in api.requests if r[0] == "PUT"]), 1)

    def test_typed_unknown_409_still_reads_fresh_state_once(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        api.put_error = real_server.ApiHttpError(409, {
            "error": {"code": "mutation_outcome_unknown", "message": "unknown"},
        })
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
        self.assertEqual(len([r for r in api.requests
                              if r[:2] == ("GET", "/api/plugins/neuropixels/presets")]), 2)

    def test_undeclared_typed_4xx_code_cannot_escape_closed_error_contract(self):
        api = FakeApi()
        api.put_error = real_server.ApiHttpError(409, {
            "error": {"code": "invented_backend_code", "message": "invented"},
        })
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "api_http_error")

    def test_definitive_typed_500_no_application_error_is_preserved(self):
        api = FakeApi()
        api.put_error = real_server.ApiHttpError(500, {
            "error": {"code": "preset_apply_failed", "message": "not dispatched"},
        })
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "preset_apply_failed")
        self.assertEqual(len([r for r in api.requests
                              if r[:2] == ("GET", "/api/plugins/neuropixels/presets")]), 1)

    def test_untyped_500_is_unknown_with_one_fresh_get_and_no_retry(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(selected_id="preset-b")]
        api.put_error = real_server.ApiHttpError(500, {"message": "server failure"})
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "mutation_outcome_unknown")
        self.assertEqual(len([r for r in api.requests if r[0] == "PUT"]), 1)
        self.assertEqual(len([r for r in api.requests
                              if r[:2] == ("GET", "/api/plugins/neuropixels/presets")]), 2)

    def test_generation_drift_after_dispatch_cannot_report_success(self):
        api = FakeApi()
        api.inventory_reads = [inventory(), inventory(generation="generation-2", selected_id="preset-b")]
        payload, is_error = self.call_tool(
            self.make_server(api), "oe_set_electrode_preset", self.set_arguments())
        self.assertTrue(is_error)
        self.assertEqual(payload["error"]["code"], "postcondition_failed")


class RequirementContractTests(unittest.TestCase):
    def test_requirement_is_implemented_unreleased_with_product_gates_pending(self):
        requirement = real_server.load_preset_contract(real_server.DEFAULT_PRESET_CONTRACT)
        self.assertEqual(requirement["contract_version"], "0.0.5")
        self.assertEqual(requirement["status"], "implemented_unreleased")
        self.assertEqual(requirement["compatible_processor_cardinality"], {
            "supported": "exactly_one",
            "zero": "capability_unavailable",
            "multiple": "ambiguous_target",
        })
        self.assertEqual(requirement["verification"], {
            "bridge_contract_tests_verified": True,
            "software_verified": False,
            "hardware_verified": False,
            "scientific_verified": False,
        })
        self.assertTrue(requirement["inventory_generation"]
                        ["stable_across_successful_preset_selection"])
        self.assertIn("current_selected_preset", requirement["inventory_generation"]["excludes"])
        self.assertEqual(requirement["readback_semantics"], {
            "state_source": "sdk_acknowledged_committed_configuration",
            "independent_from_http_put_response": True,
            "physical_electrode_map_getter_available": False,
            "physical_hardware_state_proven": False,
            "omit_target_when_probe_state_is_not_connected": True,
            "hardware_gate_required": True,
        })

    def test_requirement_route_drift_is_rejected_at_startup(self):
        requirement = real_server.load_preset_contract(real_server.DEFAULT_PRESET_CONTRACT)
        requirement["remote_api"]["operations"]["set"]["path"] = "/api/unsafe-drift"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "preset.json"
            path.write_text(json.dumps(requirement), encoding="utf-8")
            with self.assertRaises(ValueError):
                real_server.load_preset_contract(path)


if __name__ == "__main__":
    unittest.main()
