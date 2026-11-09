# FD-268: buffer and indexed-state entry points at the 3.3 profile — record

Filed by the batch-14 merge gate, 2026-11-09; closed as CL-198 by this lane.
Branch `lane-fd268`, base integration tip `2ccf74244` (batch 14 live).

## What the gate saw

At the 3.3 default profile the composed build answered GL errors to ten
coverage-harness rows that the pool lanes' own builds had answered cleanly:
`glBufferData`, `glBufferSubData`, `glGetBufferParameteriv`,
`glCopyBufferSubData`, `glMapBufferRange`, `glDrawElementsInstanced`
(1282/1282 = `GL_INVALID_OPERATION`) and `glEnablei`, `glDisablei`,
`glIsEnabledi`, `glGetBooleani_v` (1280/1280 = `GL_INVALID_ENUM`). The
harness passes literal arguments for every one of these calls, so the gate
filed the cluster as sequence state and did not isolate the per-row chain.

## What was actually wrong — three separate mechanisms

### 1. The VAO bind wipes the buffer bindings (buffer family + draw)

A minimal guest program (temporary `test-results/fd268/vao-test.exe`, not
part of the harness) binds a buffer to `GL_ARRAY_BUFFER`, binds an element
buffer, then binds and unbinds a VAO, printing the bindings after each
step. On the composed build:

```
after-bind-buffers      ab=4 eab=5
after-vao-bind          ab=0 eab=0
after-vao-unbind        ab=0 eab=0
vao-bound-with-attrib   ab=4 eab=0
after-setup-order-unbind ab=0 eab=0
```

Binding the VAO zeroes both buffer bindings, and unbinding it does not
bring them back. The same experiment run as raw WebGL2 calls on the host
(Chromium + SwiftShader, `test-results/fd268/host-vao-test.mjs`) keeps
both bindings across the VAO bind and unbind — the host is innocent.

The mechanism is in Emscripten's `LEGACY_GL_EMULATION` VAO emulation
(`src/lib/libglemu.js`, `$emulGlBindVertexArray`): every
`glBindVertexArray` — bind or unbind — first binds both buffer targets to
0 and then replays only what was recorded while that VAO was current
(`GLEmulation.vaos[vao].arrayBuffer` / `.elementArrayBuffer`). Bindings
made while no VAO was current (the default VAO's state, which is exactly
what the harness's setup does: bind buffers, then bind the VAO, set
attrib pointers, unbind the VAO) are never recorded anywhere, so they are
lost on the next VAO bind/unbind. That is why the same rows answer 0/0 at
the 2.1 profile: there the VAO entry point does not resolve, the wipe
never runs, and the bindings survive.

Fix (`source/opengl/gl64bridge.cpp`, `glBindVertexArray` handler): the
bridge already tracks the live bindings (`g_legacyArrayBuffer`,
`g_legacyElementArrayBuffer`); this lane adds the current-VAO id
(`g_currentVao`) and the default VAO's element-array binding
(`g_defaultVaoElementArrayBuffer`, updated on element-array binds made
while no VAO is current). Around the Emscripten call the handler restores
the array buffer always (desktop GL keeps `ARRAY_BUFFER` across VAO
changes — it is not VAO state) and restores the element-array binding
only on unbind to VAO 0 (binding a nonzero VAO must keep the replayed
VAO-recorded element binding, which is the spec-correct one).

### 2. The indexed-state shims had their arguments backwards

A minimal guest program (temporary `test-results/fd268/i-test.exe`) calls
the indexed-state entry points in isolation on the composed build:

```
enablei(BLEND,0)       err=1280   enablei-SWAPPED(0,BLEND)       err=0
disablei(BLEND,0)      err=1280   disablei-SWAPPED(0,BLEND)      err=0
isEnabledi(BLEND,0)    err=1280   isEnabledi-SWAPPED(0,BLEND)    err=0
```

