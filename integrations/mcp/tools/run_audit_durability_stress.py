"""Run bounded, real-disk durability checks for the v0.0.1 audit ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
import secrets
import shutil
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


def run(*, root: Path, iterations: int) -> dict[str, object]:
    if iterations < 1:
        raise ValueError("iterations must be positive")
    root = root.resolve()
    if root == Path(root.anchor) or len(root.parts) < 3:
        raise ValueError(f"refusing unsafe stress root: {root}")
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

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

    persisted_paths = [
        database,
        seal_path,
        *[path for path in (root / "artifacts").iterdir() if path.is_file()],
    ]
    key_persisted = any(hmac_key in path.read_bytes() for path in persisted_paths)
    if key_persisted:
        raise AssertionError("caller-injected HMAC key was persisted")

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
    return {
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
        "hmac_key_persisted": key_persisted,
        "reopen_append_verify_seconds": round(reopen_seconds, 6),
        "total_seconds": round(elapsed_seconds, 6),
        "failure_count": 0,
        "scope_boundary": (
            f"Bounded {iterations}-iteration real-disk test; not a "
            "100-million-event gate. "
            "No Open Ephys process or device was started or contacted."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    evidence = run(root=args.root, iterations=args.iterations)
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(evidence, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
