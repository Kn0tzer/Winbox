# FD-55 pre-warm packaging lane — report (2026-11-01)

Branch `lane-fd55prewarm` (worktree `/home/ubuntu/wt-fd55warm`), based on
integration tip `306190757`. Nothing pushed, nothing deployed; the live
site and the pristine demo archive were never touched.

## Task and outcome

End state sought: the Godot demo (`godot-squash`) presents frames through
the standard demo path inside the standard demo window — the unmodified
demo check passing with its blank-screen check green — by paying the
demo's first-run startup cost once, ahead of time, through a repeatable
packaging step, with the register separating that demo outcome from the
remaining truth about raw startup speed.

**Outcome: the end state was NOT reached, and the packaging route is
proven insufficient on its own.** The packaging step exists, is
repeatable, and measurably works (below). But three full capture runs
through the standard demo path show the demo's failure is not a
startup-cost problem: the guest's own engine log records that **every
D3D12 graphics pipeline-state creation fails with error 0x80004005
(E_FAIL)** inside the guest translation layer, so no Vulkan graphics
pipeline is ever created, no swapchain is ever created, and no frame can
be presented at any cache warmth. FD-55 stays open with its cause
narrowed to that defect; the raw-startup-speed truth is registered
separately as FD-67.

## What was built (commits 2fa876399, 3241b04c0)

