# Phase 2 Task 7 preset capability evidence

Date: 2026-07-16

Installed API10 plugin observed at
`C:\Users\Sshen\AppData\Local\Open Ephys\plugins-api10\neuropixels-pxi.dll`:

- SHA-256: `6418949072cc0c829926d6d951522eb0922c8ad0dc4536e2d1a0cd569d3b3d61`
- Size: 1,166,848 bytes
- PE file/product version metadata: absent

No Open Ephys process was running during this check, so no live UIA preset text
or set/readback capability is claimed. The resulting capability remains
`UNVERIFIABLE` until the read-only probe runs against the exact experiment
process. Pixel or video evidence alone cannot raise the classification.

The classifier and confirmation binding passed nine tests. A human
confirmation is invalidated by any change to run, part, expected preset, probe
serial, or configuration revision. The read-only PowerShell probe has a source
contract prohibiting InvokePattern, SetValue, SendKeys, mouse events, and click
operations.
