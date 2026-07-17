"""Pure fail-closed state reducer for the complete eight-part run."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum

from .events import RunEvent, RunEventKind


class RunState(str, Enum):
    DRAFT = "DRAFT"
    PREFLIGHT = "PREFLIGHT"
    WAITING_FOR_ARM = "WAITING_FOR_ARM"
    READY_FOR_PART = "READY_FOR_PART"
    WAITING_FOR_PRESET_CONFIRMATION = "WAITING_FOR_PRESET_CONFIRMATION"
    PART_PREPARED = "PART_PREPARED"
    ACQUISITION_STARTING = "ACQUISITION_STARTING"
    SETTLING = "SETTLING"
    RECORDING_STARTING = "RECORDING_STARTING"
    RECORDING = "RECORDING"
    RECORDING_STOPPING = "RECORDING_STOPPING"
    VERIFYING_PART = "VERIFYING_PART"
    PART_PASSED = "PART_PASSED"
    PART_RETRY_REQUIRED = "PART_RETRY_REQUIRED"
    RUN_REVIEW = "RUN_REVIEW"
    RUN_COMPLETE = "RUN_COMPLETE"
    PAUSED = "PAUSED"
    FAULTED = "FAULTED"
    MANUAL_TAKEOVER_REQUIRED = "MANUAL_TAKEOVER_REQUIRED"


class AcquisitionState(str, Enum):
    UNKNOWN = "unknown"
    INACTIVE = "inactive"
    STARTING = "starting"
    ACTIVE = "active"
    STOPPING = "stopping"
    ERROR = "error"


class RecordingState(str, Enum):
    UNKNOWN = "unknown"
    INACTIVE = "inactive"
    STARTING = "starting"
    ACTIVE = "active"
    STOPPING = "stopping"
    ERROR = "error"


class TransitionRejected(ValueError):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class ClassifiedAttempt:
    part_index: int
    attempt: int
    reason: str


@dataclass(frozen=True)
class AppliedEvent:
    event_id: str
    fingerprint: tuple[str, str | None, str | None]


@dataclass(frozen=True)
class RunSnapshot:
    run_id: str
    run_state: RunState
    planned_part: int
    current_attempt: int
    completed_parts: tuple[int, ...]
    classified_nonpart_attempts: tuple[ClassifiedAttempt, ...]
    unresolved_recording_units: tuple[str, ...]
    acquisition_state: AcquisitionState
    recording_state: RecordingState
    resume_state: RunState | None = None
    preset_evidence_id: str | None = None
    applied_events: tuple[AppliedEvent, ...] = ()

    @classmethod
    def created(cls, run_id: str) -> "RunSnapshot":
        return cls(
            run_id=run_id,
            run_state=RunState.DRAFT,
            planned_part=1,
            current_attempt=1,
            completed_parts=(),
            classified_nonpart_attempts=(),
            unresolved_recording_units=(),
            acquisition_state=AcquisitionState.UNKNOWN,
            recording_state=RecordingState.UNKNOWN,
        )

    @classmethod
    def ready(cls, run_id: str) -> "RunSnapshot":
        return cls(
            run_id=run_id,
            run_state=RunState.READY_FOR_PART,
            planned_part=1,
            current_attempt=1,
            completed_parts=(),
            classified_nonpart_attempts=(),
            unresolved_recording_units=(),
            acquisition_state=AcquisitionState.INACTIVE,
            recording_state=RecordingState.INACTIVE,
        )


class ExperimentReducer:
    @staticmethod
    def supported_event_kinds() -> frozenset[RunEventKind]:
        return frozenset(
            {
                RunEventKind.PREFLIGHT_STARTED,
                RunEventKind.PREFLIGHT_PASSED,
                RunEventKind.RUN_ARMED,
                RunEventKind.PRESET_CONFIRMATION_REQUIRED,
                RunEventKind.PRESET_CONFIRMED,
                RunEventKind.PART_PREPARED,
                RunEventKind.ACQUISITION_STARTING,
                RunEventKind.ACQUISITION_ACTIVE,
                RunEventKind.RECORDING_STARTING,
                RunEventKind.RECORDING_ACTIVE,
                RunEventKind.STOP_REQUESTED,
                RunEventKind.RECORDING_INACTIVE,
                RunEventKind.ACQUISITION_INACTIVE,
                RunEventKind.VERIFICATION_STARTED,
                RunEventKind.QC_PASSED,
                RunEventKind.QC_FAILED,
                RunEventKind.PAUSE_REQUESTED,
                RunEventKind.RESUME_REQUESTED,
                RunEventKind.RUN_REVIEW_APPROVED,
            }
        )

    @staticmethod
    def _reject(code: str) -> None:
        raise TransitionRejected(code)

    @classmethod
    def _commit(
        cls, snapshot: RunSnapshot, event: RunEvent, **changes: object
    ) -> RunSnapshot:
        applied = AppliedEvent(event.event_id, event.fingerprint)
        return replace(
            snapshot,
            **changes,
            applied_events=snapshot.applied_events + (applied,),
        )

    @classmethod
    def apply(cls, snapshot: RunSnapshot, event: RunEvent) -> RunSnapshot:
        if not event.event_id or len(event.event_id) > 128:
            cls._reject("INVALID_EVENT_ID")
        for applied in snapshot.applied_events:
            if applied.event_id == event.event_id:
                if applied.fingerprint == event.fingerprint:
                    return snapshot
                cls._reject("EVENT_ID_CONFLICT")

        kind = event.kind
        state = snapshot.run_state

        if kind is RunEventKind.PREFLIGHT_STARTED:
            if state is not RunState.DRAFT:
                cls._reject("PREFLIGHT_NOT_ALLOWED")
            return cls._commit(
                snapshot, event, run_state=RunState.PREFLIGHT
            )

        if kind is RunEventKind.PREFLIGHT_PASSED:
            if state is not RunState.PREFLIGHT or not event.evidence_id:
                cls._reject("PREFLIGHT_EVIDENCE_REQUIRED")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.WAITING_FOR_ARM,
                acquisition_state=AcquisitionState.INACTIVE,
                recording_state=RecordingState.INACTIVE,
            )

        if kind is RunEventKind.RUN_ARMED:
            if state is not RunState.WAITING_FOR_ARM or not event.evidence_id:
                cls._reject("ARM_APPROVAL_REQUIRED")
            return cls._commit(
                snapshot, event, run_state=RunState.READY_FOR_PART
            )

        if kind is RunEventKind.PRESET_CONFIRMATION_REQUIRED:
            if state not in {RunState.READY_FOR_PART, RunState.PART_RETRY_REQUIRED}:
                cls._reject("PRESET_CONFIRMATION_NOT_ALLOWED")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.WAITING_FOR_PRESET_CONFIRMATION,
                preset_evidence_id=None,
            )

        if kind is RunEventKind.PAUSE_REQUESTED:
            if state in {RunState.RUN_COMPLETE, RunState.FAULTED}:
                cls._reject("TERMINAL_RUN")
            if state is RunState.PAUSED:
                return cls._commit(snapshot, event)
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.PAUSED,
                resume_state=state,
            )

        if kind is RunEventKind.RESUME_REQUESTED:
            if state is not RunState.PAUSED or snapshot.resume_state is None:
                cls._reject("RUN_NOT_PAUSED")
            if not event.evidence_id:
                cls._reject("RECONCILIATION_EVIDENCE_REQUIRED")
            return cls._commit(
                snapshot,
                event,
                run_state=snapshot.resume_state,
                resume_state=None,
            )

        if kind is RunEventKind.PRESET_CONFIRMED:
            if state not in {
                RunState.READY_FOR_PART,
                RunState.PART_RETRY_REQUIRED,
                RunState.WAITING_FOR_PRESET_CONFIRMATION,
            }:
                cls._reject("PRESET_CONFIRMATION_NOT_EXPECTED")
            if not event.evidence_id:
                cls._reject("PRESET_EVIDENCE_REQUIRED")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.READY_FOR_PART,
                preset_evidence_id=event.evidence_id,
            )

        if kind is RunEventKind.PART_PREPARED:
            if state is not RunState.READY_FOR_PART or not snapshot.preset_evidence_id:
                cls._reject("PART_NOT_READY_FOR_PREPARATION")
            return cls._commit(
                snapshot, event, run_state=RunState.PART_PREPARED
            )

        if kind is RunEventKind.ACQUISITION_STARTING:
            if (
                state is not RunState.PART_PREPARED
                or snapshot.acquisition_state is not AcquisitionState.INACTIVE
                or snapshot.recording_state is not RecordingState.INACTIVE
            ):
                cls._reject("ACQUISITION_START_NOT_ALLOWED")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.ACQUISITION_STARTING,
                acquisition_state=AcquisitionState.STARTING,
            )

        if kind is RunEventKind.ACQUISITION_ACTIVE:
            if (
                state is not RunState.ACQUISITION_STARTING
                or snapshot.acquisition_state is not AcquisitionState.STARTING
            ):
                cls._reject("ACQUISITION_ACTIVE_NOT_EXPECTED")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.SETTLING,
                acquisition_state=AcquisitionState.ACTIVE,
            )

        if kind is RunEventKind.RECORDING_STARTING:
            if (
                state is not RunState.SETTLING
                or snapshot.acquisition_state is not AcquisitionState.ACTIVE
                or snapshot.recording_state is not RecordingState.INACTIVE
            ):
                cls._reject("RECORDING_START_NOT_ALLOWED")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.RECORDING_STARTING,
                recording_state=RecordingState.STARTING,
            )

        if kind is RunEventKind.RECORDING_ACTIVE:
            if (
                state is not RunState.RECORDING_STARTING
                or snapshot.recording_state is not RecordingState.STARTING
            ):
                cls._reject("RECORDING_ACTIVE_NOT_EXPECTED")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.RECORDING,
                recording_state=RecordingState.ACTIVE,
            )

        if kind is RunEventKind.STOP_REQUESTED:
            if (
                state is not RunState.RECORDING
                or snapshot.recording_state is not RecordingState.ACTIVE
            ):
                cls._reject("RECORDING_NOT_PROVEN_ACTIVE")
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.RECORDING_STOPPING,
                recording_state=RecordingState.STOPPING,
            )

        if kind is RunEventKind.RECORDING_INACTIVE:
            if (
                state is not RunState.RECORDING_STOPPING
                or snapshot.recording_state is not RecordingState.STOPPING
            ):
                cls._reject("RECORDING_INACTIVE_NOT_EXPECTED")
            return cls._commit(
                snapshot, event, recording_state=RecordingState.INACTIVE
            )

        if kind is RunEventKind.ACQUISITION_INACTIVE:
            if (
                state is not RunState.RECORDING_STOPPING
                or snapshot.recording_state is not RecordingState.INACTIVE
                or snapshot.acquisition_state is not AcquisitionState.ACTIVE
            ):
                cls._reject("ACQUISITION_INACTIVE_NOT_EXPECTED")
            return cls._commit(
                snapshot, event, acquisition_state=AcquisitionState.INACTIVE
            )

        if kind is RunEventKind.VERIFICATION_STARTED:
            if (
                state is not RunState.RECORDING_STOPPING
                or snapshot.recording_state is not RecordingState.INACTIVE
                or snapshot.acquisition_state is not AcquisitionState.INACTIVE
            ):
                cls._reject("VERIFICATION_NOT_READY")
            return cls._commit(
                snapshot, event, run_state=RunState.VERIFYING_PART
            )

        if kind is RunEventKind.QC_PASSED:
            if state is not RunState.VERIFYING_PART or not event.evidence_id:
                cls._reject("QC_PASS_NOT_ALLOWED")
            completed = snapshot.completed_parts + (snapshot.planned_part,)
            if completed != tuple(range(1, snapshot.planned_part + 1)):
                cls._reject("PART_SEQUENCE_INVALID")
            final_part = snapshot.planned_part == 8
            return cls._commit(
                snapshot,
                event,
                run_state=(
                    RunState.RUN_REVIEW if final_part else RunState.READY_FOR_PART
                ),
                planned_part=(8 if final_part else snapshot.planned_part + 1),
                current_attempt=1,
                completed_parts=completed,
                preset_evidence_id=None,
            )

        if kind is RunEventKind.QC_FAILED:
            if state is not RunState.VERIFYING_PART or not event.reason:
                cls._reject("QC_FAILURE_NOT_ALLOWED")
            attempt = ClassifiedAttempt(
                snapshot.planned_part, snapshot.current_attempt, event.reason
            )
            return cls._commit(
                snapshot,
                event,
                run_state=RunState.PART_RETRY_REQUIRED,
                current_attempt=snapshot.current_attempt + 1,
                classified_nonpart_attempts=(
                    snapshot.classified_nonpart_attempts + (attempt,)
                ),
                preset_evidence_id=None,
            )

        if kind is RunEventKind.RUN_REVIEW_APPROVED:
            if state is not RunState.RUN_REVIEW:
                cls._reject("RUN_NOT_READY_FOR_REVIEW")
            if snapshot.completed_parts != tuple(range(1, 9)):
                cls._reject("PARTS_INCOMPLETE")
            if snapshot.unresolved_recording_units:
                cls._reject("UNRESOLVED_RECORDING_UNITS")
            return cls._commit(
                snapshot, event, run_state=RunState.RUN_COMPLETE
            )

        cls._reject("EVENT_NOT_ALLOWED")
