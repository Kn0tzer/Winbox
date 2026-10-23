// web/tests/suite/unit/gl-shim-coverage.test.mjs — OpenGL bridge coverage.
//
// Verifies EVERY function in the GL ABI has:
//   (a) a shim entry in g_procs[] (E(name) format)
//   (b) a bridge case handler in gl64bridge.cpp
//
// The GL bridge was verified working for GL 2.1 immediate mode (64-bit
// triangle). This test ensures the full registered surface is wired.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const abiSrc = readFileSync(join(repoRoot, 'source/opengl/gl64bridge_abi.h'), 'utf8');
const bridgeSrc = readFileSync(join(repoRoot, 'source/opengl/gl64bridge.cpp'), 'utf8');
const glCommonSrc = readFileSync(join(repoRoot, 'source/opengl/glcommon.cpp'), 'utf8');
const shimSrc = readFileSync(join(repoRoot, 'tools/rootfs64/libgl64/libgl64.c'), 'utf8');
const esOpenGlSrc = readFileSync(join(repoRoot, 'source/opengl/es/esopengl.c'), 'utf8');
const glMarshalSrc = readFileSync(join(repoRoot, 'source/opengl/glMarshal.cpp'), 'utf8');
const glMarshalSizeSrc = readFileSync(join(repoRoot, 'source/opengl/glMarshalSize.cpp'), 'utf8');
const webGpuGlSrc = readFileSync(join(repoRoot, 'web/gl-webgpu.mjs'), 'utf8');

function getAbiFunctions(src) {
  const ids = [];
  const seen = new Set();
  for (const m of src.matchAll(/^\s*(GL64_fn_\w+)\s*(?:=\s*\d+)?\s*,/gm)) {
    if (!seen.has(m[1])) { seen.add(m[1]); ids.push(m[1]); }
  }
  return ids;
}

function getBridgeHandlers(src) {
  return new Set([...src.matchAll(/case\s+(GL64_fn_\w+)\s*:/g)].map(m => m[1]));
}

function getShimRegistered(src) {
  // GL shim uses E(name) single-arg format
  const m = src.match(/static const struct procEntry g_procs\[\] = \{([\s\S]*?)\n\};/);
  if (!m) throw new Error('g_procs[] not found in GL shim');
  const names = new Set();
  for (const mm of m[1].matchAll(/E\((gl\w+)\)/g)) {
    // Map glName -> GL64_fn_glName (ABI uses GL64_fn_ prefix)
    names.add('GL64_fn_' + mm[1]);
  }
  return names;
}

const abiFns = getAbiFunctions(abiSrc);
const handlers = getBridgeHandlers(bridgeSrc);
const registered = getShimRegistered(shimSrc);

test('GL ABI functions all have bridge handlers', () => {
  const missing = abiFns.filter(f => !handlers.has(f));
  assert.deepEqual(missing, [],
    `GL ABI functions without bridge handlers: ${missing.join(', ')}`);
});

test('GL ABI count matches bridge handler coverage', () => {
  // Every ABI function should have exactly one bridge handler
  assert.ok(abiFns.length > 0, 'ABI must define functions');
  assert.ok(handlers.size > 0, 'Bridge must have handlers');
  const coverage = abiFns.filter(f => handlers.has(f)).length;
  assert.equal(coverage, abiFns.length,
    `Bridge covers ${coverage}/${abiFns.length} ABI functions`);
});

test('framebuffer generation writes guest-visible names', () => {
  assert.match(bridgeSrc, /case GL64_fn_glGenFramebuffers:\s*if \(g_glContext && args\.a\[1\]\) genObjects\(cpu, args, GenKind::Framebuffer\)/);
  assert.match(bridgeSrc, /kind == GenKind::Texture \|\| kind == GenKind::Framebuffer \|\|\s*kind == GenKind::Renderbuffer[\s\S]{0,220}memcpyToGuest\(args\.a\[1\], ids\.data\(\), \(U64\)n \* 4\)/);
});

test('shader and program deletion follow OpenGL object lifetime semantics', () => {
  assert.match(bridgeSrc, /case GL64_fn_glDeleteShader:\s*if \(g_glContext\) GL_MT\(glDeleteShader\(\(GLuint\)ai\(args,0\)\)\)/);
  assert.match(bridgeSrc, /case GL64_fn_glDeleteProgram:\s*if \(g_glContext\) GL_MT\(glDeleteProgram\(\(GLuint\)ai\(args,0\)\)\)/);
});

