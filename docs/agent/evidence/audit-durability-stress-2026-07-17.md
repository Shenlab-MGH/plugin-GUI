# Audit durability real-disk evidence — 2026-07-17

## Scope and safety boundary

- Windows local filesystem only.
- Open Ephys was not started, contacted, or probed.
- This is a bounded durability check, not a 100-million-event qualification gate.
- Runtime files were written under `Build-runtime/audit-durability-stress-channel-a`; only the reproducible harness and summarized evidence are committed.

## Reproducible command

```powershell
.\integrations\mcp\.venv\Scripts\python.exe `
  .\integrations\mcp\tools\run_audit_durability_stress.py `
  --root .\Build-runtime\audit-durability-stress-channel-a `
  --iterations 100 `
  --evidence .\docs\agent\evidence\audit-durability-stress-2026-07-17.json
```

## Real-disk results

| Check | Result |
|---|---:|
| SQLite store reopen / append / verify iterations | 100 / 100 |
| Appended audit records | 100 |
| Final verified event count | 100 |
| Reopen / append / verify elapsed | 14.624108 s |
| Total harness elapsed | 14.733201 s |
| Real CAS file byte tamper detected | PASS |
| Seal verified before and after store restart | PASS |
| Real seal JSON terminal-hash tamper detected | PASS |
| Caller-injected HMAC key found in persisted files | NO |
| Harness failures | 0 |

Recorded digests:

- terminal audit hash: `89a1e2b53752cc3f0746c831fd8f88c22d1583b9832423641aab64e42b045fab`
- SQLite database SHA-256: `189e5e52742153195a5b5e7c7ebf3fdf6f2ecf77652bcdeee72633d7d6c3ced2`
- seal file SHA-256: `a6f55446fb18929acd37baa9a285df88beb46a28282670ceeb015cde17bc005f`
- seal-bound artifact SHA-256: `b07646da2b7384ccdecd739c9e9026b1360518e732dc991b5c741fa5673bfa33`

The machine-readable output is in `audit-durability-stress-2026-07-17.json`.

## Repeated target-suite result

The following target suite was run in two consecutive 10-round batches:

```powershell
.\.venv\Scripts\python.exe -m pytest `
  tests\experiment\test_action_audit.py `
  tests\experiment\test_durable_audit.py -q
```

Each round collected and passed 47 tests. Batch 1 completed 10 rounds in 9.399 s;
batch 2 completed 10 rounds in 9.544 s. Combined result: 20 rounds, 940 test-case
executions, 0 failed test rounds, 0 failed tests, 18.943 s measured batch time.

The first batch's hand-written PowerShell summary footer used a stale
`TESTS_PER_ROUND=36` constant, while pytest itself reported `47 passed` in every
round. The command was immediately repeated with the corrected count; this
document reports pytest's collected count and preserves that discrepancy rather
than hiding it.

## Interpretation

This evidence supports bounded restart persistence, append-chain integrity,
artifact tamper detection, seal tamper detection, and caller-key non-persistence
for the exercised 100-record case. It does not establish crash consistency under
forced power loss, filesystem/hardware fault tolerance, large-scale performance,
or a 100-million-event acceptance gate.
