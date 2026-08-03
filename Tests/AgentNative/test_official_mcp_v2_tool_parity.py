"""Gate: official MCP v2 interop EXPECTED must match the live 0.0.1 tool surface.

Fails when EXPECTED drifts (e.g. stale 6-tool list) relative to the product
contract/MCP server, and when verification claims local interop "passed"
without a runnable gate.
"""

from __future__ import annotations

import ast
import importlib.util
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGENT_DIR = ROOT / "agent_native"
CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_v0_0_1.json"
SERVER_PATH = AGENT_DIR / "open_ephys_mcp_server.py"
INTEROP_PATH = ROOT / "Tests" / "AgentNative" / "official_mcp_v2_interop.py"
PARITY_PATH = AGENT_DIR / "open_ephys_core_integration_parity_0_0_1.json"
BUNDLE_PATH = AGENT_DIR / "open_ephys_agent_release_bundle.json"

FULL_CORE_TOOL_COUNT = 10
FULL_CORE_TOOL_NAMES = [
    "oe_get_capabilities",
    "oe_get_status",
    "oe_set_status",
    "oe_get_recording_options",
    "oe_set_recording_options",
    "oe_get_recording_filename",
    "oe_set_recording_filename",
    "oe_get_cpu",
    "oe_get_disk",
    "oe_get_time",
]


def _load_interop_expected() -> list[str]:
    source = INTEROP_PATH.read_text(encoding="utf-8")
    module = ast.parse(source)
    for node in module.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == "EXPECTED":
                    return ast.literal_eval(node.value)
    raise AssertionError("official_mcp_v2_interop.py must define EXPECTED")


def _mcp_package_importable() -> bool:
    return importlib.util.find_spec("mcp") is not None


class OfficialMcpV2ToolParityTests(unittest.TestCase):
    def test_expected_matches_exact_ten_core_tools_from_contract_and_server(self) -> None:
        contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        contract_tools = [tool["name"] for tool in contract["tools"]]
        server_source = SERVER_PATH.read_text(encoding="utf-8")
        expected = _load_interop_expected()

        self.assertEqual(len(contract_tools), FULL_CORE_TOOL_COUNT)
        self.assertEqual(contract_tools, FULL_CORE_TOOL_NAMES)
        self.assertEqual(
            expected,
            FULL_CORE_TOOL_NAMES,
            f"official_mcp_v2_interop EXPECTED must list exactly {FULL_CORE_TOOL_COUNT} "
            f"core tools in contract order; got {expected!r}",
        )
        self.assertEqual(len(expected), FULL_CORE_TOOL_COUNT)
        for name in FULL_CORE_TOOL_NAMES:
            self.assertIn(f'"{name}"', server_source)

    def test_local_interop_must_not_claim_passed_when_unrunnable(self) -> None:
        """No unearned 'passed' for official MCP v2 local interop."""
        report = json.loads(PARITY_PATH.read_text(encoding="utf-8"))
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        local_status = report["verification"]["official_mcp_v2_local_interop"]
        mcp_importable = _mcp_package_importable()

        if not mcp_importable:
            self.assertNotEqual(
                local_status,
                "passed",
                "official_mcp_v2_local_interop must not claim passed when mcp is not importable",
            )
            self.assertIn(
                local_status,
                {"pending", "blocked", "not_run"},
                f"unrunnable interop must stay pending/blocked/not_run, got {local_status!r}",
            )

        # Bundle must not advertise a successful auto-fallback without a runnable gate.
        if not mcp_importable:
            self.assertFalse(
                bundle["verification"].get("official_mcp_v2_auto_fallback"),
                "release bundle must not claim auto-fallback success while interop is unrunnable",
            )


if __name__ == "__main__":
    unittest.main()
