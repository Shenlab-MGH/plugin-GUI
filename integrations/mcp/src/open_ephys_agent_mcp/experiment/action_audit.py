"""Tamper-evident, replayable records for Agent and human actions."""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from enum import Enum
from typing import Any, Iterable, Mapping


SCHEMA_VERSION = "oe-agent-action-audit/v0.0.1"
GENESIS_HASH = "0" * 64

_SECRET_KEYS = frozenset(
    {
        "access_token",
        "api_key",
        "authorization",
        "bearer_token",
        "cookie",
        "password",
        "refresh_token",
        "secret",
        "set-cookie",
        "token",
    }
)
_BEARER_PATTERN = re.compile(r"(?i)\bbearer\s+[^\s,;]+")
_QUERY_SECRET_PATTERN = re.compile(
    r"(?i)([?&](?:access_token|api_key|authorization|token)=)[^&#\s]+"
)


class ActionActor(str, Enum):
    AGENT = "AGENT"
    HUMAN = "HUMAN"
    MCP = "MCP"
    OPEN_EPHYS = "OPEN_EPHYS"
    SYSTEM = "SYSTEM"
    UIA = "UIA"
    RECORDER = "RECORDER"


class ActionKind(str, Enum):
    OBSERVATION = "OBSERVATION"
    DECISION = "DECISION"
    TOOL_INTENT = "TOOL_INTENT"
    TOOL_RESULT = "TOOL_RESULT"
    NATIVE_REQUEST = "NATIVE_REQUEST"
    NATIVE_READBACK = "NATIVE_READBACK"
    UIA_SNAPSHOT = "UIA_SNAPSHOT"
    GUI_ACTION_INTENT = "GUI_ACTION_INTENT"
    GUI_ACTION_RESULT = "GUI_ACTION_RESULT"
    VIDEO_SEGMENT = "VIDEO_SEGMENT"
    HUMAN_APPROVAL = "HUMAN_APPROVAL"
    HUMAN_TAKEOVER = "HUMAN_TAKEOVER"
    HUMAN_RESUME = "HUMAN_RESUME"
    ARTIFACT_BOUND = "ARTIFACT_BOUND"
    ERROR = "ERROR"


class AuditChainError(ValueError):
    """Raised when a stored behavior trace cannot be trusted."""


def _redact(value: Any) -> Any:
    if isinstance(value, Mapping):
        return {
            str(key): (
                "[REDACTED]"
                if str(key).lower() in _SECRET_KEYS
                else _redact(item)
            )
            for key, item in value.items()
        }
    if isinstance(value, (list, tuple)):
        return [_redact(item) for item in value]
    if isinstance(value, str):
        value = _BEARER_PATTERN.sub("Bearer [REDACTED]", value)
        return _QUERY_SECRET_PATTERN.sub(r"\1[REDACTED]", value)
    return value


def _canonical_json(value: Mapping[str, Any]) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    )


@dataclass(frozen=True)
class ActionRecord:
    schema_version: str
    event_id: str
    session_id: str
    run_id: str | None
    sequence: int
    timestamp_utc: str
    monotonic_ns: int
    actor: ActionActor
    kind: ActionKind
    correlation_id: str
    causation_id: str | None
    payload: Mapping[str, Any]
    previous_hash: str
    event_hash: str

    def content(self) -> dict[str, Any]:
        return {
            "actor": self.actor.value,
            "causation_id": self.causation_id,
            "correlation_id": self.correlation_id,
            "event_id": self.event_id,
            "kind": self.kind.value,
            "monotonic_ns": self.monotonic_ns,
            "payload": self.payload,
            "previous_hash": self.previous_hash,
            "run_id": self.run_id,
            "schema_version": self.schema_version,
            "sequence": self.sequence,
            "session_id": self.session_id,
            "timestamp_utc": self.timestamp_utc,
        }

    def canonical_json(self) -> str:
        return _canonical_json({**self.content(), "event_hash": self.event_hash})

    def recompute_hash(self) -> str:
        return hashlib.sha256(_canonical_json(self.content()).encode("utf-8")).hexdigest()

    @classmethod
    def from_canonical_json(cls, serialized: str) -> "ActionRecord":
        value = json.loads(serialized)
        return cls(
            schema_version=value["schema_version"],
            event_id=value["event_id"],
            session_id=value["session_id"],
            run_id=value["run_id"],
            sequence=value["sequence"],
            timestamp_utc=value["timestamp_utc"],
            monotonic_ns=value["monotonic_ns"],
            actor=ActionActor(value["actor"]),
            kind=ActionKind(value["kind"]),
            correlation_id=value["correlation_id"],
            causation_id=value["causation_id"],
            payload=value["payload"],
            previous_hash=value["previous_hash"],
            event_hash=value["event_hash"],
        )


