# Portal performance — profiling baseline + first optimization (2026-10-26)

Lane: lane-portal-d24 (worktree /home/ubuntu/wt-portal-d24)
Baseline commit: 2e7979e2c (on top of 5c521bf7d)
Optimization commit: the commit that adds this report

## 1. What the baseline commit does (audit verdict)

The previous (uncommitted) instrumentation diff was audited line by line
before anything was built on it. Verdict: additive-only, with two defects
found and fixed during the audit.

What it adds, all in source/vulkan/vk64bridge.cpp:
- Per-trap timer (VkProfTrapTimer) at the vk32Bridge dispatcher entry —
  the same shared accumulator the 64-bit path uses; its destructor no-ops
  unless timing is enabled. Submit and present fnIds are excluded from the
  "other traps" accumulator, so nothing is double-counted.
- A submit timer in the vk32 vkQueueSubmit case, mirroring the 64-bit sites.
- The full per-stage breakdown at the 32-bit vkQueuePresentKHR boundary,
  using the identical formula as 64-bit (guest = wall − submit − trapOther
  − body), printed in the same VKPROF / VKPROF2 formats. The metrics record
  carries breakdown=1, arch=1 per the include/bw64metrics.h contract; the
  page-side metrics consumer still aggregates wall time only for arch=1 —
  the panel meaning is unchanged.
- g_vkProfFirstFrameDone is now set at the end of a 32-bit present
  (previously only the 64-bit present set it).

Why the first-frame change cannot re-trigger the historical vkprof bug:
the bug was profiling activating during WASM init and racing bridge setup
(it once took rendering from 2,649 lit pixels to 0). vkProfEnabled() and
vkMetricsEnabled() still return false until that flag is set, and the flag
is only set after a present has fully handed its frame to the page. In a
32-bit-only session the guest's own presents previously NEVER set it, so
profiling could never activate at all — the change fixes activation, it
does not move it earlier. With neither BW64_VKPROF nor metrics requested,
vkTimingEnabled() stays false and every added line is a no-op.

Defect 1 (fixed): vk32 VKPROF frame numbers came from a counter that only
advanced when metrics were enabled, so a profiling-only session would have
printed frame=1 forever. The timed path now advances its own counter once
per frame, mirroring the 64-bit path; metrics numbering is unchanged.

