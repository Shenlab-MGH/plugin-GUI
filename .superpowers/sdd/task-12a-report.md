# Task 12A implementation report

## Scope and safety

- Read-only historical evidence importer and CLI implemented.
- Open Ephys was not launched; no device or port 37497 was used.
- `D:\OE_delivery_20260716_164811` was read only and was not modified.
- No raw recording data was copied into the repository.

## RED evidence

Initial module contract:

```powershell
py -3.12 -m uv run --frozen --project integrations\mcp pytest -q -p no:cacheprovider integrations\mcp\tests\experiment\test_historical_source_sim.py
```

Observed: `1 failed`; `historical_source_sim.py` did not exist.

Behavior contract after the module stub existed:

```text
ImportError: cannot import name 'HistoricalEvidenceError'
```

Subsequent focused RED cases were observed before implementation:

- `STRUCTURE_OEBIN_MISMATCH`: validator incorrectly accepted a 383-channel
  `structure.oebin`.
- `REPORT_OUTPUT_MISMATCH`: validator incorrectly accepted an ambiguous report
  output root.
- `EVIDENCE_MUTATED_DURING_VALIDATION`: validator initially accepted a
  same-size continuous-file mutation injected during validation.

## GREEN evidence

Focused Task 12A tests:

```powershell
py -3.12 -m uv run --frozen --project integrations\mcp pytest -q -p no:cacheprovider integrations\mcp\tests\experiment\test_historical_source_sim.py
```

Observed after the final Record Node identity case: `23 passed`.

The test matrix covers valid deterministic output, wrong/reordered preset,
duplicate and suffixed native directories, simulator identity, report output
ambiguity, missing file, continuous byte mismatch, `structure.oebin` mismatch,
malformed/traversal/absolute/duplicate/missing/extra/mismatched hash entries,
symlink/reparse escape, secret-like report content, mutation during validation,
CLI behavior, and the explicit current-fork qualification boundary.

Full locked MCP/Skill suite before the final focused refinements:

```powershell
pwsh -NoProfile -File tools\windows\Invoke-OpenEphysAgentMcpTests.ps1
```

Observed: `418 passed, 1 skipped`; the skip was the OS-privileged real-symlink
branch. The test was then refactored to use a reparse detection injection when
Windows denies symlink creation; the final focused suite has no skip.

Final full locked MCP/Skill suite after all refinements: `421 passed in 7.43s`.

## Historical delivery result

Command:

```powershell
py -3.12 -m uv run --frozen --project integrations\mcp python tools\windows\validate_historical_source_sim.py D:\OE_delivery_20260716_164811
```

Observed:

- exit code 0;
- 8 ordered accepted parts;
- 40 required files;
- 1,051,852,800 continuous bytes;
- 1,369,600 samples;
- delivery SHA-256
  `d20055ec0e1f4e78f0a5308e8bdb290e2f3f9cc71efc829e2954fd00f1e42b39`;
- provenance `historical_official_gui_simulation`;
- `qualifies_current_fork=false`.

## Concerns and boundary

The historical delivery's strict `SHA256SUMS.txt` inventory contains four
delivery-summary entries. Raw per-part required files are independently hashed
and included in the deterministic result because they are not entries in that
legacy manifest. This importer proves historical feasibility only; it cannot
qualify the current executable or replace fresh current-HEAD acceptance.
