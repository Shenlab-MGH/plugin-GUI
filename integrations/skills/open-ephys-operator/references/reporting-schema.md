# Reporting schema

Write JSON with exactly these top-level fields:

```json
{
  "schema_version": "oe-agent-observation-report/v0.0.1",
  "status": "OBSERVATION_ONLY",
  "native": {
    "session_id": "session-001",
    "revision": 7,
    "mode": "IDLE",
    "gui_version": "1.0.2"
  },
  "preflight": {
    "pass": true,
    "checks": [
      {"name": "online", "pass": true}
    ]
  },
  "shanks": [1, 2, 3, 4, 5, 6, 7, 8],
  "summary": "Observed IDLE state; no mutation was available or attempted."
}
```

Allowed status values are `OBSERVATION_ONLY`, `BLOCKED`, and `INCIDENT`.
`SUCCESS` is forbidden because this slice cannot verify recording artifacts.
The eight shanks must be present exactly once each. The `preflight.pass` value
must equal the conjunction of all check values.

Never include fields whose names contain token, authorization, password,
secret, or bearer. Keep the summary factual and at most 2,000 characters.
