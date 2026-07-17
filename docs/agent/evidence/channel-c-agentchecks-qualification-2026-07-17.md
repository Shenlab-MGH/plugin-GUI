# Channel C repeated AgentChecks and qualification evaluator evidence

Date: 2026-07-17 (UTC)

Scope: software-only validation. Open Ephys was not launched and no device was
addressed. The three full-gate runs were independent; a failed run would have
been retained and would not have been rerun to replace the evidence.

## Full AgentChecks repetition

All three runs exercised `tools/windows/Invoke-AgentChecks.ps1` against commit
`6376d6b1544ad3de818eacdbc1c2af6d51c2b81e`.

| Round | Result | pytest passed | Explicit `PASS` lines | Duration (s) | Log SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| 1 | PASS | 385 | 35 | 36.346* | `3f61bcd7c966cc00636a86ed9266bb3184f2f912c309a00e7f82f9af6df5e84d` |
| 2 | PASS, exit 0 | 385 | 35 | 39.604 | `eb2173b08d77796e352629b43af1df7ee6704601bc829d526232e17cde9e4753` |
| 3 | PASS, exit 0 | 385 | 35 | 37.962 | `07a089eae6dc48f34e15602324af80fe2743d3a63951a5abdc7a1b52bb45ee96` |

The final line in every retained log is `PASS agent fork local checks`. No
`FAILED`, `FAILURES`, or `Traceback` line was present. `pytest passed` is the
pytest summary count; the 35 explicit PASS lines cover the non-pytest contract
and aggregate gates and are reported separately to avoid double-counting.

\* Round 1's command-output cell was closed after completion before its
stopwatch summary could be collected. Its duration is the retained log's
creation-to-last-write interval, not the wrapper stopwatch. The original log
is intact; this run was not repeated as a substitute. The round 1 wrapper exit
code is therefore unavailable, while the completed aggregate gate result is
PASS.

Retained local logs:

- `%TEMP%\oe-agentchecks-channel-c-round1.log`
- `%TEMP%\oe-agentchecks-channel-c-round2.log`
- `%TEMP%\oe-agentchecks-channel-c-round3.log`

## Qualification evaluator boundary and deterministic random batch

The pure Python evaluator batch covered:

- every qualification profile volume field with invalid scalar/container
  types;
- every evidence volume field with the same invalid values;
- every positive threshold at `required - 1`;
- every zero-tolerance evidence field with invalid types and a positive value;
- invalid manifest hash shapes/types;
- invalid and incomplete first-failure record flags;
- 100,000 additional fixed-seed mixed cases.

Result:

| Metric | Value |
| --- | --- |
| Decision | PASS |
| Seed | `3235780353` (`0xC0DE1701`) |
| Deterministic boundary cases | 502 |
| Fixed-seed random cases | 100,000 |
| Total cases | 100,502 |
| Evaluator elapsed time | 1.353570 s |
| Result-ledger SHA-256 | `66e8f83a33dcd2586a5830f62ceeaa3db7ed0b6494b981006b506ee79daccd81` |

The shared worktree advanced to commit
`89520d718f592e025e22a804b932eee9f01d98aa` during parallel work. The
qualification evaluator source has no diff between that commit and the commit
used for the three AgentChecks runs. The batch result is therefore applicable
to the same evaluator implementation, but it is intentionally not represented
as a fourth full-gate run.
