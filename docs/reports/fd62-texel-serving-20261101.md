# FD-62 serving — texel-pointer atomics served by the R12A storage-buffer rewrite

Lane `lane-fd62serve` (session 067c686b), 2026-11-01. Base: integ tip
`a1e8928ba`. Fixing commit: `24becdb53`. Also on the branch, for the
in-vivo proof only: cherry-picks `aaf4182de` + `c7c2409f7` (FD-55
pre-warm capture driver and warmed-archive pointer, from
`lane-fd55prewarm`) and `de92a9ee1` (refusal logging by module
size+hash, from `lane-fd55pipe`'s `72360dda5`); citation re-pin
commit on top. Nothing pushed, nothing deployed; the live site and
master are untouched.

## The end state, and what was delivered

Shader modules performing atomics through `OpImageTexelPointer` —
refused by name at creation, and byte-identified by the FD-55
pipeline lane as the first blocker stopping the Godot demo from
drawing (captured module 5,704 B, djb2 `0b3b4d8cc939485a`) — are now
genuinely served. A Dim=Buffer storage image is guest buffer memory,
so the shader-fixup layer rewrites a variable in the served class to
a **storage buffer of texels at the same set/binding**, each
`OpImageTexelPointer` becomes an `OpAccessChain` into it, and the
loads/stores/atomics keep their instructions and land on the view's
bytes. The audit mirrors admit exactly the class the rewrite
converts, in both directions; the page tier binds the view's buffer
range for the rewritten declaration so the writes have somewhere
true to land.

## The serving strategy (spirvfix R12A)

`texelPointersToStorageBuffers` runs first in both R12 passes of
`applySpirvFix` (applied name `texel-pointer-to-storage-buffer`), so
it serves direct variables pre-specialization and R15a clones
post-specialization with one transform. For each candidate variable
it creates `struct { runtime array of elem }` (Block-decorated,
member Offset 0, ArrayStride = texel byte size), replaces the
variable in place with a StorageBuffer variable at the same
set/binding (decorations copied verbatim except Coherent, dropped
per the R15a clone rule), rewrites each formation in place to an
access chain `(var, 0, coordinate)`, retypes texel-pointer copies,
sweeps the dead cone, and sweeps the orphaned image/pointer/array
types so the ImageBuffer capability goes with them.

Eligibility is decided by one cone analysis, `analyzeTexelCone`,
shared verbatim by the rewrite, the JS classifier
(`analyzeDescriptorArrays`) and both bridge mirrors
(`r15::analyzeTexelCone` in `r15::analyzeArrays`;
`vk64TexelConeServable` + R12A coverage inside
`vk64TexelStorageServable`). The cone fixpoint-tracks image
pointers, image values and texel pointers from one root and
classifies every use:

- the image value may be consumed only by formations and dead loads
  (a mixed image/buffer element refuses — two irreconcilable
  representations);
- the texel pointer may reach only Load, Store (pointer position —
  value position is an escape) and the served atomic set; any other
  use (call, select, phi, chain, stored-as-value) refuses;
- each formation is validated: pointee is the sampled type (or the
  format's vector type), coordinate is a 32-bit integer scalar,
  sample operand absent or the constant zero.

**Served class, in exact terms.** Formats: 32-bit plain-component
only — Rgba32f/R32f/Rg32f, Rgba32i/R32i/Rg32i, Rgba32ui/R32ui/Rg32ui
(scalar formats for atomics; multi-component for loads/stores).
Atomics: Load, Store, Exchange, CompareExchange, IIncrement,
IDecrement, IAdd, ISub, SMin, UMin, SMax, UMax, And, Or, Xor.
Refusing by name, with fixtures: OpAtomicCompareExchangeWeak
(opcode 231, sibling of CompareExchange 230 — the translator
rejects the opcode itself, measured),
sub-word formats (R8ui — a packed-word atomic would touch
neighbouring texels), 64-bit formats (R64ui — WGSL atomics are
32-bit), mixed use, escapes. Texel forms through root-descriptor
slots keep their existing refusal (the root classifier is
unchanged: root cones point into buffer memory, and an image
pointee there is not a shape vkd3d emits).

**The blind spot this closes.** The FD-62 hardening's uses analysis
(`r15PointerUses`) never traces an OpLoad *result*, so a module
forming the texel pointer on the loaded image value classified
`servable: true` while carrying a texel atomic — admitted at the
audit, then rejected by the translator: the FD-49 shape. The cone
counts value-form formations separately (`valueFormations`,
`valueAtomicWrites`, `valueStoreWrites`); the fixture
`atomic-texel-heap-valueform.spv` proves attribution and end-to-end
serving.

## The page tier (web/vkwebgpu.mjs)

The bridge serializes a storage texel view binding as wire kind 6
regardless of the shader-side declaration shape. For the R12S shape
(storage texture declaration) the page materializes a texture; for
the R12A shape the declaration is a storage **buffer**, and the old
bind code resolved only kind-1/kind-4 records there — a kind-6
record fell through to a zero dummy buffer, which would have
silently swallowed the shader's atomic writes. Now
`texelBufferBind` resolves the view to a GPUBuffer over exactly the
view's bytes (content-keyed cache, view identity in the key),
called from both the bind-group fingerprint and `bindGroupEntries`
(so cache-hit binds still register), and registers it in
`storageBufferRegistry` with the binding batch. An unservable real
view — or a kind-6 record under a uniform declaration — drops the
bind group loudly (`bind-group-texel-buffer-unavailable` /
`bind-group-texel-buffer-uniform-decl`); a VK_NULL_HANDLE view keeps
null-descriptor semantics via the zero fallback. Readback
arbitration ranks a this-batch buffer registration with the fresh
texel-texture pick: if a range was bound both ways in one batch the
bytes are genuinely split and the readback declines by name
(`buffer-readback-representation-ambiguous`) rather than serving
either copy as truth.

## Proof

1. **Captured module.** `godot-fs-atomic-texel-heap.spv` classifies
   servable (set-4 array: texelFormed=2, texelAtomicWrites=2);
   `applySpirvFix` applies `descriptor-array-specialize` +
   `texel-pointer-to-storage-buffer`; the output is spirv-val clean
   (vulkan1.1), has zero OpImageTexelPointer, no descriptor runtime
   array, and both OpAtomicOr sites surviving as buffer atomics.
   Naga's only remaining rejection is `UnsupportedBuiltIn(23)` —
   the module's next blocker, registered as FD-72 (below).
2. **Mirrors, both directions.** JS:
   `spirvfix-texel-pointer-arrays.test.mjs` rewritten to the served
   contract (12 tests: captured module, pointer/copy/value forms,
   store-only, direct variable, two-array attribution; refusals for
   weak/R8ui/R64ui/mixed with the named reason and the capability
   surviving; escape still an escape). Native: the FD-62 guard flips
   to VK_SUCCESS with per-array attribution retained, plus a
   boundary guard (value form admitted; weak/R8ui/R64ui/mixed/escape
   refuse) — the boundary fixtures are Fragment-stage word arrays
   derived from the existing guard fixtures, because compute
   fixtures are admitted at creation by policy and would make the
   assertions vacuous (the FD-67 lesson, applied deliberately).
   Page: `vk-texel-buffer-binding.test.mjs` pins the binding,
   fingerprint and readback shapes. Adjacent suites
   (spirvfix-r15, spirvfix-texel-buffer, storage-texel family) pass
   unchanged.
3. **In vivo** (warmed archive, standard demo path, this branch's
   build: git_sha 2e890f315, wasm a2e1059d; evidence
   `test-results/fd62serve/warm-20261101-110605/`). The
   byte-identified module (5,704 B, djb2 0b3b4d8cc939485a) was
   submitted twice and created both times with no refusal — the
   first run in which it has ever been admitted. Across 254
   vkCreateShaderModule calls there were **zero audit refusals**
   and **zero CreateGraphicsPipelineState E_FAILs**; the two
   baseline captures had five E_FAILs each, each landing in the
   same second as this module's refusal. vkd3d completed its PSO
   warm-up (pipeline-library cache flushes) and the guest reached
   vkQueueSubmit2 (22 submit log lines). **The demo still did not
   present** (0 guest presents at the 1,500 s deadline; outcome
   "deadline"): no swapchain was created, submissions dropped at
   the bridge's `!view || !view->image` guard (8,386 drops,
   logged loudly), FD-70's two modules were never submitted in
   this run (their in-vivo status is untested, not changed), and
   FD-72's HelperInvocation blocker still stands in front of any
   draw using this module — this run produced no evidence of
   reaching that translation. Stated exactly: FD-62's class is
   eliminated as a blocker; FD-55's remaining blockers are FD-70,
   FD-72, and the downstream submit/swapchain behavior above.

## Gates

All against this branch's final dist (built from 2e890f315;
BUILD_INFO wasm a2e1059dfc1a55fb405d998adcb35348c3d86ce5cf0cafd9fb9dc91889d6b91a;
evidence in `test-results/fd62serve/`):

