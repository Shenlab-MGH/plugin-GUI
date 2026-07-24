# Agent worktree lanes

**Date:** 2026-07-24

## Ownership

| Lane | Worktree | Branch | Owner |
|------|----------|--------|--------|
| Open Ephys **1.0.2** agent-native | `../01-open-ephys-uia-root` | `agent-native-v102` | **Codex / ChatGPT** |
| Open Ephys **1.1** agent-native | `../01-open-ephys-1.1-agent` (this tree) | `agent-native-v110` | **Grok** |

## Rules

1. Do **not** edit files under the other lane’s worktree.
2. Do **not** `git reset --hard`, discard, or force-push the other lane’s branch.
3. Port **only committed** changes from the other branch after that owner has a clean tree / explicit handoff.
4. Shared skill/MCP under `../.agents/` is coordination surface — prefer additive edits; avoid rewriting mid-flight.
5. Builds stay local: `Build-Tests/` is untracked; do not commit it.

## This lane (1.1) baseline

- Upstream tip: `e4ffd1236` (declared 1.1.0) + agent-native UIA/API port
- HTTP: port 37497, `/api/capabilities`, `/api/disk`, `/api/time`, `/api/recording/options`
- Main window: `oe.window.main`, `setAccessible(true)`
- Tests: `Tests/UI/*Accessibility*`, `Tests/API/*`

## Handoff to 1.0.2 → 1.1

When Codex commits a coherent 1.0.2 change set, port with an explicit cherry-pick or file copy **into this worktree only**, then rebuild/tests here before merging behavior.
