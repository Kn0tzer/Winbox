# FD-208 — Twenty-one GL handlers answer GL errors to the coverage harness's calls, and one answers inconsistently between calls

Lane: `lane-fd208` (working copy `wt-fd208`), off integration tip
`5aa04b1b8` (batch 13). Date: 2026-11-09. Fixing commit: `215d82519`.
Register: FD-208 closed as CL-196 (provisional number claimed on this
branch; CL-194 went to FD-265, CL-195 to FD-206 — the merge lane
settles any collision).

## End state

Every entry point the entry names now answers the coverage harness's
call correctly, measured on the fixed build (run
`test-results/bridge-coverage-fd208-final/`, 334 handlers, passed):
17 names answer GL_NO_ERROR on both calls; 5 names keep an error
answer that is the spec-correct answer to the harness's exact call
(register notes below and in the register entry).

## Per-name triage

Fixed in the bridge (`source/opengl/gl64bridge.cpp`):

- **glBufferData, glBufferSubData (were 1282)** — WebGL/ANGLE forbids
  writing a buffer that is bound to a transform-feedback binding
  point, even while transform feedback is inactive; desktop GL
  allows the call. The harness binds the buffer to
  TRANSFORM_FEEDBACK_BUFFER earlier in its sequence and the binding
  persists. The bridge now tracks generic + indexed TF bindings and
  lifts them around the host write, restoring them afterwards.
- **glGetSamplerParameterIiv, glGetSamplerParameterIuiv,
  glSamplerParameterIiv, glSamplerParameterIuiv (were 1282)** — the
  shared sampler case called WebGL methods that do not exist
  (`getSamplerParameterIiv` and friends); the resulting TypeError
  surfaced to the guest as GL_INVALID_OPERATION via the bridge
  exception reporter. All sampler pnames are scalar, so all four are
  rerouted through the plain integer entry points.
- **glPointParameterf, glPointParameterfv, glPointParameteri (were
  1282 by design — `rejectBridgeEntry`)** — now validated against
  desktop rules and stored bridge-side (FD-39 precedent):
  negative size/attenuation terms answer GL_INVALID_VALUE,
  GL_POINT_SPRITE_COORD_ORIGIN accepts only LOWER_LEFT/UPPER_LEFT,
  unknown pnames answer GL_INVALID_ENUM. WebGL has no
  point-parameter rendering state, so the store is write-validated
  only; glGet* are not wired to it (documented limitation).
- **glBeginQuery, glEndQuery (were 1280)** — the guest's
  GL_SAMPLES_PASSED (0x8914) does not exist in GLES3/WebGL2. The
  bridge translates it to GL_ANY_SAMPLES_PASSED at the host call
  only; bookkeeping stays keyed by the guest target.
