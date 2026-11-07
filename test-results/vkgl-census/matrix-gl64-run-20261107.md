# GL64 Bridge Coverage Matrix

Source: glcov in-browser probe (334 handlers tested).
Slow threshold: 10000us (see METHOD.md).

## Four-bucket summary

| Bucket | Count |
|---|---|
| 1. Total handlers tested | 334 |
| 2. Fully working | 4 |
| 3. Slow or flaky | 203 |
| 4. Broken or stub | 127 |

## Per-handler table

| Handler | Bucket | Detail | err1 | err2 | us1 | us2 |
|---|---|---|---|---|---|---|
| glActiveTexture | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glAlphaFunc | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glAttachShader | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glBegin | slow | 17000us >= 10000us | 0 | 0 | 17000 | 0 |
| glBeginQuery | broken | glGetError=1280 | 1280 | 1280 | 34000 | 22000 |
| glBeginTransformFeedback | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBindAttribLocation | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindBuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindBufferBase | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindBufferRange | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindFragDataLocation | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBindFramebuffer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glBindRenderbuffer | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glBindSampler | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindTexture | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBindVertexArray | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlendColor | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBlendEquation | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBlendEquationSeparate | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glBlendEquationSeparatei | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlendEquationi | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlendFunc | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBlendFuncSeparate | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glBlendFuncSeparatei | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlendFunci | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBlitFramebuffer | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glBufferData | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glBufferStorage | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glBufferSubData | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glCheckFramebufferStatus | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glClear | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glClearBufferfi | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glClearBufferfv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glClearBufferiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glClearBufferuiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glClearColor | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glClearDepth | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glClientActiveTextureARB | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glClientWaitSync | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glColor3f | slow | 17000us >= 10000us | 0 | 0 | 17000 | 0 |
| glColor4f | slow | 17000us >= 10000us | 0 | 0 | 17000 | 0 |
| glColor4ub | slow | 33000us >= 10000us | 0 | 0 | 32000 | 33000 |
| glColorMask | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glColorMaski | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glColorMaterial | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glColorPointer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glCompileShader | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glCompressedTexImage2D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glCompressedTexImage3D | flaky | err1=37442 err2=0 | 37442 | 0 | 201000 | 32000 |
| glCompressedTexSubImage2D | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glCompressedTexSubImage3D | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glCopyBufferSubData | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glCreateProgram | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glCreateShader | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glCullFace | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glDebugMessageCallback | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDebugMessageControl | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDebugMessageInsert | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDeleteBuffers | working | 4000us | 0 | 0 | 4000 | 0 |
| glDeleteFramebuffers | working | 5000us | 0 | 0 | 5000 | 0 |
| glDeleteProgram | slow | 21000us >= 10000us | 0 | 0 | 21000 | 0 |
| glDeleteQueries | slow | 22000us >= 10000us | 0 | 0 | 22000 | 0 |
| glDeleteRenderbuffers | working | 2000us | 0 | 0 | 2000 | 0 |
| glDeleteSamplers | slow | 12000us >= 10000us | 0 | 0 | 12000 | 0 |
| glDeleteShader | slow | 21000us >= 10000us | 0 | 0 | 21000 | 0 |
| glDeleteSync | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDeleteTextures | working | 8000us | 0 | 0 | 8000 | 0 |
| glDeleteVertexArrays | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDepthFunc | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDepthMask | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDepthRange | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDetachShader | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDisable | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDisableClientState | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDisableVertexAttribArray | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDisablei | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawArrays | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glDrawArraysInstanced | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawArraysInstancedBaseInstance | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawBuffers | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDrawElements | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDrawElementsBaseVertex | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawElementsInstanced | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawElementsInstancedBaseVertex | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawElementsInstancedBaseVertexBaseInstance | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glDrawRangeElements | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glDrawRangeElementsBaseVertex | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glEnable | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glEnableClientState | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glEnableVertexAttribArray | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glEnablei | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glEnd | broken | glGetError=1281 | 1281 | 1281 | 34000 | 0 |
| glEndQuery | broken | glGetError=1280 | 1280 | 1280 | 23000 | 21000 |
| glEndTransformFeedback | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glFenceSync | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glFinalCombinerInputNV | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glFinish | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFlush | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFogf | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFogfv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glFogi | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFogiv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFramebufferRenderbuffer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFramebufferTexture | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glFramebufferTexture1D | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glFramebufferTexture2D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFramebufferTexture3D | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFramebufferTextureLayer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glFrontFace | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glFrustum | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGenBuffers | slow | 34000us >= 10000us | 0 | 0 | 34000 | 23000 |
| glGenFramebuffers | slow | 17000us >= 10000us | 0 | 0 | 17000 | 4000 |
| glGenQueries | slow | 33000us >= 10000us | 0 | 0 | 33000 | 22000 |
| glGenRenderbuffers | slow | 17000us >= 10000us | 0 | 0 | 17000 | 2000 |
| glGenSamplers | slow | 33000us >= 10000us | 0 | 0 | 33000 | 22000 |
| glGenTextures | slow | 17000us >= 10000us | 0 | 0 | 17000 | 5000 |
| glGenVertexArrays | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGenerateMipmap | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetActiveUniform | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetActiveUniformBlockName | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetActiveUniformBlockiv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetAttachedShaders | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| glGetAttribLocation | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetBooleani_v | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetBufferParameteriv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetBufferSubData | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetCompressedTexImage | slow | 16000us >= 10000us | 0 | 0 | 16000 | 16000 |
| glGetDebugMessageLog | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetError | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetFloatv | broken | glGetError=1280 | 1280 | 1280 | 34000 | 33000 |
| glGetFragDataIndex | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetFramebufferAttachmentParameteriv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetIntegeri_v | broken | glGetError=1280 | 1280 | 1280 | 34000 | 33000 |
| glGetIntegerv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetMultisamplefv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetProgramInfoLog | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetProgramiv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetQueryObjectiv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glGetQueryObjectui64v | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetQueryObjectuiv | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glGetQueryiv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glGetRenderbufferParameteriv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetSamplerParameterIiv | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glGetSamplerParameterIuiv | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glGetSamplerParameterfv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glGetSamplerParameteriv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetShaderInfoLog | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetShaderSource | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetShaderSourceImpl | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetShaderiv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetString | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glGetStringi | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetSynciv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetTexImage | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glGetTexLevelParameteriv | broken | glGetError=1281 | 1281 | 1281 | 50000 | 50000 |
| glGetTexParameteriv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glGetTextureLevelParameteriv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetTextureParameteriv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glGetUniformBlockIndex | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetUniformLocation | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glGetUniformfv | slow | 17000us >= 10000us | 0 | 0 | 16000 | 17000 |
| glGetUniformiv | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glGetUniformuiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glHint | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glIsEnabledi | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glIsFramebuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glIsQuery | slow | 17000us >= 10000us | 0 | 0 | 17000 | 17000 |
| glIsRenderbuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glIsSampler | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glIsSync | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glLightf | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glLightfv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glLineWidth | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glLinkProgram | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glLoadIdentity | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glLoadMatrixd | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glLoadMatrixf | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glMapBufferRange | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glMaterialf | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glMaterialfv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glMatrixMode | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glMinSampleShading | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glMultMatrixd | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glMultMatrixf | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glMultiDrawElementsBaseVertex | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glMultiTexCoord2fARB | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glNormal3f | slow | 17000us >= 10000us | 0 | 0 | 17000 | 0 |
| glOrtho | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glPixelStorei | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glPointParameterf | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glPointParameterfv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glPointParameteri | broken | glGetError=1282 | 1282 | 1282 | 34000 | 33000 |
| glPointParameteriv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 34000 |
| glPolygonMode | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glPolygonOffset | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glPolygonOffsetClamp | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glPopMatrix | broken | glGetError=1284 | 1284 | 1284 | 33000 | 33000 |
| glPushMatrix | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glQueryCounter | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glReadBuffer | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glReadPixels | broken | glGetError=1280 | 1280 | 1280 | 33000 | 33000 |
| glRenderbufferStorage | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glRenderbufferStorageMultisample | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glRotatef | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glSampleCoverage | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glSamplerParameterIiv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glSamplerParameterIuiv | broken | glGetError=1282 | 1282 | 1282 | 33000 | 33000 |
| glSamplerParameterf | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glSamplerParameterfv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glSamplerParameteri | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glSamplerParameteriv | slow | 35000us >= 10000us | 0 | 0 | 35000 | 32000 |
| glScalef | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glScissor | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glShadeModel | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glShaderSource | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glStencilFunc | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glStencilFuncSeparate | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glStencilMask | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glStencilMaskSeparate | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glStencilOp | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glStencilOpSeparate | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexBuffer | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexBufferARB | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexBufferRange | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexBufferRangeARB | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexCoordPointer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexEnvf | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexEnvfv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexEnvi | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexEnviv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexImage2D | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexImage2DMultisample | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexImage3D | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexImage3DMultisample | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTexParameterf | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glTexParameteri | slow | 34000us >= 10000us | 0 | 0 | 29000 | 34000 |
| glTexStorage1D | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glTexStorage2D | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glTexStorage2DMultisample | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| glTexStorage3D | slow | 50000us >= 10000us | 0 | 0 | 50000 | 50000 |
| glTexStorage3DMultisample | crashed | guest crashed on this call (TRY line is the repro); skipped after recording | -2 | -2 | 0 | 0 |
| glTexSubImage2D | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTexSubImage3D | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glTextureBarrier | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTextureBarrierNV | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTransformFeedbackVaryings | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glTranslatef | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform1f | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform1fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform1i | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform1iv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform1uiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glUniform2f | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform2fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform2i | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform2iv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniform2uiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glUniform3f | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3fv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3i | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3iv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform3uiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glUniform4f | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4fv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4i | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4iv | slow | 34000us >= 10000us | 0 | 0 | 33000 | 34000 |
| glUniform4uiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glUniformBlockBinding | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glUniformMatrix2fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniformMatrix3fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUniformMatrix4fv | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glUseProgram | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glValidateProgram | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVersionMode | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glVertex2f | slow | 17000us >= 10000us | 0 | 0 | 17000 | 0 |
| glVertex3f | slow | 17000us >= 10000us | 0 | 0 | 17000 | 0 |
| glVertexAttrib1d | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib1dv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib1f | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib1fv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib2d | slow | 33000us >= 10000us | 0 | 0 | 33000 | 33000 |
| glVertexAttrib2dv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib2f | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib2fv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib3d | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib3dv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib3f | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib3fv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib4d | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib4dv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib4f | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttrib4fv | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttribDivisor | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexAttribI4i | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glVertexAttribI4iv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glVertexAttribI4ui | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glVertexAttribI4uiv | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glVertexAttribIPointer | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
| glVertexAttribPointer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glVertexPointer | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glViewport | slow | 34000us >= 10000us | 0 | 0 | 34000 | 33000 |
| glWaitSync | stub | proc address missing: not exported/implemented | -1 | -1 | 0 | 0 |
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
