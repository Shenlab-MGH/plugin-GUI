# Audit durability real-disk evidence — 2026-07-17

## Scope and safety boundary

- Windows local filesystem only.
- Open Ephys was not started, contacted, or probed.
- This is a bounded durability check, not a 100-million-event qualification gate.
- Runtime files were written under `Build-runtime/audit-durability-stress-channel-a-v2`; only the reproducible harness and summarized evidence are committed.
- Destructive cleanup is limited to a direct child named `audit-durability-stress-*` under the repository `Build-runtime` directory, or a direct child of an explicit `--safe-parent`.
- An existing runtime directory is never deleted unless its exact ownership sentinel is present and valid.

## Reproducible command

```powershell
.\integrations\mcp\.venv\Scripts\python.exe `
  .\integrations\mcp\tools\run_audit_durability_stress.py `
  --root .\Build-runtime\audit-durability-stress-channel-a-v2 `
  --iterations 100 `
  --evidence .\docs\agent\evidence\audit-durability-stress-2026-07-17.json
```

## Real-disk results

| Check | Result |
|---|---:|
| SQLite store reopen / append / verify iterations | 100 / 100 |
| Appended audit records | 100 |
| Final verified event count | 100 |
| Reopen / append / verify elapsed | 14.539676 s |
| Total harness elapsed | 14.728170 s |
| Real CAS file byte tamper detected | PASS |
| Seal verified before and after store restart | PASS |
| Real seal JSON terminal-hash tamper detected | PASS |
| Caller-injected HMAC key found as raw/hex/base64 in persisted files | NO |
| Source worktree dirty before evidence write | NO |
| Harness failures | 0 |

Recorded digests:

- terminal audit hash: `3983742caeaa4f043d9aafc8c08870036191f0181049e77289ba89e2208e3f79`
- SQLite database SHA-256: `68c173dd279e740408ed6a81654c25804fb8fdc62892ab4281a4ab00b96fb429`
- seal file SHA-256: `7cb328294618a4785ced318e8348ccdc0bfb2e249244777e42b22bb86955dcda`
- seal-bound artifact SHA-256: `b07646da2b7384ccdecd739c9e9026b1360518e732dc991b5c741fa5673bfa33`

Provenance bound into the JSON before it was written:

- source commit: `e8f76ce9642cb66a081a4b8e1e31ec3a3263638f`
- source dirty: `false` (ignored `Build-runtime` output does not count as source dirtiness)
- stress script SHA-256: `4b0884c2932b292449d736c16b3faaac710f3438caa6965714c12bfbcf6446be`
- Python: `3.12.10`
- SQLite: `3.49.1`
- OS: `Windows-11-10.0.26200-SP0`

The key scan covered raw bytes, lowercase and uppercase hex, standard base64,
and URL-safe base64 across the SQLite database, any extant WAL/SHM sidecars,
seal, artifact tree, the in-memory evidence values, and the written evidence JSON.

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

## Post-change regression

- Stress-harness safety tests: 13 passed.
- Full MCP suite: 388 passed in 5.10 s.
- Open Ephys operator Skill tests: 10 passed in 0.72 s.
- Skill validator: `Skill is valid!`.
