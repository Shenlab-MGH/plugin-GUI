"""Active public product version surfaces must equal literal 0.0.4.

Historical r0.1.2 compatibility fixtures are excluded; MCP protocol stays 2024-11-05.
"""

from __future__ import annotations

import importlib.util
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
PUBLIC_PRODUCT_VERSION = "0.0.4"
PROTOCOL_VERSION = "2024-11-05"

# Active surfaces (not historical r0.1.2 fixtures).
ACTIVE_CONTRACT = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_v0_0_4.json"
ACTIVE_PARITY = AGENT_DIR / "open_ephys_core_integration_parity_v0_0_4.json"
RELEASE_BUNDLE = AGENT_DIR / "open_ephys_agent_release_bundle.json"
MCP_SERVER = AGENT_DIR / "open_ephys_mcp_server.py"
README = AGENT_DIR / "README.md"
SKILL = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
CAPABILITY_JSON_CPP = ROOT / "Source" / "Utils" / "ControlCapabilityJson.cpp"
HTTP_SERVER_H = ROOT / "Source" / "Utils" / "OpenEphysHttpServer.h"
CAPABILITY_TESTS_CPP = ROOT / "Tests" / "API" / "ControlCapabilityTests.cpp"


def _active_contract_path() -> Path:
    return ACTIVE_CONTRACT


def _active_parity_path() -> Path:
    return ACTIVE_PARITY


def _load_mcp_module():
    spec = importlib.util.spec_from_file_location(
        "open_ephys_mcp_server_public_version", MCP_SERVER
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class PublicProductVersion004Tests(unittest.TestCase):
    def test_active_contract_pins_literal_0_0_4(self):
        path = _active_contract_path()
        self.assertTrue(path.is_file(), f"active contract missing: {path}")
        self.assertEqual(
            path.name,
            "open_ephys_agent_contract_v1_1_0_v0_0_4.json",
            "active contract file must be renamed to v0_0_4",
        )
        contract = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], PUBLIC_PRODUCT_VERSION)
        self.assertEqual(contract["contract"]["version"], PUBLIC_PRODUCT_VERSION)
        self.assertEqual(contract["bundle"]["version"], PUBLIC_PRODUCT_VERSION)
        self.assertEqual(
            contract["api"]["capabilities_contract_version"], PUBLIC_PRODUCT_VERSION
        )
        self.assertEqual(
            contract["api"]["expected_capabilities_response"]["contract_version"],
            PUBLIC_PRODUCT_VERSION,
        )
        self.assertEqual(contract["mcp"]["protocol_version"], PROTOCOL_VERSION)
        caps_tool = next(
            tool for tool in contract["tools"] if tool["name"] == "oe_get_capabilities"
        )
        self.assertIn(PUBLIC_PRODUCT_VERSION, caps_tool["description"])
        for stale in ("r0.1.3", "0.1.4", "0.1.0", "r0.1.2"):
            self.assertNotIn(stale, caps_tool["description"])

    def test_successor_release_bundle_preserves_0_0_4_compatibility(self):
        bundle = json.loads(RELEASE_BUNDLE.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], "0.0.5")
        self.assertEqual(bundle["bundle"]["version"], "0.0.5")
        self.assertEqual(bundle["contract"]["version"], "0.0.5")
        self.assertEqual(
            bundle["contract"]["fixture"],
            "agent_native/open_ephys_agent_contract_v1_1_0_v0_0_5.json",
        )
        self.assertEqual(
            bundle["components"]["mcp"]["protocol_version"], PROTOCOL_VERSION
        )

    def test_active_parity_manifest_pins_literal_0_0_4(self):
        path = _active_parity_path()
        self.assertTrue(path.is_file(), f"active parity missing: {path}")
        self.assertEqual(
            path.name,
            "open_ephys_core_integration_parity_v0_0_4.json",
            "active parity file must be renamed to v0_0_4",
        )
        parity = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(parity["schema_version"], PUBLIC_PRODUCT_VERSION)

    def test_mcp_server_constants_and_default_contract_pin_0_0_4(self):
        module = _load_mcp_module()
        self.assertEqual(module.CONTRACT_VERSION, PUBLIC_PRODUCT_VERSION)
        self.assertEqual(module.PROTOCOL_VERSION, PROTOCOL_VERSION)
        self.assertEqual(
            module.DEFAULT_CONTRACT.name,
            "open_ephys_agent_contract_v1_1_0_v0_0_4.json",
        )
        source = MCP_SERVER.read_text(encoding="utf-8")
        self.assertIn(f'CONTRACT_VERSION = "{PUBLIC_PRODUCT_VERSION}"', source)
        active = json.loads(ACTIVE_CONTRACT.read_text(encoding="utf-8"))
        self.assertEqual(
            module.load_contract(ACTIVE_CONTRACT)["api"]["capabilities_contract_version"],
            active["api"]["capabilities_contract_version"],
        )

    def test_skill_and_operator_docs_pin_0_0_4(self):
        skill = SKILL.read_text(encoding="utf-8")
        readme = README.read_text(encoding="utf-8")
        self.assertIn(f"contract: {PUBLIC_PRODUCT_VERSION}", skill)
        self.assertIn(PUBLIC_PRODUCT_VERSION, skill)
        self.assertIn(PUBLIC_PRODUCT_VERSION, readme)
        self.assertIn(f"agent contract and bundle: `{PUBLIC_PRODUCT_VERSION}`", readme)
        self.assertIn(f"API capability contract: `{PUBLIC_PRODUCT_VERSION}`", readme)
        for text in (skill, readme):
            for stale in ("r0.1.3", "0.1.4"):
                self.assertNotIn(stale, text)

    def test_api_capability_payload_and_uia_parity_docs_pin_0_0_4(self):
        cpp = CAPABILITY_JSON_CPP.read_text(encoding="utf-8")
        header = HTTP_SERVER_H.read_text(encoding="utf-8")
        tests = CAPABILITY_TESTS_CPP.read_text(encoding="utf-8")
        self.assertIn(f'result["contract_version"] = "{PUBLIC_PRODUCT_VERSION}"', cpp)
        self.assertIn(f"contract_version {PUBLIC_PRODUCT_VERSION}", header)
        self.assertIn(
            f'EXPECT_EQ (document["contract_version"], "{PUBLIC_PRODUCT_VERSION}")',
            tests,
        )
        # Portable / machine-facing metadata: no stale active product versions.
        for path in (CAPABILITY_JSON_CPP, HTTP_SERVER_H, CAPABILITY_TESTS_CPP):
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("0.1.4", text)
            self.assertNotIn("r0.1.3", text)


if __name__ == "__main__":
    unittest.main()
