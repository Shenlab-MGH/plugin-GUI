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

## Verified

```text
test_action_audit.py + test_durable_audit.py: 36 passed
complete MCP suite: 101 passed
Skill test suite: 10 passed
skill-creator quick_validate.py: Skill is valid!
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
