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


def rechain(records):
    """Recompute a self-consistent chain after deliberate test mutations."""
    rebuilt = []
    previous_hash = "0" * 64
    for sequence, record in enumerate(records, start=1):
        updated = replace(
            record,
            sequence=sequence,
            previous_hash=previous_hash,
            event_hash="",
        )
        updated = replace(updated, event_hash=updated.recompute_hash())
        rebuilt.append(updated)
        previous_hash = updated.event_hash
    return rebuilt


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


def test_rejects_equal_monotonic_time_after_the_first_event() -> None:
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
    ]
    records[1] = replace(
        records[1],
        monotonic_ns=records[0].monotonic_ns,
    )
    records[1] = replace(records[1], event_hash=records[1].recompute_hash())

    report = verify_action_chain(records, raise_on_error=False)

    assert not report.valid
    assert "MONOTONIC_TIME_REGRESSION" in report.violation_codes


def test_builder_rejects_nonincreasing_monotonic_time_without_advancing() -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    first = event(
        builder,
        ActionKind.OBSERVATION,
        actor=ActionActor.AGENT,
        correlation_id="inspect",
        payload={"mode": "IDLE"},
    )

    with pytest.raises(AuditChainError, match="MONOTONIC_TIME_REGRESSION"):
        builder.append(
            kind=ActionKind.DECISION,
            timestamp_utc="2026-07-17T00:00:02Z",
            monotonic_ns=first.monotonic_ns,
            actor=ActionActor.AGENT,
            correlation_id="inspect",
            payload={"decision": "NO_ACTION"},
        )

    assert builder.next_sequence == 2
    second = builder.append(
        kind=ActionKind.DECISION,
        timestamp_utc="2026-07-17T00:00:03Z",
        monotonic_ns=first.monotonic_ns + 1,
        actor=ActionActor.AGENT,
        correlation_id="inspect",
        payload={"decision": "NO_ACTION"},
    )
    assert second.sequence == 2


@pytest.mark.parametrize(
    ("field", "value", "violation"),
    [
        ("schema_version", "oe-agent-action-audit/future", "SCHEMA_MISMATCH"),
        ("session_id", "session-2", "SESSION_MISMATCH"),
        ("run_id", "run-2", "RUN_MISMATCH"),
    ],
)
def test_rejects_mixed_chain_identity(
    field: str, value: str, violation: str
) -> None:
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
    ]
    records[1] = replace(records[1], **{field: value})

    report = verify_action_chain(rechain(records), raise_on_error=False)

    assert not report.valid
    assert report.violation_codes == (violation,)


def test_rejects_duplicate_event_id_in_an_otherwise_valid_chain() -> None:
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
    ]
    records[1] = replace(records[1], event_id=records[0].event_id)

    report = verify_action_chain(rechain(records), raise_on_error=False)

    assert not report.valid
    assert report.violation_codes == ("DUPLICATE_EVENT_ID",)


@pytest.mark.parametrize(
    ("result_kind", "intent_kind"),
    [
        (ActionKind.TOOL_RESULT, ActionKind.TOOL_INTENT),
        (ActionKind.GUI_ACTION_RESULT, ActionKind.GUI_ACTION_INTENT),
    ],
)
def test_confirmed_mutation_requires_readback_after_matching_intent(
    result_kind: ActionKind, intent_kind: ActionKind
) -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    records = [
        event(
            builder,
            ActionKind.NATIVE_READBACK,
            actor=ActionActor.OPEN_EPHYS,
            correlation_id="start",
            payload={"mode": "IDLE"},
        ),
        event(
            builder,
            intent_kind,
            actor=ActionActor.AGENT,
            correlation_id="start",
            payload={"mutating": True},
        ),
        event(
            builder,
            result_kind,
            actor=ActionActor.MCP,
            correlation_id="start",
            payload={"mutating": True, "status": "CONFIRMED"},
        ),
    ]

    report = verify_action_chain(records, raise_on_error=False)

    assert not report.valid
    assert report.violation_codes == ("MISSING_AUTHORITATIVE_READBACK",)


@pytest.mark.parametrize(
    ("result_kind", "intent_kind"),
    [
        (ActionKind.TOOL_RESULT, ActionKind.TOOL_INTENT),
        (ActionKind.GUI_ACTION_RESULT, ActionKind.GUI_ACTION_INTENT),
    ],
)
def test_old_completed_lifecycle_cannot_satisfy_reused_correlation(
    result_kind: ActionKind, intent_kind: ActionKind
) -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    records = []
    for kind, actor in (
        (intent_kind, ActionActor.AGENT),
        (ActionKind.NATIVE_READBACK, ActionActor.OPEN_EPHYS),
        (result_kind, ActionActor.MCP),
        (intent_kind, ActionActor.AGENT),
        (result_kind, ActionActor.MCP),
    ):
        payload = (
            {"mutating": True, "status": "CONFIRMED"}
            if kind is result_kind
            else {"mutating": True}
        )
        records.append(
            event(
                builder,
                kind,
                actor=actor,
                correlation_id="reused",
                payload=payload,
            )
        )

    report = verify_action_chain(records, raise_on_error=False)

    assert not report.valid
    assert report.violation_codes == ("MISSING_AUTHORITATIVE_READBACK",)


def test_cross_kind_intent_starts_a_new_correlation_lifecycle() -> None:
    builder = ActionAuditBuilder(session_id="session-1", run_id="run-1")
    records = [
        event(
            builder,
            ActionKind.GUI_ACTION_INTENT,
            actor=ActionActor.AGENT,
            correlation_id="shared",
            payload={"mutating": True},
        ),
        event(
            builder,
            ActionKind.NATIVE_READBACK,
            actor=ActionActor.OPEN_EPHYS,
            correlation_id="shared",
            payload={"mode": "RECORD"},
        ),
        event(
            builder,
            ActionKind.TOOL_INTENT,
            actor=ActionActor.AGENT,
            correlation_id="shared",
            payload={"mutating": False},
        ),
        event(
            builder,
            ActionKind.GUI_ACTION_RESULT,
            actor=ActionActor.UIA,
            correlation_id="shared",
            payload={"mutating": True, "status": "CONFIRMED"},
        ),
    ]

    report = verify_action_chain(records, raise_on_error=False)

    assert not report.valid
    assert report.violation_codes == (
        "MISSING_MUTATION_INTENT",
        "MISSING_AUTHORITATIVE_READBACK",
    )
