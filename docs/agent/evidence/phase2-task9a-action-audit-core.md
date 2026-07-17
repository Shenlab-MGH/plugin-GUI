# Phase 2 Task 9A - Agent action audit core evidence

Date: 2026-07-17

Commit target: `agent/v0.0.1`

## Implemented

- Versioned canonical Agent action envelope.
- SHA-256 previous-record chain and offline verification.
- Recursive secret-key, bearer-string, and URL-query redaction before hashing.
- Human-takeover mutation check.
- Correlated mutation intent and authoritative-readback check.
- SQLite WAL prototype with `synchronous=FULL`, transaction commit, restart
  continuation, identity binding, and pending-mutation reconciliation list.
- Atomic content-addressed artifact write and SHA-256 verification.
- Canonical, atomically replaced and fsynced v0.0.1 audit checkpoint seals
  binding session/run identity, non-empty event count, terminal chain hash,
  verified artifact root, creation time, and caller-supplied key identifier.
- HMAC-SHA256 seal authentication with a caller-injected key of at least 32
  bytes. The key is never written to SQLite, the seal, or an artifact.
- Seal verification rejects malformed/non-canonical fields, chain identity or
  terminal-head changes, artifact changes, and HMAC changes.
- Sealing fails closed for an empty chain or any unresolved mutation.
- Equal or decreasing monotonic timestamps after the first event fail chain
  verification and are rejected before the in-memory builder or SQLite chain
  advances.
- Chain verification requires the exact v0.0.1 schema across every event,
  stable session and run identity, and globally unique event IDs. Violations
  produce `SCHEMA_MISMATCH`, `SESSION_MISMATCH`, `RUN_MISMATCH`, or
  `DUPLICATE_EVENT_ID` without accepting a self-consistently rehashed chain.
- Confirmed mutating tool and GUI results require their matching intent,
  followed by native readback, followed by the result in the same correlation
  lifecycle. A readback before intent or evidence from a completed lifecycle
  cannot authorize a later result that reuses the correlation ID.
- The matching current lifecycle intent must itself declare
  `mutating: true`; a read-only intent followed by readback cannot authorize a
  confirmed mutating result and produces `NON_MUTATING_INTENT`.
- Pending mutation reconciliation is scoped to each intent lifecycle: tool and
  GUI intents require their matching result type plus a native readback after
  that specific intent. Every later tool or GUI intent with the same
  correlation ID, including a non-mutating or cross-kind intent, starts a new
  lifecycle boundary and cannot supply evidence for the earlier mutation.
- Seal creation timestamps use a canonical RFC3339 UTC `Z` representation.
- Seal replacement uses a flushed temporary file plus Windows
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`; POSIX uses
  atomic replacement followed by mandatory directory fsync. Durability errors
  fail closed instead of being ignored.
- Qualification thresholds and evidence counters accept only non-negative
  built-in integers; booleans, floats, NaN, infinity, strings, and negative
  values produce stable fail-closed codes instead of bypassing gates or
  raising comparison errors. First-failure completeness requires a built-in
  boolean, and manifest evidence requires an exact lowercase 64-hex SHA-256.

## Verified

```text
focused action-audit + qualification suite: 286 passed
complete MCP and Skill suite: 385 passed
skill-creator quick_validate.py: Skill is valid!
git diff --check: passed
```

## Explicitly not yet claimed

- The legacy gateway and all native/MCP/UIA/human/video producers are not yet
  wired to this ledger.
- There is no DPAPI/CNG key creation, wrapping, retrieval, or protected key
  store. Seal callers must inject and protect their own HMAC key; `key_id` is
  identification metadata only and is not proof of DPAPI protection.
- There is no external anchor, SQLite ACL hardening, multi-process collector
  protocol, video capture, or semantic GUI replay yet.
- A plain local hash chain detects edits relative to its retained head but does
  not prevent a privileged actor from replacing and recomputing an entire chain.
- No Open Ephys process or real device was touched by this implementation slice.
