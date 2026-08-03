"""Active public product version surfaces must equal literal 0.0.4.

Historical r0.1.2 fixtures remain archival only. The active 0.0.4 MCP
load_contract must reject r0.1.2, r0.1.3, API 0.1.x, and every mismatched
version. MCP protocol stays 2024-11-05.
"""

from __future__ import annotations

import importlib.util
import json
import re
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
PUBLIC_PRODUCT_VERSION = "0.0.4"
PROTOCOL_VERSION = "2024-11-05"

# Active surfaces (not historical r0.1.2 fixtures).
ACTIVE_CONTRACT = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_v0_0_4.json"
RELEASE_BUNDLE = AGENT_DIR / "open_ephys_agent_release_bundle.json"
MCP_SERVER = AGENT_DIR / "open_ephys_mcp_server.py"
README = AGENT_DIR / "README.md"
SKILL = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
CAPABILITY_JSON_CPP = ROOT / "Source" / "Utils" / "ControlCapabilityJson.cpp"
CAPABILITY_TESTS_CPP = ROOT / "Tests" / "API" / "ControlCapabilityTests.cpp"
WORKFLOW = ROOT / ".github" / "workflows" / "tests.yml"
HISTORICAL_R012 = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_r0_1_2.json"
HISTORICAL_R011 = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_r0_1_1.json"
HISTORICAL_R010 = AGENT_DIR / "open_ephys_agent_contract_v1_0_2_r0_1_0.json"


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
        self.assertTrue(ACTIVE_CONTRACT.is_file(), f"active contract missing: {ACTIVE_CONTRACT}")
        self.assertEqual(
            ACTIVE_CONTRACT.name,
            "open_ephys_agent_contract_v1_0_2_v0_0_4.json",
            "active contract file must be renamed to v0_0_4",
        )
        # Pre-rename r0.1.3 path must not remain as an active alias.
        self.assertFalse(
            (AGENT_DIR / "open_ephys_agent_contract_v1_0_2_r0_1_3.json").exists()
        )
        contract = json.loads(ACTIVE_CONTRACT.read_text(encoding="utf-8"))
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
        contract_text = json.dumps(contract)
        for stale in ("r0.1.3", "0.1.4", "r0.1.2", "0.1.3", "0.1.2", "0.1.1"):
            self.assertNotIn(stale, caps_tool["description"])
            self.assertNotIn(stale, contract_text)

    def test_active_release_bundle_pins_literal_0_0_4(self):
        bundle = json.loads(RELEASE_BUNDLE.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], PUBLIC_PRODUCT_VERSION)
        self.assertEqual(bundle["bundle"]["version"], PUBLIC_PRODUCT_VERSION)
        self.assertEqual(bundle["contract"]["version"], PUBLIC_PRODUCT_VERSION)
        self.assertEqual(
            bundle["contract"]["artifact"],
            "agent_native/open_ephys_agent_contract_v1_0_2_v0_0_4.json",
        )
        self.assertEqual(bundle["mcp"]["protocol_version"], PROTOCOL_VERSION)

    def test_mcp_server_constants_and_default_contract_pin_0_0_4(self):
        module = _load_mcp_module()
        self.assertEqual(module.CONTRACT_VERSION, PUBLIC_PRODUCT_VERSION)
        self.assertEqual(module.PROTOCOL_VERSION, PROTOCOL_VERSION)
        self.assertEqual(
            module.DEFAULT_CONTRACT.name,
            "open_ephys_agent_contract_v1_0_2_v0_0_4.json",
        )
        source = MCP_SERVER.read_text(encoding="utf-8")
        self.assertIn(f'CONTRACT_VERSION = "{PUBLIC_PRODUCT_VERSION}"', source)
        self.assertIn(
            f'capabilities.get("contract_version") != "{PUBLIC_PRODUCT_VERSION}"',
            source,
        )
        # Strict active server: no historical dual-version load path.
        self.assertNotIn('if version == "r0.1.2"', source)
        self.assertNotIn('version not in {"r0.1.2"', source)
        for stale in ("r0.1.3", "0.1.4", "r0.1.2", "0.1.3"):
            self.assertNotIn(stale, source)

    def test_skill_and_operator_docs_pin_0_0_4(self):
        skill = SKILL.read_text(encoding="utf-8")
        readme = README.read_text(encoding="utf-8")
        self.assertIn(f"contract: `{PUBLIC_PRODUCT_VERSION}`", skill)
        self.assertIn(PUBLIC_PRODUCT_VERSION, skill)
        self.assertIn(PUBLIC_PRODUCT_VERSION, readme)
        self.assertIn(
            f"agent contract and bundle: `{PUBLIC_PRODUCT_VERSION}`", readme
        )
        self.assertIn(
            f"API capability contract: `{PUBLIC_PRODUCT_VERSION}`", readme
        )
        for text in (skill, readme):
            for stale in ("r0.1.3", "0.1.4", "r0.1.2"):
                self.assertNotIn(stale, text)

    def test_api_capability_payload_pins_0_0_4(self):
        cpp = CAPABILITY_JSON_CPP.read_text(encoding="utf-8")
        tests = CAPABILITY_TESTS_CPP.read_text(encoding="utf-8")
        self.assertIn(
            f'result["contract_version"] = "{PUBLIC_PRODUCT_VERSION}"', cpp
        )
        self.assertIn(
            f'EXPECT_EQ (document["contract_version"], "{PUBLIC_PRODUCT_VERSION}")',
            tests,
        )
        for path in (CAPABILITY_JSON_CPP, CAPABILITY_TESTS_CPP):
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("0.1.4", text)
            self.assertNotIn("r0.1.3", text)

    def test_ci_and_docs_reference_active_0_0_4_modules(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        readme = README.read_text(encoding="utf-8")
        for text in (workflow, readme):
            self.assertIn("Tests.AgentNative.test_open_ephys_mcp_v0_0_4", text)
            self.assertIn("Tests.AgentNative.test_open_ephys_release_bundle", text)
            self.assertIn("Tests.AgentNative.test_public_product_version_0_0_4", text)
            self.assertNotIn("test_open_ephys_mcp_r013", text)

    def test_historical_r012_fixture_meaning_is_preserved(self):
        self.assertTrue(HISTORICAL_R012.is_file())
        contract = json.loads(HISTORICAL_R012.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], "r0.1.2")
        self.assertEqual(contract["contract"]["version"], "r0.1.2")
        self.assertEqual(
            contract["api"]["capabilities_contract_version"], "0.1.3"
        )
        self.assertEqual(
            contract["api"]["expected_capabilities_response"]["contract_version"],
            "0.1.3",
        )
        self.assertEqual(contract["mcp"]["protocol_version"], PROTOCOL_VERSION)

    def test_active_load_contract_rejects_historical_and_stale_versions(self):
        module = _load_mcp_module()
        for path in (HISTORICAL_R012, HISTORICAL_R011, HISTORICAL_R010):
            self.assertTrue(path.is_file(), path)
            with self.assertRaises(ValueError, msg=f"expected reject for {path.name}"):
                module.load_contract(path)

        active = json.loads(ACTIVE_CONTRACT.read_text(encoding="utf-8"))
        mutations = [
            ("schema_version", "r0.1.2"),
            ("schema_version", "r0.1.3"),
            ("contract.version", "r0.1.3"),
            ("bundle.version", "r0.1.2"),
            ("api.capabilities_contract_version", "0.1.4"),
            ("api.expected_capabilities_response.contract_version", "0.1.3"),
            ("api.expected_capabilities_response.contract_version", "0.1.4"),
        ]
        for field, stale in mutations:
            mutated = deepcopy(active)
            if field == "schema_version":
                mutated["schema_version"] = stale
            elif field == "contract.version":
                mutated["contract"]["version"] = stale
            elif field == "bundle.version":
                mutated["bundle"]["version"] = stale
            elif field == "api.capabilities_contract_version":
                mutated["api"]["capabilities_contract_version"] = stale
            else:
                mutated["api"]["expected_capabilities_response"]["contract_version"] = stale
            with tempfile.NamedTemporaryFile(
                "w", encoding="utf-8", suffix=".json", delete=False
            ) as handle:
                json.dump(mutated, handle)
                temp_path = Path(handle.name)
            try:
                with self.assertRaises(
                    ValueError, msg=f"expected reject for {field}={stale}"
                ):
                    module.load_contract(temp_path)
            finally:
                temp_path.unlink(missing_ok=True)

        # Positive control: active contract still loads.
        loaded = module.load_contract(ACTIVE_CONTRACT)
        self.assertEqual(loaded["contract"]["version"], PUBLIC_PRODUCT_VERSION)

    def test_active_surfaces_reject_stale_version_literals_via_negative_scan(self):
        # Non-tautological: scan source text for stale active product pins.
        active_paths = [
            ACTIVE_CONTRACT,
            RELEASE_BUNDLE,
            MCP_SERVER,
            README,
            SKILL,
            CAPABILITY_JSON_CPP,
            CAPABILITY_TESTS_CPP,
            WORKFLOW,
        ]
        stale_patterns = (
            re.compile(r"\br0\.1\.3\b"),
            re.compile(r"\b0\.1\.4\b"),
            re.compile(r"\br0_1_3\b"),
            re.compile(r"\bmcp_r013\b"),
        )
        for path in active_paths:
            self.assertTrue(path.is_file(), f"missing active surface: {path}")
            text = path.read_text(encoding="utf-8")
            for pattern in stale_patterns:
                match = pattern.search(text)
                self.assertIsNone(
                    match,
                    f"{path} still contains stale active version {pattern.pattern}",
                )


if __name__ == "__main__":
    unittest.main()
