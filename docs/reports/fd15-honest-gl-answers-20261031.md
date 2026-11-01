# FD-15 — OpenGL bridge honest answers (2026-10-31)

Branch `lane-fd15`, off integ tip `c4bab5e07`. Fix commit
`f8bedecdb`. Register: FD-15 closed as CL-07; the one genuine
honesty-vs-compatibility conflict is carried forward openly as
FD-39.

## What was wrong, and what each case does now

**(a) Texture parameters the bridge claimed to accept but the
backend dropped.** The C++ side forwarded four sampler settings
as supported — texture minimum/maximum LOD (0x813A/0x813B) and
depth-compare mode/function (0x884C/0x884D) — while the WebGPU
backend rejected them with INVALID_ENUM, and its sampler never
used any of them (LOD clamps were hardcoded 0/32, compare never
set). The backend now accepts and stores all four with real GL
validation (LOD values must be ≥ 0; compare mode must be
NONE or COMPARE_REF_TO_TEXTURE; compare func must be a valid
comparison), and its sampler wires them through:
`lodMinClamp`/`lodMaxClamp` come from the stored parameters,
and the WebGPU `compare` is set for depth-format textures when
the guest asked for COMPARE_REF_TO_TEXTURE. Desktop hosts
(WebGL2 / native GL) already honored these natively.

The REST of the dropped desktop-only set (LOD bias, mipmap
generation, border color, depth-texture mode, swizzle, …) has
no model in either backend. Forwarding it would not be honest
either: the only "honest" answers are accepting-and-dropping
(current, once-logged) or recording INVALID_ENUM — and Wine's
Direct3D layer polls the GL error slot while creating its
device, so a refusal there can fail device creation for
programs that start fine today. Kept dropped + once-logged,
with the case comment and the log line now naming the conflict;
tracked openly as FD-39 instead of decided silently here.

**(b) GLX and sync queries answered unknown questions with 0.**
`glXGetFBConfigAttrib` and `glXQueryContext` now return
GLX_BAD_ATTRIBUTE (2 — the project's own constant) for unknown
attributes and leave guest memory untouched. `glXQueryDrawable`
returns nothing in real GLX, so "refuse" there means not
writing: unknown attributes (window and pbuffer branches) now
leave the guest's output untouched instead of writing 0.
`glGetSynciv` with an unknown pname now records GL_INVALID_ENUM
(the bridge's existing error idiom) and writes neither the
value nor the result length. Known attributes behave exactly
as before.

**(c) Unknown legacy vertex-array types fabricated zeroed
data.** The type-size table returned 0 for unmodelled types, so
enabled client arrays were copied as zero bytes per component
and drawn as fabricated geometry. Two types gained real
models: GL_HALF_FLOAT (0x140B, 2 bytes, full IEEE half→float
decode) and GL_FIXED (0x140C, 4 bytes, 16.16 fixed→float).
Genuinely unmodelled types now refuse the draw itself in
glDrawArrays / glDrawElements / glDrawRangeElements (enabled,
configured, non-buffer-backed client arrays only — buffer-backed
arrays carry their data in the host/JS layer, which validates
their types): the guest keeps its real data, nothing fabricated
is rendered, and the refusal is once-logged with the type named.
This is safe because client-memory array data reaches a draw
only through those copies; a draw that proceeded would have
rendered data that was never the guest's.

**(d) Two packed pixel formats uploaded still packed.**
GL_UNSIGNED_SHORT_4_4_4_4 (0x8033) and
GL_UNSIGNED_SHORT_5_5_5_1 (0x8034) joined the unpack fixup
table with the correct strides and bit layouts (the non-REV
counterparts of cases already present). Adjacent find, not in
FD-15's named pair and deliberately left for FD-39:
GL_UNSIGNED_SHORT_5_6_5_REV (0x8364) is likewise unmodelled.

**(e) glMultiTexCoord2fARB dropped on two paths.** On the
WebGPU path the call was already fine (recorded; the JS
immediate-mode draw consumes it). The real gaps were on the
legacy-GL path: the dispatcher compiled the case to a no-op
under WASM, and the begin/end block replay never re-emitted
texcoords at all. The reason for the compile-out turned out to
be real: this build's Emscripten GL library has no
`glMultiTexCoord2f` symbol (the first full build of this lane
failed at link on exactly that). The case now records on every
path; under WASM it runs through a new `hostMultiTexCoord2f`
that expresses a per-unit current texcoord the way glemu can —
client-active texture unit + `glTexCoord2f` — and the block
replay tracks the current texcoords for the two modelled units
and re-emits them before every vertex, mirroring its existing
color/normal handling. Targets beyond the two modelled units
are once-logged, matching the bridge's two-unit texcoord model
everywhere else.

## Proof

- **Red/green.** `web/tests/suite/unit/gl-fd15-honest-answers
  .test.mjs` compiles slices of the real bridge source into a
  native harness (the established gl-query-bookkeeping pattern)
  and drives the real dispatcher cases; `gl-fd15-texparams
  .test.mjs` drives the real WebGPU backend with a recording
  device. With the fix reverted, 23 behavioral checks fail
  across every case (packed unpack 5, legacy types 7, draw
  refusal 1, texcoord replay 1, GLX answers 6, sync answers 3)
  and the texparams test fails; with the fix, all pass
  (honest-answers 3/3, texparams 1/1). Two source-shape checks
  pin the draw-refusal gates (exactly three) and the removal
  of the WASM no-op form.
- **Build.** Full `tools/bw64-build.sh` on the lane tree:
  "Browser export verified" + BUILD COMPLETE (after the link
  failure above forced the glemu-shaped fix for (e)).
- **Suites.** JS unit suite against the lane dist: 340 passed,
  0 failed (338 at the integ tip + the two new test files);
  GL-related subsets (query bookkeeping, WebGPU fixed-function /
  texture / clear, silent-failure scanner and its no-silent-
  failure companion, GL entirety, shim coverage) all green with
  no scanner-baseline changes.
- **No regression through the site path.** The project's own
  browser probes against the lane dist: `celeste-boot` PASS
  (283.6 s — the OpenGL program that works on the live site
  still starts and reaches its animated title screen; it
  exercises the changed GL bridge and backend end to end).
  `boot64-smoke` FAILED on the lane dist (two runs) — and then
  FAILED IDENTICALLY on a baseline dist built from the
  unmodified integ tip `c4bab5e07` in a scratch tree: same
  signature each time (guest stalls before `create_winstation`,
  threads waiting in `KUnixSocketObject::lockCond`, no GL code
  involved — the stall precedes any graphics work). Verdict:
  a pre-existing start stall in this environment, not an
  FD-15 regression; recorded on the register as FD-40 with
  the A/B evidence.

Not deployed, not pushed; the live site and master untouched.
