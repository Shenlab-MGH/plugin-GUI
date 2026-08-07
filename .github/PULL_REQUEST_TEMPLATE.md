## Problem

Issue: <!-- Link the fork issue and, when applicable, the official Open Ephys issue. -->

Baseline commit: <!-- Exact commit this branch started from. -->

## Single change

- <!-- Describe exactly one behavior change or engineering improvement. -->

## Verification

- [ ] A failing test or verifier demonstrated the problem before the change.
- [ ] Focused test command and result are included below.
- [ ] Full supported-suite command and result are included below.
- [ ] `open-ephys.exe` and all nine bundled plugins build, if production C++ changed.
- [ ] Applicable GUI, API, platform, or hardware checks are included below.

```text
Focused:

Full suite:

Build/runtime:
```

## Compatibility

- [ ] Plugin API remains 10.
- [ ] Configuration format remains compatible.
- [ ] Existing routes and successful response shapes remain compatible.
- [ ] No unrelated scientific or instrument behavior changed.

## Scope and rollback

Files intentionally changed:

- <!-- path and reason -->

Rollback: <!-- State how this PR can be reverted independently. -->

## Upstream suitability

- [ ] Official `CONTRIBUTING.md`, Code of Conduct, `.clang-format`, issue templates, and applicable workflows were checked.
- [ ] The change was compared with current `upstream/development`.
- [ ] An official issue exists before proposing host-application code upstream.

Upstream disposition: <!-- already upstream / candidate / fork-only / needs maintainer design discussion -->

Official issue/PR: <!-- Link when applicable. -->

## Release decision

<!-- release-worthy patch / merge without binary release / blocked, with reason -->
