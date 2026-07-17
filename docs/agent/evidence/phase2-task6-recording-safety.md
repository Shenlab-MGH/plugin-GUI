# Phase 2 Task 6 recording safety evidence

Date: 2026-07-16

## Verified in code and tests

- Human and Agent recording starts use one
  `requestValidatedRecordingStart` path.
- Missing Record Nodes, invalid active recording paths, unsynchronized streams,
  unknown state, stale revision, and missing exact Agent directory preparation
  fail closed.
- Human operation retains the official synchronization warning and explicit
  Yes/No choice. Agent operation cannot override the warning and behaves as No.
- Agent code never calls the legacy forced recording API.
- Mutation JSON strictly requires run, command, idempotency, expected mode and
  revision, process session, approval, target, and action-parameter hash.
- Reusing a command ID with changed authorization conflicts. Reusing an
  idempotency key under another command also conflicts.
- Runtime mutation is default-off and requires `--agent-mutation`, a bearer
  token, and a bounded process-scoped `OE_AGENT_APPROVAL_ID`.
- The isolated launcher requires maintenance-window approval and injects the
  approval ID only into the child process environment.
- The MSVC Release build passed; all 27 CTest targets, 61 MCP/Skill tests,
  23 Gateway tests, all source contracts, and 52/52 official-difference rows
  passed.

## Not yet claimed

A live `IDLE -> ACQUIRE -> RECORD -> ACQUIRE -> IDLE` Source Sim transition is
not claimed. No validated Source Sim/File Reader plus Record Node configuration
was present in the repository or isolated state directories. Constructing an
unverified XML merely to make the command return success would not meet the
scientific evidence standard. A validated simulation fixture and live evidence
remain required before the eight-part real-device gate.