Calling with the arguments swapped answers cleanly, so the trap path
interprets the first argument as the index and the second as the cap.
The guest shim (`tools/rootfs64/libgl64/libgl64.c`, from the A2 commit
`d9f8e8ad0`) declared `glEnablei(GLuint i, GLenum cap)` — trap (ABI)
order — instead of the desktop order `(GLenum target, GLuint index)`,
while the bridge handler reads `cap = a.a[1]`, `idx = a.a[0]` per the ABI
comment `(index, cap)`. A guest calling `glEnablei(GL_BLEND, 0)` therefore
arrived at the handler as `cap = 0`, and the host call `GLctx.enable(0)`
raised `GL_INVALID_ENUM`. Only these three functions were inverted; the
audit found every other indexed entry point already in desktop order
(`glBlendEquationi`, `glBlendFunci`, `glGetIntegeri_v`,
`glGetBooleani_v`, `glColorMaski`, `glVertexAttribI4i`, ...).

Fix (`tools/rootfs64/libgl64/libgl64.c`): the three signatures now match
the desktop call and pack the trap in ABI order
(`a.a[0] = index`, `a.a[1] = target`); the bridge handler is unchanged.
The 32-bit shim was regenerated from it (`build-libgl32.sh`) and both
`libGL.so.1` binaries rebuilt (`build-libgl64.sh`); the runtime manifest
regenerated for the new support-pack hash. The host calls themselves
were verified fine on the host: WebGL2 has no `enablei` method, and
`GLctx.enable(cap, idx)` / `isEnabled(cap, idx)` ignore the extra index
argument without error (`test-results/fd268/host-indexed-test.mjs`).

### 3. The host rejects `getIndexedParameter` for every pname

`glGetBooleani_v` answered 1280 in both argument orders in the isolation
test, so it is not the swap. The bridge handler queries
`GLctx.getIndexedParameter(pname, index)`; measured on the host, that
call returns null and sets `GL_INVALID_ENUM` for every pname tried
(`BLEND`, `COLOR_WRITEMASK`, `SAMPLE_MASK_VALUE`, `MAX_DRAW_BUFFERS`).

