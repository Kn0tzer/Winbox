# Cleanup wave — graphics bridges lane (2026-10-30)

Branch `lane-cleanup-bridges` (worktree `wt-cleanup-bridges`), base integ
tip `538f03db5`. Scope: the graphics bridges between guest programs and the
browser's graphics — `source/vulkan/vk64bridge.cpp` (+ the host marshal
layer), `source/opengl/gl64bridge.cpp`, and the page-tier bridge modules
`web/vkwebgpu.mjs`, `web/gl-webgpu.mjs`, `web/bridge-recovery.mjs`,
`web/vk-log.mjs`. Method: the clean-up skill (evidence first, smallest
behaviour-preserving change) under the campaign standard in
`docs/failure-logging-and-recovery.md` (pathfinder lane, `lane-cleanlog`).

## What changed

1. **`de6a049ef` — gl64bridge: degraded answers are logged once per case.**
   A family of guest requests was answered with fabricated or dropped
   results and no trace anywhere:
   - desktop-only `glTexParameter` pnames were dropped by the caller
     (`texParamSupported` default);
   - unknown GLX query attributes (`glXGetFBConfigAttrib`,
     `glXQueryDrawable`, `glXQueryContext`) and unknown `glGetSynciv`
     pnames were answered `0` under the single-config fiction;
   - unknown legacy client-array types got component size `0`, which
     fabricates zeroed vertex data (`legacyTypeSize` default);
   - unmodelled packed pixel types uploaded still packed
     (`glesTexFormatFixup` default);
   - recorded immediate-mode calls the glEnd block replay does not
     re-emit (today `glMultiTexCoord2fARB` on the WebGPU path) vanished
     (`replayImmBlock` default).
   Each now emits one `BW64-FAIL` line per distinct (site, value) through
   the new `gl64NoteOnce` gate (mutex + set, mirroring the file's existing
   `rejectBridgeEntry` idiom). The answers themselves are unchanged —
   same values returned, now visible. The texture-level query default
   already returned `GL_INVALID_ENUM` honestly and now also leaves the
   once-per-pname line. The one remaining empty catch in the file (an
   EM_ASM `console.warn` guard) now carries the comment naming why the
   swallow is safe.
   Also deleted `replayImmOp`: no caller exists anywhere in the tree
   (the glEnd hop calls `replayImmBlock`); its comment claimed a call
   that never happens. −16 lines of dead code.
2. **`5c5c1bcf8` — the silent-failure scanner recognises `vk64Unsupported`.**
   vk64bridge already rejects loudly through `vk64Unsupported` (counted
   `klog_fmt`: first hits, then powers of two, per feature), but the
   scanner's log-call pattern did not know the name, so ten default
   branches that reject loudly were counted as silent. Census truth:
   vk64bridge `unloggedDefault` 20 → 10. New scanner test pins the
   recognition both ways; sabotage-checked (removing the name from the
   pattern makes the test fail, restoring it passes).
3. **`9a6432492` — page tier: shader warming can no longer fail silently.**
   `createShaderCache` scheduled persistent-shader warming with
   `.catch(() => {})`. `warmPersistentShaders` reports its own per-entry
   failures, but a rejection escaping it (store errors, scaffolding
   throws) vanished. It now logs one `BW64-FAIL shader-warm` line with
   the app id, the error, and the outcome; warming stays best-effort.
   The remaining bare guards in `vkwebgpu.mjs` / `gl-webgpu.mjs` /
   `bridge-recovery.mjs` are teardown or reporter guards and now carry
   the comment naming why each swallow is safe, as the standard requires.
4. **`5e3b3e584` — `known-silent.json` baselines lowered after review.**
   vk64bridge `unloggedDefault` 20 → 10, gl64bridge 23 → 13. Both files'
   `emptyCatch` entries were stale-high against the current scanner
   before this lane (2 → 0 and 22 → 1) and are set to the scanned truth
   for files this lane audited end to end.

5. **`cb2db825a` — fallout from the JS edits, fixed.** The teardown
   comments in `vkwebgpu.mjs` moved above the readback destroy guards:
   the `vk-silent-drop-guard` source test pins those catches' exact
   empty shape (the destroy must never throw past the guest notify),
   so the explanation lives adjacent instead of inside the braces.
   The same commit re-pins five vulkan-register citations
   (rows 223/224/226/260/398 plus duplicate-scan rows) that the
   warm-site insertion shifted by 7 lines; the citation audit is back
   to 0 drift.

## Audited and deliberately left alone

- **vk64bridge.cpp's remaining 10 counted defaults** — all classifiers or
  total predicates whose default is the fail-closed/total answer, with
  loud callers: the SPIR-V capability-name table (its caller rejects
  unlisted capabilities loudly as `spirv-capability-unknown:<n>` with
  `VK_ERROR_FEATURE_NOT_PRESENT`), the R15 classifier helpers
  (`hasTypeId`, `classifyExpr`, `measure` — "cannot prove" → the audit
  rejects), the module-scan collector (only gathers facts), the integer
  colour-format predicates (false is the correct answer for every other
  format, including valid float formats — logging there would cry wolf
  on legitimate traffic), `fmt_name` (returns the honest string
  `"UNKNOWN"`), and the V1 debug emitter's kind walk (non-draw commands
  are not frame content in that path; the production V2 path logs
  unknown kinds via `unknown-cmd-kind`). Unimplemented guest entry
  points already answer `VK_ERROR_FEATURE_NOT_PRESENT` +
  `vk64Unsupported` (line ~6933), and dropped guest work already goes
  through `VK64_DROP_IF`. This file was the model, not the problem.
