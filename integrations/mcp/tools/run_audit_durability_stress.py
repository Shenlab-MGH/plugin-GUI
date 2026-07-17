"""Run bounded, real-disk durability checks for the v0.0.1 audit ledger."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import platform
import secrets
import shutil
import sqlite3
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

from open_ephys_agent_mcp.experiment.action_audit import ActionActor, ActionKind
from open_ephys_agent_mcp.experiment.durable_audit import (
    ArtifactIntegrityError,
    AuditSealError,
    ContentAddressedArtifacts,
    SqliteActionAuditStore,
    verify_seal,
)

SCRIPT_PATH = Path(__file__).resolve()
REPO_ROOT = SCRIPT_PATH.parents[3]
ROOT_NAME_PREFIX = "audit-durability-stress-"
ROOT_SENTINEL_NAME = ".open-ephys-agent-stress-root"
ROOT_SENTINEL_CONTENT = "open-ephys-agent audit durability stress root v1\n"


def canonical_utc_now() -> str:
    now = datetime.now(timezone.utc)
    base = now.strftime("%Y-%m-%dT%H:%M:%S")
    if now.microsecond:
        return f"{base}.{now.microsecond:06d}".rstrip("0") + "Z"
    return base + "Z"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def prepare_stress_root(
    root: Path,
    *,
    repo_root: Path = REPO_ROOT,
    safe_parent: Path | None = None,
) -> Path:
    root = root.resolve()
    repo_root = repo_root.resolve()
    home = Path.home().resolve()
    allowed_parents = {(repo_root / "Build-runtime").resolve()}
    if safe_parent is not None:
        approved = safe_parent.resolve()
        if not approved.is_dir() or approved in {
            repo_root,
            home,
            Path(approved.anchor),
        }:
            raise ValueError(f"refusing unsafe stress root: {root}")
        allowed_parents.add(approved)

    if (
        root in {repo_root, home, Path(root.anchor)}
        or root.parent not in allowed_parents
        or not root.name.startswith(ROOT_NAME_PREFIX)
        or root.name == ROOT_NAME_PREFIX
    ):
        raise ValueError(f"refusing unsafe stress root: {root}")

    sentinel = root / ROOT_SENTINEL_NAME
    if root.exists():
        if not root.is_dir() or sentinel.is_symlink() or not sentinel.is_file():
            raise ValueError(f"refusing to delete root without valid sentinel: {root}")
        if sentinel.read_text(encoding="utf-8") != ROOT_SENTINEL_CONTENT:
            raise ValueError(f"refusing to delete root with invalid sentinel: {root}")
        shutil.rmtree(root)

    root.mkdir(parents=True, exist_ok=False)
    sentinel.write_text(ROOT_SENTINEL_CONTENT, encoding="utf-8")
    return root


def _files_under(paths: list[Path]) -> list[Path]:
    files: set[Path] = set()
    for candidate in paths:
        candidate = candidate.resolve()
        if candidate.is_file():
            files.add(candidate)
        elif candidate.is_dir():
            files.update(path for path in candidate.rglob("*") if path.is_file())
    return sorted(files)


def assert_secret_not_persisted(
    secret: bytes,
    *,
    paths: list[Path],
    evidence: dict[str, object] | None = None,
) -> None:
    encodings = {
        "raw": secret,
        "hex": secret.hex().encode("ascii"),
        "hex-uppercase": secret.hex().upper().encode("ascii"),
        "base64": base64.b64encode(secret),
        "base64-urlsafe": base64.urlsafe_b64encode(secret),
    }
    payloads = [(str(path), path.read_bytes()) for path in _files_under(paths)]
    if evidence is not None:
        payloads.append(
            (
                "in-memory evidence",
                json.dumps(evidence, sort_keys=True).encode("utf-8"),
            )
        )
    for location, payload in payloads:
        for encoding, needle in encodings.items():
            if needle in payload:
                raise AssertionError(
                    f"caller-injected HMAC key persisted as {encoding} in {location}"
                )


def collect_provenance(
    *, repo_root: Path = REPO_ROOT, script_path: Path = SCRIPT_PATH
) -> dict[str, object]:
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    status = subprocess.run(
        ["git", "status", "--porcelain", "--untracked-files=normal"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return {
        "source_commit": commit,
        "source_dirty": bool(status.strip()),
        "script_sha256": sha256_file(script_path),
        "python_version": platform.python_version(),
        "sqlite_version": sqlite3.sqlite_version,
        "os_version": platform.platform(),
    }


def run(
    *,
    root: Path,
    iterations: int,
    repo_root: Path = REPO_ROOT,
    safe_parent: Path | None = None,
    evidence_path: Path | None = None,
) -> dict[str, object]:
    if iterations < 1:
        raise ValueError("iterations must be positive")
    root = prepare_stress_root(
        root,
        repo_root=repo_root,
        safe_parent=safe_parent,
    )

    database = root / "audit.db"
    artifact_store = ContentAddressedArtifacts(root / "artifacts")
    session_id = "durability-stress-session"
    run_id = "durability-stress-run"
    started = time.perf_counter()

    reopen_started = time.perf_counter()
    for index in range(iterations):
        with SqliteActionAuditStore(
            database,
            session_id=session_id,
            run_id=run_id,
        ) as store:
            before = store.verify()
            if before.event_count != index:
                raise AssertionError(
                    f"reopen {index}: expected {index} events, got "
                    f"{before.event_count}"
                )
            store.append(
                kind=ActionKind.OBSERVATION,
                timestamp_utc=canonical_utc_now(),
                monotonic_ns=index + 1,
                actor=ActionActor.SYSTEM,
                correlation_id=f"reopen-{index + 1:03d}",
                payload={"iteration": index + 1, "mode": "IDLE"},
            )
            after = store.verify()
            if after.event_count != index + 1:
                raise AssertionError(
                    f"append {index}: expected {index + 1} events, got "
                    f"{after.event_count}"
                )
    reopen_seconds = time.perf_counter() - reopen_started

    tampered_reference = artifact_store.put_bytes(
        b"artifact-before-tamper",
        media_type="application/octet-stream",
    )
    tampered_reference.path.write_bytes(b"artifact-after-tamper")
    artifact_tamper_detected = False
    try:
        artifact_store.verify(tampered_reference)
    except ArtifactIntegrityError as error:
        artifact_tamper_detected = str(error) == "ARTIFACT_HASH_MISMATCH"
    if not artifact_tamper_detected:
        raise AssertionError("real artifact tampering was not detected")

    seal_reference = artifact_store.put_bytes(
        b"artifact-bound-to-seal",
        media_type="application/octet-stream",
    )
    hmac_key = secrets.token_bytes(32)
    seal_path = root / "audit.seal.json"
    with SqliteActionAuditStore(
        database,
        session_id=session_id,
        run_id=run_id,
    ) as store:
        store.verify()
        seal = store.seal(
            seal_path,
            hmac_key=hmac_key,
            key_id="ephemeral-caller-injected-stress-key",
            created_utc=canonical_utc_now(),
            artifacts=[seal_reference],
        )
        verified = verify_seal(
            seal_path,
            hmac_key=hmac_key,
            records=store.records(),
            artifacts=[seal_reference],
        )
        if verified != seal:
            raise AssertionError("valid seal did not round-trip")

        tampered = json.loads(seal_path.read_text(encoding="utf-8"))
        tampered["terminal_hash"] = "f" * 64
        seal_path.write_text(
            json.dumps(
                tampered,
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
            ),
            encoding="utf-8",
        )
        seal_tamper_detected = False
        try:
            verify_seal(
                seal_path,
                hmac_key=hmac_key,
                records=store.records(),
                artifacts=[seal_reference],
            )
        except AuditSealError:
            seal_tamper_detected = True
        if not seal_tamper_detected:
            raise AssertionError("seal terminal-hash tampering was not detected")
        seal_path.write_text(seal.canonical_json(), encoding="utf-8")

    with SqliteActionAuditStore(
        database,
        session_id=session_id,
        run_id=run_id,
    ) as store:
        post_seal_report = store.verify()
        verify_seal(
            seal_path,
            hmac_key=hmac_key,
            records=store.records(),
            artifacts=[seal_reference],
        )

    elapsed_seconds = time.perf_counter() - started
    evidence = {
        "schema_version": "oe-agent-audit-durability-stress/v0.0.1",
        "created_utc": canonical_utc_now(),
        "work_root": str(root),
        "requested_reopen_iterations": iterations,
        "completed_reopen_iterations": iterations,
        "append_count": iterations,
        "final_event_count": post_seal_report.event_count,
        "terminal_hash": post_seal_report.terminal_hash,
        "database_sha256": sha256_file(database),
        "seal_sha256": sha256_file(seal_path),
        "bound_artifact_sha256": seal_reference.sha256,
        "artifact_tamper_detected": artifact_tamper_detected,
        "seal_verified_before_and_after_restart": True,
        "seal_tamper_detected": seal_tamper_detected,
        "hmac_key_persisted": False,
        "secret_scan_encodings": [
            "raw",
            "hex-lowercase",
            "hex-uppercase",
            "base64",
            "base64-urlsafe",
        ],
        "secret_scan_scope": [
            "audit.db",
            "audit.db-wal (when present)",
            "audit.db-shm (when present)",
            "audit.seal.json",
            "artifacts/**",
            "evidence JSON",
        ],
        "reopen_append_verify_seconds": round(reopen_seconds, 6),
        "total_seconds": round(elapsed_seconds, 6),
        "failure_count": 0,
        "scope_boundary": (
            f"Bounded {iterations}-iteration real-disk test; not a "
            "100-million-event gate. "
            "No Open Ephys process or device was started or contacted."
        ),
        **collect_provenance(repo_root=repo_root),
    }
    assert_secret_not_persisted(hmac_key, paths=[root], evidence=evidence)
    if evidence_path is not None:
        evidence_path = evidence_path.resolve()
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(
            json.dumps(evidence, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        assert_secret_not_persisted(hmac_key, paths=[root, evidence_path])
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--safe-parent", type=Path)
    args = parser.parse_args()
    evidence = run(
        root=args.root,
        iterations=args.iterations,
        safe_parent=args.safe_parent,
        evidence_path=args.evidence,
    )
    print(json.dumps(evidence, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
