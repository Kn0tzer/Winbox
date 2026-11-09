# FD-209 — the 71 OpenGL entry points a Windows guest could not resolve

Lane `lane-fd209` off integration tip `5aa04b1b8`. Fix commits
`140f2e504` (profile default + extension) and `991db38cf`
(glDrawRangeElementsBaseVertex implementation). Closes FD-209
(register: see CL-194).

## What was wrong

A Windows program in the emulator looks OpenGL functions up the way any
Windows program does: `wglGetProcAddress` first, then the opengl32
export table. The coverage harness (`tools/glcov/glcov.c`, run by
`web/tests/glcov-probe.mjs`) does exactly that, and 71 real GL entry
points came back with no callable pointer — even though every one of
them already had a working implementation behind the bridge
(`source/opengl/gl64bridge.cpp` carries a handler case for each) and in
the guest shim (`tools/rootfs64/libgl64/libgl64.c` carries a wrapper
and a `glXGetProcAddressARB` table entry for 69 of the 71; the other
two are ABI-internal names, below).

The filing's likely mechanism is confirmed, with one correction about
where the default comes from: Wine's opengl32 serves
`wglGetProcAddress` lookups according to the GL profile the guest
driver advertises, and the guest shim advertises the profile the host
bridge tells it to (`GL64_fn_glVersionMode`). With no explicit option
the bridge answered 2.1, so the loader withheld everything above the
2.1 profile. The coverage probe even tried to opt in — it spawns the
harness with `BW64_GLVERSION=33` in the guest environment — but that
variable never reaches the bridge: the host side reads its own
environment, which the launcher sets only from the page's `glversion`
parameter, and the probe's page URL does not carry one. Every coverage
run on record therefore measured the 2.1 profile (the preserved
consoles note `glGetString=2.1 Boxedwine64`).

## Measurement (all runs on this lane; harness and shim unchanged)

Baseline build of the integration tip (wasm `62f6123e…`, byte-identical
to the batch-13 live build), standard probe, no version parameter:
334 rows, **103 with no callable pointer — the identical set to the
2026-11-07 filing run**, and every other row's answers identical to
that run except the two multisample rows FD-207 has since fixed.
Guest version string: `2.1 Boxedwine64`.

Same build, page parameter `glversion=3.3` (the bridge's mode logic
maps the string "3.3" to mode 32 — a pre-existing quirk; the guest
string reads `3.2 Boxedwine64`): **46 of the 71 resolve** and answer;
no previously-resolving row changes its answer.

Same build, page parameter `glversion=33` (mode 33, guest string
`3.3 Boxedwine64`): **3 more resolve** (`glGetFragDataIndex`,
`glQueryCounter`, `glGetQueryObjectui64v`, all clean answers) — 49 of
71. Still no answer changes anywhere else.

Same build and profile, with the guest shim temporarily advertising
twelve further extensions: **12 more names resolve** — but eleven of
them answer the bridge's recorded `GL_INVALID_OPERATION` rejection or
share the plain buffer-data row's harness-state error pattern (see
per-family notes), so resolution alone is not a reason to advertise an
extension the backend will not honour. Only `GL_ARB_buffer_storage`
was kept (below). Ten names did not resolve under any tested
profile/extension combination.

## The fix

1. `source/opengl/gl64bridge.cpp` — `GL64_fn_glVersionMode`: with no
   explicit `BW64_GLVERSION`, the bridge now answers 33 instead of 21.
   Explicit values behave exactly as before ("3*" → 3.2/3.3; any other
   value → the 2.1 profile remains available as an opt-down). The 32-bit
   Wine shim uses the same trap, so both widths move together.
