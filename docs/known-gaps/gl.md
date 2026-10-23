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
| `web/gl-webgpu.mjs:2331` | Single output and `NONE` are tracked. Multiple color outputs are not implemented and now log a rate-limited MRT warning with `INVALID_OPERATION`. Deferred renderers still cannot run. | Breaks real games | REJECTED-LOUDLY (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2136` | Read selection previously ignored; selection now handles `NONE`, `BACK`, and `COLOR_ATTACHMENT0`, with GL errors for invalid selections. | Breaks FBO readback | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:829` | `getError()` previously always returned `NO_ERROR`; it now drains the first latched error, and draw/read buffer validation records errors. Other GL entry points still need validation coverage. | Breaks FNA/LWJGL error checks | OPEN |
| `web/gl-webgpu.mjs:719` | Common invalid capabilities, viewport sizes, draw modes, buffer sizes/usages, and subdata ranges now latch `INVALID_ENUM`, `INVALID_VALUE`, or `INVALID_OPERATION`; UBO and pixel-unpack targets are rate-limited logged rejections. Broader state/texture/FBO validation remains open. | Breaks FNA/LWJGL error checks | OPEN (partial coverage tested in `web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:954-973` | S3TC, ANGLE instancing, OES VAO, and BGRA read format are reported. Anisotropy, float/depth render targets, sRGB framebuffer, and texture_rg remain unadvertised. | Breaks games gated on those features | OPEN (advertised extension behavior tested in `web/tests/suite/unit/gl-webgpu-texture.test.mjs`) |
| `web/gl-webgpu.mjs:754` | RGB, BGRA, and normalized float output now convert from the RGBA shadow, including pack alignment. | Breaks RGB/BGRA readback consumers | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-readback.test.mjs`) |
| `web/gl-webgpu.mjs:770` | Depth formats are rejected with a rate-limited feature warning and `INVALID_OPERATION`; depth staging is still absent. | Breaks depth readback consumers | REJECTED-LOUDLY (`web/tests/suite/unit/gl-webgpu-readback.test.mjs`) |
| `web/gl-webgpu.mjs:770,1002` | Pixel-pack buffers and numeric `readPixels` offsets now work with pack layout and bounds checks. | Breaks PBO readback consumers | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-readback.test.mjs`) |
| `web/gl-webgpu.mjs:783` | GPU-written FBO readback is rejected with a rate-limited warning and `INVALID_OPERATION`; CPU-shadow FBO reads remain available. | Breaks GPU FBO readback consumers | REJECTED-LOUDLY (`web/tests/suite/unit/gl-webgpu-readback.test.mjs`) |
| `web/gl-webgpu.mjs:834` | Default-framebuffer readback now reuses two staging buffers, skips unchanged color frames, retries if both slots are busy, and only reallocates on resize. GPU results remain asynchronous and the guest readback can lag by about one swap. | Degrades present and can miss frames | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-readback.test.mjs`); one-swap latency remains documented |
| `web/gl-webgpu.mjs:439` | Resize copied the CPU shadow using updated dimensions; dimensions are now captured before replacement and overlap is preserved. | Breaks retained pixels on resize | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-readback.test.mjs`) |
| `web/gl-webgpu.mjs:683` | `blitFramebuffer` mirrors broadly on CPU but only submits GPU copies for equal-size, same-format, restricted orientations; unsupported cases silently return. | Breaks post-process/FBO blits | OPEN |
| `web/gl-webgpu.mjs:22` | Default depth target is `depth24plus`; stencil bit/storage/test operations are unsupported while context attributes say `stencil:false`. | Breaks stencil-using games | OPEN |
| `web/gl-webgpu.mjs:1920-1924` | Stencil state setters are empty; there is no stencil attachment or test in the pipeline. | Breaks stencil-using games/FBOs | OPEN |
| `web/gl-webgpu.mjs:1925` | Polygon offset is empty; valid filled polygons that rely on depth bias can z-fight. | Breaks depth-biased renderers | OPEN |
| `web/gl-webgpu.mjs:1926` | Sample coverage is empty; antialiasing is disabled in context attributes, so it cannot affect current single-sample output. | Harmless with one sample; breaks if MSAA is added without coverage support | NOT-A-GAP (context reports `antialias:false` at `web/gl-webgpu.mjs:832`) |
| `web/gl-webgpu.mjs:623` | `shadeModel` is empty; GLES 3 removed this setting and smooth shading is the fixed-function default. | Harmless | NOT-A-GAP (default smooth shading is used in `_fixedFunctionProgram`) |
| `web/gl-webgpu.mjs:1170` | `validateProgram` is empty and does not update `GL_VALIDATE_STATUS`. | Breaks callers checking validation status | OPEN |
| `web/gl-webgpu.mjs:1656` | Unsupported blend equations throw from asynchronous draw setup rather than setting guest-visible GL errors. | Breaks uncommon blend modes | OPEN |
| `web/gl-webgpu.mjs:1688` | Unsupported depth comparisons throw from draw setup rather than setting guest-visible GL errors. | Breaks invalid/unsupported state handling | OPEN |
| `web/gl-webgpu.mjs:1358` | Query objects (occlusion/timer) and transform feedback are absent. | Breaks query/feedback users | OPEN |
| `web/gl-webgpu.mjs:933` | Buffers exist but uniform buffer binding/range and std140 block binding are not implemented. | Breaks UBO shaders | OPEN |
| `web/gl-webgpu.mjs:28` | Texture binding is limited to `TEXTURE_2D`; arrays, 3D, and cube sampling are not fully represented. | Breaks modern texture users | OPEN |
| `web/gl-webgpu.mjs:2010` | Renderbuffer multisample storage/resolve and integer/float render targets are incomplete. | Breaks MSAA/HDR renderers | OPEN |
| `web/gl-webgpu.mjs:1` | Polygon line modes, point sprites/size, line width, clip control, and sRGB framebuffer behavior are incomplete. | Breaks compatibility renderers | OPEN |
| `web/gl-webgpu.mjs:2123-2160` | DXT1/3/5 including sRGB S3TC variants decode in software and S3TC is advertised. | Breaks compressed-texture games | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-texture.test.mjs`) |
| `web/gl-webgpu.mjs:2123-2160` | ETC2/EAC and BC7/BPTC compressed uploads return rate-limited warnings and `INVALID_OPERATION`. | Breaks compressed-texture games | REJECTED-LOUDLY (`web/tests/suite/unit/gl-webgpu-texture.test.mjs`) |
| `web/gl-webgpu.mjs:210` | GLSL-to-WGSL translation has explicit rejection paths; unsupported constructs must be inventoried, reported with construct names, and tested. | Breaks affected shaders | OPEN |
| `web/gl-webgpu.mjs:1547` | Fixed-function alpha comparison and texenv combinations are partial; Quake III lightmap/multitexture paths can render dark or missing. | Breaks Quake III surfaces | OPEN |
| `web/gl-webgpu.mjs:1240,1669` | Fixed-function shader and CPU mirror support modulate, replace, decal, and add; uncommon texture environment combine modes remain unimplemented. Dark/absent Q3 surfaces still need a targeted reproduction. | Breaks uncommon fixed-function materials | OPEN |
| `web/gl-webgpu.mjs:1954-2035` | `texImage2D`/`texSubImage2D` previously expanded legacy `ALPHA`, `LUMINANCE`, `LUMINANCE_ALPHA`, `RED`, and `RG` channels incorrectly; expansion is corrected and covered. Celeste's title screenshot remains unverified. | Breaks glyph/sprite atlases | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-texture.test.mjs`); Celeste visual confirmation OPEN |
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
