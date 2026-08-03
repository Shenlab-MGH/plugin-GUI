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


if __name__ == "__main__":
    unittest.main()
