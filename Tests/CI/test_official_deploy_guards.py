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
CONTRACT_WORKFLOW = REPOSITORY_ROOT / ".github" / "workflows" / "deploy-guard-contracts.yml"
HEAVY_TEST_WORKFLOW = REPOSITORY_ROOT / ".github" / "workflows" / "tests.yml"
UPSTREAM_ONLY_CONDITION = "github.repository == 'open-ephys/plugin-GUI'"
CONTRACT_TEST_COMMAND = "python -m unittest Tests.CI.test_official_deploy_guards -v"
CONTRACT_PATHS = {
    ".github/workflows/windows.yml",
    ".github/workflows/linux.yml",
    ".github/workflows/osx.yml",
    ".github/workflows/deploy-guard-contracts.yml",
    "Tests/CI/test_official_deploy_guards.py",
}
FIELD = re.compile(r"(?P<key>[A-Za-z_][A-Za-z0-9_-]*):(?:\s*(?P<value>.*))?")


def _parse_field(source: str) -> tuple[str, str] | None:
    match = FIELD.fullmatch(source)
    if match is None:
        return None
    return match.group("key"), match.group("value") or ""


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


def workflow_steps_from_text(source: str) -> list[tuple[dict[str, list[str]], list[str]]]:
    """Parse direct fields of steps located specifically under ``jobs.*.steps``."""
    lines = source.splitlines()
    steps: list[tuple[dict[str, list[str]], list[str]]] = []
    current_fields: dict[str, list[str]] | None = None
    current_lines: list[str] = []
    inside_jobs = False
    inside_job = False
    inside_steps = False
    steps_indent = -1
    step_indent: int | None = None

    def finish_step() -> None:
        nonlocal current_fields, current_lines
        if current_fields is not None:
            steps.append((current_fields, current_lines))
        current_fields = None
        current_lines = []

    def start_step(rest: str) -> None:
        nonlocal current_fields, current_lines
        finish_step()
        current_fields = {}
        current_lines = [rest]
        if (field := _parse_field(rest)) is not None:
            key, value = field
            current_fields.setdefault(key, []).append(value)

    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            if current_fields is not None:
                current_lines.append(line)
            continue

        indent = len(line) - len(line.lstrip(" "))

        if not inside_jobs:
            inside_jobs = indent == 0 and stripped == "jobs:"
            continue

        if indent == 0:
            break

        if indent == 2 and _parse_field(stripped) is not None:
            finish_step()
            inside_job = True
            inside_steps = False
            step_indent = None
            continue

        if not inside_job:
            continue

        if indent == 4 and stripped == "steps:":
            finish_step()
            inside_steps = True
            steps_indent = indent
            step_indent = None
            continue

        if not inside_steps:
            continue

        is_list_item = stripped.startswith("-") and (
            len(stripped) == 1 or stripped[1].isspace()
        )
        if step_indent is None:
            if is_list_item and indent >= steps_indent:
                step_indent = indent
                start_step(stripped[1:].strip())
            elif indent <= steps_indent:
                inside_steps = False
            continue

        if indent < step_indent or (indent == steps_indent and not is_list_item):
            finish_step()
            inside_steps = False
            step_indent = None
            continue

        if indent == step_indent and is_list_item:
            start_step(stripped[1:].strip())
            continue

        if current_fields is not None:
            current_lines.append(line)
            if indent == step_indent + 2 and (field := _parse_field(stripped)) is not None:
                key, value = field
                current_fields.setdefault(key, []).append(value)

    finish_step()
    return steps


def validate_workflow_text(source: str, workflow_name: str) -> list[str]:
    privileged_steps = [
        fields
        for fields, lines in workflow_steps_from_text(source)
        if any(
            "ARTIFACTORY_ACCESS_TOKEN" in line or "openephys.jfrog.io" in line
            for line in lines
        )
    ]

    if len(privileged_steps) != 1:
        return [f"{workflow_name} must have exactly one Artifactory deployment step"]

    conditions = privileged_steps[0].get("if", [])
    if conditions != [UPSTREAM_ONLY_CONDITION]:
        return [f"{workflow_name} Artifactory deployment must be upstream-only"]

    return []


