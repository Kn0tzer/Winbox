# GL-on-WebGPU API inventory

S0 survey for the GL-on-WebGPU lane. The bridge exposes 299 distinct `gl*`/`glX*` symbol names in `source/opengl/gl64bridge.cpp`; `tools/glcov/glcov.c` describes its broader coverage run as 306 handlers, including internal entries. Emscripten is `/opt/emsdk` (`source /opt/emsdk/emsdk_env.sh`; `which emcc` then resolves to `/opt/emsdk/upstream/emscripten/emcc`). The GL library survey covers both `/opt/emsdk/upstream/emscripten/src/lib/libwebgl.js` and `libwebgl2.js`, since Emscripten splits WebGL2 entry points across them.

`WebGPUGL` must satisfy the `GLctx` methods that Emscripten uses for those bridge entry points. The list below is grouped by the API areas in the implementation spec. It includes methods Emscripten reaches through a local `gl = context.GLctx` alias, as well as methods reached by the GL shim when legacy fixed-function calls are lowered to shaders/buffers. Context fields and extension hooks are listed separately.

## Test marks

- **C** — `tools/glcov/glcov.exe`, the broad bridge-handler probe. Its source says it calls all bridge handlers; that is intended coverage, not proof each call succeeds.
- **T** — `web/tests/suite/browser/gl32-triangle.test.mjs`, a WGL fixture using clear, viewport, immediate-mode color/vertex, error query, and swap. Immediate mode is lowered through the existing fixed-function shim.
- **F** — `web/tests/suite/browser/gl32-framebuffer.test.mjs`, an offscreen texture-backed FBO, viewport/clear, readback, split read/draw bindings, blit, read-buffer selection, and swap.
- **G** — available `tools/rootfs64/gltest/gltri.exe` and `glcube.exe` fixtures (triangle and depth-tested cube). There is no `tools/gltest` directory in this checkout; the fixtures are under `tools/rootfs64/gltest`.

All bridge APIs are in C's intended scope. T/G tags identify the `GLctx` operations needed beneath the existing fixed-function triangle path, including its generated shaders and geometry. F tags identify the framebuffer fixture path.

## Buffers

| Emscripten `GLctx` methods and fields | Checks |
|---|---|
| `createBuffer`, `deleteBuffer`, `isBuffer`, `bindBuffer`, `bufferData`, `bufferSubData`, `getBufferParameter`, `getBufferSubData`, `copyBufferSubData` | C, T, G |
| `bindBufferBase`, `bindBufferRange`, `currentArrayBufferBinding`, `currentElementArrayBufferBinding`, `currentPixelPackBufferBinding`, `currentPixelUnpackBufferBinding` | C |

The bridge also exposes buffer storage/mapping. Those calls need a WebGPUGL implementation or an explicit unsupported result.

## Vertex arrays and attributes

| Emscripten `GLctx` methods | Checks |
|---|---|
| `createVertexArray`, `deleteVertexArray`, `isVertexArray`, `bindVertexArray` | C, T, G |
| `enableVertexAttribArray`, `disableVertexAttribArray`, `vertexAttribPointer`, `vertexAttribIPointer`, `vertexAttribDivisor`, `getVertexAttrib`, `getVertexAttribOffset` | C, T, G |
| `vertexAttrib1f`, `vertexAttrib2f`, `vertexAttrib3f`, `vertexAttrib4f`, `vertexAttribI4i`, `vertexAttribI4ui` | C, T, G |

The bridge also exports integer vector attribute values and double-value aliases; these use corresponding scalar/vector WebGL wrapper methods where Emscripten provides them.

## Shaders, programs, uniforms, and UBOs

