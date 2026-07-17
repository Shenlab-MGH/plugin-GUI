from __future__ import annotations

import json
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
from open_ephys_agent_mcp.experiment import durable_audit


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


def test_reused_correlation_does_not_hide_a_newer_pending_mutation(
    tmp_path: Path,
) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        completed_mutation(store)
        append(
            store,
            ActionKind.TOOL_INTENT,
            correlation_id="start-part-1",
            payload={"tool": "oe_start_part", "mutating": True},
        )

        assert store.pending_mutation_correlations() == ("start-part-1",)


def test_gui_intent_requires_gui_result_and_fresh_readback(tmp_path: Path) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(
            store,
            ActionKind.GUI_ACTION_INTENT,
            correlation_id="click-record",
            payload={"mutating": True},
        )
        append(
            store,
            ActionKind.NATIVE_READBACK,
            actor=ActionActor.OPEN_EPHYS,
            correlation_id="click-record",
            payload={"recording_state": "ACTIVE"},
        )
        append(
            store,
            ActionKind.TOOL_RESULT,
            actor=ActionActor.MCP,
            correlation_id="click-record",
            payload={"status": "CONFIRMED", "mutating": True},
        )

        assert store.pending_mutation_correlations() == ("click-record",)


def test_nonmutating_tool_intent_starts_a_new_lifecycle_boundary(
    tmp_path: Path,
) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(
            store,
            ActionKind.TOOL_INTENT,
            correlation_id="shared-correlation",
            payload={"operation": "START_RECORDING", "mutating": True},
        )
        append(
            store,
            ActionKind.TOOL_INTENT,
            correlation_id="shared-correlation",
            payload={"operation": "GET_STATUS", "mutating": False},
        )
        append(
            store,
            ActionKind.NATIVE_READBACK,
            actor=ActionActor.OPEN_EPHYS,
            correlation_id="shared-correlation",
            payload={"recording_state": "ACTIVE"},
        )
        append(
            store,
            ActionKind.TOOL_RESULT,
            actor=ActionActor.MCP,
            correlation_id="shared-correlation",
            payload={"status": "CONFIRMED"},
        )

        assert store.pending_mutation_correlations() == ("shared-correlation",)
        with pytest.raises(durable_audit.AuditSealError, match="PENDING_MUTATION"):
            store.seal(
                tmp_path / "audit.seal.json",
                hmac_key=b"k" * 32,
                key_id="key-1",
                created_utc="2026-07-17T01:02:03Z",
            )


def test_tool_and_gui_intents_share_the_same_correlation_lifecycle_boundary(
    tmp_path: Path,
) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(
            store,
            ActionKind.GUI_ACTION_INTENT,
            correlation_id="mixed-correlation",
            payload={"operation": "CLICK_RECORD", "mutating": True},
        )
        append(
            store,
            ActionKind.TOOL_INTENT,
            correlation_id="mixed-correlation",
            payload={"operation": "GET_STATUS", "mutating": False},
        )
        append(
            store,
            ActionKind.NATIVE_READBACK,
            actor=ActionActor.OPEN_EPHYS,
            correlation_id="mixed-correlation",
            payload={"recording_state": "ACTIVE"},
        )
        append(
            store,
            ActionKind.GUI_ACTION_RESULT,
            actor=ActionActor.UIA,
            correlation_id="mixed-correlation",
            payload={"status": "CONFIRMED"},
        )

        assert store.pending_mutation_correlations() == ("mixed-correlation",)


def test_store_rejects_nonincreasing_monotonic_time_without_changing_db(
    tmp_path: Path,
) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        first = append(store, ActionKind.OBSERVATION, monotonic_ns=100)
        with pytest.raises(AuditChainError, match="MONOTONIC_TIME_REGRESSION"):
            append(store, ActionKind.DECISION, monotonic_ns=100)

        assert store.event_count == 1
        assert store.records() == [first]
        second = append(store, ActionKind.DECISION, monotonic_ns=101)
        assert second.sequence == 2


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