2. `tools/rootfs64/libgl64/libgl64.c` (+ regenerated
   `tools/rootfs64/libgl32/libgl32.c` and both committed `libGL.so.1`
   binaries; both build scripts' `--check` passes) — the advertised
   extension set (monolithic string and the `glGetStringi` list, kept
   in sync as the file requires) gains `GL_ARB_buffer_storage`. The
   bridge backs it for real (immutable storage emulated with
   `glBufferData`; contents identical), and the loader serves
   `glBufferStorage` only when the extension is advertised.
3. `source/opengl/gl64bridge.cpp` — `glDrawRangeElementsBaseVertex`
   implemented for real (see the next section; without it the profile
   change alone breaks FNA-class guests).

## The FNA regression the profile change exposed, and its fix

The first fixed build passed the whole Direct3D suite group (6/6) and
the gl32 group (3/3) at the new default — and failed `fna-runtime`:
the FNA test program loaded, read its texture back, printed READY, and
presented zero red pixels. Celeste (also an FNA game) failed the same
way in the boot group ("must present a visible game frame … got 0").
A/B on the same build proved the cause was the profile, not load or
timing: forced `glversion=2.1` passes, the 3.3 default fails, and 3.2
fails identically. A guest-side GL trace (`BW64_GLTRACE=2`) at 3.3
shows why: FNA's only draw call is `glDrawRangeElementsBaseVertex`
(20 calls, one per presented frame), an entry point that newly
resolves at 3.x — and whose bridge handler was a designed rejection
(`rejectBridgeEntry`: record `GL_INVALID_OPERATION`, draw nothing).
At 2.1 FNA cannot resolve it and falls back to `glDrawRangeElements`,
which works.

The rejection cannot simply be replaced by the platform call: this
environment's WebGL2 offers no base-vertex drawing at all — measured
directly on the probe browser, the context has no
`drawElementsBaseVertex` method and offers neither the OES nor the EXT
base-vertex extension. (The neighbouring handlers that pass through to
those methods throw on every call for the same reason; that separate
defect is filed as FD-267.) The handler therefore emulates the call
the way the spec allows: read the index data (from the bound element
buffer, or guest memory when none is bound), add `baseVertex` to every
index, and draw through plain `glDrawElements` from a scratch element
buffer, restoring the guest's binding afterwards. The start/end range
arguments are advisory hints and do not change which elements are
drawn; rebased indices preserve `gl_VertexID` semantics (id = index +
baseVertex). A NULL client index pointer with no buffer bound — the
state the coverage harness calls every draw entry point in — is
answered with the same no-error draw over zeroed index data the
bridge's plain draw handlers produce there, instead of an error.

With the implementation in place, on the final build: `fna-runtime`
**passes** at the default profile, and `celeste-boot` **passes**.

## Outcome per family (71 names)

Resolve and answer at the shipped default profile (50):
the vertex-array-object family (3), the sync family (6),
`glDrawArraysInstanced`, `glDrawElementsInstanced`,
`glDrawElementsInstancedBaseVertex`, `glDrawElementsBaseVertex`,
`glDrawRangeElementsBaseVertex`, `glMultiDrawElementsBaseVertex`,
`glCopyBufferSubData`, `glMapBufferRange`, the `glClearBuffer*`
family (4), `glGetStringi`, `glGetBooleani_v`, the indexed-state
family (`glEnablei`, `glDisablei`, `glIsEnabledi`, `glColorMaski`) (4),
the transform-feedback family (3), `glBindFragDataLocation`,
`glGetFragDataIndex`, `glFramebufferTexture`, `glGetMultisamplefv`,
`glTexImage2DMultisample`, `glTexImage3DMultisample`, `glTexBuffer`,
the timer-query pair (`glQueryCounter`, `glGetQueryObjectui64v`),
the unsigned-uniform family (5), the integer vertex-attrib family
(5), and `glBufferStorage`. Of these, **37 answer the harness's calls
cleanly (0/0)** — including `glDrawRangeElementsBaseVertex`, whose
draws are real (previous section) — and **13 record GL error answers
from their handlers**: `glBeginTransformFeedback`,
`glClearBufferiv`, `glClearBufferuiv`, `glEndTransformFeedback`,
`glGetMultisamplefv` and `glBufferStorage` (1282),
`glBindVertexArray` (1281/0), the multisample texture pair (1280),
`glTexBuffer` (1282, the family's designed rejection, recorded rather
than faked), and the three base-vertex passthrough rows
(`glDrawElementsBaseVertex`, `glDrawElementsInstancedBaseVertex`,
`glMultiDrawElementsBaseVertex`, all 1282) whose handlers call WebGL
methods this platform does not have — filed as **FD-267**. The
remaining error answers are handler-answer quality, which is FD-208's
queue; FD-209's closing terms are resolution and reaching the handler,
and both are met for all 50.

Correctly absent, with the reason recorded (21):
- Indexed blending (`glBlendFunci`, `glBlendFuncSeparatei`,
  `glBlendEquationi`, `glBlendEquationSeparatei`) and
  `glMinSampleShading`: the bridge implements all five for real (via
  the WebGL2 context surface), and the shim already advertises the
  promoting extension for the blend family — but this Wine build's
  loader table serves these names only at a 4.0 profile, and the
  backend (WebGL2) does not claim one. Absent at the advertised 3.3
  profile, as they would be from a real 3.3 driver whose loader
  behaves the same way.
- `glTexBufferRange`, `glTexBufferRangeARB`, `glTexBufferARB`:
  buffer textures are not a WebGL2 capability (they arrive with
  GLES 3.1); the bridge rejects the whole family by design, and the
  unsuffixed `glTexBuffer` — which the loader does serve at 3.3 —
  records exactly that rejection. The ARB spellings are not
  advertised, so they stay unresolved rather than resolve to a
  feature the backend does not have.
- The debug-message family (`glDebugMessageCallback`,
  `glDebugMessageControl`, `glDebugMessageInsert`,
  `glGetDebugMessageLog`): the backend has no debug-message log;
  the bridge rejects direct calls by design (recorded, never faked).
  The extensions are not advertised, so the names are correctly
  absent.
- `glTextureBarrier`, `glTextureBarrierNV`: a GLES 3.1 feature the
  WebGL2 backend does not have; the bridge rejects by design; not
  advertised.
- `glFinalCombinerInputNV`: the NV register-combiner has no
  equivalent in the backend; the bridge rejects by design; the
  loader serves it under no tested profile or extension.
- The base-instance draws (`glDrawArraysInstancedBaseInstance`,
  `glDrawElementsInstancedBaseVertexBaseInstance`): base-instance
  drawing is not a GLES 3.0 capability; the first is a designed
  bridge rejection. `GL_ARB_base_instance` is not advertised.
- The direct-state-access getters (`glGetTextureParameteriv`,
  `glGetTextureLevelParameteriv`): the bridge deliberately does not
  answer named-object queries (it cannot name the texture's values
  without lying — see the case comment), so the DSA extension is
  not advertised and the names stay absent instead of resolving to
  an answer that writes nothing.
- `glGetShaderSourceImpl`, `glVersionMode`: not application entry
  points. `glVersionMode` is the shim's own query to the bridge (it
  is how the advertised profile is chosen); the public
  `glGetShaderSource` resolves and works. Correctly absent from the
  application lookup route, as the filing itself noted.

