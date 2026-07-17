from dataclasses import replace

import pytest

from open_ephys_agent_mcp.experiment.events import RunEvent, RunEventKind
from open_ephys_agent_mcp.experiment.state import (
    AcquisitionState,
    ExperimentReducer,
    RecordingState,
    RunSnapshot,
    RunState,
    TransitionRejected,
)


def apply(snapshot: RunSnapshot, *events: RunEvent) -> RunSnapshot:
    for event in events:
        snapshot = ExperimentReducer.apply(snapshot, event)
    return snapshot


def ready_snapshot() -> RunSnapshot:
    return RunSnapshot.ready("RUN001")


def test_run_requires_preflight_and_arm_before_first_part() -> None:
    snapshot = RunSnapshot.created("RUN001")

    snapshot = apply(
        snapshot,
        RunEvent.preflight_started("preflight-start"),
        RunEvent.preflight_passed("preflight-pass", evidence_id="preflight-proof"),
        RunEvent.run_armed("arm", evidence_id="approval-proof"),
    )

    assert snapshot.run_state is RunState.READY_FOR_PART
    assert snapshot.planned_part == 1


def test_human_preset_path_requires_bound_evidence() -> None:
    snapshot = ExperimentReducer.apply(
        ready_snapshot(), RunEvent.preset_confirmation_required("need-preset")
    )
    assert snapshot.run_state is RunState.WAITING_FOR_PRESET_CONFIRMATION

    with pytest.raises(TransitionRejected, match="PRESET_EVIDENCE_REQUIRED"):
        ExperimentReducer.apply(snapshot, RunEvent.preset_confirmed("confirmed"))

    confirmed = ExperimentReducer.apply(
        snapshot,
        RunEvent.preset_confirmed("confirmed", evidence_id="human-proof"),
    )
    assert confirmed.run_state is RunState.READY_FOR_PART


def test_resume_requires_fresh_reconciliation_evidence() -> None:
    paused = ExperimentReducer.apply(
        recording_snapshot(), RunEvent.pause_requested("pause")
    )

    with pytest.raises(TransitionRejected, match="RECONCILIATION_EVIDENCE_REQUIRED"):
        ExperimentReducer.apply(paused, RunEvent.resume_requested("resume"))

    resumed = ExperimentReducer.apply(
        paused,
        RunEvent.resume_requested("resume", evidence_id="fresh-native-state"),
    )
    assert resumed.run_state is RunState.RECORDING
    assert resumed.recording_state is RecordingState.ACTIVE


def recording_snapshot(part_index: int = 1, attempt: int = 1) -> RunSnapshot:
    snapshot = replace(
        ready_snapshot(), planned_part=part_index, current_attempt=attempt
    )
    return apply(
        snapshot,
        RunEvent.preset_confirmed(
            f"preset-{part_index}", evidence_id=f"preset-proof-{part_index}"
        ),
        RunEvent.part_prepared(f"prepared-{part_index}"),
        RunEvent.acquisition_starting(f"acq-start-{part_index}"),
        RunEvent.acquisition_active(f"acq-active-{part_index}"),
        RunEvent.recording_starting(f"rec-start-{part_index}"),
        RunEvent.recording_active(f"rec-active-{part_index}"),
    )


def verifying_snapshot(part_index: int = 1, attempt: int = 1) -> RunSnapshot:
    return apply(
        recording_snapshot(part_index, attempt),
        RunEvent.stop_requested(f"stop-{part_index}"),
        RunEvent.recording_inactive(f"rec-off-{part_index}"),
        RunEvent.acquisition_inactive(f"acq-off-{part_index}"),
        RunEvent.verification_started(f"verify-{part_index}"),
    )


