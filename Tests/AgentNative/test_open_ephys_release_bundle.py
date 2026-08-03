import json
import re
import subprocess
import unittest
from pathlib import Path, PurePosixPath, PureWindowsPath

ROOT = Path(__file__).resolve().parents[2]
BUNDLE_PATH = ROOT / "agent_native" / "open_ephys_agent_release_bundle.json"
CONTRACT_PATH = ROOT / "agent_native" / "open_ephys_agent_contract_v1_0_2_v0_0_1.json"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "tests.yml"
README_PATH = ROOT / "agent_native" / "README.md"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
HTTP_SERVER_PATH = ROOT / "Source" / "Utils" / "OpenEphysHttpServer.h"


def artifact_path(root, value):
    posix, windows = PurePosixPath(value), PureWindowsPath(value)
    if not isinstance(value, str) or not value or "\\" in value or posix.is_absolute() or windows.is_absolute() or windows.drive or ".." in posix.parts:
        raise ValueError("artifact must be a repository-relative POSIX path")
    result = (root / Path(*posix.parts)).resolve()
    if not result.is_relative_to(root.resolve()): raise ValueError("artifact escapes repository")
    return result


PRODUCT_VERSION = "0.0.1"
PROTOCOL_VERSION = "2024-11-05"
ACTIVE_CONTRACT_NAME = "open_ephys_agent_contract_v1_0_2_v0_0_1.json"
ACTIVE_CONTRACT_PATH = ROOT / "agent_native" / ACTIVE_CONTRACT_NAME
SERVER_PATH = ROOT / "agent_native" / "open_ephys_mcp_server.py"
CAPABILITY_SOURCE = ROOT / "Source" / "Utils" / "ControlCapabilityJson.cpp"


