"""Reproducible qualification evaluator boundary and fixed-seed batch."""

from __future__ import annotations

import argparse
from dataclasses import fields, replace
from hashlib import sha256
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
from typing import Any


DEFAULT_SEED = 0xC0DE1701
DEFAULT_RANDOM_CASES = 100_000
EXPECTED_BOUNDARY_CASES = 502
ROOT = Path(__file__).resolve().parents[2]
MCP_SOURCE = ROOT / "integrations" / "mcp" / "src"
sys.path.insert(0, str(MCP_SOURCE))

from open_ephys_agent_mcp.experiment.qualification import (  # noqa: E402
    QualificationEvidence,
    QualificationProfile,
    evaluate_qualification,
    release_profile_v0_0_1,
)


VOLUME_CODES = {
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
ZERO_CODES = {
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
INVALID_VALUES: tuple[Any, ...] = (
    True, False, 0.0, 1.0, -1.0, "0", "1", None, [], {}, -1
)
INVALID_MANIFESTS: tuple[Any, ...] = (
    None, "", "A" * 64, "g" * 64, "a" * 63, "a" * 65, 0, True
)


def passing_evidence(profile: QualificationProfile) -> QualificationEvidence:
    values = {name: getattr(profile, name) for name in VOLUME_CODES}
    values.update({name: 0 for name in ZERO_CODES})
    return QualificationEvidence(
        **values,
        evidence_manifest_hash="a" * 64,
        first_failure_records_complete=True,
    )


def oracle_evaluate(
    profile: QualificationProfile, evidence: QualificationEvidence
) -> tuple[str, tuple[str, ...]]:
    """Independent specification oracle; does not call the production evaluator."""
    failures: list[str] = []
    for name, insufficient_code in VOLUME_CODES.items():
        required = getattr(profile, name)
        observed = getattr(evidence, name)
        required_valid = type(required) is int and required >= 0
        observed_valid = type(observed) is int and observed >= 0
        if not required_valid:
            failures.append(f"INVALID_PROFILE_{name.upper()}")
        if not observed_valid:
            failures.append(f"INVALID_EVIDENCE_{name.upper()}")
        if required_valid and observed_valid and observed < required:
            failures.append(insufficient_code)
    for name, nonzero_code in ZERO_CODES.items():
        observed = getattr(evidence, name)
        if type(observed) is not int or observed < 0:
            failures.append(f"INVALID_EVIDENCE_{name.upper()}")
        elif observed > 0:
            failures.append(nonzero_code)
    manifest = evidence.evidence_manifest_hash
    if not (
        isinstance(manifest, str)
        and len(manifest) == 64
        and all(character in "0123456789abcdef" for character in manifest)
    ):
        failures.append("MISSING_EVIDENCE_MANIFEST_HASH")
    complete = evidence.first_failure_records_complete
    if type(complete) is not bool:
        failures.append("INVALID_FIRST_FAILURE_RECORDS_COMPLETE")
    elif not complete:
        failures.append("INCOMPLETE_FIRST_FAILURE_RECORDS")
    return ("PASS" if not failures else "NO_GO", tuple(failures))


def boundary_cases(
    profile: QualificationProfile, baseline: QualificationEvidence
) -> list[tuple[str, QualificationProfile, QualificationEvidence]]:
    cases = [("baseline", profile, baseline)]
    for name in VOLUME_CODES:
        for index, value in enumerate(INVALID_VALUES):
            cases.append((f"profile-invalid:{name}:{index}", replace(profile, **{name: value}), baseline))
            cases.append((f"evidence-invalid:{name}:{index}", profile, replace(baseline, **{name: value})))
        cases.append((f"below-threshold:{name}", profile, replace(baseline, **{name: getattr(profile, name) - 1})))
    for name in ZERO_CODES:
        for index, value in enumerate(INVALID_VALUES):
            cases.append((f"zero-invalid:{name}:{index}", profile, replace(baseline, **{name: value})))
        cases.append((f"zero-positive:{name}", profile, replace(baseline, **{name: 1})))
    for index, value in enumerate(INVALID_MANIFESTS):
        cases.append((f"manifest-invalid:{index}", profile, replace(baseline, evidence_manifest_hash=value)))
    for index, value in enumerate((0, 1, None, "true")):
        cases.append((f"first-failure-invalid:{index}", profile, replace(baseline, first_failure_records_complete=value)))
    cases.append(("first-failure-incomplete", profile, replace(baseline, first_failure_records_complete=False)))
    if len(cases) != EXPECTED_BOUNDARY_CASES:
        raise RuntimeError(f"boundary case construction drifted: {len(cases)}")
    return cases


def random_cases(
    profile: QualificationProfile,
    baseline: QualificationEvidence,
    seed: int,
    count: int,
):
    rng = random.Random(seed)
    volume_names = tuple(VOLUME_CODES)
    zero_names = tuple(ZERO_CODES)
    for index in range(count):
        family = rng.randrange(7)
        candidate_profile, candidate_evidence = profile, baseline
        if family == 0:
            name = rng.choice(volume_names)
            candidate_profile = replace(profile, **{name: rng.choice(INVALID_VALUES)})
        elif family == 1:
            name = rng.choice(volume_names)
            candidate_evidence = replace(baseline, **{name: rng.choice(INVALID_VALUES)})
        elif family == 2:
            name = rng.choice(zero_names)
            candidate_evidence = replace(baseline, **{name: rng.choice(INVALID_VALUES)})
        elif family == 3:
            name = rng.choice(volume_names)
            candidate_evidence = replace(baseline, **{name: getattr(profile, name) - 1})
        elif family == 4:
            name = rng.choice(zero_names)
            candidate_evidence = replace(baseline, **{name: rng.randint(1, 1_000_000)})
        elif family == 5:
            candidate_evidence = replace(baseline, evidence_manifest_hash=rng.choice(INVALID_MANIFESTS))
        else:
            candidate_evidence = replace(
                baseline,
                first_failure_records_complete=rng.choice((0, 1, None, "true", False)),
            )
        yield f"random:{index}:{family}", candidate_profile, candidate_evidence


def git_commit() -> str:
    return subprocess.check_output(
        ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True
    ).strip()


def run(seed: int, random_count: int) -> dict[str, Any]:
    profile = release_profile_v0_0_1()
    baseline = passing_evidence(profile)
    failures: list[dict[str, Any]] = []
    ledger = sha256()
    cases = boundary_cases(profile, baseline)
    generated = list(random_cases(profile, baseline, seed, random_count))
    for tag, candidate_profile, candidate_evidence in (*cases, *generated):
        expected_decision, expected_codes = oracle_evaluate(candidate_profile, candidate_evidence)
        actual = evaluate_qualification(candidate_profile, candidate_evidence)
        record = {
            "tag": tag,
            "expected_decision": expected_decision,
            "expected_codes": expected_codes,
            "actual_decision": actual.decision,
            "actual_codes": actual.failure_codes,
        }
        ledger.update((json.dumps(record, sort_keys=True) + "\n").encode("utf-8"))
        if actual.decision != expected_decision or actual.failure_codes != expected_codes:
            failures.append(record)
    report: dict[str, Any] = {
        "schema": "open-ephys-agent/channel-c-qualification/v1",
        "seed": seed,
        "boundary_cases": len(cases),
        "random_cases": random_count,
        "total_cases": len(cases) + random_count,
        "source_commit": git_commit(),
        "script_sha256": sha256(Path(__file__).read_bytes()).hexdigest(),
        "decision": "PASS" if not failures else "FAIL",
        "failures": failures,
        "ledger_sha256": ledger.hexdigest(),
    }
    canonical = json.dumps(report, sort_keys=True, separators=(",", ":")).encode("utf-8")
    report["result_sha256"] = sha256(canonical).hexdigest()
    return report


def write_json(path: Path, report: dict[str, Any], force: bool) -> None:
    if path.suffix.lower() != ".json":
        raise ValueError("output must use a .json extension")
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing output: {path}")
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent, delete=False, suffix=".tmp"
    ) as handle:
        temporary = Path(handle.name)
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=DEFAULT_SEED)
    parser.add_argument("--random-cases", type=int, default=DEFAULT_RANDOM_CASES)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    if args.random_cases < 0:
        parser.error("--random-cases must be non-negative")
    report = run(args.seed, args.random_cases)
    write_json(args.output.resolve(), report, args.force)
    print(json.dumps(report, sort_keys=True))
    return 0 if report["decision"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
