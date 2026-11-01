# FD-72 — HelperInvocation: the boundary, proven; the failure moved to a named creation refusal (FD-75)

Lane `lane-fd72` (worktree `wt-fd72`), off integration `1d8213fb0`.
Task end state, fallback form: where no sound serving strategy exists
with the vendored stack, the end state is the boundary proven with
primary evidence — including an evaluated, evidence-backed account
of what changing the translation stack would entail — and the
register entry updated to state the boundary in those exact terms.
That is the outcome. FD-72 stays **open**, on translator-stack
terms. The lane additionally closes the mirror-discipline gap the
investigation exposed (FD-75): the unservable class is now refused
by name at module creation instead of being admitted and dropped
at pipeline translation.

## The module and its two uses

The captured module is `testdata/spirv/godot-fs-atomic-texel-heap.spv`
(5,704 B, djb2 `0b3b4d8cc939485a`) — Godot's cluster-fill fragment
shader. After the FD-62 serving fix, spirvfix applies
`strip-unused-physical-storage-buffer, descriptor-array-specialize,
texel-pointer-to-storage-buffer, strip-unused-texel-buffer-capabilities`
(5,616 B out) and both texel atomics survive as buffer atomics. The
HelperInvocation variable (`%gl_HelperInvocation`, Input bool,
BuiltIn 23) is loaded at exactly two sites:

- **Ballot election.** `%88 = OpGroupNonUniformBroadcastFirst %75`
  (the cluster id); `%90 = (%88 == %75)`; `%93 = %90 && !helper`;
  `%94 = OpGroupNonUniformBallot %93`. The ballot result is consumed
  only through popcount (`%100`) and == 0 tests (the `%178` test and
  the phi `%99` → `%127`), which gate both atomic blocks.
- **Contribution mask.** `%126 = OpSelect(helper, 0, %123)` where
  `%123` is a per-lane bit; `%124 = OpGroupNonUniformBitwiseOr
  Reduce %126`; `%124` is the *value* operand of the second
  `OpAtomicOr`.

At both sites a helper lane must contribute exactly nothing: at the
first, helpers must not vote in the election whose popcount decides
whether the real lanes run the atomics; at the second, a helper's
bit would be OR-reduced into a value no real lane set.

## Evidence (all measured 2026-11-01, this lane)

