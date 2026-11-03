# FD-72 re-measurement — still blocked; the blocker is one component, every other link measured ready

Lane `lane-fd72b` (worktree `wt-fd72b`), off integration `cc1d89ab3`.
Task end state, fallback form: where the class cannot be genuinely
served on the current base, prove it still blocked with the blocker
named to one specific component and every other link in the chain
measured ready. That is the outcome. FD-72 stays **open**, on the
same closing terms the register already carried (the WGSL → naga
chain, or an equivalent serving stack). This lane changed no product
code: the register's FD-72 entry gained a dated re-measurement block;
this record and `test-results/fd72b/` are the evidence.

## The class

A fragment shader module reads the helper-invocation state — whether
the current invocation exists only to fill out a derivative quad or
was demoted. Two read spellings exist in SPIR-V:

- a variable decorated `BuiltIn HelperInvocation` (BuiltIn 23) — the
  captured Godot module's form
  (`testdata/spirv/godot-fs-atomic-texel-heap.spv`, 5,704 B,
  sha256 `a0ddbc0e…`; disassembly re-verified this lane: the variable
  is loaded at exactly two sites, `%91` feeding the
  `!helper`-gated subgroup ballot and `%125` feeding
  `OpSelect(helper, 0, bit)` into a subgroup OR-reduce — the two
  uses the register describes);
- `OpIsHelperInvocationEXT` (opcode 5381) from
  `SPV_EXT_demote_to_helper_invocation`.

The serving chain a guest module traverses: **vkCreateShaderModule
audit** (`vk64AuditSpirv` in `source/vulkan/vk64bridge.cpp`) →
**spirvfix pre-pass** (`tools/spirvfix/spirvfix.mjs`, via
`web/shader.mjs`) → **vendored naga 30.0.1** (SPIR-V frontend →
validator → WGSL writer, `web/vendor/naga`) → **WGSL** → **browser
WebGPU** (Tint/Dawn) → pipeline and draw.

## Measurements (all on base `cc1d89ab3`, this lane)

Harness: `test-results/fd72b/measure.mjs` (log:
`measure.log`, data: `measurements.json`). Minimal modules in
`test-results/fd72b/modules/` (spirv-as sources + binaries; all
spirv-val VALID except the no-capability EXT form, invalid by
construction): `min-used` (BuiltIn 23 loaded, steers output),
`min-declared` (declared, never loaded), `min-twin` (same shape,
FragCoord predicate), `min-ext-cap` / `min-ext-nocap` (opcode 5381
with / without its capability), `textbook` (glslangValidator
compiling `gl_HelperInvocation` from GLSL).

**M1 — The blocker: the vendored naga SPIR-V frontend, parse stage.**
Staged calls (`parseSpirv` → `validate` → writers; the error's
`kind` names the stage) fail at **parse** with
`UnsupportedBuiltIn(23)` for: the captured module after the pre-pass,
`min-used`, `min-declared`, and `textbook`. The production entry
(`web/shader.mjs` `toWGSL('spirv', …)`) fails identically, and
`translate` to the glsl/hlsl/msl targets fails at the same parse
stage — the rejection precedes every writer, so it is not a
WGSL-writer gap. The gate is the decoration's presence
(`min-declared` never loads the variable). Raw captured bytes with
no pre-pass fail earlier on an unrelated class
(`UnsupportedCapability(ImageBuffer)`, the FD-62 texel class the
pre-pass serves); with the pre-pass applied, BuiltIn 23 is the
module's only parse-stage blocker.

**M2 — The frontend otherwise ingests the whole real module.**
`modules/godot-stripped.spv` is the captured module with ONLY the
HelperInvocation surface removed (decoration, variable, interface
entry; its two loads replaced by copies of a false constant —
`strip-godot.py`; a measurement probe, not a serving candidate: the
substitution is exactly the rewrite FD-72's semantics analysis
rules out). spirv-val VALID, zero helper mentions. Naga: parse
**OK**; validate fails with FD-74's error, verbatim
(`godot-stripped-validate-error.txt`): "Built-in type for
SampleMask is invalid. Found Array { base: [0], size: Constant(1),
stride: 4 }" — the validator stage, a different defect, already
filed as FD-74. So past FD-72 the module's next blocker is known
and tracked, and nothing else in the frontend blocks it.

