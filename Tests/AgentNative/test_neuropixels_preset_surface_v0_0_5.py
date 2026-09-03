"""The 0.0.5 preset slice must extend, never replace, the 0.0.4 surface."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "agent_native" / "open_ephys_agent_contract_v1_1_0_v0_0_4.json"
TARGET = ROOT / "agent_native" / "open_ephys_agent_contract_v1_1_0_v0_0_5.json"


class PresetSurfaceTests(unittest.TestCase):
    def test_exactly_extends_twelve_capabilities_and_fourteen_tools(self):
        base = json.loads(BASE.read_text(encoding="utf-8"))
        target = json.loads(TARGET.read_text(encoding="utf-8"))

        base_capabilities = {
            item["id"] for item in base["api"]["expected_capabilities_response"]["capabilities"]
        }
        target_capabilities = {
            item["id"] for item in target["api"]["expected_capabilities_response"]["capabilities"]
        }
        base_tools = {item["name"] for item in base["tools"]}
        target_tools = {item["name"] for item in target["tools"]}

        self.assertEqual(len(base_capabilities), 12)
        self.assertEqual(len(base_tools), 14)
        self.assertEqual(len(target_capabilities), 13)
        self.assertEqual(len(target_tools), 16)
        self.assertEqual(
            target_capabilities - base_capabilities,
            {"oe.control.neuropixels.preset"},
        )
        self.assertEqual(
            target_tools - base_tools,
            {"oe_get_electrode_presets", "oe_set_electrode_preset"},
        )
        self.assertTrue(base_capabilities <= target_capabilities)
        self.assertTrue(base_tools <= target_tools)


if __name__ == "__main__":
    unittest.main()
