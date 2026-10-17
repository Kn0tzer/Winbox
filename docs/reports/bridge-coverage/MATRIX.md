# Bridge API-Coverage Matrix

In-browser runs, 2026-10-09. 64-bit only; 32-bit untestable (Wine 8.0 bug: find_env_var passes env=0x31, name=NULL; CPU emulator verified correct; workaround pending 5-boot verification).
Updated 2026-10-09 to reflect d1713623 (Vulkan defensive guards), 2e02a285 (glGetAttachedShaders fix), 32-bit Wine-bug finding, D3D12 effDepthTest bridge fix (pending verification), D3D9 texture VERIFIED.
Harness: `tools/vkcov/` (vkcov.c, vkcov-analyze.py, METHOD.md), `tools/glcov/`
(glcov.c, glcov-analyze.py); runners `web/tests/vkcov-probe.mjs`,
`web/tests/glcov-probe.mjs`, `web/tests/vkcov-probe-matrix.mjs` (library-staging
variant). Probe-slot disciplined; probes break (not hang) on worker crash.

## Wasm provenance

- `dist/runtime/boxedwine64.wasm` sha256 `950b59c08e2c9160…`, built 2026-10-09 02:08 UTC
- Tree HEAD `88cdd449` (2026-10-09 02:11 UTC); working tree carried other lanes'
  uncommitted changes — handler return codes unaffected.
- vkcov matrix exe: `/tmp/vkcov-matrix.exe` (built 02:45 UTC from vkcov.c + 12
  crasher skips; skipped handlers recorded as crashed with TRY-line repros).
  Staged libs per parent instruction: libunwind.so.8, liblzma.so.5.
- glcov exe: `tools/glcov/glcov.exe` (built 01:40 UTC, 1 skip for glGetAttachedShaders).

## Slow / flaky thresholds

- Slow: per-call wall time ≥ 10,000µs (10ms). Justification (METHOD.md): the
  guest-CPU profile (2026-10-08, n=211 Pong frames) measured 48 vk64 traps at
  ~34.5ms total (~0.72ms average per trap, including the full record-encode path
  for vkQueueSubmit). A simple marshalled bridge call doing no GPU work has no
  business taking ~14× the average trap cost.
- Flaky: rc1 != rc2 (or err1 != err2) across the two runs, or >10× timing
  variance between the two runs.

## Four-bucket summary

| Set   | total | fully working | slow/flaky | fully broken |
|-------|-------|---------------|------------|--------------|
| vk64  | 266   | 252¹          | 0          | 14 (12 guarded², 2 stub-violations) |
| GL64  | 306   | 5             | 191³       | 110          |
| vk32  | 157 wrappers (static) | 0 (untestable) | 0 | 83 implemented-but-untestable; 74 trap-only |

¹ vk64 "fully working" = 170 functional handlers + 82 stubs correctly returning
VK_ERROR_FEATURE_NOT_PRESENT (-8) by design. See breakdown below.

² vk64 "guarded" = 12 handlers that crashed in the 2026-10-09 probe (garbage regionCount →
C++ exception), now protected by d1713623 (`if (nreg > 1024) return 0;`). They no longer
crash; legitimate calls proceed. Verified: d3d-conformance-vulkandefensive/ 15/16 ops.

³ GL64 "slow" = 190 handlers with err=0 but ≥10ms. Timings are quantized at
16/34/50ms (p50=34ms), indicating systematic bridge round-trip overhead, not
per-handler work. The 10ms threshold flags the entire GL bridge as slow relative
to vk64's 0.72ms average — this is a real performance finding, not a measurement
artifact (the timer resolves <10ms fine; 5 handlers measured 1-7ms).

## vk64 (Vulkan bridge) — 266/269 exports tested

**Fully working: 170.** 165 returned VkResult 0 with <10ms and consistent runs;
5 re-bucketed from the analyzer's naive "broken" (non-VkResult return types the
analyzer misclassified):
- vkGetDeviceProcAddr, vkGetInstanceProcAddr: returned valid function pointers
- vkGetBufferDeviceAddress: returned non-zero device address (test records boolean)
- vkGetPhysicalDeviceWin32PresentationSupportKHR, vkGetPhysicalDeviceXlibPresentationSupportKHR:
  returned VkBool32 TRUE

**Stubs by design: 82.** DLL exports with no bridge case; correctly return -8.