- Dist build: "Browser export verified" / BUILD COMPLETE.
- Runtime completeness vs the dist: 0 failures, 1 standing skip.
- JS unit suite: **347 passed / 0 failed** (integration baseline
  346/0; the delta is this lane's new tests).
- Legacy suite: **110 passed / 4 failed** — an improvement on the
  FD-64 baseline (109/5): the corpus gate
  (`web/tests/spirvfix.test.mjs`) was re-baselined on measured
  sets (commit 7d8673a56) and is green; the four remaining
  failures are the standing vkwebgpu baseline, byte-identical in
  name to the integration gate's.
- Native emulator suites: main **663/663**, x64 **62/62**. (One
  transient: the first main run at box load ~9.8 failed
  "CPU32 UD2 with SIG_DFL in a subprocess"; the rerun at load
  ~1.4 passed 663/663. This branch changes no CPU/kernel code —
  its only native delta is vk64bridge.cpp — and the same test
  passed in the integration gate; recorded as load flakiness,
  with both runs' logs preserved.)
- vkd3d native harness (run-native.py): 54 PASS, exit 0.
- Citation audit: 0 drift / 0 missing.

## Register actions on this branch

- FD-62 **closed as CL-25** (fixing commit `24becdb53` + this
  record). Composition note: on integration CL-25 follows FD-53's
  CL-24; this branch's register predates that merge.
- FD-55 annotated: its first-wave blocker is closed; the entry
  stays open on FD-70 and FD-72.
- **FD-72 filed** — the vendored translator cannot ingest a BuiltIn
  HelperInvocation variable in any spelling (measured:
  `UnsupportedBuiltIn(23)`; `OpIsHelperInvocationEXT` →
  `UnsupportedCapability(DemoteToHelperInvocation)`; the EXT op
  without its capability is invalid and rejected). The captured
  module loads it at two sites feeding a ballot election and the
  second atomic's value mask; substituting a constant would corrupt
  the atomics' inputs, so there is no honest local rewrite.
  Numbering per the merge lane's settled audit on integration
  (FD-70 confirmed; the pre-warm speed entry is FD-71).
- **FD-73 filed** — the pinned runtime-assets release archive
  disagrees with the manifest's file pins for wine64.zip
  (`b7476541…` vs pin `53d77f33…`) and wine32.zip (`955cc41c…` vs
  `7f2209cf…`); `bootstrap.py --local` fails on a fresh checkout
  while trees already carrying the files verify clean and mask it.
  This lane restaged from the integ tree with per-file sha256
  verification against the manifest.

## Notes for the composing merge

- Branch commits: `aaf4182de`, `c7c2409f7` (pre-warm tooling,
  already on integration via the FD-53/FD-55pipe merge — duplicates
  by patch-id), `24becdb53` (the fix), `de92a9ee1` (refusal logging,
  likewise already on integration), the citation re-pins
  (cdecd2fa6, and in 7d8673a56), 2e890f315 (format tables as
  if-chains), 7d8673a56 (legacy corpus gate re-baseline), and the
  register/record commit. The fix commit touches
  `tools/spirvfix/spirvfix.mjs`, `source/vulkan/vk64bridge.cpp`,
  `web/vkwebgpu.mjs`, tests and fixtures, and re-pins ~290
  line-number citations in `docs/known-gaps/vulkan.md` that the code
  insertions shifted (audit-verified, zero drift after).
- The register edits touch FD-55's entry (annotation), FD-62's
  header (closure marker), and append FD-72/FD-73 (end of section A)
  and CL-25 (end of section C). FD-55's entry was also annotated by
  the pre-warm/pipeline lanes on integration — expect a textual
  composition there, not an auto-merge.
- Expected wasm delta vs the current integ tip: the bridge mirror
  changes in `24becdb53` only (the refusal logging is already
  composed there).
