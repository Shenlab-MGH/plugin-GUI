from __future__ import annotations

from dataclasses import replace

import pytest

from open_ephys_agent_mcp.experiment.action_audit import (
    ActionActor,
    ActionAuditBuilder,
    ActionKind,
    AuditChainError,
    verify_action_chain,
)


def event(builder: ActionAuditBuilder, kind: ActionKind, **kwargs: object):
    return builder.append(
        kind=kind,
        timestamp_utc=f"2026-07-17T00:00:{builder.next_sequence:02d}Z",
        monotonic_ns=builder.next_sequence * 1_000,
        **kwargs,
    )


def test_builds_and_verifies_a_complete_correlated_mutation_trace() -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    records = [
        event(
            builder,
            ActionKind.OBSERVATION,
            actor=ActionActor.AGENT,
            correlation_id="part-1-start",
            payload={"recording_state": "INACTIVE"},
        ),
        event(
            builder,
            ActionKind.DECISION,
            actor=ActionActor.AGENT,
            correlation_id="part-1-start",
            payload={"decision": "START_RECORDING", "reason_code": "PREFLIGHT_PASSED"},
        ),
        event(
            builder,
            ActionKind.TOOL_INTENT,
            actor=ActionActor.AGENT,
            correlation_id="part-1-start",
            payload={"tool": "oe_start_part", "arguments_hash": "a" * 64},
        ),
        event(
            builder,
            ActionKind.NATIVE_REQUEST,
            actor=ActionActor.MCP,
            correlation_id="part-1-start",
            payload={"operation": "START_RECORDING", "request_id": "request-1"},
        ),
        event(
            builder,
            ActionKind.NATIVE_READBACK,
            actor=ActionActor.OPEN_EPHYS,
            correlation_id="part-1-start",
            payload={"recording_state": "ACTIVE", "request_id": "request-1"},
        ),
        event(
            builder,
            ActionKind.TOOL_RESULT,
            actor=ActionActor.MCP,
            correlation_id="part-1-start",
            payload={"tool": "oe_start_part", "status": "CONFIRMED"},
        ),
    ]

    report = verify_action_chain(records)

    assert report.valid
    assert report.event_count == 6
    assert report.terminal_hash == records[-1].event_hash
    assert report.violation_codes == ()


def test_detects_payload_tampering_and_missing_event() -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    records = [
        event(
            builder,
            ActionKind.OBSERVATION,
            actor=ActionActor.AGENT,
            correlation_id="inspect",
            payload={"mode": "IDLE"},
        ),
        event(
            builder,
            ActionKind.DECISION,
            actor=ActionActor.AGENT,
            correlation_id="inspect",
            payload={"decision": "NO_ACTION"},
        ),
        event(
            builder,
            ActionKind.TOOL_INTENT,
            actor=ActionActor.AGENT,
            correlation_id="inspect",
            payload={"tool": "oe_get_status"},
        ),
    ]

    with pytest.raises(AuditChainError, match="EVENT_HASH_MISMATCH"):
        verify_action_chain(
            [records[0], replace(records[1], payload={"decision": "START"}), records[2]]
        )

    with pytest.raises(AuditChainError, match="SEQUENCE_GAP"):
        verify_action_chain([records[0], records[2]])


def test_redacts_nested_secrets_before_hashing_or_persisting() -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    record = event(
        builder,
        ActionKind.TOOL_INTENT,
        actor=ActionActor.AGENT,
        correlation_id="secret-test",
        payload={
            "authorization": "Bearer secret-one",
            "nested": {"token": "secret-two", "safe": "keep"},
            "header": "Bearer secret-three",
            "url": "http://127.0.0.1:38498/status?access_token=secret-four",
        },
    )

    serialized = record.canonical_json()
    assert "secret-one" not in serialized
    assert "secret-two" not in serialized
    assert "secret-three" not in serialized
    assert "secret-four" not in serialized
    assert "keep" in serialized
    assert serialized.count("[REDACTED]") >= 4


def test_flags_agent_actions_during_human_takeover_until_bound_resume() -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    records = [
        event(
            builder,
            ActionKind.HUMAN_TAKEOVER,
            actor=ActionActor.HUMAN,
            correlation_id="takeover-1",
            payload={"reason": "inspect_dialog"},
        ),
        event(
            builder,
            ActionKind.GUI_ACTION_INTENT,
            actor=ActionActor.AGENT,
            correlation_id="illegal-click",
            payload={"semantic_target": "record_button"},
        ),
        event(
            builder,
            ActionKind.HUMAN_RESUME,
            actor=ActionActor.HUMAN,
            correlation_id="takeover-1",
            payload={"approval_id": "approval-1", "fresh_snapshot_id": "snapshot-2"},
        ),
    ]

    report = verify_action_chain(records, raise_on_error=False)

    assert not report.valid
    assert "AGENT_ACTION_DURING_HUMAN_TAKEOVER" in report.violation_codes


def test_requires_intent_and_authoritative_readback_for_mutating_result() -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    records = [
        event(
            builder,
            ActionKind.TOOL_RESULT,
            actor=ActionActor.MCP,
            correlation_id="orphan-result",
            payload={"tool": "oe_start_part", "status": "CONFIRMED", "mutating": True},
        )
    ]

    report = verify_action_chain(records, raise_on_error=False)

    assert not report.valid
    assert "MISSING_MUTATION_INTENT" in report.violation_codes
    assert "MISSING_AUTHORITATIVE_READBACK" in report.violation_codes

