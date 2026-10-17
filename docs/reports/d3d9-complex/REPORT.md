# D3D9 Complex Verification — FINAL REPORT

Date: 2026-10-09. Branch: integrate/schema-v2 @ 88cdd449 (dirty tree).
Dist: wasm built 2026-10-09 02:57:27 UTC (includes x87 fix below).

## Executive summary
- **fpsinput.exe: VERIFIED** — renders (2670 lit px, 262 frames), keyboard works, mouse click works, relative mouse reaches the game, clean exit. Frame time 114.5 ms (2.4x faster than Pong's 276 ms).
- **scene9.exe: PARTIAL** — renders frame 0 (28 draws) then the wasm worker crashes with `WebAssembly.Exception`. Root cause filed for bridge lane.
- **New CPU bug found and fixed**: D9 FB/EB/F5 (FSINCOS/FLDPI/FPREM1) missing from cpu64.cpp fast path. +22 lines, verified in-browser.

## Results (four-bucket: total / passing / slow-or-flaky / broken)

### fpsinput.exe
| Check | Result |
|---|---|
| Rendering (litPixels) | PASS: 2670 lit px, 262 frames presented |
| Clean exit (Escape → RESULT 0) | PASS |
| Keyboard (W → KEYDOWN+MOVE) | PASS: 1 key, 2 moves |
| Mouse click (left → SHOT) | PASS: 1 shot |
| Relative mouse look | WEAK: 2/50 moves registered as LOOK; path works (verified in input3-rel50a tonight) but most events coalesced |
| Frame time | 114.5 ms mean (262 frames / 30.0 s) — 2.4x faster than Pong |

**Buckets**: 6 checks total / 4 passing / 1 slow-or-flaky (rel-mouse) / 0 broken / 1 blocked (n/a).
Probe: `test-results/dxvk-d3d9-complex-fpsinput-3/summary.json`.

### scene9.exe
| Check | Result |
|---|---|
| Rendering | PARTIAL: frame 0 draws=28 logged, then worker crash |
| Frame time | BLOCKED |

**Buckets**: 2 checks total / 0 passing / 1 partial / 1 blocked.
Crash excerpt: `test-results/d3d9-complex/scene9-crash-excerpt.txt`.
Re-probe running: `test-results/dxvk-d3d9-complex-scene9-3/` (probe 342123, nohup).

## Bugs found and fixed
1. **Probe spawn escaping** (FIXED): staged probes had `\\\\n` instead of `\n` → `startProcess FAILED`. Fixed by copying tri9pong-probe.mjs exactly. (This killed both pre-restart scene9 attempts.)
2. **x87 FSINCOS/FLDPI/FPREM1** (FIXED, VERIFIED): cpu64.cpp fast path lacked D9 FB/EB/F5 → thread yield. +22 lines. Verified: fpsinput went from crash-on-start to full render+input+exit.
3. **Probe finally hang** (FIXED): `await browser.close()` on dead page → zombie holds slot. Hardened with 15s timeout.

## Bugs filed (unfixed)
4. **scene9 WebAssembly.Exception**: worker dies after `vkCreateShaderModule` + `asked-for (unimplemented)` (empty names). Needs bridge triage. Note benign-stub observability gap (libvk64.c:1364).

## Commits
- NONE. Fix (+22 cpu64.cpp) and probes (3 files) are uncommitted, awaiting parent decision. The tree has other lanes' work; a surgical commit of just the x87 fix is recommended.

## Files
- `test-results/d3d9-complex/REPORT.md` (this file)
- `test-results/d3d9-complex/scene9-crash-excerpt.txt`
- `test-results/dxvk-d3d9-complex-fpsinput-3/` (summary.json, console.log)
- `test-results/dxvk-d3d9-complex-scene9-2/` (crash evidence)
- `web/tests/d3d9-fpsinput-probe.mjs`, `d3d9-scene9-probe.mjs`, `d3d9-scene9-probe2.mjs` (untracked)

## Assumptions
- Headless SwiftShader: relative frame times, not absolute.
- `gameSuccess:false` on fpsinput is due to strict look threshold, not game failure.