test('framebufferTexture host exceptions log once and latch GL_INVALID_OPERATION', () => {
  const body = /case GL64_fn_glFramebufferTexture:([\s\S]*?)case GL64_fn_glFramebufferTextureLayer:/.exec(bridgeSrc)?.[1] || '';
  assert.match(body, /catch \(e\)/);
  assert.match(body, /__bw64glBridgeErrors\.has\('glFramebufferTexture'\)/);
  assert.match(body, /gl64 glFramebufferTexture failed:/);
  assert.match(body, /GLctx\._recordError\(0x0502\)/);
  assert.doesNotMatch(body, /catch \(e\) \{\s*\}/);
});

test('invalid pixel formats return to GL validation instead of panicking', () => {
  assert.match(glMarshalSrc, /glPixelMarshal: invalid format\/type combination/);
  assert.match(glMarshalSrc, /invalidPixelMarshalWarning\.test_and_set\(\)/);
  assert.doesNotMatch(glMarshalSrc, /kpanic_fmt\("getPixelsLen unsupported format\/type/);
  assert.match(glMarshalSizeSrc, /glPixelMarshal: invalid pixel format/);
  assert.match(glMarshalSizeSrc, /invalidPixelFormatWarning\.test_and_set\(\)/);
  assert.doesNotMatch(glMarshalSizeSrc, /kpanic_fmt\("Opengl components_in_format/);
});

test('empty WebGPUGL methods stay in the audited no-op inventory', () => {
  const emptyMethods = [...webGpuGlSrc.matchAll(/^  ([A-Za-z_$][\w$]*)\([^\n]*\) \{\s*\}/gm)]
    .map((match) => match[1]).sort();
  assert.deepEqual(emptyMethods, [
    'sampleCoverage', 'shadeModel',
  ].sort(), 'any new empty WebGPUGL method needs an explicit audit disposition');
});

test('native query bridge tracks completion and never reports fabricated nonzero samples', () => {
  assert.match(bridgeSrc, /g_glActiveQueries\[target\]\s*=\s*id/);
  assert.match(bridgeSrc, /query->second\.available\s*\?\s*1\s*:\s*0/);
  assert.doesNotMatch(bridgeSrc, /GL_QUERY_RESULT\s*\*\/\)\s*v\s*=\s*1/);
});

test('native transform-feedback calls are forwarded to the loud WebGPUGL rejection', () => {
  assert.match(bridgeSrc, /case GL64_fn_glBeginTransformFeedback:\s*if \(g_glContext\) GL_MT\(glBeginTransformFeedback/);
  assert.match(bridgeSrc, /case GL64_fn_glEndTransformFeedback:\s*if \(g_glContext\) GL_MT\(glEndTransformFeedback/);
  assert.match(webGpuGlSrc, /beginTransformFeedback\(\) \{ this\._rejectUnsupported\('transform feedback', GL\.INVALID_OPERATION\); \}/);
  assert.doesNotMatch(bridgeSrc, /case GL64_fn_glBeginTransformFeedback:\s*case GL64_fn_glEndTransformFeedback:/);
});

test('polygon mode reaches WebGPUGL conversion instead of being dropped by the bridge', () => {
  assert.match(bridgeSrc, /case GL64_fn_glPolygonMode:\s*if \(g_glContext\) \{\s*if \(g_emWebGpu\) GL_MT\(webGpuFixedCall\("polygonMode", ai\(args,0\), ai\(args,1\)/);
  assert.match(webGpuGlSrc, /polygonMode\(face, mode\) \{/);
});

test('RGBA-only ES visuals do not panic on color-index operations with no RGBA effect', () => {
  assert.match(glCommonSrc, /case GLX_RENDER_TYPE:[\s\S]{0,220}GLX_RGBA_BIT/);
  for (const name of ['es_glClearIndex', 'es_glIndexMask']) {
    const body = new RegExp(`void ${name}\\s*\\([^)]*\\)\\s*\\{([^}]*)\\}`).exec(esOpenGlSrc)?.[1] || '';
    assert.match(body, /\(void\)/, `${name} should be a documented RGBA no-op`);
    assert.doesNotMatch(body, /kpanic/);
  }
});

test('ES compatibility stubs reject loudly and preserve sticky GL error semantics', () => {
  const rejected = ['es_glAlphaFunc', 'es_glLogicOp', 'es_glPointSize', 'es_glPolygonStipple',
    'es_glGetPolygonStipple', 'es_glClipPlane', 'es_glGetClipPlane', 'es_glDrawBuffer', 'es_glReadBuffer'];
  assert.match(esOpenGlSrc, /static GLenum esShimError = GL_NO_ERROR/);
  assert.match(esOpenGlSrc, /if \(esShimError == GL_NO_ERROR\) esShimError = error/);
  assert.match(esOpenGlSrc, /OpenGL ES compatibility: %s is unsupported \(GL_INVALID_OPERATION\)/);
  assert.match(esOpenGlSrc, /strcmp\(warnedFunctions\[i\], functionName\) == 0/);
  assert.match(esOpenGlSrc, /GLenum es_glGetError\( void \)[\s\S]*?return glGetError\(\)/);
  for (const name of rejected) {
    const body = new RegExp(`void ${name}\\s*\\([^)]*\\)\\s*\\{([^}]*)\\}`).exec(esOpenGlSrc)?.[1] || '';
    assert.ok(body.includes('esRejectCompatCall') || (name === 'es_glPointSize' && /es_glPointSize[\s\S]*?esRejectCompatCall/.test(esOpenGlSrc)), `${name} must report unsupported behavior`);
    assert.doesNotMatch(body, /kpanic/);
  }
  for (const name of ['es_glLogicOp', 'es_glPolygonStipple', 'es_glGetPolygonStipple', 'es_glClipPlane', 'es_glGetClipPlane'])
    assert.match(esOpenGlSrc, new RegExp(`${name}[\\s\\S]{0,140}esRejectCompatCall`));
  assert.match(esOpenGlSrc, /void es_glPolygonMode[\s\S]*?if \(mode != GL_FILL\) esRejectCompatCall/);
  for (const name of ['es_glEdgeFlag', 'es_glEdgeFlagv']) {
    const body = new RegExp(`void ${name}\\s*\\([^)]*\\)\\s*\\{([^}]*)\\}`).exec(esOpenGlSrc)?.[1] || '';
    assert.doesNotMatch(body, /kpanic|esRejectCompatCall/);
  }
});

test('glshim TODO fields are dead scaffolding and do not gate active shim behavior', () => {
  const shimSrc = readFileSync(join(repoRoot, 'source/opengl/es/glshim.c'), 'utf8');
  assert.match(shimSrc, /\/\/ TODO: dynamic type support\?[\s\S]{0,150}\/\*[\s\S]*?\*\//);
  assert.equal([...shimSrc.matchAll(/\bblock_t\b/g)].length, 3,
    'block_t should remain declaration-only until an immediate-mode block path is enabled');
  assert.equal([...shimSrc.matchAll(/\brect_arb\b/g)].length, 1,
    'rect_arb is only a declaration, not active target selection');
  assert.match(shimSrc, /void shim_glEnable\(GLenum cap\)[\s\S]*?case GL_TEXTURE_2D:/);
  assert.match(shimSrc, /void shim_glDisable\(GLenum cap\)[\s\S]*?case GL_TEXTURE_2D:/);
  assert.match(shimSrc, /void glLineStipple\(GLint factor, GLushort pattern\)/);
});

test('GLX pixmap/window attributes reject once and return None instead of panicking', () => {
  for (const [name, warning] of [['gl_common_XCreatePixmap', 'glXCreatePixmap: unsupported attrib_list'], ['gl_common_XCreateWindow', 'glXCreateWindow: unsupported attrib_list']]) {
    const body = new RegExp(`void ${name}\\s*\\([^)]*\\)\\s*\\{([\\s\\S]*?)\\n\\}`).exec(glCommonSrc)?.[1] || '';
    assert.ok(body.includes(warning), `${name} must identify unsupported attributes`);
    assert.match(body, /attribWarning\.test_and_set\(\)/, `${name} warning must be rate-limited`);
    assert.match(body, /EAX\s*=\s*0;/, `${name} returns None on unsupported attributes`);
    assert.doesNotMatch(body, /kpanic/);
  }
});

test('core GL 2.1 immediate-mode functions are shim-registered', () => {
  // These are the functions the verified GL triangle uses
  const core = [
    'GL64_fn_glClear', 'GL64_fn_glClearColor', 'GL64_fn_glViewport',
    'GL64_fn_glBegin', 'GL64_fn_glEnd', 'GL64_fn_glVertex3f',
    'GL64_fn_glColor3f', 'GL64_fn_glFlush',
    'GL64_fn_glXChooseVisual', 'GL64_fn_glXCreateContext',
    'GL64_fn_glXMakeCurrent', 'GL64_fn_glXSwapBuffers',
  ];
  for (const fn of core) {
    assert.ok(registered.has(fn) || handlers.has(fn),
      `${fn} must be shim-registered or bridge-handled (GL triangle dependency)`);
  }
});

test('ARB multitexture entry points route through dedicated bridge ABI handlers', () => {
  for (const name of ['glMultiTexCoord2fARB', 'glClientActiveTextureARB']) {
    assert.ok(registered.has(`GL64_fn_${name}`), `${name} must resolve to a guest shim wrapper`);
    assert.match(shimSrc, new RegExp(`API void ${name}\\(`), `${name} must be exported by libgl64`);
    assert.ok(handlers.has(`GL64_fn_${name}`), `${name} must have a host bridge implementation`);
  }
  for (const source of [abiSrc, shimSrc]) {
    assert.match(source, /GL64_fn_glMultiTexCoord2fARB\s*=\s*884,[^\n]*\n\s*GL64_fn_glClientActiveTextureARB/,
      'the shared ABI and guest shim must assign the same consecutive IDs');
  }
});

test('GL shim registration count is substantial', () => {
  // The shim should register hundreds of functions, not just a handful
  assert.ok(registered.size >= 100,
    `GL shim registers only ${registered.size} functions; expected 100+ for real GL apps`);
});

// Source coverage alone missed a shipped libGL whose client-array exports were
// still one-byte return instructions. Inspect ELF64 dynamic symbols as well.
function elf64FunctionSizes(bytes) {
  assert.equal(bytes.subarray(0, 4).toString('hex'), '7f454c46');
  assert.equal(bytes[4], 2, '64-bit guest shim');
  assert.equal(bytes[5], 1, 'little-endian guest shim');
  assert.equal(bytes.readUInt16LE(18), 62, 'x86-64 guest machine');
  const sections = Number(bytes.readBigUInt64LE(40));
  const stride = bytes.readUInt16LE(58), count = bytes.readUInt16LE(60);
  const result = new Map();
  for (let i = 0; i < count; i++) {
    const section = sections + i * stride;
    if (bytes.readUInt32LE(section + 4) !== 11) continue; // SHT_DYNSYM
    const start = Number(bytes.readBigUInt64LE(section + 24));
    const size = Number(bytes.readBigUInt64LE(section + 32));
    const entrySize = Number(bytes.readBigUInt64LE(section + 56));
    const strings = sections + bytes.readUInt32LE(section + 40) * stride;
    const stringStart = Number(bytes.readBigUInt64LE(strings + 24));
    for (let symbol = start; symbol < start + size; symbol += entrySize) {
      if ((bytes[symbol + 4] & 15) !== 2) continue; // STT_FUNC
      const nameStart = stringStart + bytes.readUInt32LE(symbol);
      const name = bytes.toString('utf8', nameStart, bytes.indexOf(0, nameStart));
      result.set(name, Number(bytes.readBigUInt64LE(symbol + 16)));
    }
  }
  return result;
}

test('bundled 64-bit GL client-array entry points contain real trap wrappers', () => {
  const sizes = elf64FunctionSizes(readFileSync(process.env.BW64_GL_SHIM_BINARY || join(repoRoot, 'tools/rootfs64/libgl64/libGL.so.1')));
  for (const name of ['glEnableClientState', 'glDisableClientState', 'glVertexPointer', 'glColorPointer', 'glTexCoordPointer', 'glAlphaFunc', 'glTexEnvi', 'glLoadMatrixf', 'glLoadMatrixd', 'glMultMatrixd', 'glColor4ub', 'glActiveTextureARB']) {
    assert.ok(sizes.get(name) > 8, `${name} is missing or a no-op in the bundled guest binary; rebuild tools/rootfs64/libgl64/libGL.so.1`);
  }
});
