# GL64 Bridge Coverage Matrix

Source: glcov in-browser probe (334 handlers tested).
Slow threshold: 10000us (see METHOD.md).

## Four-bucket summary

| Bucket | Count |
|---|---|
| 1. Total handlers tested | 334 |
| 2. Fully working | 4 |
| 3. Slow or flaky | 237 |
| 4. Broken or stub | 93 |

## Per-handler table

| Handler | Bucket | Detail | err1 | err2 | us1 | us2 |
|---|---|---|---|---|---|---|
| glActiveTexture | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glAlphaFunc | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glAttachShader | broken | glGetError=1282 | 1282 | 1282 | 34000 | 34000 |
| glBegin | slow | 16000us >= 10000us | 0 | 0 | 16000 | 0 |
| glBeginQuery | broken | glGetError=1280 | 1280 | 1280 | 34000 | 18000 |
| glBeginTransformFeedback | broken | glGetError=1282 | 1282 | 1282 | 39000 | 34000 |
| glBindAttribLocation | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindBuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindBufferBase | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindBufferRange | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindFragDataLocation | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glBindFramebuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindRenderbuffer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glBindSampler | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindTexture | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindVertexArray | flaky | err1=1281 err2=0 | 1281 | 0 | 34000 | 33000 |
| glBlendColor | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glBlendEquation | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glBlendEquationSeparate | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBlendEquationSeparatei | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlendEquationi | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlendFunc | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glBlendFuncSeparate | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBlendFuncSeparatei | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlendFunci | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlitFramebuffer | broken | glGetError=1282 | 1282 | 1282 | 35000 | 33000 |
| glBufferData | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glBufferStorage | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBufferSubData | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glCheckFramebufferStatus | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glClear | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glClearBufferfi | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glClearBufferfv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glClearBufferiv | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glClearBufferuiv | broken | glGetError=1282 | 1282 | 1282 | 34000 | 34000 |
| glClearColor | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glClearDepth | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glClientActiveTextureARB | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glClientWaitSync | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glColor3f | slow | 16000us >= 10000us | 0 | 0 | 16000 | 0 |
| glColor4f | slow | 16000us >= 10000us | 0 | 0 | 16000 | 0 |
| glColor4ub | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glColorMask | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glColorMaski | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glColorMaterial | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glColorPointer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glCompileShader | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glCompressedTexImage2D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glCompressedTexImage3D | flaky | err1=37442 err2=0 | 37442 | 0 | 217000 | 33000 |
| glCompressedTexSubImage2D | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glCompressedTexSubImage3D | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glCopyBufferSubData | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glCreateProgram | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glCreateShader | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glCullFace | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDebugMessageCallback | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDebugMessageControl | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDebugMessageInsert | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDeleteBuffers | working | 1000us | 0 | 0 | 1000 | 0 |
| glDeleteFramebuffers | working | 3000us | 0 | 0 | 3000 | 0 |
| glDeleteProgram | slow | 21000us >= 10000us | 0 | 0 | 21000 | 0 |
| glDeleteQueries | slow | 20000us >= 10000us | 0 | 0 | 20000 | 0 |
| glDeleteRenderbuffers | slow | 16000us >= 10000us | 0 | 0 | 16000 | 0 |
| glDeleteSamplers | working | 2000us | 0 | 0 | 2000 | 0 |
| glDeleteShader | slow | 22000us >= 10000us | 0 | 0 | 22000 | 0 |
| glDeleteSync | slow | 22000us >= 10000us | 0 | 0 | 22000 | 0 |
| glDeleteTextures | working | 7000us | 0 | 0 | 7000 | 0 |
| glDeleteVertexArrays | slow | 14000us >= 10000us | 0 | 0 | 14000 | 0 |
| glDepthFunc | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDepthMask | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDepthRange | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDetachShader | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDisable | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDisableClientState | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDisableVertexAttribArray | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDisablei | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDrawArrays | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glDrawArraysInstanced | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glDrawArraysInstancedBaseInstance | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawBuffers | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDrawElements | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glDrawElementsBaseVertex | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glDrawElementsInstanced | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDrawElementsInstancedBaseVertex | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glDrawElementsInstancedBaseVertexBaseInstance | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawRangeElements | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDrawRangeElementsBaseVertex | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glEnable | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glEnableClientState | slow | 33000us >= 10000us | 0 | 0 | 33000 | 1000 |
| glEnableVertexAttribArray | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glEnablei | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glEnd | broken | glGetError=1281 | 1281 | 1281 | 33000 | 0 |
| glEndQuery | broken | glGetError=1280 | 1280 | 1280 | 5000 | 22000 |
| glEndTransformFeedback | broken | glGetError=1282 | 1282 | 1282 | 28000 | 18000 |
| glFenceSync | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFinalCombinerInputNV | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glFinish | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFlush | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFogf | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFogfv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFogi | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFogiv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFramebufferRenderbuffer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFramebufferTexture | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFramebufferTexture1D | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glFramebufferTexture2D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFramebufferTexture3D | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFramebufferTextureLayer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFrontFace | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFrustum | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGenBuffers | slow | 34000us >= 10000us | 0 | 0 | 34000 | 23000 |
| glGenFramebuffers | slow | 17000us >= 10000us | 0 | 0 | 17000 | 4000 |
| glGenQueries | slow | 33000us >= 10000us | 0 | 0 | 33000 | 23000 |
| glGenRenderbuffers | slow | 17000us >= 10000us | 0 | 0 | 17000 | 3000 |
| glGenSamplers | slow | 33000us >= 10000us | 0 | 0 | 33000 | 22000 |
| glGenTextures | slow | 17000us >= 10000us | 0 | 0 | 17000 | 5000 |
| glGenVertexArrays | slow | 33000us >= 10000us | 0 | 0 | 33000 | 20000 |
| glGenerateMipmap | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetActiveUniform | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetActiveUniformBlockName | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetActiveUniformBlockiv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetAttachedShaders | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| glGetAttribLocation | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetBooleani_v | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetBufferParameteriv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetBufferSubData | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetCompressedTexImage | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glGetDebugMessageLog | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetError | slow | 34000us >= 10000us | 0 | 0 | 31000 | 34000 |
| glGetFloatv | broken | glGetError=1280 | 1280 | 1280 | 33000 | 33000 |
| glGetFragDataIndex | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetFramebufferAttachmentParameteriv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetIntegeri_v | broken | glGetError=1280 | 1280 | 1280 | 33000 | 34000 |
| glGetIntegerv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetMultisamplefv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glGetProgramInfoLog | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetProgramiv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetQueryObjectiv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 16000 |
| glGetQueryObjectui64v | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetQueryObjectuiv | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glGetQueryiv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glGetRenderbufferParameteriv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetSamplerParameterIiv | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glGetSamplerParameterIuiv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glGetSamplerParameterfv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetSamplerParameteriv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetShaderInfoLog | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetShaderSource | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetShaderSourceImpl | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetShaderiv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetString | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glGetStringi | slow | 17000us >= 10000us | 0 | 0 | 17000 | 16000 |
| glGetSynciv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glGetTexImage | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glGetTexLevelParameteriv | broken | glGetError=1281 | 1281 | 1281 | 50000 | 50000 |
| glGetTexParameteriv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetTextureLevelParameteriv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetTextureParameteriv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetUniformBlockIndex | slow | 34000us >= 10000us | 0 | 0 | 34000 | 34000 |
| glGetUniformLocation | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetUniformfv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 16000 |
| glGetUniformiv | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glGetUniformuiv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glHint | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glIsEnabledi | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glIsFramebuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glIsQuery | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glIsRenderbuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glIsSampler | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glIsSync | slow | 17000us >= 10000us | 0 | 0 | 17000 | 16000 |
| glLightf | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glLightfv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glLineWidth | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glLinkProgram | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glLoadIdentity | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glLoadMatrixd | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glLoadMatrixf | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glMapBufferRange | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glMaterialf | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glMaterialfv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glMatrixMode | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glMinSampleShading | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glMultMatrixd | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glMultMatrixf | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glMultiDrawElementsBaseVertex | broken | glGetError=1282 | 1282 | 1282 | 66000 | 67000 |
| glMultiTexCoord2fARB | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glNormal3f | slow | 16000us >= 10000us | 0 | 0 | 16000 | 0 |
| glOrtho | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glPixelStorei | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glPointParameterf | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glPointParameterfv | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glPointParameteri | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glPointParameteriv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glPolygonMode | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glPolygonOffset | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glPolygonOffsetClamp | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glPopMatrix | broken | glGetError=1284 | 1284 | 1284 | 33000 | 33000 |
| glPushMatrix | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glQueryCounter | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glReadBuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glReadPixels | broken | glGetError=1280 | 1280 | 1280 | 34000 | 33000 |
| glRenderbufferStorage | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glRenderbufferStorageMultisample | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glRotatef | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glSampleCoverage | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glSamplerParameterIiv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glSamplerParameterIuiv | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glSamplerParameterf | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glSamplerParameterfv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glSamplerParameteri | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glSamplerParameteriv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glScalef | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glScissor | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glShadeModel | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glShaderSource | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glStencilFunc | slow | 35000us >= 10000us | 0 | 0 | 33000 | 35000 |
| glStencilFuncSeparate | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glStencilMask | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glStencilMaskSeparate | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glStencilOp | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glStencilOpSeparate | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexBuffer | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glTexBufferARB | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexBufferRange | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexBufferRangeARB | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexCoordPointer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexEnvf | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexEnvfv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexEnvi | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexEnviv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexImage2D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexImage2DMultisample | broken | glGetError=1280 | 1280 | 1280 | 34000 | 33000 |
| glTexImage3D | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glTexImage3DMultisample | broken | glGetError=1280 | 1280 | 1280 | 33000 | 34000 |
| glTexParameterf | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexParameteri | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexStorage1D | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glTexStorage2D | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glTexStorage2DMultisample | broken | glGetError=1280 | 1280 | 1280 | 33000 | 34000 |
| glTexStorage3D | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glTexStorage3DMultisample | broken | glGetError=1280 | 1280 | 1280 | 33000 | 33000 |
| glTexSubImage2D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexSubImage3D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTextureBarrier | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTextureBarrierNV | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTransformFeedbackVaryings | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTranslatef | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform1f | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform1fv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform1i | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform1iv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform1uiv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform2f | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform2fv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform2i | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform2iv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform2uiv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3f | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3fv | slow | 34000us >= 10000us | 0 | 0 | 29000 | 34000 |
| glUniform3i | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3iv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3uiv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4f | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4fv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4i | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4iv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4uiv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniformBlockBinding | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniformMatrix2fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniformMatrix3fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniformMatrix4fv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glUseProgram | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glValidateProgram | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glVersionMode | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glVertex2f | slow | 16000us >= 10000us | 0 | 0 | 16000 | 0 |
| glVertex3f | slow | 16000us >= 10000us | 0 | 0 | 16000 | 0 |
| glVertexAttrib1d | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glVertexAttrib1dv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glVertexAttrib1f | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib1fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib2d | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib2dv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib2f | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib2fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib3d | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib3dv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib3f | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib3fv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib4d | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib4dv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib4f | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib4fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttribDivisor | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttribI4i | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttribI4iv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glVertexAttribI4ui | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttribI4uiv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glVertexAttribIPointer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttribPointer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexPointer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glViewport | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glWaitSync | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glXChooseFBConfig | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXChooseVisual | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXCopyContext | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXCreateContext | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXCreateContextAttribsARB | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXCreatePbuffer | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXDestroyContext | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXDestroyPbuffer | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetClientString | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetConfig | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetCurrentContext | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetCurrentDrawable | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetCurrentReadDrawable | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetFBConfigAttrib | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetFBConfigs | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetSelectedEvent | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXGetVisualFromFBConfig | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXIsDirect | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXMakeContextCurrent | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXMakeCurrent | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXQueryContext | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXQueryDrawable | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXQueryExtension | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXQueryExtensionsString | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXQueryServerString | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXQueryVersion | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXSelectEvent | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXSwapBuffers | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXSwapIntervalEXT | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXWaitGL | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glXWaitX | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| traceProc | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
