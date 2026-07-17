from __future__ import annotations

import sqlite3
from pathlib import Path

import pytest

from open_ephys_agent_mcp.experiment.action_audit import (
    ActionActor,
    ActionKind,
    AuditChainError,
)
from open_ephys_agent_mcp.experiment.durable_audit import (
    ArtifactIntegrityError,
    ContentAddressedArtifacts,
    SqliteActionAuditStore,
)


def append(store: SqliteActionAuditStore, kind: ActionKind, **kwargs: object):
    return store.append(
        kind=kind,
        actor=kwargs.pop("actor", ActionActor.AGENT),
        correlation_id=str(kwargs.pop("correlation_id", "corr-1")),
        payload=kwargs.pop("payload", {}),
        timestamp_utc=str(kwargs.pop("timestamp_utc", "2026-07-17T00:00:00Z")),
        monotonic_ns=int(kwargs.pop("monotonic_ns", store.event_count + 1)),
        **kwargs,
    )


def test_persists_committed_chain_and_resumes_sequence_after_restart(tmp_path: Path) -> None:
    path = tmp_path / "audit.db"
    with SqliteActionAuditStore(path, session_id="session-1", run_id="run-1") as store:
        first = append(store, ActionKind.OBSERVATION, payload={"mode": "IDLE"})
        second = append(store, ActionKind.DECISION, payload={"decision": "NO_ACTION"})
        assert store.event_count == 2
        assert store.verify().terminal_hash == second.event_hash

    with SqliteActionAuditStore(path, session_id="session-1", run_id="run-1") as store:
        assert [record.event_id for record in store.records()] == [
            first.event_id,
            second.event_id,
        ]
        third = append(store, ActionKind.OBSERVATION, payload={"mode": "IDLE"})
        assert third.sequence == 3
        assert third.previous_hash == second.event_hash


def test_refuses_to_open_a_tampered_committed_record(tmp_path: Path) -> None:
    path = tmp_path / "audit.db"
    with SqliteActionAuditStore(path, session_id="session-1", run_id="run-1") as store:
        append(store, ActionKind.OBSERVATION, payload={"mode": "IDLE"})

    with sqlite3.connect(path) as connection:
        connection.execute(
            "UPDATE audit_events SET canonical_json = replace(canonical_json, 'IDLE', 'RECORD')"
        )
        connection.commit()

    with pytest.raises(AuditChainError, match="EVENT_HASH_MISMATCH"):
        SqliteActionAuditStore(path, session_id="session-1", run_id="run-1")


def test_reports_unresolved_mutation_for_reconciliation_after_restart(tmp_path: Path) -> None:
    path = tmp_path / "audit.db"
    with SqliteActionAuditStore(path, session_id="session-1", run_id="run-1") as store:
        append(
            store,
            ActionKind.TOOL_INTENT,
            correlation_id="start-part-4",
            payload={"tool": "oe_start_part", "mutating": True},
        )
        append(
            store,
            ActionKind.NATIVE_REQUEST,
            actor=ActionActor.MCP,
            correlation_id="start-part-4",
            payload={"operation": "START_RECORDING"},
        )

    with SqliteActionAuditStore(path, session_id="session-1", run_id="run-1") as store:
        assert store.pending_mutation_correlations() == ("start-part-4",)


def test_content_addressed_artifact_is_atomic_and_verified(tmp_path: Path) -> None:
    artifacts = ContentAddressedArtifacts(tmp_path / "artifacts")
    reference = artifacts.put_bytes(b"frame-data", media_type="image/png")

    assert reference.sha256 == artifacts.verify(reference)
    assert reference.bytes == len(b"frame-data")
    assert reference.path.name == reference.sha256

    reference.path.write_bytes(b"changed")
    with pytest.raises(ArtifactIntegrityError, match="ARTIFACT_HASH_MISMATCH"):
        artifacts.verify(reference)


def test_store_rejects_session_or_run_identity_mismatch(tmp_path: Path) -> None:
    path = tmp_path / "audit.db"
    with SqliteActionAuditStore(path, session_id="session-1", run_id="run-1") as store:
        append(store, ActionKind.OBSERVATION)

    with pytest.raises(AuditChainError, match="AUDIT_IDENTITY_MISMATCH"):
        SqliteActionAuditStore(path, session_id="session-2", run_id="run-1")

