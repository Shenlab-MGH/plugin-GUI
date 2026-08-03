import json
import re
import subprocess
import unittest
from pathlib import Path, PurePosixPath, PureWindowsPath

ROOT = Path(__file__).resolve().parents[2]
BUNDLE_PATH = ROOT / "agent_native" / "open_ephys_agent_release_bundle.json"
CONTRACT_PATH = ROOT / "agent_native" / "open_ephys_agent_contract_v1_0_2_r0_1_3.json"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "tests.yml"
README_PATH = ROOT / "agent_native" / "README.md"
SKILL_PATH = ROOT / "skills" / "open-ephys-agent-native" / "SKILL.md"
HTTP_SERVER_PATH = ROOT / "Source" / "Utils" / "OpenEphysHttpServer.h"
WINDOWS_UIA_CMAKE_PATH = ROOT / "Tests" / "WindowsUIAutomation" / "CMakeLists.txt"


def artifact_path(root, value):
    posix, windows = PurePosixPath(value), PureWindowsPath(value)
    if not isinstance(value, str) or not value or "\\" in value or posix.is_absolute() or windows.is_absolute() or windows.drive or ".." in posix.parts:
        raise ValueError("artifact must be a repository-relative POSIX path")
    result = (root / Path(*posix.parts)).resolve()
    if not result.is_relative_to(root.resolve()): raise ValueError("artifact escapes repository")
    return result


class ReleaseBundleTests(unittest.TestCase):
    def test_release_bundle_pins_contract_provenance_and_nonclaims(self):
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(bundle["format_version"], "r0.1.3")
        self.assertEqual(bundle["bundle"], {"id": "open-ephys-agent-native", "version": "r0.1.3", "platform": "windows", "coverage": "narrow-core-r0-processor-inventory"})
        self.assertEqual(bundle["official_upstream"], {"repository": "open-ephys/plugin-GUI", "tag": "v1.0.2", "commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f", "gui_version": "1.0.2"})
        self.assertEqual(bundle["development_base"], {"branch": "agent-native-v102-config-read-r012", "commit": "2b6c8bfe79ccab62f9b9a200533273241e330a01"})
        self.assertEqual(bundle["contract"]["artifact"], "agent_native/open_ephys_agent_contract_v1_0_2_r0_1_3.json")
        self.assertEqual(bundle["contract"]["version"], contract["contract"]["version"])
        self.assertEqual(bundle["verification"], {"level": "offline-contract-and-local-windows-uia", "windows_uia_external_smoke_verified": True, "hosted_ci_verified": False, "hardware_verified": False, "scientific_verified": False})
        self.assertFalse(bundle["mcp"]["modern_protocol_supported"])
        self.assertEqual(bundle["limitations"], {"mcp_loopback_scope": "outbound-client-only", "raw_api_bind_address": "0.0.0.0", "raw_api_can_bypass_mcp_recording_approval": True, "deployment_requirement": "trusted-network-or-firewall"})
        for component in bundle["components"].values(): self.assertTrue(artifact_path(ROOT, component["artifact"]).is_file())

    def test_provenance_is_ancestral_and_workflow_runs_r0_tests(self):
        bundle = json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))
        tagged = subprocess.run(["git", "rev-parse", "--verify", "refs/tags/v1.0.2^{commit}"], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()
        self.assertEqual(tagged, bundle["official_upstream"]["commit"])
        self.assertEqual(subprocess.run(["git", "merge-base", "--is-ancestor", tagged, "HEAD"], cwd=ROOT).returncode, 0)
        immediate_base = bundle["development_base"]["commit"]
        self.assertEqual(subprocess.run(["git", "merge-base", "--is-ancestor", immediate_base, "HEAD"], cwd=ROOT).returncode, 0)
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("Tests.AgentNative.test_open_ephys_mcp_r013", workflow)
        self.assertIn("Tests.AgentNative.test_open_ephys_release_bundle", workflow)
        self.assertRegex(workflow, r"fetch-depth:\s*0")
        self.assertIn("agent-native-v102-record-safety", workflow)
        self.assertIn("agent-native-v102-core-r0-mcp-r010", workflow)
        self.assertIn("- 'agent-native-v102-recording-directory-r011'", workflow)
        self.assertIn("- 'agent-native-v102-config-read-r012'", workflow)
        self.assertIn('ctest --test-dir Build -C Release --output-on-failure --no-tests=error -R "^(API_tests|UI_tests|WindowsUIAutomation_tests)$"', workflow)
        self.assertIn("set_tests_properties(WindowsUIAutomation_tests PROPERTIES TIMEOUT 30)", WINDOWS_UIA_CMAKE_PATH.read_text(encoding="utf-8"))

    def test_windows_e2e_failure_logs_are_preserved_and_uploaded(self):
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for required in (
            "-RedirectStandardOutput $pythonStdout",
            "-RedirectStandardError $pythonStderr",
            "-RedirectStandardOutput $oeStdout",
            "-RedirectStandardError $oeStderr",
            "if ($pythonExitCode -ne 0)",
            "exit $pythonExitCode",
            "python_stdout.log",
            "python_stderr.log",
            "python_output.log",
            "open_ephys_stdout.log",
            "open_ephys_stderr.log",
            "if-no-files-found: warn",
        ):
            with self.subTest(required=required):
                self.assertIn(required, workflow)

        self.assertRegex(workflow, r"(?s)- name: Set timestamp\n\s+if: always\(\)")
        self.assertRegex(workflow, r"(?s)- name: Upload test results\n\s+if: always\(\)")
        self.assertNotIn('Write-Error "Open Ephys test suite exited', workflow)

    def test_artifact_path_rejects_escape_forms(self):
        for value in ("../outside", "/absolute", "C:/absolute", "folder\\file", "nested/../../outside"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError): artifact_path(ROOT, value)

    def test_readme_pins_the_narrow_surface_and_nonclaims(self):
        readme = README_PATH.read_text(encoding="utf-8")
        for required in ("v1.0.2", "r0.1.3", "2024-11-05", "exactly fourteen", "oe_get_config", "oe_get_processors", "hardware_verified:false", "scientific_verified:false"):
            self.assertIn(required, readme)

        for document in (readme, SKILL_PATH.read_text(encoding="utf-8")):
            normalized = " ".join(document.split())
            for required in ("non-empty absolute Windows path", "does not preflight existence", "200 without applying the path"):
                self.assertIn(required, normalized)
            for required in ("id/name/predecessor", "session/configuration-scoped", "current mutable display name", "current source-path field", "not full topology", "parameters or streams", "read-only", "new semantic exposure"):
                self.assertIn(required, normalized)
            self.assertIn("real external Windows UI Automation observation", normalized)
            self.assertIn("hosted CI remains pending", normalized)
            self.assertIn("30-second CTest hard process timeout", normalized)

    def test_exposure_boundary_is_explicit_in_source_and_operator_docs(self):
        source = HTTP_SERVER_PATH.read_text(encoding="utf-8")
        self.assertIn('svr_->listen ("0.0.0.0", PORT)', source)
        for document in (README_PATH.read_text(encoding="utf-8"), SKILL_PATH.read_text(encoding="utf-8")):
            with self.subTest(document=document[:40]):
                normalized = " ".join(document.split())
                for required in ("MCP bridge outbound client", "0.0.0.0", "raw Open Ephys API", "bypass MCP RECORD approval", "trusted network or firewall", "listener remains unchanged"):
                    self.assertIn(required, normalized)
                self.assertNotIn("no listener or C++ change", normalized)


if __name__ == "__main__": unittest.main()