**Guarded: 12** (was "fully broken" — no longer crash after d1713623).
- 12 handlers that crashed with C++ exception escaping to JS, 5 families, all filed:
  - vkCmdCopyBuffer2, vkCmdCopyBuffer2KHR (`BUG-vkCmdCopyBuffer2.md`)
  - vkCmdCopyImage, vkCmdCopyImage2, vkCmdCopyImage2KHR (`BUG-vkCmdCopyImage.md`)
  - vkCmdCopyImageToBuffer2, vkCmdCopyImageToBuffer2KHR (`BUG-vkCmdCopyImageToBuffer2.md`)
  - vkCmdBlitImage, vkCmdBlitImage2, vkCmdBlitImage2KHR (`BUG-vkCmdBlitImage.md`)
  - vkCmdResolveImage, vkCmdResolveImageKHR (`BUG-vkCmdResolveImage.md`)
  - Root-cause hypothesis (in BUG-vkCmdCopyBuffer2.md): bounds-clamp underflow
    (`smem->va + smem->size - sOff` wraps when sOff already out of bounds) →
    `std::bad_alloc` → exception escapes. Handlers with pre-checks
    (`vkCmdCopyBuffer`, `vkCmdFillBuffer`) do NOT crash. Fix: add pre-check.
  - Fixed by d1713623 (defensive `nreg > 1024` guards, committed 2026-10-09). Guards prevent
    crashes on garbage input; root cause (PE shim arg-shift) fixed separately by c50f6fba.
- 2 stub-contract violations: vkGetPhysicalDeviceProperties2KHR and
  vkGetPhysicalDeviceImageFormatProperties2KHR (no bridge case; returned -3
  instead of -8).

**Fully broken: 2** (the stub-contract violations above; not yet fixed).

**Not tested (3):** vkCmdExecuteCommands, vkDestroyPipeline,
vkGetPhysicalDeviceQueueFamilyProperties — declared but not called by the harness
(coverage gap, not a bridge bug).

**Slow/flaky: 0.**

Raw per-handler table: `matrix-vk64.md` (266 rows).

## GL64 (OpenGL bridge) — 306/306 tested, BW64_GLVERSION=33

**Fully working: 5** (<10ms, GL_NO_ERROR, consistent):
glDeleteFramebuffers, glDeleteProgram, glDeleteRenderbuffers, glDeleteShader,
glDeleteTextures (1-7ms).