class OfficialDeployGuardTests(unittest.TestCase):
    def test_artifactory_deploy_steps_are_limited_to_the_upstream_repository(self) -> None:
        for workflow in OFFICIAL_WORKFLOWS:
            with self.subTest(workflow=workflow.name):
                self.assertEqual(
                    validate_workflow_text(
                        workflow.read_text(encoding="utf-8"), workflow.name
                    ),
                    [],
                )

    def test_rejects_guard_nested_under_env(self) -> None:
        source = """jobs:
  build:
    steps:
    - name: deploy
      env:
        ARTIFACTORY_ACCESS_TOKEN: secret
        if: github.repository == 'open-ephys/plugin-GUI'
"""
        self.assertNotEqual(validate_workflow_text(source, "nested-env.yml"), [])

    def test_rejects_guard_text_inside_run_script(self) -> None:
        source = """jobs:
  build:
    steps:
    - name: deploy
      run: |
        ARTIFACTORY_ACCESS_TOKEN=secret
        if: github.repository == 'open-ephys/plugin-GUI'
"""
        self.assertNotEqual(validate_workflow_text(source, "run-text.yml"), [])

    def test_rejects_privileged_non_step_list_items(self) -> None:
        source = """jobs:
  build:
    include:
    - name: deploy
      if: github.repository == 'open-ephys/plugin-GUI'
      env:
        ARTIFACTORY_ACCESS_TOKEN: secret
    steps:
    - run: echo safe
"""
        self.assertNotEqual(validate_workflow_text(source, "non-step.yml"), [])

    def test_rejects_missing_guard(self) -> None:
        source = """jobs:
  build:
    steps:
    - name: deploy
      env:
        ARTIFACTORY_ACCESS_TOKEN: secret
"""
        self.assertNotEqual(validate_workflow_text(source, "missing.yml"), [])

    def test_rejects_multiple_privileged_steps(self) -> None:
        source = f"""jobs:
  build:
    steps:
    - name: deploy one
      if: {UPSTREAM_ONLY_CONDITION}
      env:
        ARTIFACTORY_ACCESS_TOKEN: secret
    - name: deploy two
      if: {UPSTREAM_ONLY_CONDITION}
      run: curl https://openephys.jfrog.io/artifactory/example
"""
        self.assertNotEqual(validate_workflow_text(source, "multiple.yml"), [])

    def test_contract_checks_do_not_expand_the_heavy_test_workflow(self) -> None:
        source = HEAVY_TEST_WORKFLOW.read_text(encoding="utf-8")
        run_commands = [
            command
            for fields, _ in workflow_steps_from_text(source)
            for command in fields.get("run", [])
        ]
        self.assertNotIn(CONTRACT_TEST_COMMAND, run_commands)

    def test_dedicated_contract_workflow_covers_all_release_branches_and_inputs(self) -> None:
        self.assertTrue(CONTRACT_WORKFLOW.is_file())
        source = CONTRACT_WORKFLOW.read_text(encoding="utf-8")
        triggers = _mapping_block(source, "on", 0)
        pull_requests = _mapping_block(triggers, "pull_request", 2)
        pushes = _mapping_block(triggers, "push", 2)

        self.assertNotIn("    branches:", pull_requests)
        self.assertNotIn("    branches-ignore:", pull_requests)
        self.assertEqual(_sequence_values(pushes, "branches", 4), {
            "main",
            "development",
            "testing",
        })
        self.assertEqual(_sequence_values(pull_requests, "paths", 4), CONTRACT_PATHS)
        self.assertEqual(_sequence_values(pushes, "paths", 4), CONTRACT_PATHS)

        run_commands = [
            fields.get("run", [])
            for fields, _ in workflow_steps_from_text(source)
            if fields.get("run") == [CONTRACT_TEST_COMMAND]
        ]
        self.assertEqual(run_commands, [[CONTRACT_TEST_COMMAND]])


if __name__ == "__main__":
    unittest.main()
