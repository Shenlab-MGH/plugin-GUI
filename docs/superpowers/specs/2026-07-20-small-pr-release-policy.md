# Open Ephys Small-PR Release Policy

**Status:** Adopted for the v1.0.2 improvement line

**Baseline:** Official `v1.0.2` at `c91afebcfb0678a667fb93f6312ed33c56ec640f`

## Unit of change

One change unit contains exactly one user-observable fix or one independently useful engineering improvement. It has:

- one narrowly named branch;
- one short implementation plan;
- one pull request;
- one focused test set;
- one compatibility statement;
- one rollback point;
- one patch-release decision.

Unrelated fixes never share a PR merely because they touch the same subsystem. A large upstream commit is manually narrowed when it also contains UI changes, refactors, or features outside the reported defect.

## Size limits

A normal change unit should target:

- at most one production behavior;
- at most three production files, excluding mechanical build lists;
- at most one focused test file;
- no drive-by formatting or renaming;
- no plugin ABI, configuration format, or route removal;
- a review description that fits on one screen.

Exceeding a limit requires a written reason in the PR. If two parts can be approved or rejected independently, they are separate PRs.

## Required PR evidence

Every PR records:

1. the exact baseline commit;
2. the issue or local reproduction;
3. a failing test or verifier before the change;
4. the smallest implementation that makes it pass;
5. focused test output;
6. full supported-suite result;
7. executable and nine-plugin build result when production C++ changes;
8. GUI/API/hardware checks applicable to the change;
9. compatibility and rollback notes;
10. whether the change is release-worthy or should wait for the next patch.
11. its upstream disposition: already upstream, upstream candidate, fork-only, or requires official design discussion.

“Tests pass” without commands and results is not sufficient evidence. Hardware-unverified behavior remains explicitly labeled.

## Review gates

The author performs a scope review first. A separate review then checks correctness, regression risk, test quality, ownership of the affected behavior, and whether an upstream fix was copied faithfully. No later PR may be stacked on an unreviewed change unless the dependency is explicit and unavoidable.

## Release rule

- The first candidate is the next unused version after `1.0.2`, currently `1.0.3`.
- Before tagging, query official releases and tags again; never reuse an upstream identifier.
- Prefer one runtime fix per patch release while the release process is being established.
- Test-only or documentation-only PRs may be merged without forcing a binary release.
- Every binary release is rebuilt from a clean tree and compared with the prior released baseline.
- A failed release gate delays the release; it does not justify adding more changes to the same PR.
- The local 1.1 line begins only after the official upstream 1.1 release is rebuilt, compared, and accepted as its baseline.

## Initial queue

| Order | Candidate | Single change | Upstream reference |
|---|---|---|---|
| 1 | `1.0.3` | Prevent HTTP/broadcast messages over 512 characters from crashing Message Center | issue #695 / `3e4f98ca` |
| 2 | `1.0.4` | Defensively reject null events in `GenericProcessor::addEvent` | `fc6d2464` |
| 3 | `1.0.5` | Smooth Record Node disk-rate estimation to prevent false low-space stops | issue #692 / `d72dcba` |
| 4 | `1.0.6` | Correct the File Reader chained comparison for modern Clang | issue #701; manually take only the expression fix from `9c890de3` |
| 5 | unassigned | Repair one deterministic test-harness defect per PR | upstream fixture commits reviewed individually |
| 6 | unassigned | Add one HTTP validation rule per PR | `5bf85a85`, then `b5581e9b` |

Version numbers after `1.0.3` remain candidates until the preceding PR is accepted and the upstream collision check passes.

## Official contribution compatibility

Follow `docs/contributing/upstream-contribution-workflow.md`. Fork PRs target the fork integration branch; official contributions are rebuilt as clean branches from current `upstream/development` and target official `development`. Never submit fork release metadata or duplicate a fix already present upstream.

Before selecting a patch, apply the branch classification in `docs/agent-api/upstream-branch-analysis-2026-07-20.md`. Only `development` currently contains post-v1.0.2 integrated work; `testing` remains at v1.0.2 and archive/PR heads are not patch feeds.
