"""Assert every ACTIVE integrated-core product version surface is literal 0.0.1.

MCP protocol 2024-11-05 is intentionally unchanged. This is the closeout pin for
the integrated core release surfaces (agent contract, API capabilities contract,
release bundle, MCP server info/constants, skill, parity, README, and active
filename references). Capability IDs/order/tools are not asserted here.

Also pins post-merge fork safety: official Artifactory deploy steps must run only
in the canonical open-ephys/plugin-GUI repository so forks always build/test but
never attempt privileged signing/notary/JFrog deployment.
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
CONTRACT_PATH = AGENT_DIR / "open_ephys_agent_contract_v1_1_0_v0_0_1.json"
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

# Official platform workflows: deploy must be canonical-repo only; build stays open.
OFFICIAL_DEPLOY_WORKFLOWS = (
    ROOT / ".github" / "workflows" / "osx.yml",
    ROOT / ".github" / "workflows" / "windows.yml",
    ROOT / ".github" / "workflows" / "linux.yml",
)
CANONICAL_DEPLOY_GUARD = "github.repository == 'open-ephys/plugin-GUI'"
STEP_START = re.compile(r"^(?P<indent>\s*)-\s+")


def _workflow_steps(workflow: Path) -> list[list[str]]:
    """Return top-level action step blocks (indent of four spaces under jobs.*.steps)."""
    lines = workflow.read_text(encoding="utf-8").splitlines()
    starts = [
        index
        for index, line in enumerate(lines)
        if (match := STEP_START.match(line)) and len(match.group("indent")) == 4
    ]
    return [
        lines[start : starts[position + 1] if position + 1 < len(starts) else len(lines)]
        for position, start in enumerate(starts)
    ]


def _step_name(step: list[str]) -> str | None:
    for line in step:
        stripped = line.strip()
        if stripped.startswith("- "):
            stripped = stripped[2:].strip()
        if stripped.startswith("name:"):
            return stripped.removeprefix("name:").strip()
    return None


def _step_conditions(step: list[str]) -> list[str]:
    conditions: list[str] = []
    for line in step:
        stripped = line.strip()
        if stripped.startswith("- "):
            stripped = stripped[2:].strip()
        if stripped.startswith("if:"):
            conditions.append(stripped.removeprefix("if:").strip())
    return conditions


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
            "agent_native/open_ephys_agent_contract_v1_1_0_v0_0_1.json",
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
        self.assertIn('open_ephys_agent_contract_v1_1_0_v0_0_1.json', source)
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

    def test_official_deploy_steps_are_gated_to_canonical_repository(self):
        """Fork CI must build/test always; official deploy only in open-ephys/plugin-GUI."""
        for workflow in OFFICIAL_DEPLOY_WORKFLOWS:
            with self.subTest(workflow=workflow.name):
                self.assertTrue(workflow.is_file(), f"missing workflow {workflow}")
                steps = _workflow_steps(workflow)

                deploy_steps = [step for step in steps if _step_name(step) == "deploy"]
                self.assertEqual(
                    len(deploy_steps),
                    1,
                    f"{workflow.name} must have exactly one step named deploy",
                )
                self.assertEqual(
                    _step_conditions(deploy_steps[0]),
                    [CANONICAL_DEPLOY_GUARD],
                    f"{workflow.name} deploy must be gated exactly to "
                    f"{CANONICAL_DEPLOY_GUARD!r}",
                )

                build_steps = [step for step in steps if _step_name(step) == "build"]
                self.assertGreaterEqual(
                    len(build_steps),
                    1,
                    f"{workflow.name} must keep at least one unconditional build step",
                )
                for build_step in build_steps:
                    self.assertEqual(
                        _step_conditions(build_step),
                        [],
                        f"{workflow.name} build step must remain unconditional",
                    )


if __name__ == "__main__":
    unittest.main()
