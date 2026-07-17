from datetime import UTC, datetime

import pytest

from open_ephys_agent_mcp.experiment.preset import (
    PresetCapability,
    PresetConfirmation,
    PresetConfirmationError,
    PresetEvidence,
    classify_preset_capability,
)


def test_pixel_only_observation_is_unverifiable():
    result = classify_preset_capability(
        PresetEvidence(pixel_match="All Shanks 97-192")
    )
    assert result is PresetCapability.UNVERIFIABLE


@pytest.mark.parametrize(
    ("evidence", "expected"),
    [
        (
            PresetEvidence(
                semantic_set=True,
                semantic_readback="All Shanks 97-192",
                expected_preset="All Shanks 97-192",
            ),
            PresetCapability.SEMANTIC_SET_AND_READBACK,
        ),
        (
            PresetEvidence(
                accessibility_set=True,
                accessibility_readback="All Shanks 97-192",
                expected_preset="All Shanks 97-192",
            ),
            PresetCapability.ACCESSIBLE_SET_AND_READBACK,
        ),
        (
            PresetEvidence(
                human_confirmation_id="confirm-1",
                human_operator="scientist-1",
                exact_displayed_text="All Shanks 97-192",
                expected_preset="All Shanks 97-192",
            ),
            PresetCapability.HUMAN_CONFIRMED,
        ),
    ],
)
def test_only_exact_set_readback_or_bound_human_evidence_passes(
    evidence, expected
):
    assert classify_preset_capability(evidence) is expected


def confirmed_preset() -> PresetConfirmation:
    return PresetConfirmation(
        confirmation_id="confirm-1",
        run_id="run-1",
        part_index=2,
        preset="All Shanks 97-192",
        probe_serial="23299804124",
        configuration_revision=7,
        evidence_sha256="a" * 64,
        operator="scientist-1",
        confirmed_at=datetime.now(UTC),
    )


def test_human_confirmation_is_invalidated_by_probe_change():
    with pytest.raises(PresetConfirmationError, match="PROBE_IDENTITY_CHANGED"):
        confirmed_preset().verify(
            run_id="run-1",
            part_index=2,
            preset="All Shanks 97-192",
            probe_serial="23409412544",
            configuration_revision=7,
        )


@pytest.mark.parametrize(
    ("change", "code"),
    [
        ({"run_id": "run-2"}, "RUN_CHANGED"),
        ({"part_index": 3}, "PART_CHANGED"),
        ({"preset": "All Shanks 193-288"}, "PRESET_CHANGED"),
        ({"configuration_revision": 8}, "CONFIGURATION_CHANGED"),
    ],
)
def test_human_confirmation_is_bound_to_complete_context(change, code):
    context = {
        "run_id": "run-1",
        "part_index": 2,
        "preset": "All Shanks 97-192",
        "probe_serial": "23299804124",
        "configuration_revision": 7,
    }
    context.update(change)
    with pytest.raises(PresetConfirmationError, match=code):
        confirmed_preset().verify(**context)
