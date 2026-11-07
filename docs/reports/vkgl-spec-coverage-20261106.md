# Vulkan / OpenGL specification coverage — census and harness raise

Lane `lane-vkgl-coverage`, base integ `faf95d26c`, 2026-11-06.

**Status: PARTIAL — the harness raise is committed, but the probe
verification is outstanding.** The lane's dist build never acquired
the shared build lock, and the 2026-11-06 disk emergency (box at 99%
full, lanes ordered to strip and stop) ended the lane before the
coverage probes could run. The After numbers below are source-level
census counts; the end-to-end matrix numbers are owed. Resume steps
are at the end of this record.

## The instrument

`tools/conformance/vkgl_spec_census.py` (new, this lane) enumerates,
mechanically: for Vulkan — every command name in the Khronos registry
(`vk.xml`, the DXVK-vendored copy the harness builds against), the
guest ICD shim dispatch tables (E() bridged / S() shim-local / TAIL()
loud stub, `tools/rootfs64/libvk{64,32}`), the host bridge's handler
cases (`source/vulkan/vk64bridge.cpp`), and the names the coverage
harness actually calls; for OpenGL — the Khronos-derived extension
table the project carries (`tools/opengl/glfunctions.h`), the bridge
ABI ids and handler cases (`gl64bridge_abi.h`, `gl64bridge.cpp`), the
guest libGL's real and silent-stub exports, and the harness's
exercised names. The same script produced both the Before and After
counts, so the difference is the lane's work and nothing else.

## Before (base tip, census run before any edit)

Vulkan:

- Registry (SPEC): 700 commands.
- Guest-exposed (shim tables): 307 (64-bit) / 304 (32-bit).
- Bridge handler cases: 211.
- Harness (vkcov): loaded 269 exports, **called 266** — the census
  reproduces the 2026-10-26 matrix count and the figure quoted by
  `docs/reports/broad-conformance-analysis-20261030.md`.
