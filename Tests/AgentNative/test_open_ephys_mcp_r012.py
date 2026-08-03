"""Historical r0.1.2 contract fixtures are archival only.

The active 0.0.4 MCP server must reject r0.1.2 (and other mismatched contract
versions). These tests preserve fixture meaning and prove rejection; they must
not require the active server to execute an old contract.
"""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
HISTORICAL_CONTRACT = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_r0_1_2.json"
ACTIVE_CONTRACT = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_v0_0_4.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"

HISTORICAL_TOOL_NAMES = [
    "oe_get_capabilities", "oe_get_status", "oe_set_status",
    "oe_get_recording_options", "oe_set_recording_options",
    "oe_get_recording_filename", "oe_set_recording_filename",
    "oe_get_recording_directory", "oe_set_recording_directory",
    "oe_get_config",
    "oe_get_cpu", "oe_get_disk", "oe_get_time",
]
HISTORICAL_CAPABILITY_IDS = [
    "oe.control.acquisition", "oe.control.recording", "oe.control.recording.options",
    "oe.control.recording.filename", "oe.control.recording.directory",
    "oe.control.recording.new_directory",
    "oe.control.recording.force_new_directory",
    "oe.control.signal_chain.configuration", "oe.status.cpu_usage",
    "oe.status.disk_usage", "oe.status.elapsed_time",
]


def load_server_module():
    spec = importlib.util.spec_from_file_location("open_ephys_mcp_server_r012_archive", SERVER_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class HistoricalR012FixtureTests(unittest.TestCase):
    """Archive/schema checks only — no active-server execution of r0.1.2."""

    def test_historical_r012_fixture_pins_are_preserved(self):
        self.assertTrue(HISTORICAL_CONTRACT.is_file())
        contract = json.loads(HISTORICAL_CONTRACT.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], "r0.1.2")
        self.assertEqual(contract["contract"]["version"], "r0.1.2")
        self.assertEqual(contract["bundle"]["version"], "r0.1.2")
        self.assertEqual(
            contract["baseline"],
            {
                "upstream": "open-ephys/plugin-GUI",
                "version": "1.0.2",
                "commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f",
            },
        )
        self.assertEqual(
            contract["mcp"],
            {
                "protocol_version": "2024-11-05",
                "modern_protocol_supported": False,
                "server_name": "open-ephys-agent-native",
            },
        )
        self.assertEqual([tool["name"] for tool in contract["tools"]], HISTORICAL_TOOL_NAMES)
        capabilities_tool = next(
            tool for tool in contract["tools"] if tool["name"] == "oe_get_capabilities"
        )
        self.assertEqual(
            capabilities_tool["description"],
            "Read the pinned Core R0.1.2 capabilities.",
        )
        self.assertEqual(
            [item["id"] for item in contract["api"]["expected_capabilities_response"]["capabilities"]],
            HISTORICAL_CAPABILITY_IDS,
        )
        self.assertEqual(contract["api"]["capabilities_contract_version"], "0.1.3")
        self.assertEqual(
            contract["api"]["expected_capabilities_response"]["contract_version"],
            "0.1.3",
        )
        self.assertFalse(contract["verification"]["hardware_verified"])
        self.assertFalse(contract["verification"]["scientific_verified"])
        # Historical surface predates processor inventory.
        tool_names = [tool["name"] for tool in contract["tools"]]
        self.assertNotIn("oe_get_processors", tool_names)
        for tool in contract["tools"]:
            self.assertFalse(tool["inputSchema"].get("additionalProperties", True), tool["name"])

    def test_active_skill_is_not_pinned_to_historical_r012(self):
        skill = SKILL_PATH.read_text(encoding="utf-8")
        self.assertIn("0.0.4", skill)
        self.assertNotIn("r0.1.2", skill)
        self.assertNotIn("r0.1.3", skill)
        self.assertNotIn("0.1.4", skill)


class ActiveServerRejectsHistoricalVersionsTests(unittest.TestCase):
    """Release gate: active 0.0.4 load_contract rejects mismatched versions."""

    @classmethod
    def setUpClass(cls):
        cls.module = load_server_module()

    def test_active_server_constants_are_strict_0_0_4(self):
        self.assertEqual(self.module.CONTRACT_VERSION, "0.0.4")
        self.assertEqual(self.module.PROTOCOL_VERSION, "2024-11-05")
        self.assertEqual(
            self.module.DEFAULT_CONTRACT.name,
            "open_ephys_agent_contract_v1_0_2_v0_0_4.json",
        )
        self.assertTrue(ACTIVE_CONTRACT.is_file())

    def test_load_contract_accepts_active_0_0_4(self):
        contract = self.module.load_contract(ACTIVE_CONTRACT)
        self.assertEqual(contract["schema_version"], "0.0.4")
        self.assertEqual(contract["contract"]["version"], "0.0.4")

    def test_load_contract_rejects_historical_r0_1_2_fixture(self):
        with self.assertRaises(ValueError) as raised:
            self.module.load_contract(HISTORICAL_CONTRACT)
        message = str(raised.exception)
        self.assertIn("Contract pins do not match", message)

    def test_load_contract_rejects_other_archived_r0_1_x_fixtures(self):
        for name in (
            "open_ephys_agent_contract_v1_0_2_r0_1_0.json",
            "open_ephys_agent_contract_v1_0_2_r0_1_1.json",
        ):
            path = AGENT_DIR / name
            self.assertTrue(path.is_file(), path)
            with self.assertRaises(ValueError, msg=f"expected reject for {name}"):
                self.module.load_contract(path)

    def test_load_contract_rejects_r0_1_3_and_api_0_1_x_mutations(self):
        active = json.loads(ACTIVE_CONTRACT.read_text(encoding="utf-8"))
        cases = [
            ("schema_version", "r0.1.3"),
            ("schema_version", "r0.1.2"),
            ("contract_version", "r0.1.3"),
            ("bundle_version", "r0.1.2"),
            ("api_capabilities_contract_version", "0.1.4"),
            ("expected_capabilities_contract_version", "0.1.3"),
            ("expected_capabilities_contract_version", "0.1.4"),
        ]
        for field, stale in cases:
            mutated = deepcopy(active)
            if field == "schema_version":
                mutated["schema_version"] = stale
            elif field == "contract_version":
                mutated["contract"]["version"] = stale
            elif field == "bundle_version":
                mutated["bundle"]["version"] = stale
            elif field == "api_capabilities_contract_version":
                mutated["api"]["capabilities_contract_version"] = stale
            else:
                mutated["api"]["expected_capabilities_response"]["contract_version"] = stale
            with tempfile.NamedTemporaryFile(
                "w",
                encoding="utf-8",
                suffix=".json",
                delete=False,
            ) as handle:
                json.dump(mutated, handle)
                temp_path = Path(handle.name)
            try:
                with self.assertRaises(
                    ValueError,
                    msg=f"expected reject for {field}={stale}",
                ):
                    self.module.load_contract(temp_path)
            finally:
                temp_path.unlink(missing_ok=True)

    def test_mcp_server_constructor_rejects_historical_r0_1_2(self):
        with self.assertRaises(ValueError):
            self.module.McpServer(HISTORICAL_CONTRACT, "http://127.0.0.1:37497")


if __name__ == "__main__":
    unittest.main()
