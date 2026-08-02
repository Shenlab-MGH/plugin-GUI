import json
import re
import subprocess
import unittest
from pathlib import Path, PurePosixPath, PureWindowsPath

import sys

from Tests.AgentNative.workflow_test_utils import workflow_job


ROOT = Path(__file__).resolve().parents[2]
BUNDLE_PATH = ROOT / "agent_native" / "open_ephys_agent_release_bundle.json"
SURFACE_PATH = ROOT / "agent_native" / "open_ephys_agent_surface.json"
FIXTURE_PATH = ROOT / "agent_native" / "open_ephys_agent_contract_v1_0_2.json"
INSTALLER_PATH = ROOT / "Resources" / "Installers" / "Windows" / "windows_installer_script.iss"
TESTS_WORKFLOW_PATH = ROOT / ".github" / "workflows" / "tests.yml"
FULL_HISTORY_CHECKOUT_PATTERN = (
    r"(?m)^    - uses: actions/checkout@v4[ \t]*\n"
    r"      with:[ \t]*\n"
    r"        fetch-depth: 0[ \t]*$"
)

sys.path.insert(0, str(ROOT / "agent_native"))

import open_ephys_mcp_server as mcp


def repository_artifact_path(root, artifact):
    if not isinstance(artifact, str) or not artifact:
        raise ValueError("Artifact path must be a non-empty string")

    posix_path = PurePosixPath(artifact)
    windows_path = PureWindowsPath(artifact)
    if (
        "\\" in artifact
        or posix_path.is_absolute()
        or windows_path.is_absolute()
        or windows_path.drive
        or ".." in posix_path.parts
        or ".." in windows_path.parts
    ):
        raise ValueError(f"Artifact path must be repository-relative POSIX syntax: {artifact}")

    resolved = (root / Path(*posix_path.parts)).resolve()
    if not resolved.is_relative_to(root.resolve()):
        raise ValueError(f"Artifact path escapes repository: {artifact}")
    return resolved


