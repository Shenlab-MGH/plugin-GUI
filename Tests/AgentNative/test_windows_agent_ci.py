import re
import unittest
from pathlib import Path

from Tests.AgentNative.workflow_test_utils import workflow_job


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "tests.yml"


class WindowsAgentCiTests(unittest.TestCase):
    def setUp(self):
        self.workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.job = workflow_job(self.workflow, "windows-agent-tests")

    def test_job_is_bounded_and_runs_natively_on_windows(self):
        self.assertRegex(self.job, r"(?m)^    name: Windows Agent Tests\s*$")
        self.assertRegex(self.job, r"(?m)^    runs-on: windows-2022\s*$")
        self.assertRegex(self.job, r"(?m)^    timeout-minutes: 90\s*$")

    def test_job_checks_out_full_history_and_sets_up_python_and_msbuild(self):
        self.assertRegex(
            self.job,
            r"(?m)^      - uses: actions/checkout@v4\s*\n        with:\s*\n          fetch-depth: 0\s*$",
        )
        self.assertRegex(self.job, r"(?m)^        uses: actions/setup-python@v5\s*$")
        self.assertRegex(self.job, r"(?m)^          python-version: ['\"]3\.13['\"]\s*$")
        self.assertRegex(self.job, r"(?m)^        uses: microsoft/setup-msbuild@v2\s*$")

    def test_job_runs_agent_contract_tests_and_configures_cpp_tests(self):
        self.assertIn(
            "python -m unittest discover -s Tests/AgentNative -p 'test_*.py' -v",
            self.job,
        )
        self.assertRegex(
            self.job,
            r'cmake\s+-S\s+\.\s+-B\s+Build\s+-G\s+"Visual Studio 17 2022"\s+-A\s+x64\s+-DBUILD_TESTS=ON',
        )

    def test_job_builds_only_api_and_ui_targets_serially(self):
        build_commands = re.findall(r"(?m)^\s*cmake --build Build[^\n]+$", self.job)
        self.assertEqual(len(build_commands), 2, build_commands)
        self.assertTrue(any("--target API_tests" in command for command in build_commands))
        self.assertTrue(any("--target UI_tests" in command for command in build_commands))
        for command in build_commands:
            self.assertIn("--config Release", command)
            self.assertIn("-- /m:1", command)
        self.assertRegex(self.job, r"(?m)^\s*\$env:CL_MPCount\s*=\s*['\"]1['\"]\s*$")
        self.assertNotIn("ALL_BUILD", self.job)

    def test_job_runs_release_binaries_and_propagates_each_exit_code(self):
        expected_runs = {
            "API": r"&\s+\.\\Build\\TestBin\\API\\Release\\API_tests\.exe\s*\n\s*if \(\$LASTEXITCODE -ne 0\)",
            "UI": r"&\s+\.\\Build\\TestBin\\UI\\Release\\UI_tests\.exe\s*\n\s*if \(\$LASTEXITCODE -ne 0\)",
        }
        for component, pattern in expected_runs.items():
            with self.subTest(component=component):
                self.assertRegex(self.job, pattern)

    def test_job_does_not_install_audio_or_external_plugin_dependencies(self):
        forbidden = ("Scream", "OEPlugins", "open-ephys-data-format", "OpenEphysHDF5Lib", "nwb-format")
        for text in forbidden:
            with self.subTest(text=text):
                self.assertNotIn(text, self.job)


if __name__ == "__main__":
    unittest.main()
