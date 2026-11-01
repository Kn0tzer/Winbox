# FD-55 pipeline-creation lane — record (2026-11-01)

Branch `lane-fd55pipe` (worktree `wt-fd55pipe`), base `44e24abbd`
(the composed integration tip: FD-61 and the FD-62 hardening
landed, FD-67/FD-69 closed), plus the pre-warm lane's capture
driver / packager / demos pointer cherry-picked unchanged
(`0a7e33738`, `7ae177b15`, `7ad14a0cd`) and a lane-local
runtime-assets manifest re-pin (`c8793ccfd`).

## Outcome

The fallback end state: the pipeline-creation failure is
localized to one named layer — the bridge's SPIR-V module audit
and the page-tier translation stack behind it — with that
layer's own diagnostic output captured as evidence
(`test-results/fd55pipe/diag1`, `test-results/fd55pipe/diag2`).
The demo still presents no frames, and this record does not
claim otherwise. The root fix is FD-62's stated closing
condition, which is translator work, not a lane-sized change;
the reasoning is below. The demo check was not weakened,
shortened or special-cased, and no layer was made to admit a
module it cannot serve.

## What was run

Two full captures through the standard demo path (the pre-warm
lane's `web/tests/godot-warm-capture.mjs`, unmodified; the
warmed archive `godot-squash-warmed.zip`; fresh profile each):

- `diag1` — build `c8793ccfd` (integration content), deadline
  1,800 s, elapsed 1,863.6 s. Godot banner 92.3 s, window
  mapped 139.3 s.
- `diag2` — build `72360dda5` (same content plus the
  refusal-naming log lines described below), deadline 1,900 s,
  elapsed 1,923.4 s. Banner 116.6 s, mapped 168 s.

Both runs: the guest engine's own log
(`harvest/.../logs/godot.log`) records exactly five
`Create(Graphics)PipelineState failed with error 0x80004005`
(`rendering_device_driver_d3d12.cpp:5360`), and the bridge
census records zero `vkCreateGraphicsPipelines`,
`vkCreateWin32SurfaceKHR`, `vkCreateSwapchainKHR` and
`vkQueuePresentKHR` arrivals against 261 (diag1) / 266 (diag2)
shader-module creations and ~1,100 pipeline layouts. The
seeded shader-cache phase completes as the pre-warm lane
designed it (the harvest carries the engine's `.d3d12.cache`
set); packaging and cache warmth are not the blocker. A formal
`demo-matrix` run was not repeated: the captures boot the same
demo through the same path with a longer window, and a third
boot would earn no new evidence.

## What the runs prove

1. **FD-61 works in vivo.** The vertex module the FD-55 lane
   convicted (fixture `testdata/spirv/godot-vs-heap-plus-cbv-heap.spv`,
   6,072 bytes, bridge djb2 `a61ba068136e1191` — the djb2 was
   recomputed from the fixture and matches) is now ADMITTED at
   210.9 s and 211.3 s in diag1, immediately before each
   first-wave refusal below.

