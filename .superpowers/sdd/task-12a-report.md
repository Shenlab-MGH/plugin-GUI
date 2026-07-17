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

## Reviewer remediation

### Additional RED evidence

The reviewer regression expansion was run before implementation with:

```powershell
py -3.12 -m uv run --frozen --project integrations\mcp pytest -q -p no:cacheprovider integrations\mcp\tests\experiment\test_historical_source_sim.py
```

Observed: `26 failed, 22 passed`. Failures covered strict `settings.xml`
cross-correlation, timestamp recomputation, canonical manifest aliases,
extended secret patterns, duplicate/non-finite/unknown JSON, same-size and
mtime-restored content mutation, and swap-restore identity mutation. A later
focused hard-link identity regression also failed with the expected prior
behavior (`SETTINGS_XML_MISMATCH` instead of `FILE_IDENTITY_COLLISION`).

### Reviewer fixes

- Windows validation now holds verified handles for the root, every directory,
  and every file with write/delete sharing denied for the full validation.
  Final handle paths, reparse attributes, volume/file IDs, case collisions, and
  hard-link identity collisions are checked. Reads and hashes use those held
  handles rather than reopening paths.
- `settings.xml` is bounded and parsed independently. Per-part OneBox,
  `ProbeA`, SIM firmware, NP2013, Multishank probe, exact preset, second `NONE`
  preset, and Record Node 101 are cross-correlated with report and
  `structure.oebin` evidence.
- Timestamps are recomputed from bounded NPY data and must be finite, never
  `-1`, strictly increasing, approximately 1/30000 seconds apart, and have the
  correct total span and count. Sample numbers are streamed without expanding
  unbounded Python lists.
- JSON parsing rejects duplicate keys and NaN/Infinity, then applies exact
  object allowlists and strict scalar types. Secret scanning includes compact
  key aliases plus GitHub, AWS, JWT, bearer and PEM patterns.
- Manifest paths must be canonical and reject empty segments, `.` aliases,
  traversal, separators, control characters and trailing separators.
- File/header/count sizes are bounded and filesystem `OSError` races become
  stable machine errors.
- `delivery_sha256_kind=canonical_validated_evidence_digest` documents the
  digest boundary.

Real symlink creation is attempted by the integration test. On Windows hosts
where ordinary-user symlink creation is denied, the same branch is exercised
through reparse detection injection; junction/reparse handling is also enforced
by the real Win32 handle attributes in the authoritative-root run.

### Final GREEN evidence

- Focused reviewer suite: `50 passed`.
- Full locked MCP/Skill suite: `448 passed in 13.45s`.
- Authoritative historical root: exit 0, 8 parts,
  `qualifies_current_fork=false`, canonical validated-evidence digest
  `d20055ec0e1f4e78f0a5308e8bdb290e2f3f9cc71efc829e2954fd00f1e42b39`.