def pass_part(snapshot: RunSnapshot, part_index: int) -> RunSnapshot:
    snapshot = replace(snapshot, planned_part=part_index)
    snapshot = apply(
        snapshot,
        RunEvent.preset_confirmed(
            f"preset-{part_index}", evidence_id=f"preset-proof-{part_index}"
        ),
        RunEvent.part_prepared(f"prepared-{part_index}"),
        RunEvent.acquisition_starting(f"acq-start-{part_index}"),
        RunEvent.acquisition_active(f"acq-active-{part_index}"),
        RunEvent.recording_starting(f"rec-start-{part_index}"),
        RunEvent.recording_active(f"rec-active-{part_index}"),
        RunEvent.stop_requested(f"stop-{part_index}"),
        RunEvent.recording_inactive(f"rec-off-{part_index}"),
        RunEvent.acquisition_inactive(f"acq-off-{part_index}"),
        RunEvent.verification_started(f"verify-{part_index}"),
        RunEvent.qc_passed(f"pass-{part_index}", evidence_id=f"qc-{part_index}"),
    )
    return snapshot


def test_run_cannot_complete_until_all_eight_parts_pass() -> None:
    snapshot = replace(
        ready_snapshot(),
        run_state=RunState.RUN_REVIEW,
        completed_parts=tuple(range(1, 8)),
        planned_part=8,
    )

    with pytest.raises(TransitionRejected, match="PARTS_INCOMPLETE"):
        ExperimentReducer.apply(snapshot, RunEvent.run_review_approved("review"))


def test_one_sequence_completes_only_after_all_eight_parts_pass() -> None:
    snapshot = ready_snapshot()
    for part_index in range(1, 9):
        snapshot = pass_part(snapshot, part_index)

    assert snapshot.run_state is RunState.RUN_REVIEW
    assert snapshot.completed_parts == tuple(range(1, 9))

    snapshot = ExperimentReducer.apply(
        snapshot, RunEvent.run_review_approved("review")
    )
    assert snapshot.run_state is RunState.RUN_COMPLETE


def test_failed_attempt_does_not_advance_part() -> None:
    snapshot = replace(
        verifying_snapshot(part_index=3, attempt=1),
        completed_parts=(1, 2),
    )

    next_state = ExperimentReducer.apply(
        snapshot, RunEvent.qc_failed("failed-3", reason="DROPOUT")
    )

    assert next_state.run_state is RunState.PART_RETRY_REQUIRED
    assert next_state.planned_part == 3
    assert next_state.completed_parts == (1, 2)
    assert next_state.current_attempt == 2
    assert next_state.classified_nonpart_attempts[-1].attempt == 1
    assert next_state.classified_nonpart_attempts[-1].reason == "DROPOUT"


def test_pause_never_implies_stop() -> None:
    snapshot = recording_snapshot()

    paused = ExperimentReducer.apply(snapshot, RunEvent.pause_requested("pause"))

    assert paused.run_state is RunState.PAUSED
    assert paused.resume_state is RunState.RECORDING
    assert paused.recording_state is RecordingState.ACTIVE
    assert paused.acquisition_state is AcquisitionState.ACTIVE


def test_unknown_recording_state_cannot_accept_stop_result() -> None:
    snapshot = replace(
        recording_snapshot(), recording_state=RecordingState.UNKNOWN
    )

    with pytest.raises(TransitionRejected, match="RECORDING_NOT_PROVEN_ACTIVE"):
        ExperimentReducer.apply(snapshot, RunEvent.stop_requested("unsafe-stop"))


def test_replaying_identical_event_is_idempotent_but_conflict_is_rejected() -> None:
    snapshot = ready_snapshot()
    event = RunEvent.preset_confirmed("event-1", evidence_id="preset-proof")
    first = ExperimentReducer.apply(snapshot, event)

    assert ExperimentReducer.apply(first, event) == first

    conflict = RunEvent.preset_confirmed(
        "event-1", evidence_id="different-proof"
    )
    with pytest.raises(TransitionRejected, match="EVENT_ID_CONFLICT"):
        ExperimentReducer.apply(first, conflict)


def test_unresolved_recording_unit_blocks_run_completion() -> None:
    snapshot = replace(
        ready_snapshot(),
        run_state=RunState.RUN_REVIEW,
        completed_parts=tuple(range(1, 9)),
        unresolved_recording_units=("candidate-unit",),
    )

    with pytest.raises(TransitionRejected, match="UNRESOLVED_RECORDING_UNITS"):
        ExperimentReducer.apply(snapshot, RunEvent.run_review_approved("review"))


def test_reducer_declares_a_handler_for_every_event_kind() -> None:
    assert ExperimentReducer.supported_event_kinds() == frozenset(RunEventKind)
