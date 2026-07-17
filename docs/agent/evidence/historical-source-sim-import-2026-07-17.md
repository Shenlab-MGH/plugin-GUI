# Historical Source Sim delivery import

This evidence is a read-only feasibility proof imported from
`D:\OE_delivery_20260716_164811`. It is not current-HEAD qualification and does
not authorize Open Ephys mutation, device operation, or research recording.

## Reproduction

From the repository root, using the locked MCP environment:

```powershell
py -3.12 -m uv run --frozen --project integrations\mcp python tools\windows\validate_historical_source_sim.py D:\OE_delivery_20260716_164811
```

The command reads the delivery and prints deterministic JSON. It does not
write to the delivery, launch Open Ephys, access a device, or use the legacy
native HTTP port.

## Observed historical result

| Field | Value |
| --- | --- |
| Result | PASS |
| Provenance | `historical_official_gui_simulation` |
| Parts | 8 |
| Presets | ordered `All Shanks 1-96` through `All Shanks 673-768` |
| Simulator | `SIM 0.0`, NP2013, 384 channels, 30000 Hz |
| Required files | 5 per part, 40 total |
| Continuous bytes | 1,051,852,800 |
| Samples | 1,369,600 |
| Canonical validated-evidence digest | `d20055ec0e1f4e78f0a5308e8bdb290e2f3f9cc71efc829e2954fd00f1e42b39` |
| Qualifies current fork | `false` |

The validator resolves each recording unit from the report's exact Record Node
settings path plus the requirement that exactly one native recording unit is
present. It never selects the newest directory. It verifies the strict delivery
hash manifest, required-file hashes and sizes, `structure.oebin`, continuous
byte count, NPY array sizes/counts, sequential sample numbers, finite
timestamps, and the report's structural checks.

The JSON field remains named `delivery_sha256` for schema compatibility, but
`delivery_sha256_kind=canonical_validated_evidence_digest` makes clear that it
hashes the canonical validated evidence projection, not every byte in the full
directory tree.

## Qualification boundary

This proves that the historical official-GUI simulator delivery contains one
coherent eight-part recording result. It does not prove that the current fork,
current executable, current coordinator, or current acceptance harness can
reproduce it. Current-fork qualification requires a separately authorized,
fresh run tied to current executable/config/plugin hashes and current audit
evidence.