**M3 — spirvfix pre-pass: ready, and not a hidden blocker.** On the
captured module it applies exactly its four standing rewrites
(`strip-unused-physical-storage-buffer`,
`descriptor-array-specialize`, `texel-pointer-to-storage-buffer`,
`strip-unused-texel-buffer-capabilities`; 5,616 B out) and the
BuiltIn 23 decoration is present in the output — no rule rewrites
the BuiltIn-variable form, and none strips it. On the minimal
helper modules it applies nothing. Corpus gate
(`web/tests/spirvfix.test.mjs`, `corpus-gate.log`): 10/10 pass on
this base, including the standing pin that the captured module's
conversion keeps throwing `UnsupportedBuiltIn(23)`.

**M4 — Creation audit: ready (behaves as FD-75 designed).**
`tools/vkd3d/tests/run-native.py` on this base: exit 0, 57 PASS
sections (`run-native.log`), including "PASS: GUARD
helper-invocation reads (BuiltIn 23 / OpIsHelperInvocationEXT) are
refused by name at vkCreateShaderModule (FD-72)" — both spellings
refuse with `VK_ERROR_FEATURE_NOT_PRESENT` and the
`spirv-builtin-helper-invocation` counter increments; the
neighbouring FragCoord module admits. The audit is a mirror of the
translator block, not an independent defect: for the
BuiltIn-variable form its verdict and the translator's agree.

**M5 — Downstream of the translator: ready, with correct values.**
Probe: `test-results/fd72b/webgpu-probe.mjs` (results:
`webgpu-probe-results.json`), one probe-slot Chromium run
(SwiftShader Vulkan adapter, the project's probe configuration),
device created with the `subgroups` feature the adapter offers.

- The twin's production WGSL (`twin.wgsl`, naga's output for
  `min-twin`) compiles with **zero** compilation messages; a render
  pipeline on it draws a fullscreen triangle and the 64×64 readback
  is a single distinct pixel value, `255,0,0,255` — the value the
  twin's semantics require (its predicate is true at every pixel).
- A subgroup-ballot WGSL module — the captured module's other
  construct family — compiles with zero messages and renders
  uniformly `255,0,0,255` (a ballot of `true` over the active lanes
  is nonzero, as required).
- The browser's WGSL compiler itself confirms the language gap:
  `@builtin(helper_invocation)` fails with "expected builtin value
  name" and an enumerated list of 20 possible values, none of them
  a helper query; `isHelperInvocation()` fails with "unresolved
  call target 'isHelperInvocation'".