class ReleaseBundleTests(unittest.TestCase):
    def test_active_core_product_version_surfaces_are_0_0_1(self):
        """Every active core product version surface must be literal 0.0.1.

        MCP protocol 2024-11-05 is a protocol pin, not a product version, and must stay unchanged.
        """
        import importlib.util

        self.assertTrue(ACTIVE_CONTRACT_PATH.is_file(), f"missing active contract file {ACTIVE_CONTRACT_NAME}")
        self.assertFalse((ROOT / "agent_native" / "open_ephys_agent_contract_v1_0_2_r0_1_0.json").exists(),
                         "historical r0.1.0 contract filename must not remain the active artifact")

        contract = json.loads(ACTIVE_CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(contract["schema_version"], PRODUCT_VERSION)
        self.assertEqual(contract["contract"]["version"], PRODUCT_VERSION)
        self.assertEqual(contract["bundle"]["version"], PRODUCT_VERSION)
        self.assertEqual(contract["api"]["capabilities_contract_version"], PRODUCT_VERSION)
        self.assertEqual(contract["api"]["expected_capabilities_response"]["contract_version"], PRODUCT_VERSION)
        self.assertEqual(contract["mcp"]["protocol_version"], PROTOCOL_VERSION)

        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], PRODUCT_VERSION)
        self.assertEqual(bundle["bundle"]["version"], PRODUCT_VERSION)
        self.assertEqual(bundle["contract"]["version"], PRODUCT_VERSION)
        self.assertEqual(bundle["contract"]["artifact"], f"agent_native/{ACTIVE_CONTRACT_NAME}")
        self.assertEqual(bundle["mcp"]["protocol_version"], PROTOCOL_VERSION)

        server_source = SERVER_PATH.read_text(encoding="utf-8")
        self.assertIn(f'CONTRACT_VERSION = "{PRODUCT_VERSION}"', server_source)
        self.assertIn(f'PROTOCOL_VERSION = "{PROTOCOL_VERSION}"', server_source)
        self.assertIn(ACTIVE_CONTRACT_NAME, server_source)
        self.assertNotIn("open_ephys_agent_contract_v1_0_2_r0_1_0.json", server_source)

        spec = importlib.util.spec_from_file_location("open_ephys_mcp_server_version_gate", SERVER_PATH)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        self.assertEqual(module.CONTRACT_VERSION, PRODUCT_VERSION)
        self.assertEqual(module.PROTOCOL_VERSION, PROTOCOL_VERSION)
        self.assertEqual(module.DEFAULT_CONTRACT.name, ACTIVE_CONTRACT_NAME)
        server = module.McpServer(ACTIVE_CONTRACT_PATH, "http://127.0.0.1:37497")
        init = server.handle({
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {"name": "version-gate", "version": "1"},
            },
        })
        self.assertEqual(init["result"]["protocolVersion"], PROTOCOL_VERSION)
        self.assertEqual(init["result"]["serverInfo"]["version"], PRODUCT_VERSION)

        capability_source = CAPABILITY_SOURCE.read_text(encoding="utf-8")
        self.assertIn(f'result["contract_version"] = "{PRODUCT_VERSION}"', capability_source)
        self.assertNotIn('result["contract_version"] = "0.1.1"', capability_source)

        for document_path in (README_PATH, SKILL_PATH):
            text = document_path.read_text(encoding="utf-8")
            with self.subTest(document=str(document_path.relative_to(ROOT))):
                self.assertIn(PRODUCT_VERSION, text)
                self.assertIn(PROTOCOL_VERSION, text)
                self.assertNotIn("r0.1.0", text)
                self.assertNotIn("0.1.1", text)

    def test_release_bundle_pins_contract_provenance_and_nonclaims(self):
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], "0.0.1")
        self.assertEqual(bundle["bundle"], {"id": "open-ephys-agent-native", "version": "0.0.1", "platform": "windows", "coverage": "narrow-core-r0"})
        self.assertEqual(bundle["official_upstream"], {"repository": "open-ephys/plugin-GUI", "tag": "v1.0.2", "commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f", "gui_version": "1.0.2"})
        self.assertEqual(bundle["contract"]["artifact"], "agent_native/open_ephys_agent_contract_v1_0_2_v0_0_1.json")
        self.assertEqual(bundle["contract"]["version"], contract["contract"]["version"])
        self.assertEqual(bundle["verification"], {"level": "offline-contract-and-ci", "hardware_verified": False, "scientific_verified": False})
        self.assertFalse(bundle["mcp"]["modern_protocol_supported"])
        self.assertEqual(bundle["limitations"], {"mcp_loopback_scope": "outbound-client-only", "raw_api_bind_address": "0.0.0.0", "raw_api_can_bypass_mcp_recording_approval": True, "deployment_requirement": "trusted-network-or-firewall"})
        for component in bundle["components"].values(): self.assertTrue(artifact_path(ROOT, component["artifact"]).is_file())

    def test_provenance_is_ancestral_and_workflow_runs_r0_tests(self):
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        tagged = subprocess.run(["git", "rev-parse", "--verify", "refs/tags/v1.0.2^{commit}"], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()
        self.assertEqual(tagged, bundle["official_upstream"]["commit"])
        self.assertEqual(subprocess.run(["git", "merge-base", "--is-ancestor", tagged, "HEAD"], cwd=ROOT).returncode, 0)
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("Tests.AgentNative.test_open_ephys_mcp_r010", workflow)
        self.assertIn("Tests.AgentNative.test_open_ephys_release_bundle", workflow)
        self.assertRegex(workflow, r"fetch-depth:\s*0")
        self.assertIn("agent-native-v102-record-safety", workflow)

    def test_artifact_path_rejects_escape_forms(self):
        for value in ("../outside", "/absolute", "C:/absolute", "folder\\file", "nested/../../outside"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError): artifact_path(ROOT, value)

    def test_readme_pins_the_narrow_surface_and_nonclaims(self):
        readme = README_PATH.read_text(encoding="utf-8")
        for required in ("v1.0.2", "0.0.1", "2024-11-05", "exactly ten", "hardware_verified:false", "scientific_verified:false"):
            self.assertIn(required, readme)

    def test_exposure_boundary_is_explicit_in_source_and_operator_docs(self):
        source = HTTP_SERVER_PATH.read_text(encoding="utf-8")
        self.assertIn('svr_->listen ("0.0.0.0", PORT)', source)
        for document in (README_PATH.read_text(encoding="utf-8"), SKILL_PATH.read_text(encoding="utf-8")):
            with self.subTest(document=document[:40]):
                normalized = " ".join(document.split())
                for required in ("MCP bridge outbound client", "0.0.0.0", "raw Open Ephys API", "bypass MCP RECORD approval", "trusted network or firewall", "no listener or C++ change"):
                    self.assertIn(required, normalized)


if __name__ == "__main__": unittest.main()
