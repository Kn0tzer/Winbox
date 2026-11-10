# FD-270 - glReadPixels from the default framebuffer reads the host's leftover READ binding

## What was wrong, in plain terms

A guest program that draws a picture and then reads its own picture back -
without first binding its read target - got black pixels with no error,
even though the drawing had landed correctly. The bridge's readback handler
(`source/opengl/gl64bridge.cpp`, the `GL64_fn_glReadPixels` /
`GL64_fn_glGetTexImage` case) called host `glReadPixels` on whatever READ
binding the host happened to hold at that moment, instead of the guest's own
drawn framebuffer. Any guest that reads back without ever binding its read
target first hits this: correct GL usage, wrong pixels. Filed by the FD-267
lane (presentation runs fd267-present4/5/6 read black with err=0; run
fd267-present7 reads correctly once the target is bound first).

## The fix

Commit `79f3fd853` on branch `lane-fd270` (one file, +12 lines): in the
non-texture readback path, before the host `glReadPixels` calls, the handler
now saves the host READ binding and routes the read through
`bindGuestTarget(GL_READ_FRAMEBUFFER)` - the guest's own read target, or the
default framebuffer when the guest bound none - the way the `glReadBuffer`
handler already does, then restores the host binding afterwards. The texture
path (`glGetTexImage`, which binds its own scratch framebuffer) is untouched.
The depth/stencil framebuffer readback in the same block is covered by the
same bind, since it had the same defect.

## Proof - the no-bind readback now returns the drawn pixels

New verification probe: `tools/glcov/fd270-present.c` +
`web/tests/fd270-present-probe.mjs`. Same draws as `fd267-present`, but the
guest NEVER binds its read target before reading back. The probe can fail:
on the unfixed build it reports READBACK-UNAVAILABLE with exit 2.

Baseline run, TAG `fd270-baseline`
(`test-results/fd270-present-fd270-baseline/`), build from integ tip
`fd0120f4e` (wasm `05bc6962...`, the shipped batch-15 + FD-271 build),
exe sha256 `ed0096db2fabf06d719993766e80dea8ba1da5159f287921961537639fb101ee`:
verdict `passed: false, result: 2, readbackUnavailable: true`:

```
FD270: clearcheck A=(0,0,0) B=(0,0,0) FAIL
FD270: control err=0 A=(0,0,0) B=(0,0,0) FAIL
```

The blue clear reads back black and the control draw reads back black, both
with no GL error - the readback path itself is broken, not the draw.

Fixed run, TAG `fd270-fixed`
(`test-results/fd270-present-fd270-fixed/`), build from fix commit
`79f3fd853` (wasm `bcd9d500...`, BUILD_INFO dirty 0), same exe:
verdict `passed: true, result: 0, readbackUnavailable: false`:

```
FD270: clearcheck A=(0,0,255) B=(0,0,255) PASS
FD270: control err=0 A=(0,0,0) B=(255,0,0) PASS
FD270: DrawElementsBaseVertex/ebo err=0 A=(0,0,0) B=(255,0,0) PASS
FD270: DrawElementsBaseVertex/client err=0 A=(0,0,0) B=(255,0,0) PASS
FD270: DrawElementsInstancedBaseVertex/ebo err=0 A=(0,0,0) B=(255,0,0) PASS
FD270: DrawElementsInstancedBaseVertexBaseInstance/ebo SKIP
FD270: MultiDrawElementsBaseVertex/ebo err=0 A=(255,0,0) B=(255,0,0) PASS
FD270: MultiDrawElementsBaseVertex/client err=0 A=(0,0,0) B=(255,0,0) PASS
```

The clearcheck reads blue and every draw reads back its red triangle with
no prior bind. The BaseInstance SKIP is the same known skip as the FD-267
run (that entry point does not resolve at the shipped 3.3 profile), not a
failure.

## Regression - the bound-read path is unchanged

Run TAG `fd270-fd267regress`
(`test-results/fd267-present-fd270-fd267regress/`): the original
`fd267-present.exe` (which binds READ 0 before reading, exe sha256
`0d400fe4df7efff7e0feb5a8004b933008ecabb5542033514be7bdea61c05b03`) on the
fixed build passes exactly as it did on the unfixed build
(`passed: true, result: 0`, all 7 tests PASS, same BaseInstance SKIP).

## Light checks

Pipeline #86 on the fix commit `79f3fd853`: verify-source names the fix commit 79f3fd853 exactly (read-only mount enforced, tree pristine); citations exit 0 (352 rows, 0 drifts); js-legacy exit 0 (0 fail); js-unit 352 passed / 2 failed - the two known bare-checkout artifacts (provenance-artifacts, runtime-completeness); native-main 706 tests 0 FAILED (266 s); native-x64 121 tests 0 FAILED (25 s); run-native exit 0. The pipeline's overall status is failure, driven solely by the js-unit exit - the standing acceptance shape. Evidence: `winbox-project/checks/evidence/pipeline-86/`.

## Notes for the composition

- Closure number CL-202 is provisional; the composition settles it.
- No other lane is running on this defect; no collision.
- The 32-bit guest routes through the same bridge handler (there is no
  separate 32-bit readback handler), so the fix covers both widths.
  `source/opengl/glcommon.cpp` carries a separate native-test-only
  `glcommon_glReadPixels` that calls host `glReadPixels` directly; it is
  not a shipped path and was left alone.
