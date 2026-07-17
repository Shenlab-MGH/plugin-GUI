# Channel C reproducible AgentChecks and qualification evidence

Date: 2026-07-17 (UTC)

Scope: software-only validation. These commands did not launch Open Ephys or
address a device. Evidence was generated from committed harnesses at
`13874008e744660350b33efa0399975797c965b9`.

## Reproduction commands

Run from the repository root with PowerShell 7:

```powershell
py -3.12 -m uv run --frozen --project integrations\mcp python tools\qualification\run_qualification_batch.py --output docs\agent\evidence\channel-c-qualification-2026-07-17.json --force
pwsh -NoProfile -File tools\windows\Invoke-ChannelCAgentChecks.ps1 -OutputDirectory docs\agent\evidence\channel-c-logs\final
```

The qualification runner refuses to overwrite without the explicit `--force`
flag. The AgentChecks wrapper always refuses to overwrite logs or its summary,
restricts output to `docs\agent\evidence`, and rejects existing Windows
reparse points along the output path. Use a new evidence subdirectory for a
new run; do not replace or relabel a failed run.

## Qualification evaluator batch

Runner: `tools/qualification/run_qualification_batch.py`

Machine result:
`docs/agent/evidence/channel-c-qualification-2026-07-17.json`

The runner constructs exactly 502 named deterministic boundary cases and then
100,000 cases from a fixed-seed generator. Its `oracle_evaluate` function is an
independent specification oracle: it does not call the production evaluator or
import its private field/code maps. Each production result is compared with
the oracle's full decision and ordered failure-code tuple.

| Field | Recorded value |
| --- | --- |
| Decision | PASS |
| Seed | `3235780353` (`0xC0DE1701`) |
| Boundary cases | 502 |
| Fixed-seed random cases | 100,000 |
| Total cases | 100,502 |
| Failures | `[]` |
| Source commit | `13874008e744660350b33efa0399975797c965b9` |
| Script SHA-256 | `d03b904b50232eb40f0bf7d84786a5d5dd1a9a41924355497d1b3b6a2012ed6f` |
| Ledger SHA-256 | `7ef0a2916e859b684bcbe22ef13688150ac78040f6209ca590b1bd3069addef4` |
| Result SHA-256 | `1dcec5410f1407d3358798df65108f9415ffc091cb1938abd64c24f7902b06e2` |

## Final three-round AgentChecks evidence

Wrapper: `tools/windows/Invoke-ChannelCAgentChecks.ps1`

Machine summary:
`docs/agent/evidence/channel-c-logs/final/agentchecks-summary.json`

| Round | Exit | Elapsed (s) | pytest passed | Explicit PASS lines | Final line | Log SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| 1 | 0 | 38.038 | 398 | 35 | `PASS agent fork local checks` | `a025f04a55a0a727f5e7ab09cee9dc0bf18f1b752aab1c15dea704e2a3174d6a` |
| 2 | 0 | 37.331 | 398 | 35 | `PASS agent fork local checks` | `8121b09af98ad25e12642fee00d8dac85833cae4f26a6963c8b428623ba443ca` |
| 3 | 0 | 37.742 | 398 | 35 | `PASS agent fork local checks` | `9ee937791b8964a987811f7e31778dd75e125abf06ed6757a2b1205a881efb8b` |

The full, unabridged logs are:

- `docs/agent/evidence/channel-c-logs/final/agentchecks-round-1.log`
- `docs/agent/evidence/channel-c-logs/final/agentchecks-round-2.log`
- `docs/agent/evidence/channel-c-logs/final/agentchecks-round-3.log`

The expected count in the review request was 385. The current committed suite
actually reports 398 because additional tests landed before this rerun. The
evidence records the observed count rather than rewriting it to the stale
expectation. The explicit non-pytest PASS count remains 35.

## Preserved preliminary run

Before the reparse-point path hardening commit, the first reproducible wrapper
run also completed three independent rounds with exit 0, 398 pytest passes, 35
explicit PASS lines, and the same final line. It is preserved rather than
deleted or replaced:

- `docs/agent/evidence/channel-c-logs/agentchecks-summary.json`
- `docs/agent/evidence/channel-c-logs/agentchecks-round-1.log`
- `docs/agent/evidence/channel-c-logs/agentchecks-round-2.log`
- `docs/agent/evidence/channel-c-logs/agentchecks-round-3.log`

The final evidence above is a new run from the hardened committed wrapper, not
a retry substituted for a failure; both three-round batches passed.