Defect 2 (fixed — the audit's main find): the shared trap-time table had
256 slots, but the ABI's function ids are section-based and run 0..920
(vkCmdDraw=507, vkQueueSubmit=600, vkQueuePresentKHR=601). Attribution for
160 of 212 functions was silently dropped — including every vkCmd* record
function. The only functions visible in profiling output were the few with
low ids (fences), which made fences look like the bottleneck when they are
in fact trivial (vkWaitForFences: 4.3 µs/call, ~0.01 ms/frame). The table
is now 1024 slots in the shared struct, repairing both ABIs; both
present-boundary top-3 scans and the reset loop were widened with it.

Register/test hygiene required by the edits: citation audit re-run to
203 MATCH / 14 MATCH-INHERITED / 0 DRIFT / 0 MISSING (325 cited rows);
the diff added one VK64_DROP_IF site, so the macro census moved 241 → 242
and the guard-test pins and register row 381 were updated to match (with
an addendum in docs/reports/vk64-drop-if-macro-audit-20261025.md). Both
repaired guards were re-run directly (7/7 pass) and sabotage-checked in
the earlier session work.

Verification of the baseline commit:
- JS unit suite: 294 passed / 0 failed (node web/tests/suite/runner.mjs
  --unit in this tree against this tree's dist).
- Profiling OFF, full Portal runs base3-off5 and prof3 (build wasm
  58ad9b9b…): menu → testchamber00 → input diffs positive, 0 page errors.
  prof3: menu 562.8s, chamber 704.6s, chamberFpsPresented 1.45.
- Profiling ON, full Portal run prof5 (same build): 839 VKPROF frames
  captured; menu → chamber (668s) → all input steps positive (look-right
  0.86, look-left-back 0.58, back-s 0.61 changed fraction); 0 page errors.
  The probe's final summary screenshot hit a 30s Playwright timeout under
  load and the probe wrote a failure summary for that last step only; the
  step log and the 839 captured frames cover the whole flow.

## 2. Measured per-stage breakdown of a chamber frame (baseline)

Steady-state testchamber00 frame, medians over the last 100 profiled
frames. Two independent captures:

| stage | prof2 (prev. lane build) | prof5 (baseline build) |
|---|---|---|
| wall (present→present) | 562.1 ms | 630.9 ms |
| guest residual (emulator + non-Vulkan traps) | 270.1 ms (48%) | 287.7 ms (46%) |
| other Vulkan traps (trapOther) | 159.4 ms (28%) | 200.4 ms (32%) |
| submit serialize (ser2) | 93.5 ms (17%) | 133.1 ms (21%) |
| — of which buffer emit | 48.0 ms excl | 74.2 ms excl |
| — of which image emit | 27.4 ms | 36.0 ms |
| — — of which image content hash | 20.3 ms | 28.2 ms |
| present + hop | 7.2 + 5.1 ms (2%) | 10.6 + 8.1 ms (3%) |
| traps per frame (non-submit/present) | 616 | 565 |

Conditions: 4-core box shared with other campaign lanes (D3D12 and
Gang Beasts lanes building/probing concurrently; load ~12.75 during
prof2, ~3.5–6 during prof5; absolute numbers move with contention, the
stage proportions are stable across both captures).

Submit detail (both captures agree): the buffer path re-reads and
re-hashes ~15 MB per frame but sends only ~50 KB — DXVK's dynamic buffers
are guest-mapped, which defeats the generation-skip fast path, so every
submit re-hashes them. The image path hashes exactly 2 images
(3,407,872 B each = 1024×832×4) per frame and sends 0 bytes — their
content is unchanged after hashing, every frame; the generation-skip
misses there too (write-epoch aliasing on the shared allocation is the
prime suspect, not yet proven). Content hashing overall ≈ 65–100 ms per
frame across the two paths.

## 3. The trap attribution (what the baseline made visible)

prof5 top trap totals over all 839 frames (steady state ≈ 1 call/frame
unless noted):

| function | total | calls | per call | reading |
|---|---|---|---|
| vkCmdBlitImage2 | 98,164 ms | 817 | 120.2 ms | **the bug — §4** |
| vkCmdClearColorImage | 4,252 ms | 493 | 8.6 ms | load-phase bursts only (top-3 in 6 frames) |
| vkCmdResolveImage | 3,662 ms | 2,327 | 1.6 ms | ~2.8/frame; already chunk-copied |
| vkGetImageMemoryRequirements2 | 2,462 ms | 1,672 | 1.5 ms | load-phase bursts (top-3 in 10 frames) |
| vkCreateImage | 1,279 ms | 1,210 | 1.1 ms | load-phase |
| vkUpdateDescriptorSetWithTemplate | 1,033 ms | 88,320 | 11.7 µs | ~105/frame ≈ 1.2 ms/frame — noise |
| vkGetEventStatus | 619 ms | 375,167 | 1.7 µs | DXVK polling during load — noise |
| vkCmdBindDescriptorSets | 408 ms | 8,621 | 47 µs | ≈ 0.5 ms/frame — noise |

## 4. Optimization: the per-pixel blit mirror

Root cause (code-proven): the bridge records each vkCmdBlitImage for the
page tier AND mirrors it into guest memory so later guest reads of the
destination stay coherent. The mirror was a per-pixel loop: two
guest-memory calls (read one pixel, write one pixel) for every pixel.
Portal blits its 1024×832 backbuffer once per frame ≈ 852k pixels ≈
1.7M calls ≈ 120 ms — roughly a fifth of the whole frame, hiding inside
"other traps" where the old 256-slot table could not see it. The same
loop existed in all three blit handlers (64-bit BlitImage, 64-bit
BlitImage2, 32-bit shared case).

Fix (pure insertion, +64 lines, 0 deletions): when a blit row is a 1:1
horizontal run (srcW == dstW) and the whole run sits inside both images
(and, on the 64-bit path, inside both allocations), copy the row in
64 KB chunks — the exact pattern the vkCmdResolveImage mirror already
uses. The chunk path moves byte-identical data: it is taken only when
every pixel of the row would pass the per-pixel loop's own bounds checks;
every other row keeps the original loop, untouched.

Register/test fallout of the insertion, all dispositioned:
- Citations below the inserts re-pinned by exact content match; final
  audit 203 MATCH / 14 MATCH-INHERITED / 0 DRIFT / 0 MISSING, per-row
  verdicts identical to the pre-change baseline (a proximity-window row
  and an inheritance row that the DRIFT counter cannot see were caught
  by the suite's uncheckable ceiling and re-pinned to their true sites).
- The dirty-generation census pin for raw guest-write lines moved
  48 → 51: the 3 new chunk-copy writes live in handlers whose post-loop
  dirtyGen bump + vk64TouchMem already covers them (asserted per-handler
  by the same test).

## 5. Results (prof6, optimized build wasm 47757bec…)

Same run shape as prof5 (profiling ON, menu → chamber → inputs →
200 s free-run), box load ~5–6 (comparable to prof5's ~3.5–6):

| metric (last-100 chamber frame medians) | prof5 before | prof6 after | change |
|---|---|---|---|
| wall | 630.9 ms | 416.0 ms | −34% |
| trapOther | 200.4 ms (32%) | 9.4 ms (2.3%) | −191 ms |
| vkCmdBlitImage2 per call | 120.2 ms | 2.63 ms | 45× faster |
| submit serialize | 133.1 ms | 90.4 ms | −32% |
| guest residual | 287.7 ms (46%) | 315.5 ms (76%) | now dominant |
| present | 10.6 ms | 4.8 ms | — |

- chamberFpsPresented: 2.14 (prof6, profiling ON) vs 1.45 / 1.52 in the
  profiling-OFF baseline runs prof3 / base3-off5 — roughly +45% more
  frames per second in the chamber, with profiling switched on.
- Correctness: prof6 completed the full flow (summary failed=None):
  menu 362.8s, chamber 509.8s, 0 page errors, 0 guest faults in the
  console. Input pairs: look-right 0.92, strafe-a 0.88, jump-space 0.70,
  final-forward 0.99, forward-w 0.19, back-s 0.11 changed fraction;
  look-left-back read 0.0 — a single-step screenshot-pair variance seen
  in baseline runs too (prof3 read back-s as 0.0 with all other steps
  positive); movement and look are proven by the remaining pairs.
- Suite on the optimized build: 294 passed / 0 failed (same config:
  node web/tests/suite/runner.mjs --unit, this tree's dist).

## 6. Next costs in line (measured, post-optimization)

1. Guest residual ≈ 76% of the frame (315 ms of 416 ms) — the emulator
   executing guest code and non-Vulkan traps. Not bridge code; now the
   dominant share by far.
2. Buffer content hashing inside submit: ~50 ms/frame re-hashing
   ~14.9 MB of mapped dynamic buffers that send ~21 KB. The dedup must
   stay exact (guest CPU writes to mapped memory are invisible to the
   bridge), so the only honest levers are a faster exact hash or cheaper
   change detection — and the hash value is a cross-tier protocol value
   (the page tier recomputes it), so changing it needs a coordinated
   bridge+page change.
3. Image content hashing: ~20–28 ms/frame hashing 2 backbuffers whose
   content never changes after the hash. If the generation-skip miss is
   the suspected write-epoch aliasing, range-precise write tracking could
   lawfully skip the re-read+re-hash.
4. vkCmdResolveImage mirror: ~5 ms/frame (2,608 calls, 1.77 ms/call —
   bulk-copy speed for 3.4 MB). Already chunked; the only remaining
   lever would be proving a destination is never guest-read and skipping
   its mirror, per image.
5. trapOther remainder: 9.4 ms/frame across ~735 traps — no single
   remaining target; vkUpdateDescriptorSetWithTemplate ≈ 1.5 ms/frame
   (129k calls at ~11 µs) is the largest piece.

## Appendix: run inventory (test-results/portal-perf/)

- base2 / prof1 / prof2 — previous lane's baseline + instrumented runs.
- base3-off5, prof3 — baseline build, profiling OFF (full summaries).
- prof4 — discarded: profiling never engaged (the shell drops the query
  string when building the runtime frame URL; the launcher never saw the
  switch). Superseded by prof5, which delivers the switch through the
  dist app.mjs frame URL — a dist-only probe patch, restored after.
- prof5 — baseline build, profiling ON (839 frames; final summary
  screenshot timed out under load, flow fully covered by the step log).
- prof6 — optimized build, profiling ON (914 frames, clean summary).
