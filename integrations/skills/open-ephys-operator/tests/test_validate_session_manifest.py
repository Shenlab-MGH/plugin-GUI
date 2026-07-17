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


def valid_run_manifest() -> dict[str, object]:
    parts = []
    for index in range(1, 9):
        start = 1 + (index - 1) * 96
        parts.append(
            {
                "part_index": index,
                "preset": f"All Shanks {start}-{start + 95}",
                "electrode_start": start,
                "electrode_end": start + 95,
                "target_duration_seconds": 180,
                "duration_tolerance_seconds": 2,
                "maximum_overrun_seconds": 15,
                "settling_seconds": 10,
                "directory_name": f"RUN001_M3_part{index:02}_{start}-{start + 95}",
            }
        )
    return {
        "schema_version": "oe-agent-run-manifest/v0.0.2",
        "run_id": "RUN001",
        "subject_id": "M3",
        "operator": "scientist-01",
        "protocol_id": "eight-block-v1",
        "created_at_utc": "2026-07-17T03:00:00Z",
        "experiment_root": "D:/OpenEphys/RUN001",
        "archive_root": "F:/EP-WP/RUN001",
        "hardware": {
            "gui_version": "1.0.2-agent-v0.0.2-experimental",
            "pxi_plugin_version": "1.0.3-API10",
            "imec_api_version": "3.70.3",
            "onebox_serial": "25110476",
            "probe_serial": "23409412544",
            "slot": 16,
            "port": 1,
            "dock": 1,
            "reference": "External",
        },
        "storage": {
            "minimum_free_bytes": 500_000_000_000,
            "reserve_bytes": 100_000_000_000,
            "poll_interval_seconds": 1,
        },
        "stream_expectations": [
            {
                "name": "ProbeA",
                "channel_count": 384,
                "sample_rate_hz": 30000.0,
                "recording_enabled": True,
                "events_required": True,
                "timestamps_required": True,
            }
        ],
        "qc_policy": {
            "criteria_id": "lab-eight-block-qc-v1",
            "scientist_review_required": True,
        },
        "authorizations": [
            {
                "approval_id": "approval-run001",
                "scope": "SUPERVISED_COMPLETE_RUN",
                "approver": "scientist-01",
                "expires_at_utc": "2026-07-18T03:00:00Z",
            }
        ],
        "parts": parts,
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


def test_accepts_complete_eight_part_run_manifest(tmp_path: Path) -> None:
    result = run_validator(tmp_path, valid_run_manifest())

    assert result.returncode == 0
    assert json.loads(result.stdout) == {
        "schema_version": "oe-agent-run-manifest/v0.0.2",
        "valid": True,
    }


@pytest.mark.parametrize(
    ("mutate", "code"),
    [
        (lambda manifest: manifest.update(parts=manifest["parts"][:7]), "PART_COUNT_NOT_EIGHT"),
        (
            lambda manifest: manifest["parts"][1].update(
                directory_name=manifest["parts"][0]["directory_name"]
            ),
            "DUPLICATE_PART_DIRECTORY",
        ),
        (
            lambda manifest: manifest["parts"][0].update(
                target_duration_seconds="2-3 minutes"
            ),
            "DURATION_NOT_CONCRETE",
        ),
    ],
    ids=["part-count", "duplicate-directory", "duration"],
)
def test_rejects_incomplete_run_manifest(
    tmp_path: Path,
    mutate,
    code: str,
) -> None:
    manifest = deepcopy(valid_run_manifest())
    mutate(manifest)

    result = run_validator(tmp_path, manifest)

    assert result.returncode == 1
    assert result.stdout == ""
    assert json.loads(result.stderr)["error_code"] == code
