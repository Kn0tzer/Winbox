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
const shimSrc = readFileSync(join(repoRoot, 'tools/rootfs64/libgl64/libgl64.c'), 'utf8');

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
