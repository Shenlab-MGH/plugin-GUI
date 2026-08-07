# Upstream Issue Triage for the v1.0.2 Improvement Line

**Reviewed:** 2026-07-20

**Upstream:** `open-ephys/plugin-GUI`

**Baseline:** release `v1.0.2`, commit `c91afebcfb0678a667fb93f6312ed33c56ec640f`, Plugin API `10`

## Current upstream state

- The latest published upstream release and tag are still `v1.0.2`.
- Upstream `main` still points to the v1.0.2 commit.
- Upstream `development` is 115 commits ahead of v1.0.2 and identifies itself as `1.1.0`.
- An upstream maintainer stated on 2026-07-09 that an official GUI v1.1.0 release was planned for the following week. No v1.1.0 release or tag was present when this review was performed.
- The repository currently has 45 open issues and 2 open pull requests when pull requests are filtered out of the GitHub Issues API response.

The local project will continue from v1.0.2 through incremental `1.0.x` releases, beginning with the next unused patch number. It must check upstream before every public package. Once official upstream 1.1 is available, that release is rebuilt and compared before the local project enters its 1.1 line.

## Release-blocking upstream fixes

These defects affect existing v1.0.2 behavior and fit the local no-new-feature scope.

| Priority | Issue | Finding | Upstream evidence | Local action |
|---|---|---|---|---|
| P0 | #695 HTTP message crash | A message longer than 512 characters was truncated incorrectly; no TEXT event was created and a null pointer reached event processing. Reproduced on v1.0.2/Windows 11. | Fixed on `development` by `3e4f98ca` and hardened by `fc6d2464`. | Backport the minimal fixes and add boundary tests for 511, 512, 513, complex JSON, and invalid/no-event cases before adding any HTTP aliases. Preserve the documented 512-character limit. |
| P0 | #692 false disk-space stop | The five-second instantaneous data-rate estimate reacts to write-back-cache spikes and may stop a valid recording despite terabytes free. The reporter verified an exponential-moving-average patch. | Fixed on `development` by `d72dcba`. | Backport after diff review; add deterministic estimator tests and a Windows recording smoke test. Treat this as recording safety, not an API feature. |
| P1 | #701 macOS build failure | New Xcode/Clang treats the chained comparison in File Reader as an error. | Fixed on `development` in `9c890de3`. | Backport the narrow expression fix and add macOS CI/build verification. |
| P1 | RecordNode test fixture failures | The local baseline found missing application services in the test fixture. | Upstream commits `ca0d7025`, `ce93d6d7`, and `d6117d41` repair fixture initialization and multi-stream tests. | Compare and selectively backport the upstream test repairs before designing a local substitute. |
| P1 | HTTP parameter validation | Existing HTTP handlers need stricter parameter validation. | Upstream commits `5bf85a85` and `b5581e9b`. | Review and backport compatible validation plus contract tests before publishing the API contract. |
| P1 | #703 low-rate LFP rendering | Reproducible without acquisition hardware on Windows and macOS; the display drifts and can fail at 1 Hz. | Addressed by upstream commits `43c94b04`, `ffd78ac0`, `3c720497`, and tests in `caf430d9`. | Reproduce against v1.0.2, then backport the complete fix/test set if it does not require unrelated new LFP features. |

## Investigate before accepting as a local defect

| Priority | Issue | Analysis | Required evidence |
|---|---|---|---|
| P1 | #621 messages saved twice | Confirmed upstream when a Record Node is downstream from a Merger, but the report predates v1.0.2 and remains open. | Build a headless Merger-to-RecordNode regression fixture and determine whether v1.0.2 still duplicates GUI and HTTP TEXT events. |
| P1 | #699 channel map changes signal | The screenshots are consistent with already-flat channels becoming interleaved after a map; upstream has not confirmed a software defect. | Use a deterministic synthetic source and assert channel sample identity before/after a known permutation. Do not change signal behavior without a failing test. |
| P1 | #592 Apply to all can misreport probe configuration | This is a serious GUI/model mismatch, but it is Neuropixels-plugin and timing dependent and was reported on v0.6.6. | Verify with the current Neuropixels plugin and hardware. Keep it out of core changes unless v1.0.2 can reproduce it. |
| P2 | #690 Spike Viewer panels missing | v1.0.2 report; deleting and re-adding the viewer restores the panels. | Create a no-hardware configuration test that adds an electrode after initial viewer configuration. |
| P2 | #700 AUX Auto range/units | The source processor supplies the Auto range; part belongs to the Acquisition Board plugin. The remaining label may display mV while the cursor value is volts. | Separate source metadata from viewer unit rendering and test each owner independently. |
| P2 | #632 File Reader crash | Older API8/Open Ephys-format recordings load after manually changing the recording number to 1. | Add malformed/legacy metadata fixtures, but do not infer that current Binary-format or v1.0.2 recordings are affected. |

## Hardware/plugin-owned or legacy reports

- #694 is an Acquisition Board plugin/gateware performance problem on Linux/macOS. Upstream says plugin v2.0 and gateware v2.0 must be paired. It is not a core-GUI fix.
- #676 concerns external TTL recording control on GUI v0.6.7 and first requires verification of pulse semantics and current plugin behavior.
- #642 concerns Open Ephys-format event files on GUI v0.6.7/API8.
- #559 concerns timestamp resets on GUI v0.6.0.
- #586 concerns synchronized Neuropixels/NIDAQ timestamps and needs the original data and hardware topology.
- #472 is a longstanding ADC unit-contract question and may require a documented compatibility decision rather than a local scaling change.

These reports stay in the hardware/legacy matrix. They must not be represented as fixed merely because the core GUI builds or File Reader smoke tests pass.

## Explicitly outside the current release scope

Requests for new File Reader capabilities, new processors, notch filtering, Record Control buffering, analog output, cursors, display controls, or similar additions are product features. They are not part of the current improve/fix/publish release unless the work is narrowed to a reproducible regression in behavior already promised by v1.0.2.

## Upstream-integration policy

1. Record the issue, upstream commit, affected files, and reproduction before changing local code.
2. Prefer a minimal cherry-pick or manually equivalent backport over independently reimplementing a confirmed upstream fix.
3. Backport fixes independently; do not merge all 115 development commits. That branch includes new recording, synchronization, LFP, viewport, and installer features outside the local scope.
4. Add or strengthen a regression test for every accepted backport.
5. Rebuild the executable and all nine bundled plugins after each ABI-sensitive group.
6. Check upstream releases, tags, `main`, `development`, and relevant issues again at the release-candidate gate.
7. If official 1.1 is published, freeze new 1.0.x work, rebuild and compare it, and use it as the baseline for the local 1.1 line.
