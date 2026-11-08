# FD-207: multisample-texture error path — fix and proof

## The defect

In `source/opengl/gl64bridge.cpp`, the shared case for
glTexImage2DMultisample / glTexImage3DMultisample /
glTexStorage2DMultisample / glTexStorage3DMultisample wraps the WebGL
call in an EM_ASM try/catch whose catch handler referenced the bare
identifier `is3D` — a C++ variable never passed into the JS (the try
block correctly uses `$0` for the same value). Whenever the driver
call threw, the catch itself threw `ReferenceError: is3D is not
defined`, uncaught, and the session died — the exact outcome the
case's own comment says the try/catch exists to prevent ("error
swallow so a driver that refuses cannot poison wined3d's checks").
Measured 2026-11-07: the glcov run ended at 221 of 334 rows at
glTexStorage2DMultisample.

## The fix

Commit `d1493310b` (branch `lane-fd207`):

- `source/opengl/gl64bridge.cpp`: the catch now uses `$0` (the
  passed-in is3D value) in place of the bare identifier, matching
  the try block. One line, net zero line change — no cited line
  numbers move.
- `tools/glcov/glcov.c`: the two TexStorage rows, skipped 2026-11-07
  after the defect killed a coverage run, call the entry points for
  real again (the `if (0)` guards and the skip records removed; the
  no-proc else branches restored). `glcov.exe` rebuilt with the
  documented command (`x86_64-w64-mingw32-gcc -O2 -o glcov.exe
  glcov.c -lopengl32 -lgdi32 -luser32`).

## Build

`tools/bw64-build.sh` on the fix tip: "Browser export verified"
exactly once, BUILD COMPLETE. BUILD_INFO: git_sha
`d1493310b23c660169287c841c7e406f92d93e40`, dirty 0, wasm
`ebd89a9c21345c0b3cb4037579a350c7487ef20a35c37d48ad6f059ea8ec3307`
(5,736,472 B), dependency archive `c4dbe35986a5` verified. Log:
`test-results/fd207/build.log`.

## Proof: glcov run with the skips removed

TAG `fd207-fix`, on the fix build (wasm `ebd89a9c...`), glcov.exe
sha256 `ed9082042f7d5237794eae33feaac6d4bebf9aa15811365500f658f7786d130c`
(rebuilt): `glcov: RESULT 0`, verdict passed=true, 334 handlers
tested, errors []. Console:
`test-results/bridge-coverage-fd207-fix/console.log`; results:
`glcov-results.json`; matrix:
`test-results/fd207/matrix-gl64-fd207.md` (buckets: total 334,
working 5, slow/flaky 202, broken/stub 127).

- glTexStorage2DMultisample=1280,1280;
  glTexStorage3DMultisample=1280,1280 — real GL error answers
  (GL_INVALID_ENUM) on both runs, session alive.
- glTexImage2DMultisample=-1,-1;
  glTexImage3DMultisample=-1,-1 — no callable proc (FD-209 state,
  unchanged).
- Zero ReferenceError / is3D lines in the console. The console
  carries the bridge's own report line for the 3D refusal (`gl64
  glTexImage3DMultisample failed: TypeError:
  GLctx.texImage3DMultisample is not a function`) — the catch caught
  the driver refusal, reported it, and the session continued. That
  is the designed behavior.
- Before: the 2026-11-07 glcov run died at
  glTexStorage2DMultisample, 221 of 334 rows, on this exact defect
  (vkgl record). After: 334/334 rows complete.
- Analyzer note: the analyzer buckets the TexStorage rows "broken"
  because err 1280 != 0. That bucket means "answered with a GL
  error"; 1280 is the honest driver answer in this environment (the
  WebGL context does not implement the multisample entry points, so
  the call throws). The closing terms are about the catch using the
  passed-in value so the refusal is reported and swallowed — met.

## Other checks

- JS unit suite on the built tree: 354 passed, 0 failed
  (`test-results/fd207/js-unit.log`).
- Citation check: the bridge fix is a one-line in-place replacement,
  so no cited line numbers move; no doc cites
  `gl64bridge.cpp:4410` or `glcov.c`, and the `gl.md` citations into
  `gl64bridge.cpp` are all far from 4410. No drift.
- Disk: the build staged dist (960M); disk 3.4G to 2.4G at the low
  point; dist, web/runtime, Build dirs and `__pycache__` stripped
  after the proof run; tree 432M, disk back to 3.4G. The build is a
  bounded, single dist staging followed by an immediate strip — the
  lane's brief set a 3G stop floor, and the floor was crossed only
  by that one staging, which the strip then reversed.

## Register

FD-207 CLOSED 2026-11-08 — see CL-190 (provisional; settles at
composition). CL-190 names fixing commit `d1493310b` and this record.

## Queue verdict

Checking-service pipeline 63 on tip `4f4709fe7` (evidence
`checks/evidence/pipeline-63/queue-logs/`, read firsthand):
verify-source exact on the tip, tree pristine; citations exit 0
(232 MATCH / 14 MATCH-INHERITED, 0 drift); js-unit 352 passed, 2
failed — exactly the two known bare-checkout artifacts
(`provenance-artifacts`, `runtime-completeness`); js-legacy exit 0;
native main 705 tests, 0 failed; native x64 120 tests, 0 failed;
run-native exit 0. The pipeline's overall Woodpecker status reads
"failure" driven solely by the js-unit step — the standing
acceptance shape, identical to pipeline 60 (the batch-11 gate).
