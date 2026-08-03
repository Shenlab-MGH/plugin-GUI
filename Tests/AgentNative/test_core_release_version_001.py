"""Assert every ACTIVE integrated-core product version surface is literal 0.0.1.

MCP protocol 2024-11-05 is intentionally unchanged. This is the closeout pin for
the integrated core release surfaces (agent contract, API capabilities contract,
release bundle, MCP server info/constants, skill, parity, README, and active
filename references). Capability IDs/order/tools are not asserted here.
"""

from __future__ import annotations

import ast
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
PRODUCT_VERSION = "0.0.1"
MCP_PROTOCOL_VERSION = "2024-11-05"

# Active product surfaces after closeout (no r0.1.0 / 0.1.1 prerelease tags).
CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_0_0_1.json"
PARITY_PATH = AGENT_DIR / "open_ephys_core_integration_parity_0_0_1.json"
BUNDLE_PATH = AGENT_DIR / "open_ephys_agent_release_bundle.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"
README_PATH = AGENT_DIR / "README.md"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
CPP_CAPABILITY_JSON = ROOT / "Source" / "Utils" / "ControlCapabilityJson.cpp"
CPP_HTTP_SERVER_H = ROOT / "Source" / "Utils" / "OpenEphysHttpServer.h"

# Legacy prerelease names must not remain as the active surfaces.
LEGACY_CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_r0_1_0.json"
LEGACY_PARITY_PATH = AGENT_DIR / "open_ephys_core_integration_parity_r0_1_0.json"


class CoreReleaseVersion001Tests(unittest.TestCase):
    def test_active_contract_and_parity_filenames_are_0_0_1(self):
        self.assertTrue(CONTRACT_PATH.is_file(), f"missing active contract {CONTRACT_PATH.name}")
        self.assertTrue(PARITY_PATH.is_file(), f"missing active parity report {PARITY_PATH.name}")
        self.assertFalse(
            LEGACY_CONTRACT_PATH.exists(),
            "legacy r0_1_0 contract filename must not remain active",
        )
        self.assertFalse(
            LEGACY_PARITY_PATH.exists(),
            "legacy r0_1_0 parity filename must not remain active",
        )
        self.assertIn("0_0_1", CONTRACT_PATH.name)
        self.assertIn("0_0_1", PARITY_PATH.name)
        self.assertNotIn("r0_1_0", CONTRACT_PATH.name)
        self.assertNotIn("r0_1_0", PARITY_PATH.name)

    def test_agent_contract_product_versions_are_literal_0_0_1(self):
        contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], PRODUCT_VERSION)
        self.assertEqual(contract["contract"]["version"], PRODUCT_VERSION)
        self.assertEqual(contract["bundle"]["version"], PRODUCT_VERSION)
        self.assertEqual(contract["api"]["capabilities_contract_version"], PRODUCT_VERSION)
        self.assertEqual(
            contract["api"]["expected_capabilities_response"]["contract_version"],
            PRODUCT_VERSION,
        )
        self.assertEqual(contract["mcp"]["protocol_version"], MCP_PROTOCOL_VERSION)
        self.assertNotIn("r0.1.0", json.dumps(contract))
        self.assertNotIn("0.1.1", json.dumps(contract))

    def test_release_bundle_product_versions_are_literal_0_0_1(self):
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], PRODUCT_VERSION)
        self.assertEqual(bundle["bundle"]["version"], PRODUCT_VERSION)
        self.assertEqual(bundle["contract"]["version"], PRODUCT_VERSION)
        self.assertEqual(
            bundle["contract"]["fixture"],
            "agent_native/open_ephys_agent_contract_v1_1_0_0_0_1.json",
        )
        self.assertEqual(
            bundle["components"]["mcp"]["protocol_version"],
            MCP_PROTOCOL_VERSION,
        )
        self.assertNotIn("r0.1.0", json.dumps(bundle))
        self.assertNotIn("r0_1_0", json.dumps(bundle))

    def test_parity_report_schema_version_is_literal_0_0_1(self):
        report = json.loads(PARITY_PATH.read_text(encoding="utf-8"))
        self.assertEqual(report["schema_version"], PRODUCT_VERSION)
        self.assertNotIn("r0.1.0", json.dumps(report))

    def test_mcp_server_constants_and_info_are_literal_0_0_1(self):
        source = SERVER_PATH.read_text(encoding="utf-8")
        module = ast.parse(source)
        constants: dict[str, object] = {}
        for node in module.body:
            if isinstance(node, ast.Assign):
                for target in node.targets:
                    if isinstance(target, ast.Name) and isinstance(node.value, ast.Constant):
                        constants[target.id] = node.value.value
        self.assertEqual(constants.get("PROTOCOL_VERSION"), MCP_PROTOCOL_VERSION)
        self.assertEqual(constants.get("CONTRACT_VERSION"), PRODUCT_VERSION)
        self.assertIn('open_ephys_agent_contract_v1_1_0_0_0_1.json', source)
        self.assertNotIn("r0.1.0", source)
        self.assertNotIn("0.1.1", source)
        self.assertNotIn("r0_1_0", source)
        # serverInfo.version is sourced from CONTRACT_VERSION
        self.assertIn('"version":CONTRACT_VERSION', source.replace(" ", ""))

    def test_skill_and_readme_pin_literal_0_0_1(self):
        skill = SKILL_PATH.read_text(encoding="utf-8")
        readme = README_PATH.read_text(encoding="utf-8")
        self.assertIn(f"contract: {PRODUCT_VERSION}", skill)
        self.assertIn(MCP_PROTOCOL_VERSION, skill)
        self.assertNotIn("r0.1.0", skill)
        self.assertNotIn("0.1.1", skill)

        self.assertIn(f"`{PRODUCT_VERSION}`", readme)
        self.assertRegex(
            readme,
            re.compile(rf"agent contract and bundle:\s*`{re.escape(PRODUCT_VERSION)}`"),
        )
        self.assertRegex(
            readme,
            re.compile(rf"API capability contract:\s*`{re.escape(PRODUCT_VERSION)}`"),
        )
        self.assertIn(MCP_PROTOCOL_VERSION, readme)
        self.assertNotIn("r0.1.0", readme)
        self.assertNotIn("0.1.1", readme)

    def test_cpp_api_capabilities_contract_version_is_literal_0_0_1(self):
        cpp = CPP_CAPABILITY_JSON.read_text(encoding="utf-8")
        header = CPP_HTTP_SERVER_H.read_text(encoding="utf-8")
        self.assertIn(f'result["contract_version"] = "{PRODUCT_VERSION}"', cpp)
        self.assertNotIn('"0.1.1"', cpp)
        self.assertIn(f"contract_version {PRODUCT_VERSION}", header)
        self.assertNotIn("0.1.1", header)


if __name__ == "__main__":
    unittest.main()
