# Open Ephys Fork and Upstream Contribution Workflow

## Authority

The `CONTRIBUTING.md` currently visible on `Shenlab-MGH/plugin-GUI:development` is byte-for-byte identical to `open-ephys/plugin-GUI:development`, and both development branches currently point to `e4ffd12361915a03679398b4228f3f6d34df5950`. It is inherited official guidance, not a fork-specific release policy.

Follow that document for contributions intended for official Open Ephys. For the fork's v1.0.2-based `1.0.x` line, follow the small-PR policy and target the fork integration branch selected for that line; do not target `Shenlab-MGH/development` while it mirrors official 1.1.0 development.

Before preparing an upstream contribution, re-read the current files on the official `development` branch:

- `CONTRIBUTING.md`;
- `CODE_OF_CONDUCT.md`;
- `.clang-format`;
- the applicable `.github/ISSUE_TEMPLATE/*` file;
- `.github/workflows/tests.yml` and the platform build workflows.

If upstream changes these files, the current upstream instructions win over this document.

Also fetch and classify every official branch using `docs/agent-api/upstream-branch-analysis-2026-07-20.md`. `development` is the contribution target, but `testing` indicates release staging and archive branches provide historical context only.

## Official requirements

For host-application code, the official project asks contributors to:

1. open an issue describing the work;
2. make the change in a fork;
3. submit the pull request to official `development`;
4. allow the maintainers' `development -> testing -> main` release cycle to control the official release.

Documentation for users belongs in `open-ephys/gui-docs` and targets that repository's `main` branch. New instrument functionality is normally expected to live in a plugin, not in the host application.

The official repository currently provides bug-report, feature-request, and troubleshooting issue templates. It does not provide a pull-request template. The template in this fork is an additional review aid and must not be described as an official template.

## Two-track branch model

### Fork release track

- Integration base: `agent-native` until the maintainers of this fork choose a replacement.
- Change branch: one branch per change, such as `fix/message-length-512`.
- Pull request target: the fork's integration branch.
- May include fork release evidence after the portable code PR is approved.
- Version bump and tag use a separate release-only PR.

### Official contribution track

- Base: current `upstream/development`, never the fork's release branch.
- Branch: `upstream/<issue-number>-<short-topic>`.
- Contains only the portable code, test, or documentation change relevant to the official issue.
- Excludes fork version changes, release notes, agent-specific plans, local paths, branding, and unrelated backports.
- Pull request target: `open-ephys/plugin-GUI:development`.

Use a separate worktree when preparing the official branch so fork-only commits cannot leak into its history.

## Eligibility decision

Before opening an upstream PR, answer in order:

1. Does an official issue already describe the work? If not, open one with the correct official issue template and wait for maintainers when the design is non-trivial.
2. Is the change already present on `upstream/development`? If yes, do not submit duplicate production code.
3. Is a missing regression test, portability correction, documentation improvement, or independent fix still useful upstream? If yes, narrow the upstream PR to that contribution.
4. Does the patch add fork-specific API surface, semantic naming, release metadata, or policy? Keep it in the fork unless upstream explicitly accepts the design in an issue.
5. Can the patch be reviewed and reverted independently? If not, split it.
6. Is the candidate copied from an archive, testing, or pull-request head? If yes, prove it is still absent and valid on current `development`; a branch name or unique commit count is not sufficient.

## Formatting and test parity

- Format only touched C++ lines with the repository `.clang-format`; do not reformat whole legacy files.
- Preserve C++17, Plugin API 10, the existing license headers, and platform-neutral behavior.
- Run the narrow local test first and the full supported suite second.
- For an upstream-ready code PR, reproduce the relevant official CI path locally when practical:
  - Ubuntu 22.04, GCC 10 unit tests with `BUILD_TESTS=ON`;
  - Windows/Visual Studio 2022 build and Windows integration tests for GUI/API changes;
  - Xcode Release build for macOS/compiler-sensitive changes;
  - Windows, Linux, and macOS builds for shared production code.
- Never claim a hardware result without the named hardware test actually running.

## Upstream PR body

Because upstream has no PR template, use a concise body containing:

```markdown
## Problem
Closes #<official-issue-number>.

## Change
- <one behavior change>

## Verification
- `<exact focused command>` — PASS
- `<exact broader command>` — PASS
- Hardware: not required / exact hardware and result

## Compatibility
- Plugin API: unchanged (10)
- Configuration format: unchanged
- Existing routes/UI behavior: unchanged except for the reported defect

## Scope
No unrelated refactoring, formatting, release metadata, or fork-specific behavior.
```

Do not promise `Closes` when the PR only adds a test for a fix already merged; use `Regression coverage for #<number>` instead.

## Review and synchronization

- Rebase the official contribution branch on the latest `upstream/development` before final verification.
- If upstream changes the same lines, re-evaluate the design instead of resolving conflicts mechanically.
- Address official review comments in the official branch first, then port accepted corrections back to the fork when applicable.
- Record the upstream PR and final upstream commit in the fork's release notes.
- If upstream rejects or redesigns the change, document the reason before retaining a divergent fork implementation.
