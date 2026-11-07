# Vulkan / OpenGL specification coverage — census and harness raise

Lane `lane-vkgl-coverage`, base integ `faf95d26c`, 2026-11-06.

**Status: COMPLETE — the run phase finished 2026-11-07.** The
harness raise was committed in 2026-11-06; the end-to-end probe
verification it owed was completed on 2026-11-07 (see the final
section of this record). All three coverage probes ran to their
RESULT lines against a verified build of this branch's tip, and the
After section below now carries the measured four-bucket matrices
next to the source-level census counts. FD-201 is closed (CL-143);
the gaps the runs convicted are filed as FD-204 to FD-209.

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

## After — census counts and the measured matrices (2026-11-07)

Source level (the harness raise itself, measured 2026-11-06):

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

Measured end-to-end (run phase, 2026-11-07; build identity and run
history in the final section; analyzer output committed at
`test-results/vkgl-census/matrix-vk64-run-20261107.md` and
`matrix-gl64-run-20261107.md`):

Vulkan — 309 handlers tested, two complete runs with identical
return codes on every name:

- Fully working: **221**
- Slow or flaky: **3** — all slow, none flaky: vkCreateInstance
  (15 ms), vkCreateShaderModule (16 ms), vkMapMemory (17 ms),
  creation-class calls marginally over the 10 ms bar on a shared,
  loaded box. Recorded, not filed.
- Broken or stub: **85** = 62 loud stubs answering
  VK_ERROR_FEATURE_NOT_PRESENT (-8) by design; 10 crashed (-2);
  13 other-error answers, of which 3 are analyzer mis-buckets of
  non-VkResult returns (vkGetBufferDeviceAddress returned the
  value 1 — a device address, not an error code; the two
  presentation-support queries returned VK_TRUE). Genuinely
  broken: **10**, all answering -3 (VK_ERROR_INITIALIZATION_FAILED)
  — filed as FD-206.
- The 10 crashed: the vkCmdCopyImage trio and the
  vkCmdCopyImageToBuffer2 pair (bridge exceptions the guest crash
  handler contains; known and harness-skipped since 2026-10-09);
  the vkCmdBlitImage family, 3 (new — FD-204); the
  vkGetDeviceProcAddr / vkGetInstanceProcAddr pair, 2 (new —
  FD-205).
- Proc-addr census of the 418 registry names the shim does not
  expose: 405 resolve through both proc-addr entry points — 403
  of them to one shared catch-all thunk address, the other two
  being vkGetDeviceProcAddr and vkGetInstanceProcAddr themselves;
  13 resolve to nothing (the descriptor-buffer and
  cooperative-matrix families). Deliberately NOT filed: the shared
  thunk is the loader-side form of the project's designed loud
  stub — calling it answers -8, as the stub rows measure — and
  the register's standing disposition treats those as by-design.
  Recorded here so the count is not lost.
- Noted, not filed: vkGetBufferDeviceAddress returned the value
  1 in both runs. The harness records only the return value and
  cannot distinguish a handle-space answer from a wrong address;
  flagged here for any future buffer-device-address work.

OpenGL — 334 rows tested (the 333 harness names plus the
traceProc plumbing row), complete run:

- Fully working, under the 10 ms bar: **4** (glDeleteBuffers,
  glDeleteFramebuffers, glDeleteRenderbuffers, glDeleteTextures).
- Slow or flaky: **203** — 202 answered GL_NO_ERROR on both calls
  but measured ≥ 10 ms, with timings quantized at ~17/33/50 ms;
  plus 1 flaky (glCompressedTexImage3D, in FD-208). Adjudication:
  in the same environment the Vulkan calls measured 0–3 ms, so
  the GL slow bucket records the GL path's frame-paced round trip
  (≈ one to three 16.7 ms frames per call), not 202 individual
  handler defects; no per-handler performance filing is made
  from it.
- Broken or stub: **127** = 103 rows with no callable proc
  (32 glX*/traceProc rows, which are not Windows-path entry
  points, and 71 real entry points the guest cannot resolve —
  FD-209); 3 crashed (-2: glGetAttachedShaders, known since
  2026-10-09; glTexStorage2DMultisample — FD-207;
  glTexStorage3DMultisample on the shared-case precedent);
  21 answering GL errors (FD-208).

## Outstanding (resume steps)

*(Discharged 2026-11-07 — see the final section. Two details in
the steps below are superseded by events: the filing number in
step 5 reads FD-201 after the rebase renumber, and the gaps the
runs convicted were filed as FD-204 to FD-209.)*

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

## Run phase — completed 2026-11-07

