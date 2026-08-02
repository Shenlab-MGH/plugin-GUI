import json
import re
import unittest
from pathlib import Path

import sys


ROOT = Path(__file__).resolve().parents[2]
BUNDLE_PATH = ROOT / "agent_native" / "open_ephys_agent_release_bundle.json"
SURFACE_PATH = ROOT / "agent_native" / "open_ephys_agent_surface.json"
FIXTURE_PATH = ROOT / "agent_native" / "open_ephys_agent_contract_v1_0_2.json"
INSTALLER_PATH = ROOT / "Resources" / "Installers" / "Windows" / "windows_installer_script.iss"

sys.path.insert(0, str(ROOT / "agent_native"))

import open_ephys_mcp_server as mcp


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
            bundle["bundle"],
            {
                "id": "open-ephys-agent-native",
                "version": "0.1.2",
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

        if INSTALLER_PATH.is_file():
            installer = INSTALLER_PATH.read_text(encoding="utf-8")
            installer_match = re.search(r"^AppVersion=(.+)$", installer, re.MULTILINE)
            self.assertIsNotNone(installer_match, "Windows installer AppVersion is missing")
            self.assertEqual(installer_match.group(1).strip(), expected_version)

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

    def test_contract_identity_is_synchronized_across_all_machine_sources(self):
        bundle = self.load_bundle()
        surface = json.loads(SURFACE_PATH.read_text(encoding="utf-8"))
        fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
        contract = bundle["contract"]
        expected_requirement = {
            "id": contract["id"],
            "version": contract["version"],
        }

        self.assertEqual(contract["id"], "open-ephys-agent")
        self.assertEqual(contract["schema_version"], "0.1.2")
        self.assertEqual(contract["version"], "0.1.2")
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

        for component_name, component in bundle["components"].items():
            with self.subTest(component=component_name):
                self.assertEqual(component["requires_contract"], expected_requirement)

    def test_component_artifacts_are_existing_repository_files(self):
        bundle = self.load_bundle()

        self.assertEqual(set(bundle["components"]), {"api", "uia", "mcp", "skill"})
        for component_name, component in bundle["components"].items():
            artifact = component["artifact"]
            artifact_path = Path(artifact)
            with self.subTest(component=component_name, artifact=artifact):
                self.assertNotIn("\\", artifact, "Artifact paths must use repository-relative POSIX syntax")
                self.assertFalse(artifact_path.is_absolute())
                self.assertNotIn("..", artifact_path.parts)
                resolved = (ROOT / artifact_path).resolve()
                self.assertTrue(resolved.is_relative_to(ROOT.resolve()))
                self.assertTrue(resolved.is_file(), f"Missing component artifact: {artifact}")

        self.assertEqual(bundle["components"]["skill"]["source_contract_pin"], "prose")

    def test_mcp_metadata_matches_running_initialize_behavior(self):
        bundle = self.load_bundle()
        mcp_component = bundle["components"]["mcp"]
        server = mcp.McpServer(manifest_path=SURFACE_PATH)
        response = server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
        result = response["result"]

        self.assertEqual(mcp_component["implementation"], "python-stdio")
        self.assertEqual((ROOT / mcp_component["artifact"]).resolve(), Path(mcp.__file__).resolve())
        self.assertEqual(mcp_component["protocol_version"], result["protocolVersion"])
        self.assertEqual(mcp_component["server_name"], result["serverInfo"]["name"])
        self.assertEqual(bundle["bundle"]["version"], result["serverInfo"]["version"])


if __name__ == "__main__":
    unittest.main()
