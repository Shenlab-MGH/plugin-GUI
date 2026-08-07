# Official Upstream Branch Analysis

**Reviewed:** 2026-07-20

**Repository:** `open-ephys/plugin-GUI`

**Baseline:** `main` / release `v1.0.2` / `c91afebcfb0678a667fb93f6312ed33c56ec640f`

## Branch topology

The official repository exposes seven branch heads.

| Branch | Tip | Relation to v1.0.2 | Interpretation |
|---|---|---|---|
| `main` | `c91afeb` | exact v1.0.2 | Released baseline; protected. |
| `testing` | `c91afeb` | exact v1.0.2 | No post-1.0.2 staging content yet; protected. |
| `development` | `e4ffd123` | 115 commits ahead, 0 behind | Only active branch containing current post-1.0.2 work; declares GUI 1.1.0. |
| `copilot/research-record-node-disk-space-detection` | `c91afeb` | exact v1.0.2 | Named research branch with no committed difference; not a source of a newer disk fix. |
| `development-archive` | `3959551c` | historical/diverged | Pre-v1.0 development snapshot from 2025-03; not a current integration base. |
| `archive-v0.6.7` | `8c645395` | historical/diverged | Frozen v0.6.7 history. |
| `archive-v0.5.5.4` | `697922ad` | historical/diverged | Frozen v0.5.5.4 history. |

The archive branches report commits not reachable from current development because the project history diverged. Those commits are not automatically missing fixes. Never cherry-pick an archive commit based only on `git rev-list`; first show that current v1.0.2/development lacks the behavior and that the old implementation is compatible with Plugin API 10 and JUCE 8.

## Development branch state

At review time, `development` contains 115 total commits (110 non-merge commits) beyond v1.0.2. Excluding the deletion of obsolete `Resources/DeveloperTools`, its diff affects 90 files with about 8,578 insertions and 2,072 deletions. This is a release train, not a patch set that should be merged wholesale into the local 1.0.x line.

Plugin API remains `10`, but the GUI version is already `1.1.0`.

The tip has successful official Windows, Linux, and macOS build workflows. The `Tests` workflow is configured for pull requests to `development`/`testing` and pushes to `testing`, not pushes to `development`; therefore the tip's green platform builds are not evidence that the complete unit and Windows integration test workflow ran on the final `e4ffd123` commit.

## Development change groups

### Narrow fixes suitable for individual 1.0.x review

These are candidates for one-at-a-time backport plans, never a group merge:

- `3e4f98ca`: Message Center 512-character truncation crash (#695).
- `fc6d2464`: reject null event/channel input in `GenericProcessor::addEvent`.
- `d72dcba`: smooth Record Node disk-rate estimation (#692).
- the chained-comparison expression inside `9c890de3`: modern Clang build fix (#701). The rest of that commit changes scrubber cursor and rendering and must not accompany the backport.
- `5bf85a85`: HTTP parameter validation, after route-level contract tests are written.
- `b5581e9b`: selected/masked-channel JSON array validation as a separate change.
- `ddb8d7a7`: tab-renaming fix, after an isolated UI regression test.
- `575de13d`: low-channel-height freeze prevention, after an isolated rendering test.
- `326892c`: RecordNode invalid-pointer fix, only after reproducing its lifetime conditions.
- `4cba6872`: clickable crash-report paths, as an independent usability change rather than part of core stability work.

### Changes that require complete series evaluation

Do not cherry-pick isolated commits from these sequences because data layout, timing, or tests evolve across multiple commits:

- RecordNode/DataQueue SIMD, batching, per-stream queues, and block-write optimizations.
- Harp timestamps, Synchronizer behavior, event translation, and per-sample timestamp registry.
- low-sample-rate LFP Viewer and File Reader fractional-carry work (`43c94b`, `b146d7`, `ffd78a`, `3c7204`, `caf430`).
- LFP channel-height, label, rendering, and high-channel-count optimization sequence.
- Plugin Installer table redesign and Windows driver-installer sequence.
- RecordNode fixture/benchmark changes, including later benchmark removal.

For our no-new-feature 1.0.x line, a series is accepted only if it fixes a reproduced v1.0.2 defect, all required commits are identified, and a dedicated clean build/test matrix passes.

### New features excluded from 1.0.x

- Harp barcode decoding and expanded synchronization behavior.
- high-pass filtering and common-average referencing inside LFP Viewer.
- Record Node synchronization status UI.
- vertical DataViewport splits.
- searchable/redesigned Plugin Installer.
- LFP depth display and new visual indicators.
- explicit per-sample timestamp support when it changes the data contract rather than repairing an existing v1.0.2 promise.

These may be evaluated when official 1.1 becomes the new baseline; they are not backports for our current line.

### Build, packaging, and maintenance changes

- GitHub Actions runner/version updates.
- Windows FTDI and VC++ redistributable installer changes.
- removal of obsolete DeveloperTools.
- exclusion of driver installers from the Windows ZIP.
- GUI version changes.

These must be reviewed against our own packaging process and never mixed with a runtime fix PR.

## Testing branch meaning

Official contribution documentation says `development` moves to `testing` two to three weeks before a release. Because `testing` still equals v1.0.2, the current 1.1 development contents have not been promoted through that published branch gate. The local project should monitor `testing`; a new testing tip is a stronger signal to start full 1.1 comparison, but it is still not a released baseline until `main` and an official release/tag advance.

## Open pull-request heads

The two open PRs are not additional official integration branches:

- #670 proposes a breaking RecordEngine API signature change and still targets `main`; it is not compatible with the local Plugin API 10/no-ABI-change constraint.
- #645 targets archived v0.6.7 and infers AUX type from channel names; it is stale relative to v1.0.2/1.1 and is not a safe File Reader patch source without redesign and tests.

## Monitoring and selection policy

1. Fetch all official heads before planning each local patch.
2. Compare the candidate commit against both v1.0.2 and current `upstream/development`.
3. Record whether the change is already upstream, a clean backport, part of a coupled series, a new feature, or obsolete history.
4. Never merge `development`, `testing`, an archive branch, or a PR head wholesale into the 1.0.x line.
5. Require a failing v1.0.2 reproduction before accepting a behavioral backport.
6. Keep one production behavior per PR and rebuild all nine bundled plugins after shared C++ changes.
7. Recheck official build and Tests workflow results independently; one does not imply the other.
8. When `testing` advances, run a separate comparison build without changing the 1.0.x integration branch.
9. When official `main` and the release/tag advance to 1.1, freeze new 1.0.x work and start the verified 1.1 baseline transition.