The 32 `glX*` rows and the `traceProc` plumbing row in the harness
are not Windows-path entry points (counted for completeness in the
filing) and are unchanged.

## Proof

- glcov on the final build (wasm `ed01a8441cfc…`, git `991db38cf`)
  at the default profile (no version parameter):
  `test-results/bridge-coverage-fd209-final2/` — 334 rows, **53 with
  no callable pointer** (103 at baseline): exactly the 32 glX/trace
  rows plus the 21 recorded-absent names above. The only row whose
  answer changed between the profile-flip build and the final build
  is `glDrawRangeElementsBaseVertex` (1282 → 0/0, the implementation);
  every row that resolved on the baseline build keeps its exact
  baseline answers (machine-compared), and no row is newly missing.
  Guest version string at the default is now `3.3 Boxedwine64`.
- Guest suites: on the profile-flip build (`140f2e504`), the full
  Direct3D group passed 6/6 (d3d9/d3d10/d3d11/d3d12 comprehensive,
  d3d11-triangle, d3d9-32) and gl32 passed 3/3, while fna-runtime and
  celeste-boot failed as the previous section records; boot smokes
  (boot32-vkboot, boot64-smoke) passed on the following build. On the
  final build (`991db38cf`): **fna-runtime PASS, celeste-boot PASS,
  gl32 3/3 PASS, d3d9 2/2 PASS** (logs in `test-results/fd209/`).
- Checking queue: pipeline #71 on `991db38cf` — citations exit 0,
  js-legacy 0 failures, native-main 706 tests / 0 failed,
  native-x64 121 / 0, run-native pass; js-unit 352 passed / 2
  failed, the failures being exactly the two known bare-checkout
  artifacts (`provenance-artifacts`, `runtime-completeness`).
  That is the standing acceptance shape: the overall pipeline
  status is “failure” driven solely by js-unit.
- Shim builds: `build-libgl64.sh --check` and the libgl32 generator
  check both pass on the committed tree (and inside every build).

## Notes for the composition

- The wasm changes (bridge default + the draw implementation) and
  the committed `libGL.so.1` binaries both have to ship; the shim
  binaries are the copies the rootfs staging takes (the build
  scripts' `--check` is part of staging, so a stale binary cannot
  ship silently).
- Support archive: restaging this tree's support pack changes its
  hash from the batch-11 pin `c4dbe35986a5…` to
  `94deeb6b24787be0d6b0d1317e12bc4499ac7680820243ced542593d6bb64e0d`
  (the runtime manifest was regenerated accordingly, in the fix
  commit). The next composition re-pins the support archive.
- Sibling interaction: FD-208 works the GL error answers in the same
  bridge file; this lane changed only the `glVersionMode` case and
  the `glDrawRangeElementsBaseVertex` case there. The 13
  error-answering rows above are FD-208's triage material; the three
  base-vertex passthrough rows among them are additionally filed as
  FD-267 (a platform-capability defect, not an error-code choice).
- The `glversion=3.3` → mode-32 quirk (the bridge tests the second
  character for '3') is pre-existing and left as found; the
  games-catalog probe's "3.2" and this record's "33" are the
  spellings that mean what they say.
