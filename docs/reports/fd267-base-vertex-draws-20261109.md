# FD-267 — the base-vertex draw handlers threw on every call

Lane `lane-fd267` off integration tip `2ccf74244` (batch 14). Fix commits
`c84d0d931` (product fix + harness ABI fix + presentation test),
`1ddba5c09` (probe spawn line + test proc-loader fallback),
`f7ece04dc` (BaseInstance load tolerated), `11d8709dc` (read-target
bind + clear bisection). Closes FD-267 (register: see CL-198).

## What was wrong

A Windows program that draws indexed geometry with a base vertex —
`glDrawElementsBaseVertex`, `glDrawElementsInstancedBaseVertex`,
`glDrawElementsInstancedBaseVertexBaseInstance`, and
`glMultiDrawElementsBaseVertex` — lost every draw. The handlers in
`source/opengl/gl64bridge.cpp` issued their draws through
`GLctx.drawElementsBaseVertex` /
`GLctx.drawElementsInstancedBaseVertex` /
`GLctx.drawElementsInstancedBaseVertexBaseInstance` EM_ASM calls, but
the browser's WebGL 2.0 context has no such methods and offers neither
`OES_draw_elements_base_vertex` nor `EXT_draw_elements_base_vertex`,
so every call threw `TypeError: ... is not a function`, the handler's
own catch reported it, and the guest saw error 1282
(`GL_INVALID_OPERATION`) with nothing drawn. The coverage harness
recorded 1282/1282 for the resolvable rows on the FD-209 fixed build.

One correction to the filing: its claim of "1282/1282 for all three
rows" was wrong for `glDrawElementsInstancedBaseVertexBaseInstance`.
That name answers -1/-1 (no callable pointer) in both the FD-209
final2 baseline and this lane's fixed run, because Wine's loader
withholds it: it is a GL 4.2 entry point, `GL_ARB_base_instance` is
not advertised, and base-instance drawing is not a GLES 3.0
capability. The FD-209 record already closed the base-instance pair
under the alternative term on exactly those grounds. The handler is
implemented anyway (same helper as the proven forms), so it draws
correctly if the name is ever resolvable.

## The fix

`c84d0d931` adds a shared helper `drawElementsRebased()` in
`source/opengl/gl64bridge.cpp` (just before `glBridge`) and rewrites
all four handlers onto it — the same index-rebasing approach FD-209
used for `glDrawRangeElementsBaseVertex`. The helper reads the index
data from the bound element buffer, or from guest memory when no
buffer is bound, or from zeroed data for the NULL-pointer/no-buffer
harness state; adds `baseVertex` to every index; and draws the rebased
indices from a scratch element buffer through plain `glDrawElements`
/ `glDrawElementsInstanced`, restoring the guest's element-buffer
binding afterwards. Bad type answers `GL_INVALID_ENUM`, negative
count/instanceCount answers `GL_INVALID_VALUE`, unavailable index data
refuses with 1282. Known limitation, documented in the code:
`gl_InstanceID` cannot be offset on WebGL2, so the BaseInstance form
draws the right geometry but a shader reading `gl_InstanceID` sees
un-offset ids; `baseInstance` is accepted and otherwise ignored.

`glMultiDrawElementsBaseVertex` also fixes two latent bugs it carried:
its draw count was read from `counts[0]` instead of `primcount`, and
it issued an instanced draw per batch instead of one plain draw
(`primcount` is the number of draws, not an instance count).

The same commit fixes a harness bug (`tools/glcov/glcov.c`): the
`glMultiDrawElementsBaseVertex` typedef passed `mode` as
`const GLenum *`, but the real entry point (and the guest shim wrapper
in `tools/rootfs64/libgl64/libgl64.c:1406`) takes a `GLenum` value —
the harness was calling it with the wrong ABI, so the bridge read a
guest address as the mode. Typedef and both call sites now pass
`GL_TRIANGLES`.

## Measurement

Coverage run on the fixed build, TAG `fd267-fix`
(`test-results/bridge-coverage-fd267-fix/`, 334 handlers,
`glcov: RESULT 0`, summary `passed: true`): against the correct
baseline, the batch-14 gate run
(`wt-merge-batch14/test-results/bridge-coverage-merge-batch14/`),

- `glDrawElementsBaseVertex`: 1282/1282 -> 0/0
- `glDrawElementsInstancedBaseVertex`: 1282/1282 -> 0/0
- `glDrawElementsInstancedBaseVertexBaseInstance`: -1/-1 in both runs
  (correctly absent, above)
- `glMultiDrawElementsBaseVertex`: 1282/1282 in both runs (see below)

Error-code comparison over all 334 rows: exactly those two rows
changed, both fixes, zero regressions (timing fields vary run to run
and are not compared). Browser build verified: `tools/bw64-build.sh`
-> "Browser export verified" exactly once; BUILD_INFO git_sha
`c84d0d931`, wasm sha256
`9ed6ade3feced7c986ac9096595621878d9af460652b97c905d9312f128675a6`
(5,755,718 B), dependency archive `caf6b5aed680` verified; log
`test-results/fd267/build.log`. The later commits touch only the test
program and probe, so the product code in the verified build is the
fix commit.