| Emscripten `GLctx` methods and fields | Checks |
|---|---|
| `createShader`, `deleteShader`, `isShader`, `shaderSource`, `compileShader`, `getShaderParameter`, `getShaderInfoLog`, `getShaderSource`, `getShaderPrecisionFormat` | C, T, G |
| `createProgram`, `deleteProgram`, `isProgram`, `attachShader`, `detachShader`, `bindAttribLocation`, `linkProgram`, `validateProgram`, `useProgram`, `getProgramParameter`, `getProgramInfoLog`, `getAttachedShaders`, `currentProgram` | C, T, G |
| `getAttribLocation`, `getActiveAttrib`, `getFragDataLocation`, `getUniformLocation`, `getActiveUniform`, `getActiveUniforms`, `getUniform`, `getUniformIndices` | C, T, G |
| `uniform1f`, `uniform1fv`, `uniform1i`, `uniform1iv`, `uniform2f`, `uniform2fv`, `uniform2i`, `uniform2iv`, `uniform3f`, `uniform3fv`, `uniform3i`, `uniform3iv`, `uniform4f`, `uniform4fv`, `uniform4i`, `uniform4iv` | C, T, G |
| `uniformMatrix2fv`, `uniformMatrix3fv`, `uniformMatrix4fv`, `uniformMatrix2x3fv`, `uniformMatrix2x4fv`, `uniformMatrix3x2fv`, `uniformMatrix3x4fv`, `uniformMatrix4x2fv`, `uniformMatrix4x3fv` | C, T, G |
| `uniform1ui`, `uniform1uiv`, `uniform2ui`, `uniform2uiv`, `uniform3ui`, `uniform3uiv`, `uniform4ui`, `uniform4uiv` | C |
| `getUniformBlockIndex`, `getActiveUniformBlockName`, `getActiveUniformBlockParameter`, `uniformBlockBinding` | C |

The bridge's uniform-block data/size queries and unsigned uniform getters are also in C's scope.

## Textures, samplers, and pixel store

| Emscripten `GLctx` methods and fields | Checks |
|---|---|
| `createTexture`, `deleteTexture`, `isTexture`, `bindTexture`, `activeTexture`, `texImage2D`, `texSubImage2D`, `texImage3D`, `texSubImage3D`, `compressedTexImage2D`, `compressedTexSubImage2D`, `compressedTexImage3D`, `compressedTexSubImage3D`, `texStorage2D`, `texStorage3D`, `generateMipmap`, `texParameteri`, `texParameterf`, `getTexParameter` | C, F |
| `pixelStorei`, `readPixels` | C, F |
| `getInternalformatParameter`, `getExtension`, `disjointTimerQueryExt` | C |
| `createSampler`, `deleteSampler`, `isSampler`, `bindSampler`, `samplerParameteri`, `samplerParameterf`, `getSamplerParameter` | C |

The bridge additionally calls immutable storage, multisample texture, texture-buffer, and integer sampler APIs. Raw multisample and integer sampler accessors are listed under direct bridge calls below. DXT compressed texture calls are in the Emscripten surface and need WebGPU BC feature negotiation before they can work.

## Framebuffers and renderbuffers

| Emscripten `GLctx` methods | Checks |
|---|---|
| `createFramebuffer`, `deleteFramebuffer`, `isFramebuffer`, `bindFramebuffer`, `framebufferTexture2D`, `framebufferTextureLayer`, `framebufferRenderbuffer`, `getFramebufferAttachmentParameter` | C, F |
| `createRenderbuffer`, `deleteRenderbuffer`, `isRenderbuffer`, `bindRenderbuffer`, `renderbufferStorage`, `renderbufferStorageMultisample`, `getRenderbufferParameter` | C |
| `drawBuffers`, `readBuffer`, `blitFramebuffer`, `checkFramebufferStatus`, `invalidateFramebuffer`, `invalidateSubFramebuffer` | C, F |

`F` needs texture-backed color attachment, framebuffer completeness, separate read/draw bindings, and color blit. The fixture does not cover depth/stencil attachments or multisample renderbuffers.

## State and draw calls

| Emscripten `GLctx` methods and fields | Checks |
|---|---|
| `enable`, `disable`, `isEnabled`, `getParameter`, `clear`, `clearColor`, `viewport`, `scissor`, `colorMask`, `depthMask`, `depthFunc`, `sampleCoverage`, `pixelStorei` | C, T, F, G |
| `clearBufferfv`, `clearBufferiv`, `clearBufferuiv`, `clearBufferfi` | C |
| `blendFunc`, `blendFuncSeparate`, `blendEquation`, `blendEquationSeparate`, `stencilFunc`, `stencilFuncSeparate`, `stencilOp`, `stencilOpSeparate`, `stencilMask`, `cullFace`, `frontFace`, `polygonOffset`, `webglPolygonMode` | C |
| `drawArrays`, `drawElements`, `drawArraysInstanced`, `drawElementsInstanced`, `multiDrawWebgl`, `dibvbi`, `mdibvbi` | C, T, G |
| `getIndexedParameter`, `extClipControl`, `extPolygonOffsetClamp` | C |