def completed_mutation(store: SqliteActionAuditStore) -> None:
    append(
        store,
        ActionKind.TOOL_INTENT,
        correlation_id="start-part-1",
        payload={"tool": "oe_start_part", "mutating": True},
    )
    append(
        store,
        ActionKind.NATIVE_READBACK,
        actor=ActionActor.OPEN_EPHYS,
        correlation_id="start-part-1",
        payload={"recording_state": "ACTIVE"},
    )
    append(
        store,
        ActionKind.TOOL_RESULT,
        actor=ActionActor.MCP,
        correlation_id="start-part-1",
        payload={"status": "CONFIRMED", "mutating": True},
    )


def test_seals_canonical_checkpoint_and_verifies_chain_artifacts_and_hmac(
    tmp_path: Path,
) -> None:
    key = b"k" * 32
    seal_path = tmp_path / "audit.seal.json"
    artifacts = ContentAddressedArtifacts(tmp_path / "artifacts")
    frame = artifacts.put_bytes(b"frame-data", media_type="image/png")

    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        completed_mutation(store)
        seal = store.seal(
            seal_path,
            hmac_key=key,
            key_id="dpapi-key-slot-not-implemented",
            created_utc="2026-07-17T01:02:03Z",
            artifacts=[frame],
        )

        assert set(json.loads(seal_path.read_text(encoding="utf-8"))) == {
            "artifact_root",
            "created_utc",
            "event_count",
            "hmac_sha256",
            "key_id",
            "run_id",
            "schema_version",
            "session_id",
            "terminal_hash",
        }
        assert seal.session_id == "session-1"
        assert seal.run_id == "run-1"
        assert seal.event_count == 3
        assert seal.terminal_hash == store.verify().terminal_hash
        assert len(seal.artifact_root) == 64
        assert len(seal.hmac_sha256) == 64
        assert seal_path.read_text(encoding="utf-8") == seal.canonical_json()
        assert not list(tmp_path.glob(".audit.seal.json.*.tmp"))

        verified = durable_audit.verify_seal(
            seal_path,
            hmac_key=key,
            records=store.records(),
            artifacts=[frame],
        )
        assert verified == seal


def test_seal_requires_caller_key_of_at_least_32_bytes(tmp_path: Path) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(store, ActionKind.OBSERVATION)
        with pytest.raises(ValueError, match="at least 32 bytes"):
            store.seal(
                tmp_path / "audit.seal.json",
                hmac_key=b"too-short",
                key_id="key-1",
                created_utc="2026-07-17T01:02:03Z",
            )


@pytest.mark.parametrize(
    "created_utc",
    [
        "2026-07-17T01:02:03+00:00",
        "2026-07-17t01:02:03Z",
        "2026-07-17T01:02:03.120Z",
        "not-a-time",
    ],
)
def test_seal_rejects_noncanonical_rfc3339_utc(
    tmp_path: Path, created_utc: str
) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(store, ActionKind.OBSERVATION)
        with pytest.raises(ValueError, match="canonical RFC3339 UTC"):
            store.seal(
                tmp_path / "audit.seal.json",
                hmac_key=b"k" * 32,
                key_id="key-1",
                created_utc=created_utc,
            )


def test_store_refuses_to_seal_with_pending_mutation(tmp_path: Path) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(
            store,
            ActionKind.TOOL_INTENT,
            correlation_id="start-part-2",
            payload={"mutating": True},
        )
        with pytest.raises(durable_audit.AuditSealError, match="PENDING_MUTATION"):
            store.seal(
                tmp_path / "audit.seal.json",
                hmac_key=b"k" * 32,
                key_id="key-1",
                created_utc="2026-07-17T01:02:03Z",
            )


def test_store_refuses_to_seal_an_empty_chain(tmp_path: Path) -> None:
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        with pytest.raises(durable_audit.AuditSealError, match="EMPTY_CHAIN"):
            store.seal(
                tmp_path / "audit.seal.json",
                hmac_key=b"k" * 32,
                key_id="key-1",
                created_utc="2026-07-17T01:02:03Z",
            )


