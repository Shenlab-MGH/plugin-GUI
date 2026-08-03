"""Executable contracts for Open Ephys Agent Windows prerelease packaging."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PACKAGER = REPOSITORY_ROOT / "Scripts" / "ci" / "package_agent_windows_release.py"
RELEASE_WORKFLOW = (
    REPOSITORY_ROOT
    / ".github"
    / "workflows"
    / "agent-windows-portable-prerelease.yml"
)
CONTRACT_WORKFLOW = (
    REPOSITORY_ROOT / ".github" / "workflows" / "agent-release-contracts.yml"
)
TEST_COMMIT = "0123456789abcdef0123456789abcdef01234567"
ALLOWED_TAGS = {"oe-agent-v1.0.2-r0.1.0", "oe-agent-v1.1.0-r0.1.0"}
ACTION_PINS = {
    "checkout": "11d5960a326750d5838078e36cf38b85af677262",
    "download-artifact": "d3f86a106a0bac45b974a628896c90dbdf5c8093",
    "setup-msbuild": "30375c66a4eea26614e0d39710365f22f8b0af57",
    "upload-artifact": "ea165f8d65b6e75b540449e92b4886f43607fa02",
}
CONTRACT_COMMAND = (
    "python -m unittest Tests.CI.test_agent_windows_portable_prerelease -v"
)
CONTRACT_PATHS = {
    ".github/workflows/agent-windows-portable-prerelease.yml",
    ".github/workflows/agent-release-contracts.yml",
    "Scripts/ci/package_agent_windows_release.py",
    "Tests/CI/test_agent_windows_portable_prerelease.py",
}


def _mapping_block(source: str, key: str, indent: int) -> str:
    lines = source.splitlines()
    header = " " * indent + key + ":"
    try:
        start = lines.index(header)
    except ValueError:
        return ""

    end = len(lines)
    for index in range(start + 1, len(lines)):
        stripped = lines[index].strip()
        if not stripped or stripped.startswith("#"):
            continue
        line_indent = len(lines[index]) - len(lines[index].lstrip(" "))
        if line_indent <= indent:
            end = index
            break
    return "\n".join(lines[start + 1 : end])


def _sequence_values(source: str, key: str, indent: int) -> set[str]:
    block = _mapping_block(source, key, indent)
    values = set()
    for line in block.splitlines():
        stripped = line.strip()
        line_indent = len(line) - len(line.lstrip(" "))
        if line_indent == indent + 2 and stripped.startswith("- "):
            values.add(stripped[2:].strip().strip("'\""))
    return values


class AgentWindowsPortablePackagerTests(unittest.TestCase):
    def _fixture(self, root: Path, gui_version: str) -> tuple[Path, list[Path]]:
        (root / "CMakeLists.txt").write_text(
            f"set(GUI_VERSION {gui_version})\n", encoding="utf-8"
        )
        (root / "LICENSE").write_text("fixture license\n", encoding="utf-8")
        release = root / "Build" / "Release"
        (release / "configs").mkdir(parents=True)
        (release / "open-ephys.exe").write_bytes(b"fixture executable")
        (release / "open-ephys.lib").write_bytes(b"linker import library")
        (release / "open-ephys.exp").write_bytes(b"linker exports")
        (release / "icon-small.png").write_bytes(b"fixture icon")
        (release / "configs" / "default.xml").write_text(
            "<SETTINGS/>\n", encoding="utf-8"
        )

        runtimes = []
        for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
            runtime = root / "runtime" / name
            runtime.parent.mkdir(exist_ok=True)
            runtime.write_bytes(f"fixture {name}".encode("ascii"))
            runtimes.append(runtime)
        return release, runtimes

    def _run_packager(
        self,
        root: Path,
        gui_version: str,
        release_tag: str,
        *,
        invocation: str = "tag",
        output_name: str = "dist",
        unexpected_build_file: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        release, runtimes = self._fixture(root, gui_version)
        if unexpected_build_file:
            (release / "unexpected.pdb").write_bytes(b"unexpected")
        command = [
            sys.executable,
            str(PACKAGER),
            "--repository-root",
            str(root),
            "--build-directory",
            str(release),
            "--output-directory",
            str(root / output_name),
            "--repository",
            "Shenlab-MGH/plugin-GUI",
            "--commit",
            TEST_COMMIT,
            "--release-tag",
            release_tag,
            "--invocation",
            invocation,
            "--runner-image",
            "windows-2022@test-image",
            "--runner-architecture",
            "X64",
        ]
        for runtime in runtimes:
            command.extend(("--runtime-dll", str(runtime)))
            command.extend(
                ("--runtime-dll-version", f"{runtime.name}=fixture-version-1")
            )
        for name, sha in ACTION_PINS.items():
            command.extend(("--action", f"{name}={sha}"))
        command.extend(("--toolchain", "cmake=3.30.0"))
        command.extend(("--toolchain", "msbuild=17.10.4"))
        return subprocess.run(command, capture_output=True, text=True, check=False)

    def test_packages_v110_with_deterministic_name_checksum_and_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self._run_packager(
                root, "1.1.0", "oe-agent-v1.1.0-r0.1.0"
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            artifact = root / "dist" / "open-ephys-agent-v1.1.0-r0.1.0-windows-x64.zip"
            checksum_file = artifact.with_suffix(".zip.sha256")
            provenance_file = artifact.with_suffix(".provenance.json")
            self.assertTrue(artifact.is_file())
            self.assertTrue(checksum_file.is_file())
            self.assertTrue(provenance_file.is_file())

            provenance = json.loads(provenance_file.read_text(encoding="utf-8"))
            self.assertEqual(
                provenance,
                {
                    "agent_version": "0.1.0",
                    "artifact": artifact.name,
                    "build_environment": {
                        "actions": ACTION_PINS,
                        "runner": {
                            "architecture": "X64",
                            "image": "windows-2022@test-image",
                        },
                        "toolchains": {
                            "cmake": "3.30.0",
                            "msbuild": "17.10.4",
                            "python": re.fullmatch(
                                r"\d+\.\d+\.\d+", sys.version.split()[0]
                            ).group(),
                        },
                    },
                    "commit": TEST_COMMIT,
                    "gui_version": "1.1.0",
                    "invocation": "tag",
                    "official_baseline": "1.1.0",
                    "official_commit": "c2ce076f5b2d4182222f9d0fd9bb9b97a60582e7",
                    "official_tag": "v1.1.0",
                    "platform": "windows-x64",
                    "release_eligible": True,
                    "release_tag": "oe-agent-v1.1.0-r0.1.0",
                    "repository": "Shenlab-MGH/plugin-GUI",
                    "runtime_dlls": [
                        {
                            "file_version": "fixture-version-1",
                            "name": name,
                            "sha256": hashlib.sha256(
                                f"fixture {name}".encode("ascii")
                            ).hexdigest(),
                        }
                        for name in (
                            "msvcp140.dll",
                            "vcruntime140.dll",
                            "vcruntime140_1.dll",
                        )
                    ],
                    "validation_level": {
                        "hardware": False,
                        "scientific": False,
                    },
                },
            )

            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            self.assertEqual(
                checksum_file.read_text(encoding="ascii"),
                f"{digest}  {artifact.name}\n",
            )
            with zipfile.ZipFile(artifact) as archive:
                self.assertEqual(
                    archive.namelist(),
                    [
                        "open-ephys/LICENSE",
                        "open-ephys/configs/default.xml",
                        "open-ephys/icon-small.png",
                        "open-ephys/msvcp140.dll",
                        "open-ephys/open-ephys.exe",
                        "open-ephys/provenance.json",
                        "open-ephys/vcruntime140.dll",
                        "open-ephys/vcruntime140_1.dll",
                    ],
                )
                self.assertTrue(
                    all(info.date_time == (1980, 1, 1, 0, 0, 0) for info in archive.infolist())
                )
                self.assertEqual(
                    json.loads(archive.read("open-ephys/provenance.json")), provenance
                )

    def test_packages_v102_with_its_own_baseline_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self._run_packager(
                root, "1.0.2", "oe-agent-v1.0.2-r0.1.0"
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            artifact = root / "dist" / "open-ephys-agent-v1.0.2-r0.1.0-windows-x64.zip"
            provenance = json.loads(
                artifact.with_suffix(".provenance.json").read_text(encoding="utf-8")
            )
            self.assertEqual(provenance["official_tag"], "v1.0.2")
            self.assertEqual(
                provenance["official_commit"],
                "c91afebcfb0678a667fb93f6312ed33c56ec640f",
            )

    def test_rejects_release_when_checked_out_gui_baseline_does_not_match(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self._run_packager(
                root, "1.1.0", "oe-agent-v1.0.2-r0.1.0"
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("does not match GUI_VERSION 1.1.0", result.stderr)
            self.assertFalse((root / "dist").exists())

    def test_rejects_unversioned_or_unapproved_release_names(self) -> None:
        for release_tag in (
            "latest",
            "v1.1.0-r0.1.0",
            "oe-agent-oe-agent-v1.1.0-r0.1.0",
            "oe-agent-v1.1.0-r0.2.0",
        ):
            with self.subTest(release_tag=release_tag):
                with tempfile.TemporaryDirectory() as directory:
                    result = self._run_packager(
                        Path(directory), "1.1.0", release_tag
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("unsupported release version", result.stderr)

    def test_rejects_unexpected_build_products_instead_of_shipping_them(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = self._run_packager(
                Path(directory),
                "1.1.0",
                "oe-agent-v1.1.0-r0.1.0",
                unexpected_build_file=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unexpected Release build product: unexpected.pdb", result.stderr)

    def test_workflow_dispatch_bundle_is_explicitly_non_release(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self._run_packager(
                root,
                "1.1.0",
                "oe-agent-v1.1.0-r0.1.0",
                invocation="workflow_dispatch",
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            provenance = json.loads(
                (
                    root
                    / "dist"
                    / "open-ephys-agent-v1.1.0-r0.1.0-windows-x64.provenance.json"
                ).read_text(encoding="utf-8")
            )
            self.assertFalse(provenance["release_eligible"])
            self.assertEqual(provenance["invocation"], "workflow_dispatch")

    def test_same_inputs_produce_byte_identical_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            first_root = Path(first)
            second_root = Path(second)
            first_result = self._run_packager(
                first_root, "1.1.0", "oe-agent-v1.1.0-r0.1.0"
            )
            second_result = self._run_packager(
                second_root, "1.1.0", "oe-agent-v1.1.0-r0.1.0"
            )

            self.assertEqual(first_result.returncode, 0, first_result.stderr)
            self.assertEqual(second_result.returncode, 0, second_result.stderr)
            filename = "open-ephys-agent-v1.1.0-r0.1.0-windows-x64.zip"
            self.assertEqual(
                (first_root / "dist" / filename).read_bytes(),
                (second_root / "dist" / filename).read_bytes(),
            )


class AgentWindowsPortableWorkflowTests(unittest.TestCase):
    def setUp(self) -> None:
        self.assertTrue(RELEASE_WORKFLOW.is_file(), f"missing {RELEASE_WORKFLOW}")
        self.release_source = RELEASE_WORKFLOW.read_text(encoding="utf-8")

    def test_release_workflow_has_only_exact_tag_and_dispatch_triggers(self) -> None:
        triggers = _mapping_block(self.release_source, "on", 0)
        pushes = _mapping_block(triggers, "push", 2)
        dispatch = _mapping_block(triggers, "workflow_dispatch", 2)

        self.assertEqual(_sequence_values(pushes, "tags", 4), ALLOWED_TAGS)
        self.assertNotIn("branches:", pushes)
        self.assertEqual(_sequence_values(dispatch, "options", 8), ALLOWED_TAGS)
        self.assertNotIn("latest", triggers.lower())

        concurrency = _mapping_block(self.release_source, "concurrency", 0)
        self.assertIn("github.event_name == 'push' && github.ref || github.run_id", concurrency)
        self.assertIn("cancel-in-progress: false", concurrency)

    def test_build_is_windows_release_portable_and_uploads_exact_bundle(self) -> None:
        self.assertIn("runs-on: windows-2022", self.release_source)
        self.assertIn(
            "msbuild Build/ALL_BUILD.vcxproj -p:Configuration=Release -p:Platform=x64 -m",
            self.release_source,
        )
        self.assertIn("package_agent_windows_release.py", self.release_source)
        self.assertIn("git merge-base --is-ancestor", self.release_source)

        self.assertEqual(self.release_source.count("actions/upload-artifact@"), 1)
        self.assertIn("steps.release.outputs.bundle_name", self.release_source)
        self.assertIn("steps.release.outputs.zip_name", self.release_source)
        self.assertIn("name: ${{ steps.release.outputs.bundle_name }}", self.release_source)
        self.assertIn("dist/${{ steps.release.outputs.zip_name }}", self.release_source)
        self.assertIn("dist/${{ steps.release.outputs.checksum_name }}", self.release_source)
        self.assertIn("dist/${{ steps.release.outputs.provenance_name }}", self.release_source)
        self.assertNotIn(
            "\n        name: ${{ steps.release.outputs.zip_name }}", self.release_source
        )
        self.assertIn("$releaseEligible = 'false'", self.release_source)
        self.assertIn('"release_eligible=$releaseEligible"', self.release_source)
        self.assertIn("-validation-", self.release_source)

    def test_publish_job_is_limited_to_matching_shenlab_tag_and_existing_prerelease(self) -> None:
        publish_job = _mapping_block(self.release_source, "publish-prerelease", 2)

        self.assertIn("github.repository == 'Shenlab-MGH/plugin-GUI'", publish_job)
        self.assertIn("github.event_name == 'push'", publish_job)
        self.assertIn("github.ref_type == 'tag'", publish_job)
        self.assertIn("needs.build.outputs.release_tag == github.ref_name", publish_job)
        self.assertIn("contents: write", publish_job)
        self.assertIn("prerelease", publish_job)
        self.assertIn("gh release upload", publish_job)
        self.assertNotIn("gh release create", publish_job)
        self.assertNotIn("--clobber", publish_job)
        self.assertIn("GH_REPO: ${{ github.repository }}", publish_job)
        self.assertIn("git/ref/tags", publish_job)
        self.assertIn("release.assets", publish_job)
        self.assertIn("Asset already exists", publish_job)

        top_permissions = _mapping_block(self.release_source, "permissions", 0)
        self.assertIn("contents: read", top_permissions)
        build_job = _mapping_block(self.release_source, "build", 2)
        self.assertNotIn("contents: write", build_job)

    def test_every_third_party_action_is_pinned_to_a_reviewed_commit(self) -> None:
        contract_source = CONTRACT_WORKFLOW.read_text(encoding="utf-8")
        combined = self.release_source + "\n" + contract_source
        uses = [
            line.split("uses:", 1)[1].strip().split()[0]
            for line in combined.splitlines()
            if "uses:" in line
        ]

        self.assertGreaterEqual(len(uses), 5)
        for action in uses:
            with self.subTest(action=action):
                self.assertRegex(action, r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{40}$")
        for name, sha in ACTION_PINS.items():
            self.assertIn(f"@{sha}", combined, name)

    def test_packaging_records_runtime_toolchain_runner_and_action_provenance(self) -> None:
        for argument in (
            "--runtime-dll-version",
            "--runner-image",
            "--runner-architecture",
            "--toolchain",
            "--action",
        ):
            self.assertIn(argument, self.release_source)

    def test_release_path_has_no_official_deploy_installer_driver_or_updater_hooks(self) -> None:
        source = self.release_source.lower()
        for forbidden in (
            "artifactory_access_token",
            "openephys.jfrog.io",
            "iscc",
            "frontpanelusb-driveronly",
            "ftd3xxdriver",
            "autoupdater",
            "resources/installers",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_dedicated_contract_workflow_covers_release_contract_files(self) -> None:
        source = CONTRACT_WORKFLOW.read_text(encoding="utf-8")
        triggers = _mapping_block(source, "on", 0)
        pull_requests = _mapping_block(triggers, "pull_request", 2)
        pushes = _mapping_block(triggers, "push", 2)

        self.assertNotIn("branches:", pull_requests)
        self.assertEqual(
            _sequence_values(pushes, "branches", 4),
            {"main", "development", "testing"},
        )
        self.assertEqual(_sequence_values(pull_requests, "paths", 4), CONTRACT_PATHS)
        self.assertEqual(_sequence_values(pushes, "paths", 4), CONTRACT_PATHS)
        self.assertIn(CONTRACT_COMMAND, source)


if __name__ == "__main__":
    unittest.main()