The MultiDraw coverage row stays 1282/1282, but the row is a
harness-state artifact, not a handler refusal: no `rejectBridgeEntry`
line fires for it in the fixed run (the console's single "unsupported"
line is for `glTexBuffer`, a known designed rejection), so the 1282
comes from elsewhere in the call path — the FD-208-documented class
of rows whose answers flip with the harness's shared GL state. The
presentation test below discriminates it: MultiDraw draws correctly.

## Proof — a guest drawing through them presents

New test program `tools/glcov/fd267-present.c` + probe
`web/tests/fd267-present-probe.mjs` (modeled on the glcov probe).
Two triangles in NDC (A left, vertices 0-2; B right, vertices 3-5),
solid-red shader; each entry point draws indices {0,1,2} with
baseVertex 3 (EBO and client-memory paths; MultiDraw batches base 0
and 3) and the test reads the pixels at both triangle centroids
(120,150) and (360,150) in a 480x360 window. A control draw with plain
`glDrawElements` runs first; a blue-clear bisection separates a broken
readback from a draw that did not paint.

Run TAG `fd267-present7`
(`test-results/fd267-present-fd267-present7/`), exe sha256
`0d400fe4df7efff7e0feb5a8004b933008ecabb5542033514be7bdea61c05b03`,
verdict `passed: true, result: 0`:

```
FD267: clearcheck A=(0,0,255) B=(0,0,255) PASS
FD267: control err=0 A=(0,0,0) B=(255,0,0) PASS
FD267: DrawElementsBaseVertex/ebo err=0 A=(0,0,0) B=(255,0,0) PASS
FD267: DrawElementsBaseVertex/client err=0 A=(0,0,0) B=(255,0,0) PASS
FD267: DrawElementsInstancedBaseVertex/ebo err=0 A=(0,0,0) B=(255,0,0) PASS
FD267: DrawElementsInstancedBaseVertexBaseInstance/ebo SKIP
FD267: MultiDrawElementsBaseVertex/ebo err=0 A=(255,0,0) B=(255,0,0) PASS
FD267: MultiDrawElementsBaseVertex/client err=0 A=(0,0,0) B=(255,0,0) PASS
```

Every resolvable entry point presents the right triangle in red with
no GL error, through both the element-buffer and the client-memory
index paths; MultiDraw presents both triangles at once. The
BaseInstance line is a skip, not a failure: the entry point does not
resolve at the shipped 3.3 profile (see above).

## What the earlier probe runs taught (kept, so the next lane does not re-learn them)

- `fd267-present` (first run): the probe spawn line carried a literal
  backslash-n instead of the newline separator the runtime splits on,
  so the session logged `Could not find ... wine64\nZ:\home\...` and
  the exe never spawned. Fixed by copying the glcov probe's spawn line
  byte-identically (commit `1ddba5c09`).
- `fd267-present2`: the guest exited 1 with `no proc glClearColor` —
  `wglGetProcAddress` returns nothing for OpenGL 1.0/1.1 core entry
  points; they come from the opengl32 export table. The test loader
  now falls back to `GetProcAddress`, the same shape as the coverage
  harness loader (commit `1ddba5c09`).
- `fd267-present3`: the guest exited 1 with
  `no proc glDrawElementsInstancedBaseVertexBaseInstance` — the
  confirmation that the name does not resolve at 3.3. Its load is now
  optional and the test notes the skip (commit `f7ece04dc`).
- `fd267-present4/5/6`: the test ran and reported
  `READBACK-UNAVAILABLE` — control err=0 but both pixels black. The
  draws reached the host (`gl64 DRAW: first glDrawElements ...`
  in the console) and the readback target was the drawn framebuffer,
  so the reads were hitting the host's leftover READ binding: the
  bridge's readback handler reads whatever READ binding is current,
  and the guest had never bound one. Binding `GL_READ_FRAMEBUFFER, 0`
  before reading (correct GL usage) makes every read correct
  (commit `11d8709dc`). A guest that reads back without ever binding
  its read target still gets the leftover binding's pixels — filed
  as FD-270 (below), not fixed here.

## New filing from this lane

- **FD-270** — `glReadPixels` from the default framebuffer without a
  prior `glBindFramebuffer(GL_READ_FRAMEBUFFER, 0)` reads the host's
  leftover READ binding instead of the guest's drawn framebuffer
  (present4/5/6 read black with err=0; present7 reads correctly once
  the target is bound). The `glReadBuffer` handler already shows the
  pattern: it calls `bindGuestTarget(GL_READ_FRAMEBUFFER)` first. Open.

## Notes for the composition

- Closure number: CL-198. Sibling lanes re-checked immediately before
  closing: `lane-fd268` has no commits beyond the base; `lane-fd269`
  has one commit (`b4c51b2d9`, test-only) that touches no register
  entry and claims no closure number. No collision.
- FD-270 is filed open by this lane; it is not part of this closure.
- The light checks for the lane tip are pipeline #74 (verify-source OK
  for commit `11d8709dc442ccce242a49ba213ebecea000aad6`; citations exit 0;
  js-legacy exit 0; native-main 706 tests 0 FAILED; native-x64 121 tests
  0 FAILED; run-native exit 0; js-unit 352 passed / 2 failed, the two
  failures being the known bare-checkout artifacts
  `provenance-artifacts` and `runtime-completeness` — the standing
  acceptance shape; evidence
  `winbox-project/checks/evidence/pipeline-74/`).
