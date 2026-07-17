from copy import deepcopy

import pytest
from pydantic import ValidationError

from open_ephys_agent_mcp.experiment.manifest import RunManifest


PRESETS = [
    "All Shanks 1-96",
    "All Shanks 97-192",
    "All Shanks 193-288",
    "All Shanks 289-384",
    "All Shanks 385-480",
    "All Shanks 481-576",
    "All Shanks 577-672",
    "All Shanks 673-768",
]


def valid_manifest() -> dict[str, object]:
    parts = []
    for index, preset in enumerate(PRESETS, start=1):
        start = 1 + (index - 1) * 96
        parts.append(
            {
                "part_index": index,
                "preset": preset,
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
            },
            {
                "name": "OneBox-ADC",
                "channel_count": 12,
                "sample_rate_hz": 30300.5,
                "recording_enabled": True,
                "events_required": True,
                "timestamps_required": True,
            },
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


def test_default_actual_run_has_exactly_eight_ordered_parts() -> None:
    manifest = RunManifest.model_validate(valid_manifest())

    assert [part.preset for part in manifest.parts] == PRESETS
    assert [part.part_index for part in manifest.parts] == list(range(1, 9))


def test_rejects_duplicate_or_colliding_directory_names() -> None:
    payload = valid_manifest()
    payload["parts"][1]["directory_name"] = payload["parts"][0]["directory_name"]

    with pytest.raises(ValidationError, match="DUPLICATE_PART_DIRECTORY"):
        RunManifest.model_validate(payload)


def test_directory_collision_is_case_insensitive() -> None:
    payload = valid_manifest()
    payload["parts"][1]["directory_name"] = payload["parts"][0][
        "directory_name"
    ].lower()

    with pytest.raises(ValidationError, match="DUPLICATE_PART_DIRECTORY"):
        RunManifest.model_validate(payload)


def test_duration_is_concrete_and_parameterized_per_part() -> None:
    payload = valid_manifest()
    payload["parts"][3]["target_duration_seconds"] = 150

    manifest = RunManifest.model_validate(payload)

    assert manifest.parts[3].target_duration_seconds == 150
    assert manifest.parts[0].target_duration_seconds == 180


@pytest.mark.parametrize(
    ("directory_name", "error_code"),
    [
        ("../escape", "INVALID_NATIVE_DIRECTORY_NAME"),
        ("bad.name", "INVALID_NATIVE_DIRECTORY_NAME"),
        ("CON", "INVALID_NATIVE_DIRECTORY_NAME"),
        ("part01 (1)", "AUTO_SUFFIX_FORBIDDEN"),
    ],
)
def test_rejects_names_that_native_agent_mode_must_not_accept(
    directory_name: str,
    error_code: str,
) -> None:
    payload = valid_manifest()
    payload["parts"][0]["directory_name"] = directory_name

    with pytest.raises(ValidationError, match=error_code):
        RunManifest.model_validate(payload)


def test_rejects_unknown_or_secret_fields() -> None:
    unknown = valid_manifest()
    unknown["extra"] = True
    with pytest.raises(ValidationError):
        RunManifest.model_validate(unknown)

    secret = valid_manifest()
    secret["OE_AGENT_TOKEN"] = "must-not-enter-manifest"
    with pytest.raises(ValidationError):
        RunManifest.model_validate(secret)


def test_manifest_is_immutable_after_validation() -> None:
    manifest = RunManifest.model_validate(deepcopy(valid_manifest()))

    with pytest.raises(ValidationError):
        manifest.parts[0].target_duration_seconds = 120
