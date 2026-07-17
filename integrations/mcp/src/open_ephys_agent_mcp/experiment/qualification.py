"""Fail-closed evaluation of realistic simulation qualification evidence."""

from __future__ import annotations

import re
from dataclasses import dataclass


@dataclass(frozen=True)
class QualificationProfile:
    profile_id: str
    model_events: int
    golden_traces: int
    crash_cases: int
    corruption_cases: int
    clock_cases: int
    video_cases: int
    gui_cases: int
    secret_canaries: int
    clean_eight_part_runs: int
    faulted_eight_part_runs: int
    accelerated_source_sim_runs: int
    nominal_source_sim_runs: int
    journal_soak_hours: int
    capture_soak_hours: int
    restart_cycles: int
    supervised_device_runs: int


def release_profile_v0_0_1() -> QualificationProfile:
    return QualificationProfile(
        profile_id="oe-agent-qualification/release-v0.0.1",
        model_events=100_000_000,
        golden_traces=400,
        crash_cases=20_000,
        corruption_cases=10_000,
        clock_cases=8_600,
        video_cases=4_720,
        gui_cases=20_000,
        secret_canaries=50_000,
        clean_eight_part_runs=100,
        faulted_eight_part_runs=1_000,
        accelerated_source_sim_runs=30,
        nominal_source_sim_runs=10,
        journal_soak_hours=24,
        capture_soak_hours=4,
        restart_cycles=100,
        supervised_device_runs=3,
    )


@dataclass(frozen=True)
class QualificationEvidence:
    model_events: int
    golden_traces: int
    crash_cases: int
    corruption_cases: int
    clock_cases: int
    video_cases: int
    gui_cases: int
    secret_canaries: int
    clean_eight_part_runs: int
    faulted_eight_part_runs: int
    accelerated_source_sim_runs: int
    nominal_source_sim_runs: int
    journal_soak_hours: int
    capture_soak_hours: int
    restart_cycles: int
    supervised_device_runs: int
    intact_replay_mismatches: int
    undetected_corruptions: int
    duplicate_scientific_mutations: int
    false_complete_reports: int
    takeover_mutations: int
    blind_non_idempotent_retries: int
    coordinate_fallback_mutations: int
    plaintext_secret_leaks: int
    failed_clean_runs: int
    flaky_retries: int
    evidence_manifest_hash: str
    first_failure_records_complete: bool


@dataclass(frozen=True)
class QualificationReport:
    profile_id: str
    decision: str
    failure_codes: tuple[str, ...]


_VOLUME_FIELDS = {
    "model_events": "INSUFFICIENT_MODEL_EVENTS",
    "golden_traces": "INSUFFICIENT_GOLDEN_TRACES",
    "crash_cases": "INSUFFICIENT_CRASH_CASES",
    "corruption_cases": "INSUFFICIENT_CORRUPTION_CASES",
    "clock_cases": "INSUFFICIENT_CLOCK_CASES",
    "video_cases": "INSUFFICIENT_VIDEO_CASES",
    "gui_cases": "INSUFFICIENT_GUI_CASES",
    "secret_canaries": "INSUFFICIENT_SECRET_CANARIES",
    "clean_eight_part_runs": "INSUFFICIENT_CLEAN_EIGHT_PART_RUNS",
    "faulted_eight_part_runs": "INSUFFICIENT_FAULTED_EIGHT_PART_RUNS",
    "accelerated_source_sim_runs": "INSUFFICIENT_ACCELERATED_SOURCE_SIM_RUNS",
    "nominal_source_sim_runs": "INSUFFICIENT_NOMINAL_SOURCE_SIM_RUNS",
    "journal_soak_hours": "INSUFFICIENT_JOURNAL_SOAK_HOURS",
    "capture_soak_hours": "INSUFFICIENT_CAPTURE_SOAK_HOURS",
    "restart_cycles": "INSUFFICIENT_RESTART_CYCLES",
    "supervised_device_runs": "INSUFFICIENT_SUPERVISED_DEVICE_RUNS",
}

_ZERO_FIELDS = {
    "intact_replay_mismatches": "INTACT_REPLAY_MISMATCH",
    "undetected_corruptions": "UNDETECTED_CORRUPTION",
    "duplicate_scientific_mutations": "DUPLICATE_SCIENTIFIC_MUTATION",
    "false_complete_reports": "FALSE_COMPLETE_REPORT",
    "takeover_mutations": "TAKEOVER_MUTATION_DETECTED",
    "blind_non_idempotent_retries": "BLIND_NON_IDEMPOTENT_RETRY",
    "coordinate_fallback_mutations": "COORDINATE_FALLBACK_MUTATION",
    "plaintext_secret_leaks": "PLAINTEXT_SECRET_LEAK",
    "failed_clean_runs": "CLEAN_RUN_FAILURE",
    "flaky_retries": "FLAKY_RETRY_USED",
}


def evaluate_qualification(
    profile: QualificationProfile, evidence: QualificationEvidence
) -> QualificationReport:
    failures: list[str] = []
    for field, code in _VOLUME_FIELDS.items():
        required = getattr(profile, field)
        observed = getattr(evidence, field)
        profile_is_valid = type(required) is int and required >= 0
        evidence_is_valid = type(observed) is int and observed >= 0
        if not profile_is_valid:
            failures.append(f"INVALID_PROFILE_{field.upper()}")
        if not evidence_is_valid:
            failures.append(f"INVALID_EVIDENCE_{field.upper()}")
        if profile_is_valid and evidence_is_valid and observed < required:
            failures.append(code)
    for field, code in _ZERO_FIELDS.items():
        observed = getattr(evidence, field)
        if type(observed) is not int or observed < 0:
            failures.append(f"INVALID_EVIDENCE_{field.upper()}")
        elif observed != 0:
            failures.append(code)
    if not isinstance(evidence.evidence_manifest_hash, str) or not re.fullmatch(
        r"[0-9a-f]{64}", evidence.evidence_manifest_hash
    ):
        failures.append("MISSING_EVIDENCE_MANIFEST_HASH")
    if type(evidence.first_failure_records_complete) is not bool:
        failures.append("INVALID_FIRST_FAILURE_RECORDS_COMPLETE")
    elif not evidence.first_failure_records_complete:
        failures.append("INCOMPLETE_FIRST_FAILURE_RECORDS")
    return QualificationReport(
        profile_id=profile.profile_id,
        decision="PASS" if not failures else "NO_GO",
        failure_codes=tuple(failures),
    )
