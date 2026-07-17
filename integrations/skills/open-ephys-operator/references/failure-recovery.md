# Failure and recovery

## Endpoint or MCP failure

1. Stop issuing tools.
2. Preserve the last proven `session_id`, `revision`, and `mode`.
3. Mark the result `BLOCKED` or `INCIDENT`.
4. State that current native state is unknown after the failure.
5. Ask a scientist to inspect the visible Open Ephys GUI.

Do not restart the MCP server repeatedly while a result is ambiguous. Never
restart, close, or stop Open Ephys as recovery.

## Identity or revision mismatch

Treat a session change as a new process. Treat an unexpected revision change
as possible human takeover or another state transition. Discard the prior
preflight and require a fresh observation sequence after human review.

## Human takeover

Manual GUI control wins. Pause Agent work immediately when a human operates
the GUI. Do not interpret pause, abort, disconnect, or client exit as a request
to change acquisition or recording.

## Reporting an incident

Include the last proven state, first failing check/error code, unavailable
evidence, and the exact next human review action. Exclude tokens, raw neural
data, and speculative root causes.
