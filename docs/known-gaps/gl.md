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
| `web/gl-webgpu.mjs:2490-2565` | Color blits now support nearest/linear filtering, scaling, flips, and format conversion in the GPU path, with corresponding CPU-shadow updates. | Breaks post-process/FBO color blits | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2520-2530` | Depth/stencil blits are not supported by the current attachment readback path and now log a rate-limited warning with `INVALID_OPERATION`; unknown filters return `INVALID_ENUM`. | Breaks depth/stencil blits | REJECTED-LOUDLY for depth/stencil (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:520-526,1000` | The default target now allocates `depth24plus-stencil8` and reports `stencil:true`; stencil clears include masked clears through a fullscreen pass. | Breaks stencil-using games | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2020-2058,1890-1920` | Global stencil func/op/mask state now enters render-pipeline descriptors and render passes; invalid enums set GL errors. | Breaks stencil-using games/FBOs | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2030-2055` | Distinct front/back stencil read or write masks cannot be represented by WebGPU's shared masks and are rejected with rate-limited warnings and `INVALID_OPERATION`. | Breaks games relying on asymmetric stencil masks | REJECTED-LOUDLY (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2030-2065,1880-1925` | `polygonOffset` factor/units are applied through WebGPU depth bias when `POLYGON_OFFSET_FILL` is enabled; non-finite parameters raise `INVALID_VALUE`. | Breaks depth-biased renderers | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:1926` | Sample coverage is empty; antialiasing is disabled in context attributes, so it cannot affect current single-sample output. | Harmless with one sample; breaks if MSAA is added without coverage support | NOT-A-GAP (context reports `antialias:false` at `web/gl-webgpu.mjs:832`) |
| `web/gl-webgpu.mjs:623` | `shadeModel` is empty; GLES 3 removed this setting and smooth shading is the fixed-function default. | Harmless | NOT-A-GAP (default smooth shading is used in `_fixedFunctionProgram`) |
| `web/gl-webgpu.mjs:1285-1291` | `validateProgram` now sets `GL_VALIDATE_STATUS` from the completed link result and `getProgramParameter` exposes it. | Breaks callers checking validation status | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2070-2125,1900-1940` | Blend factors/equations and depth/cull/front-face comparisons are now validated synchronously; invalid enums latch `INVALID_ENUM` before async pipeline creation. | Breaks invalid/unsupported state handling | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:420,1358-1415` | Query objects support occlusion, elapsed-time, primitives-generated, and timestamp targets with zero results; availability is false while active and true after end/counter. Native bridge mirrors completion and zero-result semantics. | Breaks query users that require measured GPU values | IMPLEMENTED for deterministic zero query behavior (`gl-webgpu.test.mjs`, `gl-shim-coverage.test.mjs`) |
| `web/gl-webgpu.mjs:1430,source/opengl/gl64bridge.cpp:4750-4775` | Transform feedback cannot be captured by this draw path. Its bridge calls now reach WebGPUGL methods, which log `transform feedback` and set `INVALID_OPERATION`. | Breaks transform-feedback shaders | REJECTED-LOUDLY (`gl-webgpu.test.mjs`, `gl-shim-coverage.test.mjs`) |
| `web/gl-webgpu.mjs:933` | Buffers exist but uniform buffer binding/range and std140 block binding are not implemented. | Breaks UBO shaders | OPEN |
| `web/gl-webgpu.mjs:28` | Texture binding is limited to `TEXTURE_2D`; arrays, 3D, and cube sampling are not fully represented. | Breaks modern texture users | OPEN |
| `web/gl-webgpu.mjs:2010` | Renderbuffer multisample storage/resolve and integer/float render targets are incomplete. | Breaks MSAA/HDR renderers | OPEN |
| `web/gl-webgpu.mjs:1850-1885,2075-2110` | `glPolygonMode` fill/line/point selection is implemented for `FRONT_AND_BACK`; line and point modes expand triangles into WebGPU-supported topologies. | Breaks wireframe and point-rendered compatibility paths | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2075-2110` | WebGPU supports one-pixel lines/points only; wider line/point sizes and `GL_POINT_SPRITE` are logged and return `INVALID_OPERATION`. | Breaks wide-line and point-sprite games | REJECTED-LOUDLY (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:2210-2300` | `SRGB8`/`SRGB8_ALPHA8` and sRGB S3TC formats upload to `rgba8unorm-srgb`; CPU fixed-function sampling also converts RGB to linear. | Breaks color-managed texture sampling | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-texture.test.mjs`) |
| `web/gl-webgpu.mjs:271-370,2118-2135` | `glClipControl` validates origin/depth mode and rewrites programmable and fixed-function clip coordinates; linked programs are rebuilt after state changes. | Breaks modern depth-range/origin paths | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu.test.mjs`) |
| `web/gl-webgpu.mjs:1` | sRGB framebuffer conversion remains incomplete. | Breaks color-managed framebuffer output | OPEN |
| `web/gl-webgpu.mjs:2123-2160` | DXT1/3/5 including sRGB S3TC variants decode in software and S3TC is advertised. | Breaks compressed-texture games | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-texture.test.mjs`) |
| `web/gl-webgpu.mjs:2123-2160` | ETC2/EAC and BC7/BPTC compressed uploads return rate-limited warnings and `INVALID_OPERATION`. | Breaks compressed-texture games | REJECTED-LOUDLY (`web/tests/suite/unit/gl-webgpu-texture.test.mjs`) |
| `web/gl-webgpu.mjs:210` | GLSL-to-WGSL translation has explicit rejection paths; unsupported constructs must be inventoried, reported with construct names, and tested. | Breaks affected shaders | OPEN |
| `web/gl-webgpu.mjs:1547` | Fixed-function alpha comparison and texenv combinations are partial; Quake III lightmap/multitexture paths can render dark or missing. | Breaks Quake III surfaces | OPEN |
| `web/gl-webgpu.mjs:1240,1669` | Fixed-function shader and CPU mirror support modulate, replace, decal, and add; uncommon texture environment combine modes remain unimplemented. Dark/absent Q3 surfaces still need a targeted reproduction. | Breaks uncommon fixed-function materials | OPEN |
| `web/gl-webgpu.mjs:1954-2035` | `texImage2D`/`texSubImage2D` previously expanded legacy `ALPHA`, `LUMINANCE`, `LUMINANCE_ALPHA`, `RED`, and `RG` channels incorrectly; expansion is corrected and covered. Celeste's title screenshot remains unverified. | Breaks glyph/sprite atlases | IMPLEMENTED (`web/tests/suite/unit/gl-webgpu-texture.test.mjs`); Celeste visual confirmation OPEN |
| `source/opengl/es/esopengl.c:31` | `glClearIndex` has no effect because the bridge exposes only RGBA visuals (`glcommon.cpp:1140-1142`). It now returns without panicking. | Harmless in RGBA mode | NOT-A-GAP (color-index buffers are not exposed by this bridge) |
| `source/opengl/es/esopengl.c:86` | `glIndexMask` has no effect because the bridge exposes only RGBA visuals (`glcommon.cpp:1140-1142`). It now returns without panicking. | Harmless in RGBA mode | NOT-A-GAP (color-index buffers are not exposed by this bridge) |
| `source/opengl/es/esopengl.c:94` | `glAlphaFunc` panics; alpha-tested fixed-function textures disappear. | Breaks compatibility games | OPEN |
| `source/opengl/es/esopengl.c:102` | `glLogicOp` panics. | Breaks callers using logic ops | OPEN |
| `source/opengl/es/esopengl.c:114` | `glPointSize` panics; point sprites/points cannot be sized. | Breaks point rendering | OPEN |
| `source/opengl/es/esopengl.c:126` | `glPolygonMode` panics; wireframe requests terminate instead of rendering/rejecting. | Breaks debug/render modes | OPEN |
| `source/opengl/gl64bridge.cpp:3062` | The 64-bit guest bridge previously discarded polygon mode without changing WebGPUGL state; line/point polygon conversion was unreachable from guests. | Breaks wireframe and point-mode rendering | IMPLEMENTED (`web/tests/suite/unit/gl-shim-coverage.test.mjs`) |
| `source/opengl/es/esopengl.c:134-138` | Polygon stipple queries/calls panic. | Breaks legacy compatibility calls | OPEN |
| `source/opengl/es/esopengl.c:142-146` | Edge flag calls panic. | Breaks legacy compatibility calls | OPEN |
| `source/opengl/es/esopengl.c:154-158` | User clip plane set/get panics. | Breaks compatibility clipping | OPEN |
| `source/opengl/es/esopengl.c:162-166` | `glDrawBuffer` and `glReadBuffer` panic in this shim. | Breaks compatibility callers | OPEN |
| `source/opengl/gl64bridge.cpp:2676,2737` | Shader and program deletion now reach the owning GL thread synchronously; attached/in-use objects follow GL's deferred destruction semantics. | Can leak or alter shader lifecycle | IMPLEMENTED (`web/tests/suite/unit/gl-shim-coverage.test.mjs`) |
| `source/opengl/gl64bridge.cpp:2874` | `glMapBufferRange` returns zero as unsupported; valid mapping users cannot access buffer storage. | Breaks buffer mapping callers | OPEN |
| `source/opengl/gl64bridge.cpp:3342,1384` | Framebuffer names are generated in the guest namespace and copied to guest memory before lazy host allocation. | Corrupts guest state | IMPLEMENTED (`web/tests/suite/unit/gl-shim-coverage.test.mjs`) |
| `source/opengl/gl64bridge.cpp:3431` | `framebufferTexture` host exceptions are rate-limited logged with the GL function name and latch `INVALID_OPERATION` on WebGPUGL. | Silently drops attachment operation | IMPLEMENTED (`web/tests/suite/unit/gl-shim-coverage.test.mjs`) |
| `source/opengl/gl64bridge.cpp:3742-3747,3867,3941` | Several GL state/texture bridge operations swallow JS exceptions. | Silently drops valid operations/errors | OPEN |
| `source/opengl/gl64bridge.cpp:4520-4689` | Multiple blend/draw wrappers catch and discard JS exceptions. | Silently drops valid operations/errors | OPEN |
| `source/opengl/gl64bridge.cpp:4710-4735` | Best-effort no-op dispatch cases need individual ABI/caller audit and visible error handling. | Can break valid guest calls | OPEN |
| `source/opengl/glMarshal.cpp:237` | Unsupported pixel format/type combinations panic in marshaling rather than returning guest GL error. | Breaks invalid-input handling | OPEN |
| `web/bridge-recovery.mjs:9,41` | Recovery detection/logging catches exceptions to preserve fallback, but log callback failure is silently ignored. | Harmless unless diagnostic callback itself fails | NOT-A-GAP (recovery must not fail because logging failed) |
| `web/gl-webgpu.mjs:477,571,1714` | Failure callback and error-scope cleanup catches prevent secondary errors from replacing the primary device/validation failure. | Harmless cleanup paths | NOT-A-GAP (primary error is reported before cleanup) |
| `source/opengl/es/khash.h:308` | Shrinking after removal is an optimization TODO; lookup/insertion correctness is independent of shrinking and the table already grows as needed. | Harmless memory/performance behavior | NOT-A-GAP (resize-on-growth remains implemented at `khash.h:235-279`) |
| `source/opengl/es/glshim.c:37,145` | Dynamic type and texture-target TODOs need caller review; target handling may exclude valid texture behavior. | Could break callers | OPEN |
| `source/opengl/glcommon.cpp:1318` | GLX pixmap texture attributes are not supported; a non-empty list now logs once and returns `None` instead of panicking. Empty lists remain accepted. | Breaks callers using texture-from-pixmap | REJECTED-LOUDLY (`web/tests/suite/unit/gl-shim-coverage.test.mjs`) |
| `source/opengl/glcommon.cpp:1346` | GLX window attributes are not supported; a non-empty list now logs once and returns `None` instead of panicking. Empty lists remain accepted. | Breaks callers using window attributes | REJECTED-LOUDLY (`web/tests/suite/unit/gl-shim-coverage.test.mjs`) |

## Audit notes

This is an initial register, not a completed audit. Grep hits in enum conversion
helpers (`gl-webgpu-utils.mjs`) throw synchronously for bad internal inputs;
their callers and GL error conversion remain to be audited. The bridge's many
`default:` branches and `catch {}` sites still require line-by-line disposition.
Every `OPEN` row needs implementation or a tested, rate-limited loud rejection
before this register can be considered complete.