class ActionAuditBuilder:
    """Builds one ordered chain; durable persistence is supplied by the caller."""

    def __init__(self, *, session_id: str, run_id: str | None = None):
        if not session_id:
            raise ValueError("session_id is required")
        self.session_id = session_id
        self.run_id = run_id
        self._records: list[ActionRecord] = []

    @classmethod
    def resume(cls, records: Iterable[ActionRecord]) -> "ActionAuditBuilder":
        materialized = list(records)
        if not materialized:
            raise ValueError("cannot infer identity from an empty audit chain")
        verify_action_chain(materialized)
        builder = cls(
            session_id=materialized[0].session_id,
            run_id=materialized[0].run_id,
        )
        builder._records.extend(materialized)
        return builder

    @property
    def next_sequence(self) -> int:
        return len(self._records) + 1

    def append(
        self,
        *,
        kind: ActionKind,
        timestamp_utc: str,
        monotonic_ns: int,
        actor: ActionActor,
        correlation_id: str,
        payload: Mapping[str, Any],
        causation_id: str | None = None,
        event_id: str | None = None,
    ) -> ActionRecord:
        if not correlation_id:
            raise ValueError("correlation_id is required")
        if self._records and monotonic_ns <= self._records[-1].monotonic_ns:
            raise AuditChainError("MONOTONIC_TIME_REGRESSION")
        sequence = self.next_sequence
        previous_hash = self._records[-1].event_hash if self._records else GENESIS_HASH
        record = ActionRecord(
            schema_version=SCHEMA_VERSION,
            event_id=event_id or f"{self.session_id}:{sequence:08d}",
            session_id=self.session_id,
            run_id=self.run_id,
            sequence=sequence,
            timestamp_utc=timestamp_utc,
            monotonic_ns=monotonic_ns,
            actor=actor,
            kind=kind,
            correlation_id=correlation_id,
            causation_id=causation_id,
            payload=_redact(payload),
            previous_hash=previous_hash,
            event_hash="",
        )
        record = ActionRecord(**{**record.__dict__, "event_hash": record.recompute_hash()})
        self._records.append(record)
        return record


@dataclass(frozen=True)
class ActionAuditReport:
    valid: bool
    event_count: int
    terminal_hash: str
    violation_codes: tuple[str, ...]


def _semantic_violations(records: list[ActionRecord]) -> list[str]:
    violations: list[str] = []
    human_has_control = False
    active_mutations: dict[tuple[str, ActionKind], bool] = {}
    result_to_intent = {
        ActionKind.TOOL_RESULT: ActionKind.TOOL_INTENT,
        ActionKind.GUI_ACTION_RESULT: ActionKind.GUI_ACTION_INTENT,
    }
    for record in records:
        if record.kind is ActionKind.HUMAN_TAKEOVER:
            human_has_control = True
            continue
        if record.kind is ActionKind.HUMAN_RESUME:
            if not record.payload.get("approval_id") or not record.payload.get(
                "fresh_snapshot_id"
            ):
                violations.append("UNBOUND_HUMAN_RESUME")
            else:
                human_has_control = False
            continue
        if human_has_control and record.actor is ActionActor.AGENT and record.kind in {
            ActionKind.TOOL_INTENT,
            ActionKind.GUI_ACTION_INTENT,
        }:
            violations.append("AGENT_ACTION_DURING_HUMAN_TAKEOVER")

        if record.kind in {ActionKind.TOOL_INTENT, ActionKind.GUI_ACTION_INTENT}:
            for key in tuple(active_mutations):
                if key[0] == record.correlation_id:
                    active_mutations.pop(key)
            active_mutations[(record.correlation_id, record.kind)] = False
            continue

        if record.kind is ActionKind.NATIVE_READBACK:
            for key in tuple(active_mutations):
                if key[0] == record.correlation_id:
                    active_mutations[key] = True
            continue

        intent_kind = result_to_intent.get(record.kind)
        if intent_kind is None:
            continue
        lifecycle_key = (record.correlation_id, intent_kind)
        has_intent = lifecycle_key in active_mutations
        has_readback = active_mutations.get(lifecycle_key, False)
        is_confirmed_mutation = (
            record.payload.get("mutating") is True
            and record.payload.get("status") == "CONFIRMED"
        )
        if is_confirmed_mutation:
            if not has_intent:
                violations.append("MISSING_MUTATION_INTENT")
            if not has_readback:
                violations.append("MISSING_AUTHORITATIVE_READBACK")
        active_mutations.pop(lifecycle_key, None)
    return violations


def verify_action_chain(
    records: Iterable[ActionRecord], *, raise_on_error: bool = True
) -> ActionAuditReport:
    materialized = list(records)
    violations: list[str] = []
    previous_hash = GENESIS_HASH
    previous_monotonic_ns = -1
    expected_session_id = materialized[0].session_id if materialized else None
    expected_run_id = materialized[0].run_id if materialized else None
    event_ids: set[str] = set()

    for expected_sequence, record in enumerate(materialized, start=1):
        if record.schema_version != SCHEMA_VERSION:
            violations.append("SCHEMA_MISMATCH")
            break
        if record.session_id != expected_session_id:
            violations.append("SESSION_MISMATCH")
            break
        if record.run_id != expected_run_id:
            violations.append("RUN_MISMATCH")
            break
        if record.event_id in event_ids:
            violations.append("DUPLICATE_EVENT_ID")
            break
        event_ids.add(record.event_id)
        if record.sequence != expected_sequence:
            violations.append("SEQUENCE_GAP")
            break
        if record.previous_hash != previous_hash:
            violations.append("PREVIOUS_HASH_MISMATCH")
            break
        if record.recompute_hash() != record.event_hash:
            violations.append("EVENT_HASH_MISMATCH")
            break
        if expected_sequence > 1 and record.monotonic_ns <= previous_monotonic_ns:
            violations.append("MONOTONIC_TIME_REGRESSION")
            break
        previous_hash = record.event_hash
        previous_monotonic_ns = record.monotonic_ns

    if not violations:
        violations.extend(_semantic_violations(materialized))

    unique_violations = tuple(dict.fromkeys(violations))
    report = ActionAuditReport(
        valid=not unique_violations,
        event_count=len(materialized),
        terminal_hash=materialized[-1].event_hash if materialized else GENESIS_HASH,
        violation_codes=unique_violations,
    )
    if unique_violations and raise_on_error:
        raise AuditChainError(",".join(unique_violations))
    return report