- **glGetFloatv (was 1280)** — two defects: the case copied exactly
  one float regardless of pname (truncating e.g. GL_VIEWPORT's four),
  and its measured error at baseline was fallout of the wild
  compressed-texture call below. Now answers per-pname element
  counts, with integer-valued pnames served via glGetIntegerv.
- **glGetTexImage (was 1282, then 1286 once the poison below was
  removed)** — for a compressed (DXT) texture the bridge's readback
  attaches the texture to a scratch framebuffer; a compressed
  texture is not colour-renderable in WebGL2, so the framebuffer was
  incomplete. The bridge now records level metadata for
  glCompressedTexImage2D/3D, retains the uploaded DXT payloads
  (bounded), and decodes BC1/BC2/BC3 in software for glGetTexImage,
  matching desktop GL's decompress-on-read.
- **glEnd (was 1281)** — the host's fixed-function emulation
  (glemu) chooses its flush renderer by the *current program* and
  looks the fixed-function attribute names (`a_position`,
  `a_color`) up in it. Those names exist only in shaders written
  for the toolchain's converter; the harness's own program (bound
  in its setup) uses ordinary attribute names, so the lookups
  answered −1 and the flush called `vertexAttribPointer(-1)`,
  recording GL_INVALID_VALUE. Proven by an instrumented build:
  the End hop logged `saved=11 guest=11 currProgram=11` and the
  error appearing exactly at the glEnd step; the method-level
  trace showed `vertexAttribPointer(-1,4,5126,false,20,4)
  err=0x501` and `enableVertexAttribArray(-1) err=0x501`.
  The End replay now checks whether the current program has an
  `a_position`; if not, it flushes with no program bound (the pure
  fixed-function renderer) and restores the guest's program
  afterwards. Desktop GL feeds immediate-mode vertices to the
  current program's generic attributes by index and never errors
  here; the fixed-function flush is the closest available
  semantics and answers GL_NO_ERROR.

Fixed in the guest shim (`tools/rootfs64/libgl64/libgl64.c`, with
the 32-bit shim regenerated from it and the runtime manifest
regenerated):

- **glCompressedTexImage3D (was 37442 on the first call, 0 on the
  second)** — the shim declared the entry point *without* the
  desktop `border` parameter, so on every call `imageSize` landed
  in the border slot and the data "pointer" became the imageSize
  value; the bridge read a wild size and attempted a giant copy
  from guest address 64 (first call ~205 ms). With the signature
  corrected the call delivers its real arguments and answers
  consistently. The exact origin of the value 37442 (0x9242, not a
  GL error code) inside the corrupted first call was not traced
  further; it does not occur once the arguments arrive intact.
  This one wild call was also the poison behind the baseline
  answers of four other names: an A/B run with the fixed shim and
  probes interleaved into the harness sequence shows
  glGetFloatv, glGetTexLevelParameteriv, glReadPixels and the
  pre-fix glGetTexImage answering cleanly at the same sequence
  positions where the old shim's run (and the stock baseline) had
  them erroring. glGetTexLevelParameteriv's remaining behaviour
  needed no code change of its own.

Correct as measured — no fix (the entry's closing terms allow a
register note per name):

- **glAttachShader 1282** — the harness's setup already attached
  the same shader to the same program; re-attaching mandates
  GL_INVALID_OPERATION.
- **glBlitFramebuffer 1282** — the call blits the default
  framebuffer onto itself with identical, fully overlapping
  rectangles, which mandates GL_INVALID_OPERATION.
- **glGetIntegeri_v 1280** — the harness queries
  GL_MAX_VIEWPORT_DIMS, which is not an indexed parameter;
  GL_INVALID_ENUM is the mandated answer.
- **glPointParameteriv 1280 (after the fix)** — the harness passes
  its zero-initialised scratch value as
  GL_POINT_SPRITE_COORD_ORIGIN; 0 is neither LOWER_LEFT nor
  UPPER_LEFT, so GL_INVALID_ENUM is the mandated answer. (The
  float/int scalar forms pass valid values and answer 0.)
- **glPopMatrix 1284** — every matrix stack is at depth 1 when the
  harness pops; GL_STACK_UNDERFLOW is the mandated answer.

## Proof

Baseline run of the unmodified tree
(`test-results/bridge-coverage-fd208-base/`, build verified
"Browser export verified", wasm byte-identical to the shipped
batch-13 wasm) reproduced the register's table exactly, including
CompressedTexImage3D 37442/0.

Final run on the fixed build (`test-results/bridge-coverage-fd208-final/`):
build log `test-results/fd208/build-final2.log` ("Browser export
verified" ×1), BUILD_INFO git_sha `5aa04b1b8`, 6 dirty files (the
fix set), wasm sha256
`21c39fb738a5bac8d7a4dedf68eb2c4d3d997f8eca44d35e96596fec7a68d6d6`
(5,749,104 bytes). The run's summary reports passed=true, 334
handlers; the staged libGL.so.1 in its provenance is the fixed
shim (sha256 `c78efe47…`). Answers (err1/err2):
BufferData 0/0, BufferSubData 0/0, GetSamplerParameterIiv 0/0,
GetSamplerParameterIuiv 0/0, GetTexImage 0/0, PointParameterf 0/0,
PointParameterfv 0/0, PointParameteri 0/0, SamplerParameterIiv 0/0,
SamplerParameterIuiv 0/0, BeginQuery 0/0, EndQuery 0/0,
GetFloatv 0/0, ReadPixels 0/0, End 0/0,
GetTexLevelParameteriv 0/0, CompressedTexImage3D 0/0;
AttachShader 1282/1282, BlitFramebuffer 1282/1282,
GetIntegeri_v 1280/1280, PointParameteriv 1280/1280,
PopMatrix 1284/1284 (all spec-correct, above).
Analyzer buckets: baseline 3 working / 204 slow-or-flaky / 127
broken-or-stub → final 6 / 204 / 124 (the "working" bucket is
timing-gated in this rig; the per-name error table above is the
load-bearing comparison).

## Full-run delta outside this entry (for the merge gate)

15 non-FD-208 names read clean at baseline and read errors on the
fixed build (identical set on two different fixed builds —
deterministic, not run variance): glCompressedTexSubImage3D,
glFramebufferTextureLayer, glGenerateMipmap,
glGetActiveUniformBlockName, glGetActiveUniformBlockiv,
glGetBufferSubData, glGetRenderbufferParameteriv, glHint,
glRenderbufferStorage, glRenderbufferStorageMultisample,
glTexStorage1D, glTexStorage2D, glTexStorage3D, glTexSubImage2D,
glUniformBlockBinding. Mechanism: the harness keeps shared scratch
variables that getter calls write and later calls consume as
arguments; at baseline the broken sampler getters left those slots
at zero, and now they write the real values (e.g.
GL_TEXTURE_MIN_FILTER 9987), so later calls are made with
different arguments and several of the new errors are the correct
answers to the calls as actually made. This is the same
harness-state class the FD-206 lane documented; adjudication
belongs to the batch gate, and no product change in this lane
touches those handlers' own code paths.

## What was not done

- The same current-program exposure exists in principle for the
  bridge's vertex-array draw flush (glDrawArrays/glDrawElements);
  those harness rows measured clean on every run, so they are left
  untouched and noted here for the record.
- glGetCompressedTexImage remains a pre-existing no-op (not an
  FD-208 name); retained DXT payloads would serve it, but wiring
  it is another lane's scope.
- CompressedTexSubImage2D is likewise a pre-existing no-op case;
  retained payloads therefore always match the host image.
- The internal corruption mechanism by which the wild
  compressed-texture call disturbed four later calls was not
  traced beyond the A/B proof; with the ABI fixed the wild call
  cannot recur from this entry point.

## Post-commit adjustments (same fixing commit, amended)

The first queue run (pipeline 70) and a local unit-suite run caught
two test-compatibility defects in the fix as first committed; both
are fixed inside the same fixing commit `215d82519`. The
coverage proof was re-run on the amended code at every step; the
operative proof is the run on the final committed tip (clean
tree, 0 dirty files): build log
`test-results/fd208/build-final4.log` ("Browser export verified"),
wasm sha256
`d5a21e0a6b0a18ccb051df10da352268250a7c1fefa9ac0af1b2f260301095e6`;
run `test-results/bridge-coverage-fd208-final3/` (334 handlers,
passed) reproduces the fd208-final table above exactly — zero
diffs on all 22 names, and no handler that was clean in fd208-final
regressed:

- `gl-query-bookkeeping` extracts the bridge's query-bookkeeping
  span into a standalone compile; the transform-feedback block
  originally sat inside that span and used types and GL calls the
  extraction does not model. The block now sits below the span.
- `no-silent-failure` counts unlogged switch defaults: the four
  defaults the fix added were restructured (default values hoisted
  out of the switches), and the one genuinely degraded default
  (an unmodelled buffer target in `bufferBindingPname`) now logs
  once through the bridge's BW64-FAIL gate before answering.

Local unit-suite result on the final tree (bare-checkout state):
353 passed, 1 failed — the only failure is the known
bare-checkout artifact `provenance-artifacts` (log
`test-results/fd208/js-unit-local3.log`); `runtime-completeness`
passes locally only because the store-linked runtime archives are
still linked in, and fails in the queue's true bare checkout as
usual. A second extraction harness pair (FD-15/FD-39 honest-answer
suites) initially caught the same block from another span and a
signature-extraction clash with a forward declaration; the block
now uses plain `int64_t` fields and the note-once gate's real
definition sits above it — all five affected suites pass.

## Register changes on this branch

- Section A, FD-208: closing pointer appended (CLOSED 2026-11-09 —
  see CL-196).
- Section C: CL-196 added, naming fixing commit `215d82519` and
  this record.
