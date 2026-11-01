# FD-55 — Godot demo presents no frame: characterization, one fix, two new register entries

Lane: `lane-fd55` (worktree `wt-fd55`), off integration tip e9e67be23 plus the
two deploy-composition cherry-picks (1c0a1b534, fe4cfc265) the build's
completeness gate required. Date: 2026-11-01. Evidence root:
`test-results/fd55/`.

## The question

The final-batch gate confirmed the Godot demo's guest stream is healthy
(engine starts, shaders accepted, pipelines compile, cache flushes) while
the demo check counts zero presented frames and its blank-screen check
fails. FD-55 asked which side of the screen the gap lives on: 'the guest
never presents' or 'it presents and the page never shows it'.

## Answer: the guest never presents — it never reaches a first frame

Five instrumented runs through the standard demo path (vktrace + drvinit
milestones + periodic guest thread dumps; consoles in `test-results/fd55/`):

- char1 (617 s), long1 (1,752 s), dxvk1, verb1, cap1, cap2 all reproduce the
  demo failure identically: window maps, blank-screen FAIL (non-dark
  fraction ≈ 0.0044), overlay never clears, presented-frame counter counts
  only the SDL presents of the empty X11 window.
- The bridge's surface/swapchain/present handlers log unconditionally.
  Across all runs: ZERO vkCreateWin32SurfaceKHR, ZERO vkCreateSwapchainKHR,
  ZERO vkAcquireNextImageKHR, ZERO vkQueuePresentKHR. The DRVINIT milestone
  chain for the game process stops at `vk-submit`; the surface milestone
  never fires.
- Submitted work is startup work only: resource uploads (61
  buffer-to-image copies, a few fills/barriers), ZERO draws, ZERO render
  passes, ZERO dispatches in the Vulkan stream. Pipeline work is
  vkd3d/Godot shader compilation: ~213 pipeline-layout pairs and ~94 shader
  modules in long1, delivered at a decelerating rate with long quiet gaps,
  still producing new module hashes at 1,738 s.
- Thread dumps agree: the guest main thread sits on a futex hand-off inside
  the game executable for ~24 of 25 samples while exactly one worker thread
  runs engine code; the two alternate (a hand-off, not a deadlock). No
  thread is parked inside a Vulkan trap; semaphore waits return.
- A `--verbose` run's engine output advances to the D3D12 device line
  (155 s) and then prints nothing more for the remaining ~420 s — no
  scene-load lines ever appear.

Conclusion: Godot's first frame is gated on its in-engine shader-compilation
workload for the built-in renderer set, executed on one worker at
interpreter speed. The workload is finite and deterministic but exceeds the
demo window by an order of magnitude. The present path (surface →
swapchain → present → page tier) is never reached and is not convicted;
the D3D12 oracle programs present and read back on the same build.
An independent Claude Code consult on the same evidence ranked this
explanation first (`test-results/fd55/claude-consult.md`).

FD-55 therefore stays OPEN, rewritten as a characterization: closing it
needs the initialization workload to finish inside a demo window
(pre-warmed shader/pipeline cache shipped for the demo, or a step change
in guest execution speed) — product-level decisions, not a fix-lane change.
The demo check was not weakened in any way; all five runs fail it honestly.

## Found and fixed on this branch: FD-61 (shader-audit false rejections)

The runs also showed three shader-capability rejections, each killing a
Godot pipeline (Create(Graphics)PipelineState failed, 0x80004005). With a
purpose-built capture harness variant (demo-matrix + guest-home extraction
through the runtime VFS), the rejected modules were identified by dump
adjacency and captured: vkd3d dumps 480d68cc811ceb30 and 29caf5794322da20
(vertex shaders), plus 6f52b154d73c804f after the fix (see FD-62).

Root cause for the first two (proven with the bridge's own classifier,
extracted verbatim from vk64bridge.cpp and compiled standalone): the
audit admits RuntimeDescriptorArray only when its simulation of the
spirvfix orphan sweep leaves no OpTypeRuntimeArray alive. vkd3d emits
buffer-CONTENT runtime arrays in the same modules — the SSBO-offsets block
(a runtime array inside a storage-buffer struct) and the CBV heap (a bare
storage-buffer variable of runtime-array-of-struct type). Specialization
serves those as ordinary buffers (verified offline: applySpirvFix +
vendored naga translate both modules cleanly), so the sweep's surviving
buffer arrays vetoed servable modules.

Fix: the sweep verdict counts a surviving runtime array against the
capability only when it is a descriptor array type (direct pointee of a
UniformConstant pointer). Comments in both mirrors aligned.
`docs/known-gaps/vulkan.md` citations re-pinned for the +14-line shift
(audit tool: 50 DRIFT → 0).

Proof:
- Standalone C++ classifier, before/after: both captured modules flip
  skip5302 0 → 1; the big Godot module and the FD-16 fixture module flip
  too; a genuinely unservable module (fa2e574ddf633847) stays 0.
- New unit test `web/tests/suite/unit/spirvfix-r15-buffer-arrays.test.mjs`
  (fixtures `testdata/spirv/godot-vs-heap-plus-*.spv`): 3/3 pass;
  sabotage-checked (reverting the bridge edit fails the mirror guard).
- In vivo (cap1 vs cap2 on identical harness): capability rejections
  3 → 2; both convicted modules now create successfully (their
  vkCreateShaderModule lines print at 459.2 s / 547.7 s / 548.1 s).
- Full dist rebuild: 'Browser export verified', completeness 0 failures,
  wasm e40a762d….

## Found, not fixed: FD-62 and FD-63 (registered)

- FD-62: after FD-61, the remaining rejections are fragment module
  6f52b154d73c804f — a writable image heap array whose elements feed
  OpImageTexelPointer + OpAtomicOr. Both classifier mirrors treat
  texel-pointer formation as a pointer escape and decline. The register
  entry names the fix shape (audit through the texel pointer in both
  mirrors). Not attempted here: it is FD-16-class shader-semantics work,
  and a hasty version risks silently serving atomics to the wrong image —
  worse than the current loud rejection.
- FD-63: the emulator's periodic METRICS emission stops ~228 s into a
  session (three lines total in long1's 1,752 s), freezing metrics.jsonl.
  Any late-session metrics analysis is reading a frozen instrument.

## Verification summary

- Demo check (unmodified) on the fixed build: still FAILS, as FD-55's
  characterization predicts (cap2: blank, overlay up, 4 SDL presents).
- JS unit suite: 345 passed / 0 failed (clean run with nothing else on
  the box; two earlier runs under concurrent probe/build load showed
  one-off failures in provenance-artifacts and spirvfix-r15-guards-sync —
  both files pass standalone and in the clean run).
- The patched file compiles into the shipped wasm (build above); the r15
  namespace also compiles standalone under g++ for the classifier harness.
- Guest-home extraction harness and runners are lane scratch under
  test-results/fd55/ and web/tests/demo-matrix-cap-fd55.mjs (untracked,
  removed at cleanup); the dist demos.mjs --verbose scratch edit used for
  verb1 was reverted before cap1.
