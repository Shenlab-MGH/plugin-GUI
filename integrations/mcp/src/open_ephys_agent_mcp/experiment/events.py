"""Immutable events accepted by the complete experiment reducer."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class RunEventKind(str, Enum):
    PREFLIGHT_STARTED = "PREFLIGHT_STARTED"
    PREFLIGHT_PASSED = "PREFLIGHT_PASSED"
    RUN_ARMED = "RUN_ARMED"
    PRESET_CONFIRMATION_REQUIRED = "PRESET_CONFIRMATION_REQUIRED"
    PRESET_CONFIRMED = "PRESET_CONFIRMED"
    PART_PREPARED = "PART_PREPARED"
    ACQUISITION_STARTING = "ACQUISITION_STARTING"
    ACQUISITION_ACTIVE = "ACQUISITION_ACTIVE"
    RECORDING_STARTING = "RECORDING_STARTING"
    RECORDING_ACTIVE = "RECORDING_ACTIVE"
    STOP_REQUESTED = "STOP_REQUESTED"
    RECORDING_INACTIVE = "RECORDING_INACTIVE"
    ACQUISITION_INACTIVE = "ACQUISITION_INACTIVE"
    VERIFICATION_STARTED = "VERIFICATION_STARTED"
    QC_PASSED = "QC_PASSED"
    QC_FAILED = "QC_FAILED"
    PAUSE_REQUESTED = "PAUSE_REQUESTED"
    RESUME_REQUESTED = "RESUME_REQUESTED"
    RUN_REVIEW_APPROVED = "RUN_REVIEW_APPROVED"


@dataclass(frozen=True)
class RunEvent:
    event_id: str
    kind: RunEventKind
    evidence_id: str | None = None
    reason: str | None = None

    @property
    def fingerprint(self) -> tuple[str, str | None, str | None]:
        return (self.kind.value, self.evidence_id, self.reason)

    @classmethod
    def preset_confirmed(
        cls, event_id: str, *, evidence_id: str | None = None
    ) -> "RunEvent":
        return cls(event_id, RunEventKind.PRESET_CONFIRMED, evidence_id)

    @classmethod
    def preflight_started(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.PREFLIGHT_STARTED)

    @classmethod
    def preflight_passed(cls, event_id: str, *, evidence_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.PREFLIGHT_PASSED, evidence_id)

    @classmethod
    def run_armed(cls, event_id: str, *, evidence_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.RUN_ARMED, evidence_id)

    @classmethod
    def preset_confirmation_required(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.PRESET_CONFIRMATION_REQUIRED)

    @classmethod
    def part_prepared(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.PART_PREPARED)

    @classmethod
    def acquisition_starting(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.ACQUISITION_STARTING)

    @classmethod
    def acquisition_active(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.ACQUISITION_ACTIVE)

    @classmethod
    def recording_starting(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.RECORDING_STARTING)

    @classmethod
    def recording_active(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.RECORDING_ACTIVE)

    @classmethod
    def stop_requested(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.STOP_REQUESTED)

    @classmethod
    def recording_inactive(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.RECORDING_INACTIVE)

    @classmethod
    def acquisition_inactive(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.ACQUISITION_INACTIVE)

    @classmethod
    def verification_started(cls, event_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.VERIFICATION_STARTED)

    @classmethod
    def qc_passed(cls, event_id: str, *, evidence_id: str) -> "RunEvent":
        return cls(event_id, RunEventKind.QC_PASSED, evidence_id)

    @classmethod
    def qc_failed(cls, event_id: str, *, reason: str) -> "RunEvent":
        return cls(event_id, RunEventKind.QC_FAILED, reason=reason)

    @classmethod
    def pause_requested(cls, event_id: str = "pause") -> "RunEvent":
        return cls(event_id, RunEventKind.PAUSE_REQUESTED)

    @classmethod
    def resume_requested(
        cls, event_id: str = "resume", *, evidence_id: str | None = None
    ) -> "RunEvent":
        return cls(event_id, RunEventKind.RESUME_REQUESTED, evidence_id)

    @classmethod
    def run_review_approved(cls, event_id: str = "review") -> "RunEvent":
        return cls(event_id, RunEventKind.RUN_REVIEW_APPROVED)