The bridge also exports blend equations/functions, stencil, cull/front-face, scissor, viewport/depth-range, polygon offset, and indexed state functions. Most use standard Emscripten wrappers; the raw indexed and base-vertex paths below bypass them.

## Queries, sync, and miscellaneous

| Emscripten `GLctx` methods and fields | Checks |
|---|---|
| `getError`, `getParameter`, `getShaderPrecisionFormat`, `getExtension`, `isContextLost`, `canvas` | C, T, F, G |
| `createQuery`, `beginQuery`, `endQuery`, `deleteQuery`, `isQuery`, `getQuery`, `getQueryParameter`, `fenceSync`, `clientWaitSync`, `waitSync`, `deleteSync`, `getSyncParameter`, `disjointTimerQueryExt` | C |
| `bindTransformFeedback`, `deleteTransformFeedback`, `isTransformFeedback`, `transformFeedbackVaryings`, `getTransformFeedbackVarying`, `beginTransformFeedback`, `endTransformFeedback` | C |
| `getActiveUniformBlockName`, `getActiveUniformBlockParameter`, `getUniformBlockIndex` | C |

`glGetString`/`glGetStringi`, debug callbacks, texture readback, and several indexed queries are bridge calls without a WebGPU-native equivalent. They need WebGPUGL query/state emulation or a documented unsupported path. `readPixels` is synchronous in the current bridge contract.

## Direct `GLctx` escape hatches in `gl64bridge.cpp`

These uses bypass the normal Emscripten GL wrappers and must be preserved or routed through WebGPUGL if the bridge stays unchanged:

- **Fixed-function quad index helper:** `createBuffer`, `getParameter`, `bindBuffer`, `bufferData`; properties `ELEMENT_ARRAY_BUFFER`, `ELEMENT_ARRAY_BUFFER_BINDING`, `STATIC_DRAW` (T/G).
- **Framebuffer, sampler, and multisample paths:** `framebufferTexture`, `getSamplerParameterIiv`, `getSamplerParameterIuiv`, `samplerParameterIiv`, `samplerParameterIuiv`, `texImage2DMultisample`, `texImage3DMultisample`, `getMultisamplefv` (C). `framebufferTexture` is not part of the WebGL2 context API.
- **Context and query helpers:** `isContextLost`, `getAttachedShaders`, `getIndexedParameter`, `isEnabled`, `enable`, `disable`, `clearBufferfi` (C).
- **Indexed state and draws:** `blendEquationSeparate`, `blendFuncSeparate`, `colorMask`, `minSampleShading`, `drawElementsBaseVertex`, `drawElementsInstancedBaseVertex`, `drawElementsInstancedBaseVertexBaseInstance` (C). Some argument lists in the bridge are core-GL forms and differ from WebGL2 overloads; preserve the bridge's invocation behavior only where required by its current call sites.
- **Context fields and extension hooks:** `canvas`, `currentArrayBufferBinding`, `currentElementArrayBufferBinding`, `currentPixelPackBufferBinding`, `currentPixelUnpackBufferBinding`, `currentProgram`, `disjointTimerQueryExt`, `extClipControl`, `extPolygonOffsetClamp`.

## S0 result and implementation implications

The Emscripten library gives a useful WebGL2-shaped API, but it does not cover every exported GL bridge call. The first implementation stages can target the stable context methods listed above and keep a WebGL2 fallback behind the runtime switch. GL queries, sync, indexed/base-vertex calls, multisampling, integer samplers, and legacy fixed-function state need explicit emulation or clear unsupported behavior. `gl32-triangle` validates the shim-generated draw path; `gl32-framebuffer` validates the texture-backed FBO/readback/blit path; `glcov` remains the broad call-surface probe.