Build: `tools/bw64-build.sh` on this branch's tip — the log
(`test-results/vkgl-census/build-run-20261107b.log`) carries the
literal "Browser export verified" line, and dist BUILD_INFO reads
git_sha `4c84741b6e7bb48055e6dc3e84fe9e2c5c398015`, 0 dirty files,
wasm sha256
`92b36180e9b3469b07014b91cf0054e4f50e1b119b2881e6ca1a85819d27bfdf`
(5,722,692 bytes) — byte-identical to the shipped batch-10 build,
as expected: this branch changes no product code, so every result
below is a measurement of the shipped product path. The build
first waited out a sibling lane's probe (the build script's
probe-drain wait), then compiled cleanly. The branch's
checking-service verdict had already been collected separately
and is clean; it is not repeated here.

Runs (all through the shared probe-slot helper; consoles and
results preserved under `test-results/bridge-coverage-<tag>/` on
this tree, probe logs under `test-results/vkgl-census/`):

- vkcov run 1 (`vkgl-vk-20261107`): wedged at vkCmdBlitImage
  after 108 result lines — the guest redelivered one fetch fault
  past 22 million deliveries; stopped from outside; the partial
  results were harvested from the preserved console log.
- vkcov run 2 (`vkgl-vk2-20261107`): with vkCmdBlitImage
  harness-skipped, reached 258 result lines including the full
  proc-addr census; wedged at the vkGetDeviceProcAddr stub row
  (FD-205) after 255 unique results; stopped; harvested.
- vkcov run 3 (`vkgl-vk3-20261107`): with the proc-addr rows also
  skipped, wedged at vkCmdBlitImage2 after 109 lines; stopped;
  harvested.
- vkcov run 4 (`vkgl-vk4-20261107`): **complete** —
  `vkcov: RESULT 0`, 309 handlers. The Vulkan matrix above is
  from this run.
- vkcov matrix variant (`vkgl-vkm-20261107`): **complete** —
  RESULT 0, 309 handlers, return codes identical to run 4 on
  every one of the 309 names.
- glcov run 1 (`vkgl-gl-20261107`): the session died at
  glTexStorage2DMultisample, 221 of 334 rows in — the FD-207
  ReferenceError in the bridge's own error-report path.
- glcov run 2 (`vkgl-gl2-20261107`): complete, RESULT 0, 334
  rows — but it exposed a harness recording flaw (below), so
  its results file mis-records the three skipped rows.
- glcov run 3 (`vkgl-gl3-20261107`): **complete** — RESULT 0,
  334 rows, skips correctly recorded. The OpenGL matrix above is
  from this run (the broken set is identical to run 2's).

Harness changes made during the run phase (measurement
instruments only, all on this branch; the exes were rebuilt with
the project's documented compilers and are committed):

- vkcov.c: vkCmdBlitImage and vkCmdBlitImage2 converted to
  known-crasher skips after each was measured wedging the guest
  (FD-204); vkCmdBlitImage2KHR skipped on the same-code-path
  precedent; the vkGetDeviceProcAddr stub row converted to a
  skip after it was measured wedging the guest (FD-205), and
  vkGetInstanceProcAddr with it on the pair precedent. Both
  proc-addr names are working loader functions — the stub rows'
  premise (a -8 stub called with no arguments) was false.
- glcov.c: glTexStorage2DMultisample converted to a skip after
  it was measured killing the session (FD-207);
  glTexStorage3DMultisample skipped on the shared-bridge-case
  precedent.
- glcov.c recording fix: a skipped row's trailing no-proc else
  branch still executed and overwrote the skip's -2 record with
  a -1 in the results file (the console log shows both lines;
  the results parser keeps the last). The else branches of the
  two TexStorage rows and of the pre-existing glGetAttachedShaders
  skip are now neutralised. Consequence beyond this lane: every
  earlier GL results file, including the 2026-10-26 matrix,
  records glGetAttachedShaders as missing (-1) when it was in
  fact a recorded crash (-2).

Register outcome: FD-201 CLOSED (CL-143, provisional).
New filings from these runs: FD-204 (blit family wedge),
FD-205 (unbounded fault redelivery), FD-206 (ten Vulkan
handlers answering -3), FD-207 (GL multisample error path
kills the session), FD-208 (twenty-one GL error answers plus
one inconsistent answer), FD-209 (seventy-one GL entry points
the guest cannot resolve). Register audit after the filings:
FD 1..209 each exactly once (standing gaps 46/56/57; FD-202
and FD-203 live on a sibling branch and join at composition),
CL 1..143 each exactly once (CL-141/CL-142 likewise on the
sibling branch); no duplicates.

The tree was stripped of generated build output after the runs
(dist, web/runtime, the native Build tree); the test-results
evidence stays.