class OpenEphysReleaseBundleTests(unittest.TestCase):
    def load_bundle(self):
        self.assertTrue(BUNDLE_PATH.is_file(), f"Missing release bundle: {BUNDLE_PATH}")
        return json.loads(BUNDLE_PATH.read_text(encoding="utf-8"))

    def test_release_bundle_exists(self):
        self.load_bundle()

    def test_bundle_identifies_limited_windows_v102_release(self):
        bundle = self.load_bundle()

        self.assertEqual(bundle["format_version"], "1.0.0")
        self.assertEqual(
            bundle.get("format_semantics"),
            "Repository-local document shape for this release bundle; versioned independently from the agent contract.",
        )
        self.assertEqual(
            bundle["bundle"],
            {
                "id": "open-ephys-agent-native",
                "version": "0.1.3",
                "platform": "windows",
                "coverage": "contracted-core-only",
            },
        )
        self.assertEqual(
            bundle["official_upstream"],
            {
                "repository": "open-ephys/plugin-GUI",
                "tag": "v1.0.2",
                "commit": "c91afebcfb0678a667fb93f6312ed33c56ec640f",
                "gui_version": "1.0.2",
            },
        )
        self.assertEqual(
            bundle["verification"],
            {
                "level": "offline-contract-and-ci",
                "hardware_verified": False,
                "scientific_verified": False,
            },
        )

    def test_upstream_version_matches_authoritative_build_metadata(self):
        bundle = self.load_bundle()
        expected_version = bundle["official_upstream"]["gui_version"]
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cmake_match = re.search(r"^set\(GUI_VERSION\s+([^\s\)]+)\)", cmake, re.MULTILINE)

        self.assertIsNotNone(cmake_match, "CMake GUI_VERSION is missing")
        self.assertEqual(cmake_match.group(1), expected_version)

        self.assertTrue(INSTALLER_PATH.is_file(), f"Missing authoritative installer: {INSTALLER_PATH}")
        installer = INSTALLER_PATH.read_text(encoding="utf-8")
        installer_match = re.search(r"^AppVersion=(.+)$", installer, re.MULTILINE)
        self.assertIsNotNone(installer_match, "Windows installer AppVersion is missing")
        self.assertEqual(installer_match.group(1).strip(), expected_version)

    def test_upstream_tag_resolves_to_bundle_commit_and_is_ancestral(self):
        bundle = self.load_bundle()
        upstream = bundle["official_upstream"]
        tagged_commit = subprocess.run(
            ["git", "rev-parse", "--verify", f"refs/tags/{upstream['tag']}^{{commit}}"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

        self.assertEqual(tagged_commit, upstream["commit"])
        ancestry = subprocess.run(
            ["git", "merge-base", "--is-ancestor", upstream["commit"], "HEAD"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            ancestry.returncode,
            0,
            ancestry.stderr or f"{upstream['commit']} is not an ancestor of HEAD",
        )

    def test_unit_ci_fetches_history_required_for_provenance(self):
        workflow = TESTS_WORKFLOW_PATH.read_text(encoding="utf-8")
        unit_job = workflow_job(workflow, "unit-tests")

        self.assertRegex(
            unit_job,
            FULL_HISTORY_CHECKOUT_PATTERN,
        )

    def test_unit_provenance_checkout_cannot_be_masked_by_windows_job(self):
        workflow = TESTS_WORKFLOW_PATH.read_text(encoding="utf-8")
        unit_job = workflow_job(workflow, "unit-tests")
        windows_job = workflow_job(workflow, "windows-agent-tests")
        mutated_unit_job, replacement_count = re.subn(
            r"(?m)^        fetch-depth: 0[ \t]*$",
            "        fetch-depth: 1",
            unit_job,
            count=1,
        )

        self.assertEqual(replacement_count, 1)
        self.assertNotIn("windows-agent-tests:", unit_job)
        self.assertNotRegex(mutated_unit_job, FULL_HISTORY_CHECKOUT_PATTERN)
        self.assertIn("fetch-depth: 0", windows_job)

    def test_bundle_baseline_matches_surface_and_fixture(self):
        bundle = self.load_bundle()
        surface = json.loads(SURFACE_PATH.read_text(encoding="utf-8"))
        fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
        expected_baseline = {
            "upstream": bundle["official_upstream"]["repository"],
            "version": bundle["official_upstream"]["gui_version"],
            "commit": bundle["official_upstream"]["commit"],
        }

        self.assertEqual(surface["baseline"], expected_baseline)
        self.assertEqual(fixture["baseline"], expected_baseline)

    def test_contract_identity_matches_surface_fixture_and_mcp_machine_sources(self):
        bundle = self.load_bundle()
        surface = json.loads(SURFACE_PATH.read_text(encoding="utf-8"))
        fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
        contract = bundle["contract"]

        self.assertEqual(contract["id"], "open-ephys-agent")
        self.assertEqual(contract["schema_version"], "0.1.3")
        self.assertEqual(contract["version"], "0.1.3")
        self.assertEqual(contract["fixture"], "agent_native/open_ephys_agent_contract_v1_0_2.json")
        self.assertEqual(surface["schema_version"], contract["schema_version"])
        self.assertEqual(fixture["schema_version"], contract["schema_version"])
        self.assertEqual(surface["contract"]["id"], contract["id"])
        self.assertEqual(surface["contract"]["version"], contract["version"])
        self.assertEqual(fixture["contract"]["id"], contract["id"])
        self.assertEqual(fixture["contract"]["version"], contract["version"])
        self.assertEqual(mcp.SUPPORTED_SCHEMA_VERSION, contract["schema_version"])
        self.assertEqual(mcp.SUPPORTED_CONTRACT_ID, contract["id"])
        self.assertEqual(mcp.SUPPORTED_CONTRACT_VERSION, contract["version"])

    def test_components_declare_contract_compatibility_without_overclaiming_source_proof(self):
        bundle = self.load_bundle()
        contract = bundle["contract"]
        expected_requirement = {"id": contract["id"], "version": contract["version"]}
        expected_evidence = {
            "api": "declared-compatibility",
            "uia": "declared-compatibility",
            "mcp": "machine-synchronized",
            "skill": "declared-compatibility",
        }

        for component_name, component in bundle["components"].items():
            with self.subTest(component=component_name):
                self.assertEqual(component["requires_contract"], expected_requirement)
                self.assertEqual(component.get("contract_evidence"), expected_evidence[component_name])

        self.assertEqual(bundle["components"]["skill"]["source_contract_pin"], "prose")

    def test_component_artifacts_are_existing_repository_files(self):
        bundle = self.load_bundle()

        self.assertEqual(set(bundle["components"]), {"api", "uia", "mcp", "skill"})
        for component_name, component in bundle["components"].items():
            artifact = component["artifact"]
            with self.subTest(component=component_name, artifact=artifact):
                resolved = repository_artifact_path(ROOT, artifact)
                self.assertTrue(resolved.is_file(), f"Missing component artifact: {artifact}")

    def test_artifact_path_validator_rejects_posix_and_windows_escape_forms(self):
        invalid_paths = (
            "../outside",
            "nested/../../outside",
            "/absolute/path",
            "C:/absolute/path",
            "C:drive-relative",
            "folder\\windows-separator",
            "\\rooted",
            "\\\\server\\share\\file",
            "//server/share/file",
        )

        for artifact in invalid_paths:
            with self.subTest(artifact=artifact):
                with self.assertRaises(ValueError):
                    repository_artifact_path(ROOT, artifact)

    def test_mcp_metadata_matches_running_initialize_behavior(self):
        bundle = self.load_bundle()
        mcp_component = bundle["components"]["mcp"]
        server = mcp.McpServer(manifest_path=SURFACE_PATH)
        response = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {},
                    "clientInfo": {"name": "agent-native-tests", "version": "1.0.0"},
                },
            }
        )
        result = response["result"]

        self.assertEqual(mcp_component["implementation"], "python-stdio")
        self.assertEqual((ROOT / mcp_component["artifact"]).resolve(), Path(mcp.__file__).resolve())
        self.assertEqual(mcp_component["protocol_version"], result["protocolVersion"])
        self.assertEqual(mcp_component["server_name"], result["serverInfo"]["name"])
        self.assertEqual(bundle["bundle"]["version"], result["serverInfo"]["version"])


if __name__ == "__main__":
    unittest.main()
