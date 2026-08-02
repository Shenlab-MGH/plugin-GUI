"""Contract tests for privileged deployment steps in official workflows."""

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
OFFICIAL_WORKFLOWS = (
    REPOSITORY_ROOT / ".github" / "workflows" / "windows.yml",
    REPOSITORY_ROOT / ".github" / "workflows" / "linux.yml",
    REPOSITORY_ROOT / ".github" / "workflows" / "osx.yml",
)
UPSTREAM_ONLY_CONDITION = "github.repository == 'open-ephys/plugin-GUI'"
STEP_START = re.compile(r"^(?P<indent>\s*)-\s+")


def workflow_steps(workflow: Path) -> list[list[str]]:
    """Return top-level action step blocks from the repository workflows."""
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


class OfficialDeployGuardTests(unittest.TestCase):
    def test_artifactory_deploy_steps_are_limited_to_the_upstream_repository(self) -> None:
        for workflow in OFFICIAL_WORKFLOWS:
            with self.subTest(workflow=workflow.name):
                privileged_steps = [
                    step
                    for step in workflow_steps(workflow)
                    if any(
                        "ARTIFACTORY_ACCESS_TOKEN" in line
                        or "openephys.jfrog.io" in line
                        for line in step
                    )
                ]

                self.assertEqual(
                    len(privileged_steps),
                    1,
                    f"{workflow.name} must have exactly one Artifactory deployment step",
                )

                conditions = [
                    line.strip().removeprefix("if:").strip()
                    for line in privileged_steps[0]
                    if line.strip().startswith("if:")
                ]
                self.assertEqual(
                    conditions,
                    [UPSTREAM_ONLY_CONDITION],
                    f"{workflow.name} Artifactory deployment must be upstream-only",
                )


if __name__ == "__main__":
    unittest.main()
