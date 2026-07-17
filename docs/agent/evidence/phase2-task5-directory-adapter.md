# Phase 2 Task 5 directory adapter evidence

Date: 2026-07-16

Scope: prepare one exact native Open Ephys top-level recording directory name
without starting acquisition or recording and without replacing the native
Record Node writer path.

## Verified

- The pure policy rejects non-IDLE state, stale revision, invalid names,
  existing targets, Windows case collisions, and Open Ephys-style ` (n)`
  auto-suffixes.
- The endpoint is asynchronous and schedules ControlPanel work through the
  JUCE message thread.
- `PUT /v1/experiment/directory` is authenticated, session-bound, armed-only,
  size-bounded, strict about unknown JSON fields, and idempotent by
  `command_id` within `run_id`.
- The ControlPanel adapter uses the official parent, prepend, base, append,
  and new-directory controls and reads the exact generated native name back.
- The adapter does not call RecordThread, RecordEngine, or forced recording
  status APIs.
- The full MSVC Release build and all 26 CTest targets passed.
- Agent checks passed: 50/50 official-difference coverage, native core tests,
  23 Gateway tests, 61 MCP/Skill tests, and all source contracts.

## Explicit fork behavior change

Selecting no prepend or append now clears the stored field value as well as
setting its state to `NONE`. Official v1.0.2 could retain a stale hidden value.
This correction is necessary for exact Agent readback and is registered in
`OFFICIAL-DIFF.md`.

## Remaining live gate

The packaged runtime remains intentionally mutation-disabled. Therefore, a
live isolated `PUT` is not claimed in Task 5. The armed live no-record test is
deferred until Task 6 adds scoped approval and synchronization-safe mutation
authorization. Unit and real HTTP tests exercise the same endpoint without
touching acquisition or recording.
