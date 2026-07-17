"""Durable single-writer audit ledger and content-addressed artifacts."""

from __future__ import annotations

import hashlib
import os
import sqlite3
import tempfile
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

from .action_audit import (
    ActionActor,
    ActionAuditBuilder,
    ActionAuditReport,
    ActionKind,
    ActionRecord,
    AuditChainError,
    verify_action_chain,
)


class ArtifactIntegrityError(ValueError):
    """Raised when content-addressed evidence is missing or changed."""


@dataclass(frozen=True)
class ArtifactReference:
    sha256: str
    bytes: int
    media_type: str
    path: Path


class ContentAddressedArtifacts:
    def __init__(self, root: Path):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)

    def put_bytes(self, content: bytes, *, media_type: str) -> ArtifactReference:
        digest = hashlib.sha256(content).hexdigest()
        destination = self.root / digest
        if not destination.exists():
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=f".{digest}.", suffix=".tmp", dir=self.root
            )
            try:
                with os.fdopen(descriptor, "wb") as stream:
                    stream.write(content)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary_name, destination)
            finally:
                try:
                    os.unlink(temporary_name)
                except FileNotFoundError:
                    pass
        reference = ArtifactReference(digest, len(content), media_type, destination)
        self.verify(reference)
        return reference

    def verify(self, reference: ArtifactReference) -> str:
        if not reference.path.is_file():
            raise ArtifactIntegrityError("ARTIFACT_MISSING")
        content = reference.path.read_bytes()
        digest = hashlib.sha256(content).hexdigest()
        if digest != reference.sha256 or len(content) != reference.bytes:
            raise ArtifactIntegrityError("ARTIFACT_HASH_MISMATCH")
        return digest


class SqliteActionAuditStore:
    """One process-local writer with FULL synchronous WAL commits."""

    def __init__(self, path: Path, *, session_id: str, run_id: str | None):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._connection = sqlite3.connect(self.path)
        try:
            self._connection.execute("PRAGMA journal_mode=WAL")
            self._connection.execute("PRAGMA synchronous=FULL")
            self._connection.execute("PRAGMA foreign_keys=ON")
            self._connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS audit_metadata (
                    key TEXT PRIMARY KEY,
                    value TEXT
                );
                CREATE TABLE IF NOT EXISTS audit_events (
                    sequence INTEGER PRIMARY KEY,
                    event_id TEXT NOT NULL UNIQUE,
                    correlation_id TEXT NOT NULL,
                    kind TEXT NOT NULL,
                    canonical_json TEXT NOT NULL,
                    event_hash TEXT NOT NULL UNIQUE
                );
                """
            )
            self._initialize_identity(session_id, run_id)
            existing = self.records()
            verify_action_chain(existing)
            self._builder = (
                ActionAuditBuilder.resume(existing)
                if existing
                else ActionAuditBuilder(session_id=session_id, run_id=run_id)
            )
        except BaseException:
            self._connection.close()
            raise

    def _initialize_identity(self, session_id: str, run_id: str | None) -> None:
        stored = dict(self._connection.execute("SELECT key, value FROM audit_metadata"))
        expected_run = run_id if run_id is not None else ""
        if stored:
            if stored.get("session_id") != session_id or stored.get("run_id") != expected_run:
                raise AuditChainError("AUDIT_IDENTITY_MISMATCH")
            return
        with self._connection:
            self._connection.executemany(
                "INSERT INTO audit_metadata(key, value) VALUES (?, ?)",
                (("session_id", session_id), ("run_id", expected_run)),
            )

    @property
    def event_count(self) -> int:
        return self._builder.next_sequence - 1

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
        with self._lock:
            record = self._builder.append(
                kind=kind,
                timestamp_utc=timestamp_utc,
                monotonic_ns=monotonic_ns,
                actor=actor,
                correlation_id=correlation_id,
                payload=payload,
                causation_id=causation_id,
                event_id=event_id,
            )
            try:
                with self._connection:
                    self._connection.execute(
                        """
                        INSERT INTO audit_events(
                            sequence, event_id, correlation_id, kind,
                            canonical_json, event_hash
                        ) VALUES (?, ?, ?, ?, ?, ?)
                        """,
                        (
                            record.sequence,
                            record.event_id,
                            record.correlation_id,
                            record.kind.value,
                            record.canonical_json(),
                            record.event_hash,
                        ),
                    )
            except BaseException:
                # Rebuild from committed storage so a failed commit cannot advance
                # the process-local chain.
                committed = self.records()
                self._builder = (
                    ActionAuditBuilder.resume(committed)
                    if committed
                    else ActionAuditBuilder(
                        session_id=record.session_id, run_id=record.run_id
                    )
                )
                raise
            return record

    def records(self) -> list[ActionRecord]:
        rows = self._connection.execute(
            "SELECT canonical_json FROM audit_events ORDER BY sequence"
        ).fetchall()
        return [ActionRecord.from_canonical_json(row[0]) for row in rows]

    def verify(self) -> ActionAuditReport:
        return verify_action_chain(self.records())

    def pending_mutation_correlations(self) -> tuple[str, ...]:
        records = self.records()
        pending: list[str] = []
        correlations = dict.fromkeys(
            record.correlation_id
            for record in records
            if record.kind in {ActionKind.TOOL_INTENT, ActionKind.GUI_ACTION_INTENT}
            and record.payload.get("mutating") is True
        )
        for correlation_id in correlations:
            correlated = [
                record for record in records if record.correlation_id == correlation_id
            ]
            terminal = any(
                record.kind in {ActionKind.TOOL_RESULT, ActionKind.GUI_ACTION_RESULT}
                for record in correlated
            )
            readback = any(
                record.kind is ActionKind.NATIVE_READBACK for record in correlated
            )
            if not terminal or not readback:
                pending.append(correlation_id)
        return tuple(pending)

    def close(self) -> None:
        self._connection.close()

    def __enter__(self) -> "SqliteActionAuditStore":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