- **gl64bridge.cpp's remaining 13 counted defaults** — closed-set
  dispatch fall-throughs where the value is computed from the fn id
  (`comps`/`dim` uniform and vertex-attrib switches, sampler get/set),
  format classifiers with documented contracts (`sizedFormatBits`,
  `canonicalFloatTuple`, `renderbufferStorageFixup` — "left to fail
  honestly" is the recorded disposition), the `glGetIntegerv` default
  that falls through to a real backend query, the multi-int count table
  (listed pnames are the complete multi-value set), and
  `legacyReadComponent`'s default (unreachable for unknown types — the
  size guard above it returns first, and that guard's path is now
  logged). The dispatch tail already rejects unknown ABI ids loudly
  via `rejectBridgeEntry`.
- **vk_host.cpp pNext defaults (3)** — already once-log with the sType
  and the outcome (skip node / node left allocated / output skipped).
- **`vk_host_marshal.cpp` — FOUND, NOT FIXED HERE.** Two defaults (and
  one known-type case) in `MarshalVkPerformanceValueINTEL::read/write`
  `kpanic_fmt`/`kpanic` on a type field read from guest memory — a
  guest-supplied value killing the whole emulator, the pattern the
  standard bans. The file is autogenerated ("DON'T MODIFY") from the
  Java generator in `tools/vulkan/gen/`; the fix belongs in the
  generator (emit log + skip-payload for unknown union types — the
  struct stride is fixed, so the degrade is mechanical) and a
  regeneration. Recorded here as the follow-up; editing the generated
  file directly would be overwritten and would hide the real source.
- **`web/vk-log.mjs`** — its 3 catches are the logging infrastructure's
  own never-throw guards; canonical benign.
- **`web/naga-rt.mjs`** — has its own failure design (64-slot ring
  buffer + stats counters); its guards are commented accordingly.
  Not part of the trap path; left for its own lane if wanted.
- **`tools/spirvfix/spirvfix.mjs`** — the translator's internal
  fail-soft helpers return null to callers that decide (the bridge
  audit above is where rejections surface). Not scanned by the suite
  (it scans `source/` + `web/`); no silent guest-facing path found.
- Splitting the very large bridge files (vk64bridge 13.6k lines,
  vk_host_marshal 29k generated) was considered and deferred: a
  >500-line refactor under the clean-up skill needs codemod care and
  is not required by this lane's end state.

## Verification

- Scanner tests: `silent-failure-scanner.test.mjs` (incl. the new
  two-way test) + `no-silent-failure.test.mjs` pass; sabotage check
  done as described above.
- Graphics unit subset (47 tests: no-silent-drops, vk-spirv-rejections,
  vkwebgpu readback/depth/blit/copy/storage-texel/frame-capture,
  gl-webgpu-clear) passes. Its initial failures on this fresh worktree
  were environmental (missing `web/vendor` copy), classified against
  the integ tree and fixed with the standard fresh-worktree recipe.
- Full JS unit suite on this tree (pre-dist): **327 passed, 6 failed,
  every failure classified.** Two were this lane's own fallout and are
  fixed in `cb2db825a` (vk-register-citations — re-pinned, audit at
  0 drift; vk-silent-drop-guard — comments moved out of the pinned
  catch shape; both re-run standalone and pass). Two are the known
  own-dist-dependent pair (runtime-completeness, provenance-artifacts),
  which inspect staged wine-zip contents this lane's changes cannot
  affect. Two were ~31 s timeouts under box saturation
  (boot-progress-snapshot, fs-dynamic-link-cache); both pass standalone
  on this tree, and another wave lane independently A/B-proved
  boot-progress-snapshot fails identically on the pristine base under
  the same load.
- C++ compile proof: `gl64bridge.cpp` (the only C++ file changed) was
  compiled with the exact emcc command the project makefile uses
  (harvested via `make -n wasm64-mt`: -O3, BOXEDWINE_OPENGL/GUEST_X64/
  VULKAN64, C++20) — clean object, empty log.
- Full dist build: not run. This lane queued behind two other wave
  lanes' builds for ~50 minutes (the first attempt died on the known
  runtime-assets 404 until the 13 manifest files were copied from the
  integ tree and hash-verified, per the fresh-worktree recipe); the
  queued build was stood down once the TU-level compile proof above
  was in hand, rather than hold the lane (and ~0.5 GB of staged assets)
  hostage to the shared queue. The native suite was likewise not run:
  the changed C++ body compiles only under `__EMSCRIPTEN__` (the
  native build takes the stub branch), and nothing else in this lane
  touches native-compiled code.

## Follow-ups left for whoever picks this area up next

1. Generator fix for `MarshalVkPerformanceValueINTEL` panics (above).
2. `replayImmBlock` drops recorded `glMultiTexCoord2fARB` ops on the
   WebGPU path — now logged once per fn; actually re-emitting texcoords
   per vertex in the block replay is functional graphics work.
3. Packed pixel types `GL_UNSIGNED_SHORT_4_4_4_4` (0x8033) and
   `GL_UNSIGNED_SHORT_5_5_5_1` (0x8034) have no unpack model — now
   logged once per type when a guest uses them; the layouts are
   mechanical to add if a guest ever needs them.
