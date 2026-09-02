import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WINDOWS_WORKFLOW_PATH = ROOT / ".github" / "workflows" / "windows.yml"
UI_CMAKE_PATH = ROOT / "Tests" / "UI" / "CMakeLists.txt"


class WindowsUiaWorkflowTests(unittest.TestCase):
    def test_windows_uia_cases_run_in_fresh_processes(self):
        cmake = UI_CMAKE_PATH.read_text(encoding="utf-8")
        workflow = WINDOWS_WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertIn("set(COMPONENT_REGISTER_AGGREGATE_TEST OFF)", cmake)
        self.assertIn("gtest_discover_tests(", cmake)
        self.assertIn('TEST_PREFIX "${COMPONENT_NAME}_tests_"', cmake)
        self.assertNotIn("DISCOVERY_MODE PRE_TEST", cmake)
        self.assertIn("PROPERTIES TIMEOUT 30", cmake)
        self.assertNotIn("UIA_TEST_CASES", cmake)
        self.assertIn("-R '^UI_tests_'", workflow)

    def test_windows_workflow_tracks_ui_tests_for_push_and_pull_request_changes(self):
        workflow = WINDOWS_WORKFLOW_PATH.read_text(encoding="utf-8")

        for event in ("push", "pull_request"):
            with self.subTest(event=event):
                path_block = re.search(
                    rf"(?m)^  {event}:\r?\n    paths:\r?\n(?P<paths>(?:    - .+\r?\n)+)",
                    workflow,
                )
                self.assertIsNotNone(path_block)
                self.assertIn("    - 'Tests/UI/**'", path_block.group("paths"))

    def test_windows_workflow_keeps_production_and_ui_test_builds_isolated(self):
        workflow = WINDOWS_WORKFLOW_PATH.read_text(encoding="utf-8")
        production_configure = (
            'cmake -S . -B Build -G "Visual Studio 17 2022" -A x64 '
            "-DBUILD_TESTS=OFF"
        )
        production_build = (
            "cmake --build Build --config Release --target ALL_BUILD --parallel 8"
        )
        test_configure = (
            'cmake -S . -B BuildContractTests -G "Visual Studio 17 2022" -A x64 '
            "-DBUILD_TESTS=ON -DOE_DONT_CHECK_BUILD_PATH=TRUE"
        )
        test_build = (
            "cmake --build BuildContractTests --config Release --target UI_tests "
            "--parallel 8"
        )
        ui_test_command = (
            "ctest --test-dir BuildContractTests -C Release "
            "-R '^UI_tests_' --output-on-failure --no-tests=error"
        )

        commands = (
            production_configure,
            production_build,
            test_configure,
            test_build,
            ui_test_command,
        )

        for command in commands:
            with self.subTest(command=command):
                self.assertEqual(workflow.count(command), 1)

        if all(workflow.count(command) == 1 for command in commands):
            self.assertLess(workflow.index(production_configure), workflow.index(production_build))
            self.assertLess(workflow.index(test_configure), workflow.index(test_build))
            self.assertLess(workflow.index(test_build), workflow.index(ui_test_command))


if __name__ == "__main__":
    unittest.main()
