# Eight-shank SOP boundary

## Intended complete workflow

The scientist's target workflow is eight unique shanks, one at a time, each
recorded for 120-180 seconds, with a unique recording target and independent
artifact verification per shank.

## What v0.0.1 read-only can do

- Verify fork/MCP/GUI identity.
- Observe current acquisition/recording mode.
- Run compatibility and state preflight.
- Produce a plan for shanks 1 through 8.
- Preserve native session and revision evidence.

## What remains unavailable

- Detecting or selecting a probe or shank.
- Verifying shank semantic readback.
- Creating or renaming a recording directory.
- Starting/stopping acquisition or recording.
- Measuring accepted recording duration.
- Verifying first block, sustained file growth, synchronization, disk health,
  file closure, parseability, channels, sample rates, metadata, or hashes.

## Planning output

List shanks exactly as `[1,2,3,4,5,6,7,8]`. For every shank mark selection,
recording, duration, and artifacts `UNVERIFIED`. Do not advance a shank based
on video or a timer. Require a scientist and future qualified typed mutation
tools before execution.