**M6 — The WGSL specification has not moved.** TR and living draft
fetched 2026-11-02 (`upstream-checks.txt`): full-text search finds
zero occurrences of `isHelper` / `helper_invocation`; the built-in
value name list contains no helper entry; the non-portability
warning FD-72 relied on is verbatim current ("helper invocations
may participate in subgroup operations, while on other devices
they may not…").

**M7 — Upstream naga has not moved.** crates.io: the newest
published naga is 30.0.1 — the vendored version
(`web/vendor/naga/version.js`). The published 30.0.1 crate source
contains the string `HelperInvocation` in zero files; its IR
`BuiltIn` enum (`src/ir/mod.rs:370`) has 49 variants, none for
helper invocation. The IR cannot represent the value, so no
re-vendor of any published naga changes M1.

## The EXT-spelling refinement (the one place the picture moved)

M1's boundary is the BuiltIn-variable form. The EXT opcode form
behaves differently on this base, and the register entry now says
so precisely. Measured: `min-ext-cap` and `min-ext-nocap` pass the
**whole production chain** — spirvfix applies
`lower-mid-shader-discard` and `toWGSL` returns WGSL (374 chars).
This is not new behaviour and not a serving of the class:

- The lowering is the discard-delay machinery (commit `a300e71c2`,
  2026-10-20 — it predates FD-72's filing). It rewrites each
  `OpIsHelperInvocationEXT` into an `OpLoad` of a private bool
  (`modules/min-ext-cap.postfix.dis.txt`): the flag is stored
  `false` at entry and stored `true` only at the module's own
  demote / lowered-discard sites. The emitted WGSL
  (`ext-cap.wgsl`) reads that flag.
- The substituted value is therefore the **self-demotion
  component** of helper state only. Rasterization helper state —
  lanes that were helpers before the shader ran — is
  unrepresented: `min-ext-cap` has no demote sites, its WGSL reads
  a constant false, and the probe renders it uniformly
  `0,255,0,255` (green = "not a helper" on every lane). On the
  Vulkan side those lanes can exist at any primitive edge; for
  reads steering subgroup collectives or atomic inputs — the
  idiomatic use, and the captured module's — the flag's answer is
  wrong exactly where it matters. (The render cannot show this:
  helper lanes' own writes are discarded by the hardware either
  way. The boundary is proven at the instruction level, not by
  pixels.)
- The guest path never reaches that translation anyway: the
  creation audit refuses the EXT spelling by name on this base
  (M4), because FD-75 mirrored the translator's raw verdict
  (naga alone still rejects the raw opcode form:
  `UnsupportedCapability(DemoteToHelperInvocation)` with its
  capability, `UnsupportedExtension` without — the FD-72 lane's
  E3; the acceptance above exists only through the pre-pass
  rewrite).

Admitting the EXT spelling on the flag's terms would trade a named
refusal for silently wrong values in the poisoned shapes. Extending
the same flag rewrite to the BuiltIn-variable form (the captured
module's) would do the same for the module whose two uses are the
ballot and reduce this entry documents. Both are rejected for the
reason FD-72 already records: silently wrong data is worse than
refusal. The demote WRITE side is untouched by all of this and
stays served (capability 5379 admitted; demotes lowered).

## Serving paths evaluated and closed on this base

- **spirvfix rewrite of the BuiltIn form** — the flag substitution
  above; unsound for rasterization helpers and for this module's
  collective-steering uses. Rejected (measured, M5/EXT section).
- **Re-vendor naga** — newest published is the vendored 30.0.1;
  its IR has no variant for the value (M7). Nothing to gain.
- **Patch the vendored naga build** — it could be made to accept
  the decoration, but its WGSL writer would have no legal value to
  emit (M5/M6): the target language has no query. Dead end at the
  same link.
- **Another translator route for guest SPIR-V** — production
  targets WGSL only (WebGPU consumes nothing else); the GLSL/HLSL/
  MSL writers are never reached because parse fails first (M1),
  and no browser shader path consumes them.

## Verdict

FD-72 remains **open and blocked**, on its existing closing terms.
The blocker is one component — **the vendored naga 30.0.1 SPIR-V
frontend** (parse stage; root cause one level up: naga's IR has no
HelperInvocation variant, and one level above that, WGSL has no
helper-invocation query to translate it to). Every other link —
creation audit, pre-pass, validator/writers for all other content,
the browser's compiler and GPU pipeline including subgroup
collectives — is measured ready on this base, several of them with
value-correct rendering proofs. Behind FD-72, FD-74 (validator,
SampleMask) waits for the same module, also re-measured this lane
(M2). FD-55's position is unchanged.

## Reproduction

```
node test-results/fd72b/measure.mjs            # M1, M2, M3 (translator/pre-pass)
python3 tools/vkd3d/tests/run-native.py        # M4 (audit guard)
node --test web/tests/spirvfix.test.mjs        # M3 (corpus gate)
node test-results/fd72b/webgpu-probe.mjs       # M5 (browser links)
# M6/M7: fetches and greps recorded in test-results/fd72b/upstream-checks.txt
```

Notes on harness shape, for the record: per-target `translate`
calls for the GLSL target report a writer-options artifact
("missing field `version`") on modules that PASS parse/validate;
irrelevant here — production consumes WGSL only, and every refused
module fails at parse, before any writer, on every target. The
probe GPU is SwiftShader, the project's standard probe target; the
measurements are compile/verdict/value checks, not performance
claims.