- `web/tests/godot-warm-capture.mjs` — capture driver. Boots the demo
  through demo-matrix's own harness functions (same Chromium flags, same
  launcher path, fresh profile, interpreter configuration), watches the
  presented-frame counters and the engine banner, snapshots the display
  periodically, and at the end harvests the launcher's persistence store
  (the exact state a returning visitor's browser would restore) to disk.
- `tools/demo-packaging/build-warmed-archive.mjs` — the repeatable
  packaging step. Merges one or more capture harvests (later captures
  win per file), applies an explicit curation list, mirrors the pristine
  archive's two entries byte-for-byte as stored entries, and verifies
  the result by re-reading it and hashing every seed and every pristine
  payload back to its source. It refuses to package a run that banked
  nothing after the engine banner, and fails loudly on any harvested
  home-tree file the curation list does not account for.
- `tools/demo-packaging/godot-squash-warm-files.txt` — the curation
  list: 67 seeded files with a stated reason each; 5 excluded files
  (the three registry hives, the engine log, the prefix-update
  timestamp) with reasons — session state rewritten every boot, not
  cache.
- `web/demos.mjs` — the `godot-squash` entry now names the warmed
  archive (`godot-squash-warmed.zip`, size field kept truthful). The
  demo check itself is untouched: no weakening, no shortening, no
  special case.
- The warmed archive itself: `/home/ubuntu/kn0tzer/games/godot-squash-warmed.zip`,
  114,471,810 bytes, sha256 `0d95847ec141d180…`, 69 entries (2 pristine
  payloads + 67 seeds). It sits alongside the pristine archive, which is
  unmodified (111,031,102 bytes).

## Capture measurements (standard demo path, fresh profile each run)

| Capture | Seed state | Deadline | Engine banner | Window mapped | VK presents | All presents | Display |
| --- | --- | --- | --- | --- | --- | --- | --- |
| warm-main | none (pristine archive) | 3,300 s | 94.9 s | 142.4 s | 0 | 12 (SDL) | blank signature throughout |
| warm-iter2 | package v1 (63 seeds) | 3,300 s | 131.6 s | 210.4 s | 0 | 16 (SDL) | blank |
| warm-iter3 | package v2 (67 seeds) | 4,200 s | 100.2 s | 164.9 s | 0 | 10 (SDL) | blank (flat full-page signature, 8 samples to 3,916 s + final) |

Seeds are genuinely consumed, and the cache converges:

- Capture 1 banked 62 engine shader-cache files (GDSC format, ~1.59 MB)
  plus the translation layer's staged pipeline cache (1,416,208 bytes).
- Capture 2, starting from those seeds, banked only 4 new shader files;
  41 seeded files were never rewritten. Capture 3 banked **no new
  files at all**; the distinct shader-file set is stable at 66 files.
- The 21 files that are re-saved on every run are re-saved early (all
  cache writes complete by ~213 s into capture 3) and their compiled
  payloads are byte-identical across runs — the only difference is a
  3-byte header field (offsets 96–99). Recorded as an observation; no
  defect is claimed from it.
- No new translation-layer cache writes occur in the seeded captures,
  consistent with the seeded pipeline cache being accepted.

## The blocking defect (guest-log proof, all three captures)

The harvested engine log (`godot.log`, identical shape in every
capture, cold or seeded) records, immediately after device creation:

```
Godot Engine v4.7.2.stable.official
D3D12 11_0 - Forward+ - Using Device #0: Unknown - VKWGPU WebGPU Virtual Device
ERROR: Create(Graphics)PipelineState failed with error 0x80004005.
   at: render_pipeline_create (drivers/d3d12/rendering_device_driver_d3d12.cpp:5360)
ERROR: Condition "!pipeline.driver_id" is true. Returning: RID()
ERROR: Condition "pipeline.is_null()" is true.
   at: _create_pipeline (servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp:506)
```

Bridge verb census over the capture consoles agrees exactly: 239
`vkCreateShaderModule`, ~1,100 `vkCreatePipelineLayout`, **0
`vkCreateGraphicsPipelines`, 0 swapchain creations, 0 VK presents**.
Shaders compile and cache fine; pipeline *layouts* are created;
pipeline *state* creation fails deterministically inside the guest
(D3D12 → translation layer) before any Vulkan pipeline exists. The
engine logs the failure and continues with a null pipeline, so the
renderer can never draw a first frame. This corrects FD-55's original
framing ("although its graphics pipelines compile"): the pipelines do
not compile — their creation is refused — and the register entry has
been annotated accordingly.

Why the demo-matrix proof run was not re-run: its outcome is
established by stronger evidence — three full runs through the same
standard demo path (demo-matrix's own harness code) with pixel
verification, plus the guest log proving the failure is deterministic
and independent of cache state and of input (input only reaches the
demo after a first frame). Re-running would re-confirm, not measure.

## What this means for the end state

- Once the pipeline-creation defect is fixed, the warmed archive is
  the mechanism that makes this demo's startup fit the standard demo
  window: the cacheable portion of startup (66 shader files + the
  translation layer's pipeline cache) is paid once at packaging time,
  and the seeded cache phase completes in ~213 s.
- Raw startup speed remains honestly open as FD-67: cold, the engine
  was still working through shader compilation at 1,738 s in the
  sibling lane's characterization and at 3,322 s here — but every
  cold measurement so far is confounded by the pipeline defect, so
  true cold time-to-first-frame cannot be measured until FD-55's
  defect is fixed.

## Tests

- JS unit suite in this worktree: **344 passed, 0 failed** (the
  campaign baseline figure). The legacy group was started in the
  same run but its runner exited mid-group without producing results
  (the log ends at the group header; no failure list exists to
  compare against FD-64's known pre-existing set), so no legacy
  figure is claimed for this branch; the composed-tree gate re-runs
  the full suite in any case. Log:
  `test-results/fd55prewarm/unit-suite.log`.
- Native suite not run: this branch changes no emulator or product
  source — only the demo catalog entry, a capture tool under
  `web/tests/`, and packaging tooling under `tools/demo-packaging/`.
- The demo check (`web/tests/demo-matrix.mjs`) is unmodified.

Evidence: `test-results/fd55prewarm/` (`warm-main/`, `warm-iter2/`,
`warm-iter3/` — summaries, status streams, consoles, harvest manifests,
screenshots; `package-report.json` in the later capture directories;
`unit-suite.log`).
