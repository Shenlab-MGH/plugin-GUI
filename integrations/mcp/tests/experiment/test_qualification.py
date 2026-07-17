from __future__ import annotations

from open_ephys_agent_mcp.experiment.qualification import (
    QualificationEvidence,
    QualificationProfile,
    evaluate_qualification,
    release_profile_v0_0_1,
)


def passing_evidence(profile: QualificationProfile) -> QualificationEvidence:
    return QualificationEvidence(
        model_events=profile.model_events,
        golden_traces=profile.golden_traces,
        crash_cases=profile.crash_cases,
        corruption_cases=profile.corruption_cases,
        clock_cases=profile.clock_cases,
        video_cases=profile.video_cases,
        gui_cases=profile.gui_cases,
        secret_canaries=profile.secret_canaries,
        clean_eight_part_runs=profile.clean_eight_part_runs,
        faulted_eight_part_runs=profile.faulted_eight_part_runs,
        accelerated_source_sim_runs=profile.accelerated_source_sim_runs,
        nominal_source_sim_runs=profile.nominal_source_sim_runs,
        journal_soak_hours=profile.journal_soak_hours,
        capture_soak_hours=profile.capture_soak_hours,
        restart_cycles=profile.restart_cycles,
        supervised_device_runs=profile.supervised_device_runs,
        intact_replay_mismatches=0,
        undetected_corruptions=0,
        duplicate_scientific_mutations=0,
        false_complete_reports=0,
        takeover_mutations=0,
        blind_non_idempotent_retries=0,
        coordinate_fallback_mutations=0,
        plaintext_secret_leaks=0,
        failed_clean_runs=0,
        flaky_retries=0,
        evidence_manifest_hash="a" * 64,
        first_failure_records_complete=True,
    )


def test_release_profile_has_exact_large_scale_gates() -> None:
    profile = release_profile_v0_0_1()

    assert profile.model_events == 100_000_000
    assert profile.crash_cases == 20_000
    assert profile.clean_eight_part_runs == 100
    assert profile.faulted_eight_part_runs == 1_000
    assert profile.accelerated_source_sim_runs == 30
    assert profile.nominal_source_sim_runs == 10
    assert profile.journal_soak_hours == 24
    assert profile.capture_soak_hours == 4
    assert profile.supervised_device_runs == 3


def test_pass_requires_every_volume_and_zero_safety_violation() -> None:
    profile = release_profile_v0_0_1()
    report = evaluate_qualification(profile, passing_evidence(profile))

    assert report.decision == "PASS"
    assert report.failure_codes == ()


def test_insufficient_population_is_no_go() -> None:
    profile = release_profile_v0_0_1()
    evidence = passing_evidence(profile)
    evidence = QualificationEvidence(
        **{**evidence.__dict__, "model_events": profile.model_events - 1}
    )

    report = evaluate_qualification(profile, evidence)

    assert report.decision == "NO_GO"
    assert "INSUFFICIENT_MODEL_EVENTS" in report.failure_codes


def test_one_unsafe_event_or_flaky_retry_is_no_go() -> None:
    profile = release_profile_v0_0_1()
    evidence = passing_evidence(profile)
    evidence = QualificationEvidence(
        **{
            **evidence.__dict__,
            "takeover_mutations": 1,
            "plaintext_secret_leaks": 1,
            "flaky_retries": 1,
        }
    )

    report = evaluate_qualification(profile, evidence)

    assert report.decision == "NO_GO"
    assert "TAKEOVER_MUTATION_DETECTED" in report.failure_codes
    assert "PLAINTEXT_SECRET_LEAK" in report.failure_codes
    assert "FLAKY_RETRY_USED" in report.failure_codes


def test_missing_manifest_or_first_failure_record_is_no_go() -> None:
    profile = release_profile_v0_0_1()
    evidence = passing_evidence(profile)
    evidence = QualificationEvidence(
        **{
            **evidence.__dict__,
            "evidence_manifest_hash": "",
            "first_failure_records_complete": False,
        }
    )

    report = evaluate_qualification(profile, evidence)

    assert report.decision == "NO_GO"
    assert "MISSING_EVIDENCE_MANIFEST_HASH" in report.failure_codes
    assert "INCOMPLETE_FIRST_FAILURE_RECORDS" in report.failure_codes

