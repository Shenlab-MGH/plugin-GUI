import json
import subprocess
import sys
from copy import deepcopy
from pathlib import Path

import pytest


SKILL = Path(__file__).resolve().parents[1]
VALIDATOR = SKILL / "scripts" / "validate-session-manifest.py"


def valid_report() -> dict[str, object]:
    return {
        "schema_version": "oe-agent-observation-report/v0.0.1",
        "status": "OBSERVATION_ONLY",
        "native": {
            "session_id": "session-001",
            "revision": 7,
            "mode": "IDLE",
            "gui_version": "1.0.2",
        },
        "preflight": {
            "pass": True,
            "checks": [
                {"name": "online", "pass": True},
                {"name": "mode_known", "pass": True},
            ],
        },
        "shanks": [1, 2, 3, 4, 5, 6, 7, 8],
        "summary": "Observed IDLE state; no mutation was available or attempted.",
    }


def run_validator(tmp_path: Path, report: dict[str, object]) -> subprocess.CompletedProcess[str]:
    path = tmp_path / "report.json"
    path.write_text(json.dumps(report), encoding="utf-8")
    return subprocess.run(
        [sys.executable, str(VALIDATOR), str(path)],
        capture_output=True,
        text=True,
        check=False,
    )


def test_accepts_valid_observation_report(tmp_path: Path) -> None:
    result = run_validator(tmp_path, valid_report())

    assert result.returncode == 0
    assert json.loads(result.stdout) == {"valid": True}
    assert result.stderr == ""


@pytest.mark.parametrize(
    ("mutate", "code"),
    [
        (lambda report: report.update(status="SUCCESS"), "SUCCESS_NOT_SUPPORTED"),
        (lambda report: report.update(shanks=[0, 1]), "INVALID_SHANKS"),
        (
            lambda report: report.update(OE_AGENT_TOKEN="secret-value"),
            "SECRET_FIELD_FORBIDDEN",
        ),
        (
            lambda report: report["native"].pop("session_id"),
            "INVALID_NATIVE_IDENTITY",
        ),
        (lambda report: report.update(extra=True), "UNKNOWN_TOP_LEVEL_FIELD"),
    ],
    ids=["false-success", "shank-range", "secret", "identity", "unknown-field"],
)
def test_rejects_unsafe_or_incomplete_report(
    tmp_path: Path,
    mutate,
    code: str,
) -> None:
    report = deepcopy(valid_report())
    mutate(report)

    result = run_validator(tmp_path, report)

    assert result.returncode == 1
    assert result.stdout == ""
    assert json.loads(result.stderr)["error_code"] == code
