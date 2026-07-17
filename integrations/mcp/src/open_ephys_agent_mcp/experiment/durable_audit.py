"""Durable single-writer audit ledger and content-addressed artifacts."""

from __future__ import annotations

import hashlib
import hmac
import json
import os
import re
import sqlite3
import tempfile
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping

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


class AuditSealError(ValueError):
    """Raised when an audit checkpoint cannot be created or trusted."""


SEAL_SCHEMA_VERSION = "oe-agent-audit-seal/v0.0.1"
_SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
_SEAL_FIELDS = frozenset(
    {
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
)


def _canonical_json(value: Mapping[str, Any] | list[str]) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    )


def _validated_hmac_key(hmac_key: bytes) -> bytes:
    if not isinstance(hmac_key, bytes):
        raise TypeError("hmac_key must be bytes")
    if len(hmac_key) < 32:
        raise ValueError("hmac_key must contain at least 32 bytes")
    return hmac_key


def _artifact_root(artifacts: Iterable["ArtifactReference"]) -> str:
    digests: list[str] = []
    for reference in artifacts:
        if not reference.path.is_file():
            raise AuditSealError("ARTIFACT_MISSING")
        content = reference.path.read_bytes()
        digest = hashlib.sha256(content).hexdigest()
        if digest != reference.sha256 or len(content) != reference.bytes:
            raise AuditSealError("ARTIFACT_HASH_MISMATCH")
        digests.append(digest)
    return hashlib.sha256(
        _canonical_json(sorted(digests)).encode("utf-8")
    ).hexdigest()


@dataclass(frozen=True)
class AuditSeal:
    schema_version: str
    session_id: str
    run_id: str | None
    event_count: int
    terminal_hash: str
    artifact_root: str
    created_utc: str
    key_id: str
    hmac_sha256: str

    def content(self) -> dict[str, Any]:
        return {
            "artifact_root": self.artifact_root,
            "created_utc": self.created_utc,
            "event_count": self.event_count,
            "key_id": self.key_id,
            "run_id": self.run_id,
            "schema_version": self.schema_version,
            "session_id": self.session_id,
            "terminal_hash": self.terminal_hash,
        }

    def canonical_json(self) -> str:
        return _canonical_json({**self.content(), "hmac_sha256": self.hmac_sha256})

    @classmethod
    def from_canonical_json(cls, serialized: str) -> "AuditSeal":
        try:
            value = json.loads(serialized)
        except (json.JSONDecodeError, TypeError) as error:
            raise AuditSealError("SEAL_INVALID_JSON") from error
        if not isinstance(value, dict) or set(value) != _SEAL_FIELDS:
            raise AuditSealError("SEAL_FIELDS_INVALID")
        if value["schema_version"] != SEAL_SCHEMA_VERSION:
            raise AuditSealError("SEAL_SCHEMA_INVALID")
        if not isinstance(value["session_id"], str) or not value["session_id"]:
            raise AuditSealError("SEAL_SESSION_INVALID")
        if value["run_id"] is not None and not isinstance(value["run_id"], str):
            raise AuditSealError("SEAL_RUN_INVALID")
        if type(value["event_count"]) is not int or value["event_count"] <= 0:
            raise AuditSealError("SEAL_EVENT_COUNT_INVALID")
        for field in ("terminal_hash", "artifact_root", "hmac_sha256"):
            if not isinstance(value[field], str) or not _SHA256_PATTERN.fullmatch(
                value[field]
            ):
                raise AuditSealError(f"SEAL_{field.upper()}_INVALID")
        if not isinstance(value["created_utc"], str) or not value["created_utc"]:
            raise AuditSealError("SEAL_CREATED_UTC_INVALID")
        if not isinstance(value["key_id"], str) or not value["key_id"]:
            raise AuditSealError("SEAL_KEY_ID_INVALID")
        return cls(**value)


def _seal_hmac(content: Mapping[str, Any], hmac_key: bytes) -> str:
    return hmac.new(
        _validated_hmac_key(hmac_key),
        _canonical_json(content).encode("utf-8"),
        hashlib.sha256,
    ).hexdigest()


def _write_atomic_fsynced(destination: Path, content: str) -> None:
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", suffix=".tmp", dir=destination.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content.encode("utf-8"))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, destination)
        try:
            directory_descriptor = os.open(destination.parent, os.O_RDONLY)
        except OSError:
            directory_descriptor = None
        if directory_descriptor is not None:
            try:
                os.fsync(directory_descriptor)
            except OSError:
                pass
            finally:
                os.close(directory_descriptor)
    finally:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass


def verify_seal(
    path: Path,
    *,
    hmac_key: bytes,
    records: Iterable[ActionRecord],
    artifacts: Iterable["ArtifactReference"] = (),
) -> AuditSeal:
    key = _validated_hmac_key(hmac_key)
    try:
        serialized = Path(path).read_text(encoding="utf-8")
        seal = AuditSeal.from_canonical_json(serialized)
    except OSError as error:
        raise AuditSealError("SEAL_UNREADABLE") from error
    if serialized != seal.canonical_json():
        raise AuditSealError("SEAL_NONCANONICAL_JSON")
    expected_hmac = _seal_hmac(seal.content(), key)
    if not hmac.compare_digest(seal.hmac_sha256, expected_hmac):
        raise AuditSealError("SEAL_HMAC_MISMATCH")

    materialized = list(records)
    try:
        report = verify_action_chain(materialized)
    except AuditChainError as error:
        raise AuditSealError("CHAIN_MISMATCH") from error
    identities_match = all(
        record.session_id == seal.session_id and record.run_id == seal.run_id
        for record in materialized
    )
    if (
        not materialized
        or not identities_match
        or report.event_count != seal.event_count
        or report.terminal_hash != seal.terminal_hash
    ):
        raise AuditSealError("CHAIN_MISMATCH")
    if _artifact_root(artifacts) != seal.artifact_root:
        raise AuditSealError("ARTIFACT_ROOT_MISMATCH")
    return seal


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

    def seal(
        self,
        path: Path,
        *,
        hmac_key: bytes,
        key_id: str,
        created_utc: str,
        artifacts: Iterable[ArtifactReference] = (),
    ) -> AuditSeal:
        key = _validated_hmac_key(hmac_key)
        if not key_id:
            raise ValueError("key_id is required")
        if not created_utc:
            raise ValueError("created_utc is required")
        materialized_artifacts = tuple(artifacts)
        with self._lock:
            records = self.records()
            if not records:
                raise AuditSealError("EMPTY_CHAIN")
            pending = self.pending_mutation_correlations()
            if pending:
                raise AuditSealError("PENDING_MUTATION:" + ",".join(pending))
            report = verify_action_chain(records)
            seal = AuditSeal(
                schema_version=SEAL_SCHEMA_VERSION,
                session_id=self._builder.session_id,
                run_id=self._builder.run_id,
                event_count=report.event_count,
                terminal_hash=report.terminal_hash,
                artifact_root=_artifact_root(materialized_artifacts),
                created_utc=created_utc,
                key_id=key_id,
                hmac_sha256="",
            )
            seal = AuditSeal(
                **{
                    **seal.__dict__,
                    "hmac_sha256": _seal_hmac(seal.content(), key),
                }
            )
            _write_atomic_fsynced(Path(path), seal.canonical_json())
            return seal

    def close(self) -> None:
        self._connection.close()

    def __enter__(self) -> "SqliteActionAuditStore":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
