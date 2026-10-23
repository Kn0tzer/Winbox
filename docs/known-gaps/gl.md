# OpenGL bridge gap register

Audit scope: `web/gl-webgpu.mjs`, `web/gl-webgpu-utils.mjs`, `web/gl-*.mjs`,
`web/bridge-recovery.mjs`, `source/opengl/**`, and the GL game probe. The source
scan covered unsupported/unimplemented/TODO/FIXME/ENOSYS/EINVAL markers, empty
methods, catch blocks, early returns, switch defaults, and the known GL path
list supplied for this lane. Rows below record observed behavior. `OPEN` means
the required implementation or rejection evidence is still outstanding; it is
not a disposition.

| Ref | Valid guest/app behavior and impact | Severity | Disposition |
|---|---|---|---|
| `web/gl-webgpu.mjs:2126` | Multiple `glDrawBuffers` color attachments are discarded; deferred renderers/FNA lose MRT outputs. Single output and `NONE` are tracked, but MRT is not implemented. | Breaks real games | OPEN |
| `web/gl-webgpu.mjs:2136` | Read selection previously ignored; selection now handles `NONE`, `BACK`, and `COLOR_ATTACHMENT0`, with GL errors for invalid selections. | Breaks FBO readback | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:829` | `getError()` previously always returned `NO_ERROR`; it now drains the first latched error, and draw/read buffer validation records errors. Other GL entry points still need validation coverage. | Breaks FNA/LWJGL error checks | OPEN |
| `web/gl-webgpu.mjs:832` | Extension discovery always returns empty/null, so games cannot use supported compression, anisotropy, VAO, instancing, or sRGB extensions. | Breaks feature-gated games | OPEN |
| `web/gl-webgpu.mjs:753` | `readPixels` rejects RGB, BGRA, float/depth formats and non-Uint8 destinations without setting an error; PBO destinations are not handled. | Breaks readback consumers | OPEN |
| `web/gl-webgpu.mjs:491` | Non-default FBO attachment pixels come from the CPU shadow; GPU programmable output is not asynchronously read back. | Breaks FBO screenshot/readback | OPEN |
| `web/gl-webgpu.mjs:789` | Default-framebuffer readback allocates/uses one staging buffer, attempts every swap, can skip while busy, and exposes about one-swap latency. | Degrades present and can miss frames | OPEN |
| `web/gl-webgpu.mjs:439` | Resize copied the CPU shadow using updated dimensions; dimensions are now captured before replacement and overlap is preserved. | Breaks retained pixels on resize | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-readback.test.mjs`) |
| `web/gl-webgpu.mjs:683` | `blitFramebuffer` mirrors broadly on CPU but only submits GPU copies for equal-size, same-format, restricted orientations; unsupported cases silently return. | Breaks post-process/FBO blits | OPEN |
| `web/gl-webgpu.mjs:22` | Default depth target is `depth24plus`; stencil bit/storage/test operations are unsupported while context attributes say `stencil:false`. | Breaks stencil-using games | OPEN |
| `web/gl-webgpu.mjs:1656` | Unsupported blend equations throw from asynchronous draw setup rather than setting guest-visible GL errors. | Breaks uncommon blend modes | OPEN |
| `web/gl-webgpu.mjs:1688` | Unsupported depth comparisons throw from draw setup rather than setting guest-visible GL errors. | Breaks invalid/unsupported state handling | OPEN |
| `web/gl-webgpu.mjs:1358` | Query objects (occlusion/timer) and transform feedback are absent. | Breaks query/feedback users | OPEN |
| `web/gl-webgpu.mjs:933` | Buffers exist but uniform buffer binding/range and std140 block binding are not implemented. | Breaks UBO shaders | OPEN |
| `web/gl-webgpu.mjs:28` | Texture binding is limited to `TEXTURE_2D`; arrays, 3D, and cube sampling are not fully represented. | Breaks modern texture users | OPEN |
| `web/gl-webgpu.mjs:2010` | Renderbuffer multisample storage/resolve and integer/float render targets are incomplete. | Breaks MSAA/HDR renderers | OPEN |
| `web/gl-webgpu.mjs:1` | Polygon line modes, point sprites/size, line width, clip control, and sRGB framebuffer behavior are incomplete. | Breaks compatibility renderers | OPEN |
| `web/gl-webgpu.mjs:1` | Compressed DXT/ETC2/BC7 formats do not negotiate `texture-compression-bc` or software-decode absent formats. | Breaks compressed-texture games | OPEN |
| `web/gl-webgpu.mjs:210` | GLSL-to-WGSL translation has explicit rejection paths; unsupported constructs must be inventoried, reported with construct names, and tested. | Breaks affected shaders | OPEN |
| `web/gl-webgpu.mjs:1547` | Fixed-function alpha comparison and texenv combinations are partial; Quake III lightmap/multitexture paths can render dark or missing. | Breaks Quake III surfaces | OPEN |
| `web/gl-webgpu.mjs:1560` | Fixed-function texture combine fallback returns a fixed value for unhandled modes; sprite atlas sampling/upload paths can garble Celeste glyphs. | Breaks textured sprite/text rendering | OPEN |
| `source/opengl/es/esopengl.c:86` | `glIndexMask` panics when called by a compatibility guest. | Breaks real callers | OPEN |
| `source/opengl/es/esopengl.c:94` | `glAlphaFunc` panics; alpha-tested fixed-function textures disappear. | Breaks compatibility games | OPEN |
| `source/opengl/es/esopengl.c:102` | `glLogicOp` panics. | Breaks callers using logic ops | OPEN |
| `source/opengl/es/esopengl.c:114` | `glPointSize` panics; point sprites/points cannot be sized. | Breaks point rendering | OPEN |
| `source/opengl/es/esopengl.c:126` | `glPolygonMode` panics; wireframe requests terminate instead of rendering/rejecting. | Breaks debug/render modes | OPEN |
| `source/opengl/es/esopengl.c:134-138` | Polygon stipple queries/calls panic. | Breaks legacy compatibility calls | OPEN |
| `source/opengl/es/esopengl.c:142-146` | Edge flag calls panic. | Breaks legacy compatibility calls | OPEN |
| `source/opengl/es/esopengl.c:154-158` | User clip plane set/get panics. | Breaks compatibility clipping | OPEN |
| `source/opengl/es/esopengl.c:162-166` | `glDrawBuffer` and `glReadBuffer` panic in this shim. | Breaks compatibility callers | OPEN |
| `source/opengl/gl64bridge.cpp:2676,2737` | Shader deletion paths are explicitly deferred no-ops; guest object lifecycle is not forwarded. | Can leak or alter shader lifecycle | OPEN |
| `source/opengl/gl64bridge.cpp:2874` | `glMapBufferRange` returns zero as unsupported; valid mapping users cannot access buffer storage. | Breaks buffer mapping callers | OPEN |
| `source/opengl/gl64bridge.cpp:3342` | Unsupported framebuffer generation path leaves caller IDs unwritten. | Corrupts guest state | OPEN |
| `source/opengl/gl64bridge.cpp:3431` | `framebufferTexture` catches and swallows host exceptions. | Silently drops attachment operation | OPEN |
| `source/opengl/gl64bridge.cpp:3742-3747,3867,3941` | Several GL state/texture bridge operations swallow JS exceptions. | Silently drops valid operations/errors | OPEN |
| `source/opengl/gl64bridge.cpp:4520-4689` | Multiple blend/draw wrappers catch and discard JS exceptions. | Silently drops valid operations/errors | OPEN |
| `source/opengl/gl64bridge.cpp:4710-4735` | Best-effort no-op dispatch cases need individual ABI/caller audit and visible error handling. | Can break valid guest calls | OPEN |
| `source/opengl/glMarshal.cpp:237` | Unsupported pixel format/type combinations panic in marshaling rather than returning guest GL error. | Breaks invalid-input handling | OPEN |
| `web/bridge-recovery.mjs:9,41` | Recovery detection/logging catches exceptions to preserve fallback, but log callback failure is silently ignored. | Harmless unless diagnostic callback itself fails | NOT-A-GAP (recovery must not fail because logging failed) |
| `web/gl-webgpu.mjs:477,571,1714` | Failure callback and error-scope cleanup catches prevent secondary errors from replacing the primary device/validation failure. | Harmless cleanup paths | NOT-A-GAP (primary error is reported before cleanup) |
| `source/opengl/es/khash.h:308` | Shrinking is an optimization TODO; hash table correctness does not depend on shrinking. | Harmless memory/performance behavior | OPEN |
| `source/opengl/es/glshim.c:37,145` | Dynamic type and texture-target TODOs need caller review; target handling may exclude valid texture behavior. | Could break callers | OPEN |
| `source/opengl/glcommon.cpp:1318,1346` | Non-empty X pixmap/window attribute lists panic. | Breaks callers using those attributes | OPEN |

## Audit notes

This is an initial register, not a completed audit. Grep hits in enum conversion
helpers (`gl-webgpu-utils.mjs`) throw synchronously for bad internal inputs;
their callers and GL error conversion remain to be audited. The bridge's many
`default:` branches and `catch {}` sites still require line-by-line disposition.
Every `OPEN` row needs implementation or a tested, rate-limited loud rejection
before this register can be considered complete.
