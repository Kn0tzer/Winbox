# Boxedwine64 suite

The suite discovers Node unit checks and browser probe classes. A green
result proves those assertions. Source/report guards, fake GPU devices and
real guest runs provide different evidence; see the [test catalog](../CATALOG.md)
for subsystem and 32/64-bit coverage.

```sh
npm test                                     # legacy Node + suite unit files
node web/tests/suite/runner.mjs --unit         # no browser
node web/tests/suite/runner.mjs --browser      # runtime and guest assets needed
node web/tests/suite/runner.mjs --unit --filter=d3d11
node web/tests/suite/runner.mjs --unit --json
```

Filtering uses `--filter=value`. Exit zero means the selected files passed;
native CPU checks and flat guest/browser probes have separate entrypoints.
The maintenance baseline contained 93 unit files, 11 browser files and nine
legacy Node test files, all preserved. Discovery includes new regression
files automatically; these historical counts are not fixed coverage targets.

## Layout

```text
runner.mjs            discovers unit/*.test.mjs and browser/*.test.mjs
lib/assert.mjs        check/checkEqual/checkGreaterThan helpers
lib/probe-base.mjs    slot, server, browser and console capture
unit/                 source/package/report guards and behavioral checks
browser/              guest/browser ProbeTest subclasses
fixtures/             SPIR-V, WGSL and fake WebGPU inputs
```

Unit files run in separate Node processes with a 120-second timeout.
Browser runs have their per-test timeout plus setup/teardown grace; the base
class bounds setup and teardown. A timeout reports failure. The no-stall
guards inspect these mechanisms; they do not prove that every asynchronous
resource cancels immediately.

## Coverage limits

CPU/Vulkan/OpenGL/D3D `entirety` files primarily inspect source coverage
or historical reports. They do not execute every instruction/API. The D3D
matrix is at `docs/reports/d3d-conformance-MATRIX.md` and pins recorded
failures. `game-capability-matrix` checks profile/test mappings and
documentation; passing it cannot guarantee game playability.

Seven browser checks expect unimplemented `?test=...` fixtures and completion
globals: `audio-tone`, `boot32-vkboot`, `celeste-boot`, `d3d11-triangle`,
`gl32-triangle`, `input-keyboard`, `input-mouse-rel`. Their assertions remain
intact, but they may time out and are not verified browser coverage. Real
flat probes such as `wine32-boot-regression.mjs`, `d3d-conformance.mjs`,
`game-probe.mjs` and `tri11-93-stats.mjs` are retained. The comprehensive
D3D classes stage real guest binaries but check fewer operations than their
comments imply. Full stack coverage, including every architecture variant,
remains incomplete.

## Adding a check

Add `unit/<subsystem>-<case>.test.mjs` for behavior executable under Node,
using real functions/fixtures where practical. Label source-string or
recorded-report checks as static. Preserve assertions that fail with the
regression present.

Add a `browser/<case>.test.mjs` ProbeTest subclass for real guest/GPU behavior.
Wire an actual runtime/page hook, stage its guest fixture, use bounded waits
and validate guest output or measured pixels/audio/events. An API call,
mapped window or mentioned string cannot substitute for the intended output.

Run the focused check and the unit suite. Keep fixtures/useful probes outside
ignored output directories and record runtime hashes with browser evidence.