**E1 — The vendored translator rejects the builtin in its SPIR-V
frontend, on every backend.** The post-FD-62 module through the
vendored naga 30.0.1, called exactly as production calls it
(`web/shader.mjs`'s `translate({from:'spirv', to:'wgsl', source})`),
fails with `UnsupportedBuiltIn(23)` for the wgsl, glsl, hlsl and
msl targets alike — so the rejection is not a WGSL-writer gap.

**E2 — The gate is the decoration's presence, not its use.**
Hand-written minimal modules (spirv-as clean): a BuiltIn 23
variable *declared but never loaded* fails identically to
load-with-unused-result and load-used-in-select forms. Replacing
both loads in the captured module with `OpCopyObject
OpConstantFalse` still fails. Removing the decoration, the variable
and its entry-point interface entry clears the frontend entirely —
the module's next failure is E6's SampleMask defect, nothing
helper-shaped. The predicate source is the module's only
translation-level blocker of this class.

**E3 — Both EXT spellings fail too.** `OpIsHelperInvocationEXT`
with the DemoteToHelperInvocation capability:
`UnsupportedCapability(DemoteToHelperInvocation)`. The opcode
without the capability: `UnsupportedExtension(
"SPV_EXT_demote_to_helper_invocation")`.

**E4 — The target language has no value to translate to.** The
current WGSL specification (w3.org/TR/WGSL, read 2026-11-01)
contains no helper-invocation query: no `@builtin` value and no
function (full-text search for `helper_invocation`: zero matches).
Tint exposes what WGSL defines; there is nothing for a writer to
emit.

**E5 — WGSL's helper semantics forbid every predicate-free
rewrite.** WGSL §15.4: helper invocations "help compute derivatives
and may participate in subgroup operations"; their atomics return
indeterminate results and storage writes do not happen. §15.5:
"Helper invocations may be active or inactive in an operation.
That is, on some devices helper invocations may participate in
subgroup operations, while on other devices they may not", with an
explicit warning that assuming either yields non-portable results.
The captured module's two uses are exactly the poisoned shape:
ballot and subgroup-reduce inputs. On a spec-permitted
implementation where helpers *do* participate, constant
substitution (helper ≡ false), dropping the `!helper` ballot term,
or dropping the masking select each change the popcount / reduced
value, and so whether and with what value the atomics fire —
silently wrong data, the one outcome this task ranks worse than
refusal. No local rewrite is sound, and the unsoundness is not a
property of this module alone: it applies to any module whose
helper predicate feeds a subgroup collective or an atomic input,
which is the idiomatic use of the builtin.

**E6 — A second, distinct defect one step past FD-72 (filed as
FD-74).** The vendored naga's *validator* rejects fragment
SampleMask builtins as a class: after E2's strip, the captured
module fails with "Built-in type for SampleMask is invalid. Found
Array {…, size: Constant(1), stride: 4}" — SPIR-V specifies the
builtin as an array; the validator wants another shape. Proven
general: three minimal hand-written modules (access-chain load;
+Flat; whole-array load + CompositeExtract) fail identically, as
does a textbook glslangValidator-compiled GLSL fragment shader
reading `gl_SampleMaskIn[0]`, with and without SampleRateShading.
The write direction fails too, with a different error
("Initializer doesn't match the variable type" on a glslang module
writing `gl_SampleMask`). Corpus scan: the captured module is the
only corpus module carrying a SampleMask input. No spirvfix
rewrite can change the variable's spec-mandated type. FD-72's
original closing expectation ("after which the captured module
should translate end to end") was therefore incomplete; FD-74
carries this defect separately.

**E7 — Upstream naga cannot serve it either.** The current
upstream naga's IR `BuiltIn` enum (50 variants, docs.rs, checked
2026-11-01) contains **no HelperInvocation variant** — the IR
cannot represent the value at all, so no re-vendor of any released
naga changes E1, and its WGSL writer would have nothing legal to
emit regardless (E4).

## What changing the translation stack would entail

In dependency order, all three steps are prerequisites; none
exists today:

1. **A WGSL helper-invocation query** — a specification change
   (a `@builtin(helper_invocation)` value or an `isHelperInvocation`
   function), then its implementation in the browser's WGSL
   compiler (Tint). This step is outside this project entirely.
2. **naga support built on it** — a new IR `BuiltIn` variant, the
   SPIR-V frontend accepting BuiltIn 23 and `OpIsHelperInvocationEXT`,
   and the WGSL writer emitting the query.
3. **Re-vendoring here** — replacing the pinned naga 30.0.1 and
   re-proving the corpus and the audit mirrors against the new
   verdicts (the corpus gate's REFUSED pin for this module would
   flip to a served pin).

A vendored-naga patch alone is insufficient (it could accept the
builtin but would have no legal WGSL to emit — E4/E5); a spirvfix
rewrite alone is unsound (E5). Serving also remains gated behind
FD-74 (E6) for this module even once the chain exists.

## The hardening fix (FD-75, commit 69bb72126)

The investigation exposed a mirror-discipline gap of the
FD-49/FD-67 shape: `vk64AuditSpirv` had **no BuiltIn 23 rule**, so
the module was admitted at `vkCreateShaderModule` and failed later
in the page tier (`createShaderCache`: "page tier cannot translate
the fragment SPIR-V module (…) — frame skipped"), its draws
dropped after the guest had been told creation succeeded. The
audit now scans for both markers (an OpDecorate BuiltIn 23, and
opcode 5381 OpIsHelperInvocationEXT) and refuses the module at
creation with `VK_ERROR_FEATURE_NOT_PRESENT`, counted and logged
as `spirv-builtin-helper-invocation`; the creation path's existing
refusal log names the module's size + djb2 hash. The boundary is
presence-based because the translator's is (E2). The demote WRITE
side is untouched: capability 5379 stays admitted, only the read
refuses. `docs/known-gaps/vulkan.md` gained the matching
REJECTED-LOUDLY row; all vk64bridge.cpp citations in it were
re-pinned through the change's exact line map (audit: 352 rows,
zero DRIFT / zero MISSING, MATCH counts identical to baseline).

Proof of the fix: a native GUARD in
`tools/vkd3d/tests/vk64_native_test.cpp` (both spellings refuse and
count; a FragCoord-neighbour module admits), **sabotage-verified** —
with the audit triggers neutered the harness aborts at the guard's
first assertion; with them live the full native run is green.

## In vivo

Warmed archive, standard demo path (`web/tests/godot-warm-capture.mjs`,
the FD-55/FD-62 driver), this branch's build (git_sha `69bb72126`,
wasm `04e73f59`; evidence `test-results/fd72/warm-20261101/`).
Outcome "deadline" at the 1,500 s deadline, 0 Vulkan presents —
the same headline outcome as the FD-62 lane's run on the admitted
build. The failure shape is what changed, exactly as intended:

- **256 `vkCreateShaderModule` lines: 254 admitted, 2 refused —
  and the 2 refusals are the byte-identified module, both times:**
  `vkCreateShaderModule REFUSED 5704 bytes (hash 0b3b4d8cc939485a)
  result=-8` at 267.8 s and 268.2 s, each preceded by the named
  counter line `UNSUPPORTED spirv-builtin-helper-invocation
  (shader reads the fragment helper-invocation state (BuiltIn
  HelperInvocation / OpIsHelperInvocationEXT); the page-tier
  translator rejects the builtin and WGSL has no helper-invocation
  query (FD-72); VK_ERROR_FEATURE_NOT_PRESENT) in
  vkCreateShaderModule`. (The FD-62 run's census was 254 creations,
  0 refusals: the same module set, with this one module now
  refused by name instead of admitted.)
- **Zero `page tier cannot translate` lines in the entire run** —
  the silent-drop path this module took in the FD-62 run no longer
  exists for it (nor did any other module hit it).
- The guest absorbed the refusals without a pipeline-creation
  failure cascade: **0 `CreateGraphicsPipelineState` E_FAILs**,
  and the guest reached `vkQueueSubmit2` (22 submit log lines —
  the same count as the FD-62 run). No other module was refused
  at creation; the only other UNSUPPORTED lines are the standing
  `spirv-compute-entry` acceptance logs.
- The demo still presented nothing (0 guest Vulkan presents;
  screen static after the initial window), so FD-55's remaining
  blockers stand exactly as the FD-62 lane left them: FD-70
  (untested again this run — nothing in this run's evidence
  changes its status), FD-72 itself for any actual draw using
  this module, and the downstream submit/swapchain behaviour.
  Stated independently of FD-70's fate, per the lane brief: what
  this run proves about FD-72's own blocker is that it now stops
  the module at creation, by name, with the reason logged —
  instead of surfacing as dropped draws after a successful
  creation.

## Gates (this branch, against the lane build)

- Dist: `tools/bw64-build.sh` full build from commit `69bb72126`
  + `--export-only` after restaging the generated payloads a fresh
  worktree lacks (DXVK build trees, GStreamer payloads, per-file
  sha256-verified runtime assets, 13/13 against the manifest);
  "Browser export verified" / BUILD COMPLETE; BUILD_INFO
  git_sha `69bb72126`, wasm sha256 `04e73f59…`; the dependency
  (support) archive is byte-identical to integration's
  (`c4dbe35986a5`).
- Native: `tools/vkd3d/tests/run-native.py` completes, exit 0
  (55 PASS sections — the integration gate's 54 plus the new
  FD-72 guard), including the new FD-72 guard.
- JS unit suite: at the integration baseline before the build
  (347 passed; the only failures the two dist-state audits,
  `provenance-artifacts` and `runtime-completeness`, which require
  a built dist) — and both dist-state audits pass against the
  built dist (9/0 across the two files), so the suite is fully
  green on the built tree.
- Legacy suite: 110 passed / 4 failed — the standing vkwebgpu
  DESC_SET set, identical to the integration gate's; the corpus
  gate (`spirvfix.test.mjs`, which pins this module's refusal by
  name as `UnsupportedBuiltIn(23)`) passes 10/10 standalone.
- Citation audit: 352 rows, zero DRIFT / zero MISSING.
- Known-gaps coverage check: clean.

## Register actions on this branch

- **FD-72** — rewritten: stays OPEN, boundary stated in the exact
  terms of E1–E7; closing condition is the three-step stack chain
  above (with FD-74 noted as waiting behind it).
- **FD-74** — filed (E6, SampleMask validator class).
- **FD-75** — filed as FIXED on this branch (the audit gap above),
  with a Section B row.
- **FD-55** — annotated: FD-72's boundary proven; its module's
  in-vivo failure shape changes from dropped draws to a named
  creation refusal via FD-75; the entry stays open on FD-70 +
  FD-72 and the downstream behaviour.