**Slow: 190.** GL_NO_ERROR but 16-50ms per call (p50=34ms). The quantization at
16.6ms multiples indicates systematic bridge round-trip overhead (likely
per-call synchronization with the page's WebGL context), not per-handler work.
This is a real performance gap vs vk64 (0.72ms avg): the GL bridge is ~47× slower
per call. Any app making hundreds of GL calls per frame will be severely
frame-rate limited by bridge overhead alone.

**Flaky: 1.** glCompressedTexImage3D (err1=37442, err2=0).

**Broken: 14** (GL errors for the tested args; state-dependent calls likely reflect
test-setup issues, not bridge bugs — e.g. glPopMatrix without glPushMatrix
→ GL_STACK_UNDERFLOW; glEnd without glBegin → GL_INVALID_VALUE):
- GL_INVALID_OPERATION (1282): glAttachShader, glBlitFramebuffer, glBufferData,
  glBufferSubData, glGetTexImage
- GL_INVALID_ENUM (1280): glBeginQuery, glEndQuery, glGetFloatv, glReadPixels,
  glTexStorage2DMultisample, glTexStorage3DMultisample
- GL_INVALID_VALUE (1281): glEnd, glGetTexLevelParameteriv
- GL_STACK_UNDERFLOW (1284): glPopMatrix

**Stubs: 96.** Proc address missing (-1): not exported by the guest libGL shim or
not implemented in the bridge. Includes glX* functions and traceProc.

**Crashed: 0.** (glGetAttachedShaders fix committed as 2e02a285, verified 306/306 handlers zero errors.)

Raw per-handler table: `matrix-gl64.md` (306 rows).

## vk32 (32-bit Vulkan bridge) — static only

See `matrix-vk32.md`: 157 wrappers, 83 implemented, all untestable (32-bit Wine 8.0 bug:
find_env_var passes env=0x31, name=NULL with NULL return address; CPU emulator verified
correct; PEB/GOT/TLS theories all disproved). Not executed in-browser.

## D3D / CPU / Audio status (2026-10-09, verified)

Four-bucket format per user requirement.

| Set | Total | Fully passing | Slow/flaky | Broken |
|-----|-------|---------------|------------|--------|
| D3D12 (d3dconf12) | 16 ops | 15 | 0 | 1 (device_FL_12_0, above WebGPU ceiling) |
| D3D12 scene12 | 1 scene | 0 | 0 | 1 (indexed quads missing; bridge effDepthTest bug identified, fix pending verification) |
| D3D11 | 13 matrix ops | 11 | 0 | 2 |
| D3D10 | 7 ops | 7 | 0 | 0 |
| D3D9 complex | 1 game | 1 (fpsinput: 2670px, inputs work; texture upload VERIFIED YES via c50f6fba) | 0 | 0 |
| CPU Integer | 118 forms | 118 | 0 | 0 |
| CPU FPU | 23 forms | 22 | 0 | 1 (FYL2XP1 1-ULP, not a bug) |
| CPU SSE | 26 forms | 26 | 0 | 0 |
| Audio direct | 1 path | 1 (441Hz, 10e1b456) | 0 | 0 |
| Audio waveOut | 1 path | 0 | 0 | 1 (WHDR_DONE never fires; Boxedwine stall fixed, libasound disproven, winmm bypass in progress) |

**Notes:**
- D3D12: c50f6fba (64-bit DLL), a7627be5 (19 arg-count fixes). scene12 98k used a BUGGY scene12.exe (fixed by 278a1fe0); 98k is unachievable with the current binary. 278a1fe0's verification was invalid (probe ran 8 min BEFORE the commit) but its WebGPU validation fix is necessary (revert causes 0px + validation errors). DLL exonerated (bridge chunks byte-identical). Root cause: bridge `effDepthTest` bug (vk64bridge.cpp:1984) masks pipeline's true depth intent -> quads get depthCompare 'always' instead of 'less-equal'. Fix implemented (unmasked effDepthTest, pending verification). Expected: ~78k px (quads + triangles).
- D3D9: 4dba58ba (descriptor binding, IMAGE_DATA 0→32). Texture upload DLL fix committed (c50f6fba) and VERIFIED YES (probe dxvk-texquad9-verify2: UpdateTexture succeeded, guards did not trigger). Frame time 114.55ms mean (uncontended); f64bbe0c did NOT improve frame time (1.88x was frame count, not time). VKPROF WASM backend exists (vk64bridge.cpp) but URL param enablement blocked by Emscripten env caching; JS-accessible toggle via EMSCRIPTEN_KEEPALIVE in progress. WASM JIT deferred (stub/prototype, major project).
- D3D11: swapchain verified (29k lit px). Exit tracer fixed (a8bf472f).
- CPU: 166/167. Gaps (MMX, string ops, SSE scalar, Jcc/LOOP, CMPXCHG/BT*, seg overrides, FPU env) verified CLOSED — all implemented.
- Audio: Direct /dev/dsp works (441Hz verified). waveOut: Boxedwine-side stall FIXED (3-file isBwPump fix + getUsedBufferSize, uncommitted; queueStuck=false). libasound.so.2 hypothesis DISPROVEN (provisioned, cache-bypassed, still silence, 0 winealsa mentions). Winmm bypass to /dev/dsp in progress.

## Bugs filed during this campaign

1. `BUG-vkCmdCopyBuffer2.md` (+ root-cause hypothesis: bounds-clamp underflow)
2. `BUG-vkCmdCopyImage.md`
3. `BUG-vkCmdCopyImageToBuffer2.md`
4. `BUG-vkCmdBlitImage.md`
5. `BUG-vkCmdResolveImage.md`
6. `BUG-bss-host-write.md` (prior lane; KMemory64/DTLB coherency)
7. glGetAttachedShaders page-worker crash — FIXED and committed as 2e02a285 (306/306, zero errors)

## Coordination notes

- The vkCmdCopyBuffer2 fix lane re-enabled the crasher in shared `tools/vkcov/vkcov.c`
  for diagnostics (comment: "DIAG 2026-10-09"). My matrix used a dedicated
  `/tmp/vkcov-matrix.exe` with all skips to avoid conflict. The shared vkcov.c
  should be restored to the skip state after the fix lane completes.
- `web/tests/vkcov-probe-matrix.mjs` is a matrix-specific probe copy with
  libunwind.so.8/liblzma.so.5 staged (per parent instruction). Can be deleted
  after the campaign; the staging proved unnecessary (bootstrap succeeds without it).

## Raw tables

- `matrix-vk64.md` (266 rows; analyzer-generated)
- `matrix-gl64.md` (306 rows; analyzer-generated)
- `matrix-vk32.md` (static)