Fix (`source/opengl/gl64bridge.cpp`, `glGetBooleani_v` handler): after
the indexed query, if the host raised an error (consumed by the
`getError` call, so it cannot leak into the guest's own checks) or
returned null, fall back to the scalar `GLctx.getParameter(pname)`,
which answers the draw-buffer-0 value. When the host does support the
indexed query, its value is used unchanged.

## Rows closed by register note, not by code

- `glCopyBufferSubData` stays 1282/1282. The harness calls
  `glCopyBufferSubData(GL_ARRAY_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, 16)`;
  nothing ever binds a buffer to `GL_COPY_WRITE_BUFFER`, so the write
  target is unbound and desktop GL correctly answers
  `GL_INVALID_OPERATION`. The bridge handler is a straight passthrough
  and is not at fault. Binding a buffer to the write target is a harness
  setup step, not a product fix.
- `glMapBufferRange` stays 1282/1282. The bridge handler deliberately
  rejects direct bridge calls (`rejectBridgeEntry`, which records
  `GL_INVALID_OPERATION`): a trap cannot return a host pointer to the
  guest. The supported path is the guest libGL's client-side
  `glMapBufferRange` (`tools/rootfs64/libgl64/libgl64.c`, which returns
  its mapped scratch pointer and flushes on unmap); the harness bypasses
  it by resolving the raw bridge trap through `wglGetProcAddress`. Making
  the trap return a guest-writable mapping is a larger design change,
  not part of this entry.

## Proof (fixed build, committed harness)

Build: `tools/bw64-build.sh`, "Browser export verified" in
`test-results/fd268/build-fix2.log`; wasm
`dist/runtime/boxedwine64.wasm` sha256 prefix `c2452af4c6d1d3a5`
(base build `b01c7dc6de6bc9aa`). Coverage runs use the unmodified
committed `tools/glcov/glcov.exe` (provenance sha256
`ed908204…` in both verdicts), 334 handlers, `glcov: RESULT 0`.

- Default profile (3.3), `test-results/bridge-coverage-fd268-fix/`:
  8 of the 10 named rows now answer 0/0 (`glBufferData`,
  `glBufferSubData`, `glGetBufferParameteriv`,
  `glDrawElementsInstanced`, `glEnablei`, `glDisablei`,
  `glIsEnabledi`, `glGetBooleani_v`). Full 334-row diff against the
  composed baseline
  (`test-results/bridge-coverage-merge-batch14/glcov-results.json`):
  8 improved, **0 regressed**, 0 other changes.
- Forced 2.1 profile, `test-results/bridge-coverage-fd268-fix-v21/`
  (scratch probe copy with only the page's `glversion` parameter
  changed, as the batch-14 gate did; the copy is deleted after the
  run): full 334-row diff against the batch-14 v21 baseline:
  **0 improved, 0 regressed, 0 changed** — the 2.1 answers are
  unchanged, as the closing terms require.

## Not in this entry (observed, left open)

- `glHint` stays 1280/1280 at both profiles. The harness calls
  `glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST)`; that hint target
  is desktop-only and WebGL2 has no such target, so the host correctly
  raises `GL_INVALID_ENUM`. Why the FD-209 lane build answered 0/0 for
  the same call is not resolved; it needs its own look, not a guess here.
- The gate's other flagged rows (`glCompressedTexSubImage3D`,
  `glFramebufferTextureLayer`, `glGenerateMipmap`,
  `glGetActiveUniformBlockName/iv`, `glGetBufferSubData`,
  `glGetRenderbufferParameteriv`, `glRenderbufferStorage(+Multisample)`,
  `glTexStorage1D/2D/3D`, `glTexSubImage2D`, `glUniformBlockBinding`,
  `glPointParameteriv`) are untouched by this fix: on the fixed build
  they still answer as on the composed build (`glGetBufferSubData`
  1282/1282, `glTexSubImage2D` 1281/1281, `glUniformBlockBinding`
  1281/1281, `glPointParameteriv` 1280/1280, the rest 1282/1282).
  They are separate mechanisms (texture/renderbuffer state and the
  buffer readback path, not the VAO wipe or the indexed-state swap)
  and belong in their own entries if the coordinator wants them.
- Known limitation of the VAO restore: if a guest deletes the
  currently-bound VAO without unbinding it first, the bridge's
  `g_currentVao` goes stale (per spec the binding should reset to 0);
  the array-buffer restore is unaffected, only the default-VAO
  element-array restore could use a stale value. No harness row does
  this.

## Files changed

- `source/opengl/gl64bridge.cpp` — VAO bind save/restore;
  `glGetBooleani_v` indexed-query fallback.
- `tools/rootfs64/libgl64/libgl64.c` — the three inverted signatures.
- `tools/rootfs64/libgl32/libgl32.c`, `tools/rootfs64/libgl64/libGL.so.1`,
  `tools/rootfs64/libgl32/libGL.so.1` — regenerated via
  `build-libgl64.sh` / `build-libgl32.sh`.
- `docs/known-gaps/runtime-artifact-manifest.md` — regenerated for the
  new support-pack hash (`93fd17c09af7…`).
- `docs/defect-register.md` — FD-268 closed as CL-198.
- Evidence (untracked, kept in the lane tree):
  `test-results/fd268/` (diagnostic sources, host experiments, build +
  probe logs), `test-results/bridge-coverage-fd268-fix/` and
  `test-results/bridge-coverage-fd268-fix-v21/` (proof runs),
  `test-results/bridge-coverage-fd268-diag/` and
  `test-results/bridge-coverage-fd268-vao/` + `-i/` (diagnostics).
