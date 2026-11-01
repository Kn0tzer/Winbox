# FD-62 — texel-pointer atomics through a descriptor-heap array: the audit now refuses for the true, named reason

Lane: `lane-fd57` (worktree `wt-fd57`), off integration tip `cb10e167b`.
Register: FD-62 in Section A (stays OPEN — the serving blocker is the
translation stack; this lane's classification hardening is recorded there
and as a Section B row). Provisional number on the finding lane's register
was FD-57 (`lane-fd55`).

## The defect as found

The captured Godot fragment module `6f52b154d73c804f` (FD-55 capture;
copied to `testdata/spirv/godot-fs-atomic-texel-heap.spv`) indexes a
writable storage-image heap array (set 4, binding 0) and runs
`OpAtomicOr` through `OpImageTexelPointer` on the elements (two sites).
Both descriptor-array audit mirrors refused the array:

- JS: `tools/spirvfix/spirvfix.mjs` `r15PointerUses` — the texel pointer
  formation (op 60) was an unclassified use of the access chain, so the
  chain's classification was the generic escape
  `array element pointer escape (op60)`.
- C++: `source/vulkan/vk64bridge.cpp` `r15::pointerUses` — same shape.

The pointer's actual uses (the atomics) were never audited on either
side. The refusal was correct only by accident: it rested on the
formation being unnameable, not on any property of what the module does.

## The decisive measurement (why the register's suggested fix shape was wrong)

The register entry proposed teaching the pointer-use analysis to audit
through OpImageTexelPointer and admit the module. Before touching the
classifier, this lane measured the translation stack on minimal
hand-built modules (spirv-as / spirv-val clean) through the vendored
translator (`web/vendor/naga`, nagaVersion 30.0.1):

- 2D R32ui storage image, texel pointer + `OpAtomicOr`:
  FAIL `UnsupportedStorageClass(11)` (Image storage class) — for the
  wgsl, glsl, msl and hlsl targets alike (a frontend refusal).
- Same module with texel pointer + plain `OpLoad`: same failure.
- Same module with the texel pointer (and its `OpTypePointer Image`
  declaration) removed: translates.
- Buffer-dim storage image forms (the captured module's shape) fail
  earlier still: `UnsupportedCapability(ImageBuffer)`.

So the vendored translator cannot ingest ANY module containing a texel
pointer today. A classifier-only "admit" change would have flipped the
captured module to served at `vkCreateShaderModule` and failed it later,
inside pipeline creation / translation — the FD-49 failure shape
(admitted at the audit, dropped quieter downstream). Under this lane's
brief (a silent wrong answer is worse than the current loud refusal),
that change is forbidden. The load-result form of the texel pointer
(chain → OpLoad → texel pointer on the load result) is not a loophole:
the SPIR-V assembler itself rejects it ("Expected Image to be
OpTypePointer") — the operand is always a pointer (variable or access
chain), which is the form the audit now follows.

## What changed (both mirrors, instruction-for-instruction)

`r15PointerUses` (spirvfix.mjs) and `r15::pointerUses` (vk64bridge.cpp):

- A tracked id consumed as the Image operand of OpImageTexelPointer is
  followed THROUGH the texel pointer (the visit gains a texel flag;
  OpCopyObject preserves it). The texel pointer's loads / stores /
  atomics classify as image-CONTENT reads and writes, counted separately
  (`texelFormed`, `texelWrites`, `texelStoreWrites`, `texelAtomicWrites`)
  from writes to the tracked slot itself — an OpStore through a texel
  pointer rewrites a texel, not the descriptor slot.
- The texel pointer VALUE escaping (stored as a value, passed on,
  chained further, forming a second texel pointer) stays an escape, as
  does every other unclassified use. Non-texel behaviour is unchanged.

Callers:

- Array classifiers (`analyzeDescriptorArrays` / `r15::analyzeArrays`):
  slot writes and escapes keep their existing refusals; any texel
  formation through an element now refuses with a named reason —
  `texel-pointer atomic write through an array element is not served
  (the translation stack rejects OpImageTexelPointer)` (store and use
  variants for the weaker shapes). The JS rec exposes the texel counts;
  the C++ `ArrayVerdict` exposes `texelAtomicByVar`.
- Root-descriptor classifiers (`analyzeRootDescriptors` /
  `r15::analyzeRoot`): texel writes in a root slot reject the slot like
  atomic writes do — defensive accounting (a root variable can never be
  an image operand in valid SPIR-V), so no caller can silently ignore
  the new counters.

Net behaviour change: **zero modules change verdict.** Every module
served before is served now; every refused module is still refused. Only
the classification content and the refusal reasons changed — the refusal
now rests on the demonstrated translator wall and says so.

Also in this lane's commit, bookkeeping the mirror edits forced:

- `web/tests/suite/unit/spirvfix-r15.test.mjs`: one pre-existing test
  pinned the old incidental wording (`escape (op60)`) for a
  formed-but-unused texel pointer. Its refusal assertions are kept and
  strengthened (it now also pins `texelFormed == 1`); the reason pin
  moves to the named texel reason. The fail-closed verdict is unchanged.
- `docs/known-gaps/vulkan.md` (+ one cross-reference line in
  `tests.md`): every citation of the two edited files re-pinned through
  the exact diff line map (bridge net +17 below the edits, per-hunk
  partial shifts inside the edited spans; spirvfix map likewise). The
  citations suite is green, including the coverage floor. The pass also
  repaired reg:410's dual citation, which was already drifting on the
  base (its two cites now point at the grep-verified construct lines,
  spirvfix.mjs:3834 and vk64bridge.cpp:1332). One tests.md citation that
  refers to another lane's tree was deliberately NOT moved (it is that
  lane's numbering, not this tree's).

