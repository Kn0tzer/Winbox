# Final-batch merge record — FD-16 + FD-44 + FD-48 + FD-01 remedy + FD-49 (2026-10-31)

Integration: `integ/winbox-20261022b`, `51d0aff98` -> composed tip
(merge commits `c90cae341` fd16, `eca093d60` fd44, `ab1333602`
fd48, `ca4166040` fd01-so, `c34488cf7` fd49, plus composition
commits for cross-references/citations, the container re-pin, the
process-thread-exit model, and this record). Nothing pushed,
nothing deployed; the result is deploy-ready and NOT deployed.
Evidence: `test-results/merge-finalbatch-20261031/`.

## Composition

All five branches merged. FD-49 (the optional fifth) composed
cleanly: its one code conflict (web/wgsl-utils.mjs) was two
spellings of the same rule — a specialized clone records its
guest/origin binding when the binding in its name differs from
the slot it sits on — unified with FD-49's not-already-set guard;
both lanes' suites pass on the composition (23/23 across
spirvfix-texel-heap, vk-storage-format-multi-type,
vk-descriptor-binding-remap before the merge was committed).
The vulkan.md citation conflict was line-number drift from both
sides; the four affected rows were re-pinned against the merged
files through the audit's own constructs (vk-register-citations
4/4, DRIFT 0).

Register renumbering, exactly per the composition map:
FD-16 branch 'FD-48' (run-native fn-921 abort) -> FD-50;
FD-44 branch 'FD-47' (RIP sampler) -> FD-51; FD-44 branch
'FD-48' (KMemory teardown race) -> FD-52; FD-01 remedy branch
'FD-45' (duplicate libvulkan.so.1) -> FD-53; FD-48 branch
'FD-53' (DLLTRACE dialect) -> FD-54; FD-16 closure 'CL-12' ->
CL-13 (CL-12 is FD-46's); FD-49 keeps its number, closed as
CL-16. FD-44 closed CL-14, FD-48 closed CL-15, each naming its
fixing commit and gate re-proof. FD-01 stays OPEN: its entry
carries the merge update; the remaining terms are deploy and
the live re-measurement. Mechanical audit of the composed
register: 54 FD entries, all unique; CL-01..CL-16 in order; no
dangling closure references. Lane reports' own cross-references
were renumbered to match; no code comments carried stale
numbers.

## Container

wine64.zip re-staged from the live base with the merged tree's
stage-wine8-backports.sh and re-pinned (`7d9e89f9...`,
209,611,866 B): member audit 1,257 members, name set identical
to the live base, exactly two content differences — win32u.dll
`a8ca5fbb` -> `49b87438` (FD-40 stamp) and win32u.so
`5dac5821` -> `bffd9036` (FD-01 patched unix half) — both
byte-identical to the committed backport files. Contents are
identical to the FD-01 lane's pin `d71ca275`; only zip metadata
differs. The other five runtime zips are the canonical
runtime-assets copies (pin-matching). First dist build failed
its completeness step on exactly one line — the manifest still
carried the lane pin — the manifest was regenerated from the
shipped zips (single-line diff) and the build re-run clean.

## Gate (all on the composed tree)

- Dist build: **Browser export verified**; dependency archive
  `de592964f772` byte-identical (its duplicate libvulkan.so.1
  member warning is FD-53's subject, registered open).
  BUILD_INFO git_sha = the re-pin composition commit;
  wasm `d0aff354...`.
- Completeness: 0 failures (1 skip: prefix64 built artifact,
  the standing skip).
- JS unit suite: **344 passed / 0 failed** (final run). One
  composition catch inside it: `process-thread-exit` — a
  source-model test that extracts the real `deleteThread` —
  failed on any tree containing FD-44, whose design changed
  that function (the FD-44 lane ran no JS suite). The model
  was updated, not weakened: the mock KThread now models the
  FD-44 reference surface (count starts at 1, releaseRef
  destroys on last drain, mirroring kthread.cpp), the
  memory exactly-once assertions are unchanged, and the
  three scenarios additionally pin thread destructions at
  exactly one per thread.
- Native main flavour: **662/662 full serial, 0 FAILED,
  246 s** (the FD-48 branch's owed full run, stalled there at
  659 under load ~28, discharged here). x64 flavour:
  **62/62, 0 FAILED**.
- boot64-smoke: PASS. boot32-vkboot: PASS with a certified
  start (deskpin handle `0x10052`; the 32-bit OPENED spawn
  signal present throughout the console).
- dbwin-echo: first attempt FAILED without booting — it timed
  out waiting for a probe slot (300 s) while another lane's
  build held the build-pending interlock; console empty.
  Re-run at clear slots: **PASS**. Recorded as an
  infrastructure contention failure, not a product result.
- D3D12 oracles: 64-bit comprehensive **PASS** (VERDICT
  passed, RESULT 0, certified start). 32-bit system-placement
  oracle (web/tests/d3d12-32-system.mjs): **VERDICT passed,
  tri12Result 0** — the run FD-48 blocked outright at the
  batch-3 gate now completes. (An earlier invocation in this
  gate used a non-existent d3d12-oracle.mjs path and failed
  MODULE_NOT_FOUND; the in-tree harness is
  d3d12-32-system.mjs. No product meaning.)
- Cold-start measurement (the FD-01 merge term), merged
  dist, fresh profile per boot, stopgap budget spent,
  900 s window, offline classification under the hardened
  rules: **x86-64 3/3 certified** (handle `0x10048`, 5 X11,
  explorer; starts began at load 4.3, 12.5, 11.5) and
  **x86 3/3 certified** (handle `0x10052`, 5 X11, explorer).
  Reference points: four-arm pre-remedy 0/10 in this shape;
  remedy branch 5/5.
- Godot presented frame (the FD-16 merge term): **NOT
  CONFIRMED — reported as not confirmed.** demo-matrix
  failed its blank-screen criterion twice with an identical
  signature (non-dark fraction 0.0044, ~1,450 distinct
  colors); its presented-frame counter stayed at 0 across
  a full 387 s observation. The capture harness on the
  merged dist shows the guest side healthy in the FD-16
  sense (engine banner at 126 s, 56 shader modules, zero
  capability rejections, zero 'failed', pipeline cache
  flushing), and the D3D12 oracles present + read back on
  the same build — so the present path works in general
  and this demo's missing frame is a separate,
  uncharacterized defect, registered as **FD-55** with
  this evidence and the next step named. FD-16's closure
  stands on its own registered terms (the pipeline-
  building class), which this gate re-proved at unit,
  audit, and guest-stream level.
- Correction carried: FD-49's lane text 'the demo
  presented' (its Section B row and report) is not
  supported by the preserved evidence — the capture
  harness's final screenshot is the launcher page,
  pixel-identical at 5 s and 410 s, and no harness has
  ever counted or pixel-verified a presented frame for
  this demo. CL-16 records the narrower, supported impact
  statement (modules accepted at creation; no dispatch
  reached their translation in the captured run; the
  class was latent).

## Disposition

The composed tree is deploy-ready. FD-01's remaining terms
(deploy, live re-measurement) and FD-55 are the head of the
queue. Nothing in this batch was pushed or deployed.