- **Called share of the registry: 242 of 700 = 34.6%** (the other 24
  called names are the shim's invented exports, not registry names).
- Exposed but never called: **43**. Implemented but never called: 17.
- Registry names the guest cannot even obtain: 418.

OpenGL:

- Extension table: 2,554 entry points; bridge ABI ids 334 (incl. the
  `_MAX` sentinel); bridge handler cases 333 (incl. the `traceProc`
  plumbing pseudo-function).
- Harness (glcov): resolved 306, **exercised 305 = 91.6% of bridge
  cases**; bridge handlers never exercised: **28**.
- Exercised share of the extension table: 8.3% — stated with its
  caveat: most of that table is vendor-extension entry points the
  guest path cannot name at all (Wine's opengl32 surface is the
  ~336-name WGL set); the bridge/ABI share is the operative one.
- No exercised name was a silent libGL stub (vacuous-row check: 0).

Cross-checks that closed false leads before any filing: every GL ABI
id has a bridge case (an early census parse truncated names at the
underscore and briefly suggested `glGetIntegeri_v`/`glGetBooleani_v`
were uncased — they are cased, ids 862/863); no bridge-implemented
Vulkan function is unreachable through the loader (implemented ∩
not-exposed = ∅); all TAIL stubs were already exercised.

## Finding fixed on this branch

The bridge-coverage probes could not run at all: `vkcov-probe.mjs`,
`glcov-probe.mjs` and `vkcov-probe-matrix.mjs` imported
`acquireProbeSlot` from `/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs`,
a path deleted by the project move, so every coverage run died at
import time. Filed as **FD-195**; fixed on this branch by importing
`./probe-slots.mjs` relatively in all three probes. The closing proof
(a probe run end-to-end) is part of the outstanding verification.

## What changed on this branch

- `tools/vkcov/vkcov.c` (+ rebuilt `vkcov.exe`): new
  `t_extended_coverage()` section, run between `t_surface_swapchain()`
  and `t_stubs()`. It calls all 43 exposed-but-uncalled entry points:
  the DLL exports among them through the harness's timed macros, the
  37 device-extension entry points (extended-dynamic-state and
  synchronization2 families, push-descriptor commands, render-pass-2
  creation, pool resets) resolved through `vkGetDeviceProcAddr` — the
  path a real application uses — with locally owned objects where a
  call is destructive, and a reported single destroy for
  `vkDestroyPipeline`. The section ends with a proc-addr census of
  the 418 registry names the shim does not expose (`VKCOV-GPA:` lines
  in the console log), classifying each by whether either proc-addr
  entry point resolves it anyway.
- `tools/glcov/glcov.c` (+ rebuilt `glcov.exe`): new `run_extended()`
  with rows for all 28 unexercised bridge handlers — fixed-function
  state setters, fog and texture-env rows, matrix loaders,
  client-array enables and pointers, and the eight remaining GLX
  rows — plus the GL constants and PFN declarations they needed.
- `web/tests/vkcov-probe.mjs`, `glcov-probe.mjs`,
  `vkcov-probe-matrix.mjs`: the FD-195 import fix.
- `tools/conformance/vkgl_spec_census.py`: the census.

## After (source level — probe verification outstanding)

Vulkan:

- Harness called: 266 → **309**; exposed-not-called: 43 → **0**.
- **Called share of the registry: 284 of 700 = 40.6%** (from 34.6%).
- Bridge-not-called: 17 → 1, and the 1 is explained:
  `vkCmdSetExtendedDynamicState` is the bridge's internal multiplexed
  trap (ABI id 918) that the shim's dynamic-state wrappers funnel
  through; it has no guest entry point of its own and is exercised
  through the named commands this lane added.

OpenGL:

- Harness exercised: 305 → **333**; bridge-not-exercised: 28 → **0**.
- **Exercised share of bridge cases: 91.6% → 100%** (99.7% of ABI
  ids; the residual is the `_MAX` sentinel, not a function).

## Outstanding (resume steps)

1. Prep a worktree from this branch. Note: the shared store's
   `wine64.zip`/`wine32.zip`/`prefix32.zip` currently mismatch the
   integ pins (another lane refreshed the store); this lane built a
   shadow store (`BW64_SHARED_STORE`) whose three zips are the pinned
   copies from `backups/winbox-runtime-assets-20261106/` and re-ran
   prep successfully (23 links, pins OK). Rebuild it the same way if
   the store is still mismatched.
2. Build dist (`tools/bw64-build.sh`; trust only the literal
   "Browser export verified" line).
3. Run the probes serially (probe slot):
   `TAG=vkgl-vk-20261106 node web/tests/vkcov-probe.mjs`, then
   `TAG=vkgl-gl-20261106 node web/tests/glcov-probe.mjs`.
4. Analyze: `tools/vkcov/vkcov-analyze.py` and
   `tools/glcov/glcov-analyze.py` on the two results files; count
   `VKCOV-GPA:` resolutions in the vkcov console log (any name that
   resolves is loader-exposed beyond the shim table — a finding).
5. Replace this record's After section with the matrix numbers
   (handlers tested, four buckets per API), and file an FD entry for
   every newly convicted gap (broken/stub rows among implemented
   handlers, GPA mismatches), starting at FD-196. FD-195 closes when
   step 3 completes.

## Lane closeout under the disk emergency

The lane freed ~830 MB of its own /tmp scratch (shadow store and
pinned-zip staging) and removed its payload symlinks into that
scratch; its tree had produced no dist/Build output (the build never
acquired the lock), so there was nothing further to strip.
`test-results/vkgl-census/` holds the census outputs
(`before.json`, `after-src.json`, force-added with this record) and
the build log. All heavy work ran on cores 0-2 (the lane's build
processes inherited the SSH affinity; verified by taskset).

## Run phase — attempt of 2026-11-07 (stopped at the disk guardrail)

- The branch was rebased onto the current integration tip
  `334c70be4` (batch 10 landed and deployed); the only conflict was
  this lane's register entry, resolved by renumbering it: the
  batch-10 composition settled FD-195 to the bus-discovery filing,
  so the coverage-probe filing is **FD-201** on this branch
  (provisional; final settlement at batch-11 composition). Register
  audit after the rebase: FD 1..201 each exactly once (standing
  gaps 46/56/57), no duplicates.
- Prep verified against the shared store
  (`winbox-wt-prep.sh`: 23 links, pins OK) — no private store was
  built; the 2026-11-06 store mismatch is gone.
- The dist build was started and waited on the shared build lock
  behind a relay of other lanes' builds; `bw64-build.sh` gave up
  with `FATAL: build lock busy or invalid` after its wait
  (log: `test-results/vkgl-census/build-run-20261107.log`). No
  "Browser export verified" line exists for this branch yet, so no
  probe was run — the matrices remain owed, exactly as listed in
  the Outstanding steps above.
- While the build waited, free disk fell from 12G to 6.0G under the
  combined weight of the running lane wave, reaching this lane's
  stop-and-report line. The lane stopped here with the tree clean
  and nothing of its own to strip (the build never wrote output).
- Resume: re-run step 2 (build) when the build lock and disk allow,
  then steps 3–5 unchanged, except new gap filings now start at
  FD-202 (FD-201 is this lane's renumbered probe filing) and FD-195
  references read as FD-201.