## Proof

- New JS suite `web/tests/suite/unit/spirvfix-texel-pointer-arrays.test.mjs`
  (5 tests):
  1. The captured module itself: set-4 array refused with the
     texel-atomic reason, atomic counted; the fix tier does not
     specialize it, RuntimeDescriptorArray survives, and the translator
     still rejects the output (the honest end state).
  2. Minimal reduction (hand-authored, spirv-as/spirv-val clean,
     `testdata/spirv/src/atomic-texel-heap.spvasm`): same refusal,
     counts exactly 1 formed / 1 atomic.
  3. Sabotage direction: the OpCopyObject twin
     (`atomic-texel-heap-copy.spvasm`) — texel pointer formed on a copy
     of the chain — refuses identically. A classifier that only audits
     a texel pointer formed directly on the chain id would admit it
     while the atomic writes the element unexamined.
  4. Attribution (`atomic-texel-heap-twoarrays.spvasm`): the benign
     writable array (load + OpImageWrite) in the same module stays
     servable with zero texel counts; only the texel-atomic array
     refuses.
  5. Escape preserved (`atomic-texel-heap-escape.spvasm`): a texel
     pointer stored as a value refuses through the escape
     classification, not the texel-serving reason.
  Red/green: with the spirvfix change stashed the suite fails 5/5;
  with it, 5/5 pass.
- Native mirror: guard block added to
  `tools/vkd3d/tests/vk64_native_test.cpp` (fixture bytes embedded in
  the FD-16 idiom; classifier assertions on `r15::analyzeArrays` —
  refusal, `texelAtomicByVar` attribution, benign sibling servable —
  plus audit-level assertions: all three modules return
  VK_ERROR_FEATURE_NOT_PRESENT with the RuntimeDescriptorArray
  unsupported count advancing by one each). The classifier half is
  proven green via a scratch harness built from the test's own preamble
  and the guard's exact fixture bytes (the audit half cannot run until
  FD-50 lands — see below; the harness binary also fails to compile
  against the unpatched bridge, on the missing `texelAtomicByVar`
  member — the native red signal).
- Affected suites: spirvfix-r15 (35/35), spirvfix-texel-heap,
  spirvfix-texel-buffer, vk-storage-format-mirror,
  vk-texel-format-mirror — green.
- Full JS unit suite on this tree: 1433 pass / 7 fail, the 7 being the
  dist-state audits (BUILD_INFO / dist / web-runtime completeness) of
  an unbuilt lane tree — this lane builds no dist; the merge gate runs
  them on the composed, built tree.

## FD-50 re-confirmation (found along the way; already registered)

`tools/vkd3d/tests/run-native.py` still aborts on the pristine base
(`cb10e167b`) and on the integration tip current at the time
(`306190757`), identically: gdb backtrace —
`KMemory64::memcpyToGuest(addr=281474976698064, size=8)` called from
`vk64Bridge` in the `VK64_fn_vkGetPipelineCacheData` handler
(fn 921; the handler writes `*pDataSize = 0` through `args.a[2]`, and
the value arriving there is a host-pointer-range address, not a guest
offset), tripping the harness memory model's assertion after 14 PASS
lines. This is FD-50's registered defect (found by the FD-16 lane);
this lane appended a dated re-confirmation to that entry instead of
opening a duplicate. Consequence for this lane: every native guard
after that section — FD-16's heap guards, the root-descriptor guards,
and FD-62's — cannot execute in the standard harness until FD-50
lands; FD-62's were proven by the scratch method above.

## What closing FD-62 still needs (for the register)

A translation stack that ingests OpImageTexelPointer — translator
support for the Image pointer storage class and Buffer-dim storage
images — or a sound pre-translation rewrite eliminating texel pointers.
Loads/stores through texel pointers have image read/write equivalents;
atomics through texel pointers have no WGSL equivalent and need a
different serving strategy. When that lands, the classifier change is
small and safe to make THEN: the uses are already audited and counted,
so admission can be granted per-use-class with the counts as the gate —
which is exactly the state this lane leaves behind.