@pytest.mark.parametrize(
    ("field", "changed"),
    [
        ("session_id", "session-tampered"),
        ("run_id", "run-tampered"),
        ("event_count", 99),
        ("terminal_hash", "1" * 64),
        ("artifact_root", "2" * 64),
        ("created_utc", "2030-01-01T00:00:00Z"),
        ("key_id", "different-key"),
        ("hmac_sha256", "3" * 64),
    ],
)
def test_verify_seal_rejects_any_tampered_checkpoint_field(
    tmp_path: Path, field: str, changed: object
) -> None:
    key = b"k" * 32
    seal_path = tmp_path / "audit.seal.json"
    artifacts = ContentAddressedArtifacts(tmp_path / "artifacts")
    frame = artifacts.put_bytes(b"frame-data", media_type="image/png")
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(store, ActionKind.OBSERVATION)
        store.seal(
            seal_path,
            hmac_key=key,
            key_id="key-1",
            created_utc="2026-07-17T01:02:03Z",
            artifacts=[frame],
        )
        value = json.loads(seal_path.read_text(encoding="utf-8"))
        value[field] = changed
        seal_path.write_text(
            json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True),
            encoding="utf-8",
        )

        with pytest.raises(durable_audit.AuditSealError):
            durable_audit.verify_seal(
                seal_path,
                hmac_key=key,
                records=store.records(),
                artifacts=[frame],
            )


def test_verify_seal_rejects_changed_chain_head_or_artifact_bytes(tmp_path: Path) -> None:
    key = b"k" * 32
    seal_path = tmp_path / "audit.seal.json"
    artifacts = ContentAddressedArtifacts(tmp_path / "artifacts")
    frame = artifacts.put_bytes(b"frame-data", media_type="image/png")
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        first = append(store, ActionKind.OBSERVATION)
        store.seal(
            seal_path,
            hmac_key=key,
            key_id="key-1",
            created_utc="2026-07-17T01:02:03Z",
            artifacts=[frame],
        )

        with pytest.raises(durable_audit.AuditSealError, match="CHAIN_MISMATCH"):
            durable_audit.verify_seal(
                seal_path,
                hmac_key=key,
                records=[first, first],
                artifacts=[frame],
            )

        frame.path.write_bytes(b"changed")
        with pytest.raises(durable_audit.AuditSealError, match="ARTIFACT"):
            durable_audit.verify_seal(
                seal_path,
                hmac_key=key,
                records=store.records(),
                artifacts=[frame],
            )


def test_verify_seal_rejects_noncanonical_json_even_when_fields_are_unchanged(
    tmp_path: Path,
) -> None:
    key = b"k" * 32
    seal_path = tmp_path / "audit.seal.json"
    with SqliteActionAuditStore(
        tmp_path / "audit.db", session_id="session-1", run_id="run-1"
    ) as store:
        append(store, ActionKind.OBSERVATION)
        store.seal(
            seal_path,
            hmac_key=key,
            key_id="key-1",
            created_utc="2026-07-17T01:02:03Z",
        )
        value = json.loads(seal_path.read_text(encoding="utf-8"))
        seal_path.write_text(json.dumps(value, indent=2), encoding="utf-8")

        with pytest.raises(durable_audit.AuditSealError, match="NONCANONICAL"):
            durable_audit.verify_seal(
                seal_path,
                hmac_key=key,
                records=store.records(),
            )


def test_atomic_seal_write_fails_closed_when_durable_replace_fails(
    tmp_path: Path,
) -> None:
    destination = tmp_path / "audit.seal.json"
    destination.write_text("original", encoding="utf-8")

    def fail_replace(source: Path, target: Path) -> None:
        assert source.is_file()
        assert target == destination
        raise OSError("injected durable replacement failure")

    with pytest.raises(
        durable_audit.AuditSealError,
        match="SEAL_DURABLE_REPLACE_FAILED",
    ):
        durable_audit._write_atomic_fsynced(
            destination,
            "replacement",
            durable_replace=fail_replace,
        )

    assert destination.read_text(encoding="utf-8") == "original"
    assert not list(tmp_path.glob(".audit.seal.json.*.tmp"))