2. **The first-wave blocker is FD-62's module, byte-identified.**
   In diag2 the bridge's refusal logging names it:
   `vkCreateShaderModule REFUSED 5704 bytes (hash
   0b3b4d8cc939485a) result=-8` at 250.2 s and 250.7 s. The
   djb2 of `testdata/spirv/godot-fs-atomic-texel-heap.spv`
   (FD-62's captured fragment module) is exactly
   `0b3b4d8cc939485a`. Its refusal reasons, logged in the same
   second as the guest translation layer's
   `hresult_from_vk_result: Unhandled VkResult -8` and the
   engine's E_FAIL: `spirv-capability:ImageBuffer` and
   `spirv-capability:RuntimeDescriptorArray`. Audit mechanics,
   read from `vk64AuditSpirv`: the module's storage texel
   buffer heap is consumed through `OpImageTexelPointer`
   atomics, which `vk64TexelStorageServable` declines by
   design ("OpImageTexelPointer / atomics on it are not
   wrappable"), so `texelServable` is false and capability 47
   is refused by name; and the heap variable is a descriptor
   runtime array the R15a classifier cannot specialize (FD-62:
   its array classifiers refuse texel formation by name), so
   `skip5302` is false and capability 5302 is refused by name.
   Capability 5347, also declared, is correctly exempted (the
   R8 unused-form census) and appears in neither refusal.

3. **A second wave exists (filed as FD-70).** At
   1,880.9–1,882.3 s in diag2 (1,819.4–1,821.1 s in diag1) the
   engine retries scene pipeline creation
   (`scene_shader_forward_clustered.cpp:506`) and hits two
   further refused modules: 23,440 bytes (djb2
   `28a2394720432fda`, one creation) and 16,304 bytes (djb2
   `3eccbc1e60e9f7a4`, two creations), refused for a
   non-specializable descriptor runtime array and for
   PhysicalStorageBufferAddresses (5347) in its USED form —
   neither the unused-declaration exemption nor R15
   root-descriptor servability applies. Three more E_FAILs.
   No fixtures exist at these sizes; the second 16,304-byte
   creation's log shows only the 5347 reason (aggregate-logger
   coalescing vs a below-the-hash difference was not chased;
   the verdict and layer are identical either way).

## Why there is no sound fix in this lane

Serving the first-wave module requires the translation stack
to ingest `OpImageTexelPointer` with atomic operations
through it. FD-62 measured the vendored translator (naga
30.0.1) rejecting every module that contains a texel pointer,
and WGSL/WebGPU has no storage-texture atomics to rewrite
them to; the tier's texel-buffer serving model materializes
texel buffers as textures precisely on the paths where texture
operations exist. The remaining theoretical route — a
pre-translation rewrite giving texel atomics a buffer-backed
serving strategy — is the FD-16 descriptor-heap serving
design, not a lane change, and a partial version risks
silently serving atomics to the wrong image, which FD-62
already judged worse than the loud refusal. Admitting the
module at the audit without serving it would only move the
same E_FAIL to pipeline translation, later and quieter (the
FD-49 failure shape). Not done. FD-62's closing terms stand
unchanged; this lane adds the proof that they are now the
Godot demo's first blocker, not a side case.

## Changes on this branch

- Cherry-picks from `lane-fd55prewarm` (capture driver,
  packager, demos pointer) — listed in the header.
- `c8793ccfd` — runtime-assets manifest re-pinned to the
  canonical payload set staged in this worktree (established
  lane pattern; the composition lane re-pins to its own
  reproduction).
- `72360dda5` — the bridge's two `vkCreateShaderModule` audit-
  failure paths now log the refused module's size and djb2
  hash (`REFUSED` line, both ABIs). Log-only: verdicts,
  counters and return values unchanged. Proven in vivo by
  diag2's REFUSED lines — the first-wave attribution above is
  impossible without it (the success path already logged
  size+hash; refusals named only the capability).
- `14290fc87`, `b14341dae` — vulkan gap-register citation
  re-pins for the lines the logging change shifted (five rows
  pushed past the citation guard's 6-line tolerance; every
  target line's content verified before pinning; reg:68's pin
  now cites the fn32 unimplemented-trap fallback line its own
  text describes — its previous pin had already drifted ~100
  lines from that block and passed only on an unrelated
  same-name call within tolerance).

## Verification

- Dist builds for both capture builds: "Browser export
  verified" (dependency archive `de592964f772`), BUILD
  COMPLETE, clean git state in `BUILD_INFO.json`.
- JS unit suite on the final branch tip: **346 passed, 0
  failed** (`test-results/fd55pipe/js-unit2.log`). The first
  full run showed 345/1 — the single failure was the citation
  guard, closed by the re-pin commits above; the re-run is
  the figure claimed.
- Citation audit (`tools/vk-register-citation-audit.py`):
  351 rows, DRIFT 0, MISSING 0.
- Native suites were not run: the only product-code change is
  two log lines on a failure path already exercised in vivo
  5x per capture; no classifier, verdict or ABI behavior
  changed. The demo check itself was not run as a separate
  gate for the reason stated under "What was run" — and on
  this evidence it would fail, honestly: zero frames.

## Register actions on this branch

- FD-55: annotated — on the composed tree the entry's blocker
  is pipeline creation (FD-62's class first, FD-70's class
  second), proven by the captures above; the entry stays open
  on those closing terms.
- FD-62: annotated — its captured module is now measured as
  the demo's first pipeline-creation blocker.
- FD-70: the late-wave class — non-specializable (filed
  provisionally as FD-70; confirmed at composition)
  descriptor runtime array plus used-form physical addressing.

## What closing FD-55 now requires

FD-62 closed (a translation stack that ingests
OpImageTexelPointer atomics, or a sound texel-eliminating
rewrite with an atomics serving strategy), then a fresh
standard-path capture: if the first wave then creates its
pipelines and the demo presents, FD-70's wave either is never
reached or becomes the next blocker in line — the captures
here cannot distinguish those, because the engine's late wave
may be a retry of a renderer that never initialized.
