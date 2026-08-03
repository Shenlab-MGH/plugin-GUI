import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WINDOWS_WORKFLOW_PATH = ROOT / ".github" / "workflows" / "windows.yml"


class WindowsUiaWorkflowTests(unittest.TestCase):
    def test_windows_workflow_tracks_uia_tests_for_push_and_pull_request_changes(self):
        workflow = WINDOWS_WORKFLOW_PATH.read_text(encoding="utf-8")

        for event in ("push", "pull_request"):
            with self.subTest(event=event):
                path_block = re.search(
                    rf"(?m)^  {event}:\r?\n    paths:\r?\n(?P<paths>(?:    - .+\r?\n)+)",
                    workflow,
                )
                self.assertIsNotNone(path_block)
                self.assertIn("    - 'Tests/UI/**'", path_block.group("paths"))

    def test_windows_workflow_runs_only_the_ui_tests_after_building_release(self):
        workflow = WINDOWS_WORKFLOW_PATH.read_text(encoding="utf-8")
        configure_command = (
            'cmake -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTS=ON ..'
        )
        build_command = (
            "msbuild Build/ALL_BUILD.vcxproj "
            "-p:Configuration=Release -p:Platform=x64 -m"
        )
        ui_test_command = (
            "ctest --test-dir Build -C Release "
            "-R '^UI_tests$' --output-on-failure"
        )

        self.assertIn(configure_command, workflow)
        self.assertIn(build_command, workflow)
        self.assertIn(ui_test_command, workflow)
        self.assertLess(workflow.index(configure_command), workflow.index(build_command))
        self.assertLess(workflow.index(build_command), workflow.index(ui_test_command))


if __name__ == "__main__":
    unittest.main()
