// web/tests/suite/unit/gl-shim-build.test.mjs — guest GL shim provenance.
//
// Why this exists: the libgl64 fog incident (fixed in 215438445) shipped a
// libGL.so.1 whose glFog* exports were 5-byte no-op stubs while libgl64.c
// had real wrappers — the tests of the day asserted source patterns, never
// the binary. The binary/source divergence audit then found the SAME
// disease in the 32-bit shim: the committed libgl32.c had been generated
// from a stale libgl64.c, a stale copy of libgl64_stubs.h inside libgl32/
// shadowed the shared header (same-directory include beats -I), and the
// shipped 32-bit libGL.so.1 exported 18 fixed-function entry points
// (glFog*, client-array pointers, glTexEnv*, matrix loaders, ...) as
// no-op stubs, with 7 fewer exports than the 64-bit shim.
//
// The tie, modelled on build_vulkan_shims.py --check for the Vulkan
// shims: rebuild through the tree's own build scripts in a scratch copy
// and require byte-identical output (generated source AND binary), and
// pin the guest-visible symptom directly on the committed 32-bit binary.

import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const rootfs64 = join(repoRoot, 'tools/rootfs64');

function scratchRootfs() {
  const dir = mkdtempSync(join(tmpdir(), 'gl-shim-build-'));
  const dest = join(dir, 'rootfs64');
  cpSync(join(rootfs64, 'build-libgl32.sh'), join(dest, 'build-libgl32.sh'));
  cpSync(join(rootfs64, 'build-libgl64.sh'), join(dest, 'build-libgl64.sh'));
  cpSync(join(rootfs64, 'libgl64'), join(dest, 'libgl64'), { recursive: true });
  cpSync(join(rootfs64, 'libgl32'), join(dest, 'libgl32'), { recursive: true });
  return { dir, dest };
}

test('libgl32 rebuilds byte-identically from libgl64.c via build-libgl32.sh', () => {
  const { dir, dest } = scratchRootfs();
  try {
    execFileSync('bash', [join(dest, 'build-libgl32.sh')], { stdio: 'pipe' });
    assert.ok(
      readFileSync(join(dest, 'libgl32/libgl32.c')).equals(readFileSync(join(rootfs64, 'libgl32/libgl32.c'))),
      'committed libgl32.c is not the generator output of the committed libgl64.c; rerun tools/rootfs64/build-libgl32.sh and commit both files');
    assert.ok(
      readFileSync(join(dest, 'libgl32/libGL.so.1')).equals(readFileSync(join(rootfs64, 'libgl32/libGL.so.1'))),
      'committed libgl32/libGL.so.1 does not match a fresh build of its source; rerun tools/rootfs64/build-libgl32.sh and commit the binary');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('libgl64 rebuilds byte-identically via build-libgl64.sh', () => {
  const { dir, dest } = scratchRootfs();
  try {
    execFileSync('bash', [join(dest, 'build-libgl64.sh')], { stdio: 'pipe' });
    assert.ok(
      readFileSync(join(dest, 'libgl64/libGL.so.1')).equals(readFileSync(join(rootfs64, 'libgl64/libGL.so.1'))),
      'committed libgl64/libGL.so.1 does not match a fresh build of libgl64.c; rerun tools/rootfs64/build-libgl64.sh and commit the binary');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('libgl32 has no shadowing copy of the shared stubs header', () => {
  // build-libgl32.sh compiles libgl32.c with -I tools/rootfs64/libgl64 so
  // the ONE stubs header is libgl64/libgl64_stubs.h. A same-named file in
  // libgl32/ silently wins the #include (same-directory rule) — that is
  // exactly how the stale stub set survived the libgl64 fog fix.
  assert.ok(!existsSync(join(rootfs64, 'libgl32/libgl64_stubs.h')),
    'libgl32/libgl64_stubs.h shadows the shared header; delete it and let the build use libgl64/libgl64_stubs.h');
});

function elf32FunctionSizes(bytes) {
  assert.equal(bytes.subarray(0, 4).toString('hex'), '7f454c46');
  assert.equal(bytes[4], 1, '32-bit guest shim');
  assert.equal(bytes[5], 1, 'little-endian guest shim');
  assert.equal(bytes.readUInt16LE(18), 3, 'i386 guest machine');
  const sections = bytes.readUInt32LE(32);
  const stride = bytes.readUInt16LE(46), count = bytes.readUInt16LE(48);
  const result = new Map();
  for (let i = 0; i < count; i++) {
    const section = sections + i * stride;
    if (bytes.readUInt32LE(section + 4) !== 11) continue; // SHT_DYNSYM
    const start = bytes.readUInt32LE(section + 16);
    const size = bytes.readUInt32LE(section + 20);
    const entrySize = bytes.readUInt32LE(section + 36);
    const strings = sections + bytes.readUInt32LE(section + 24) * stride;
    const stringStart = bytes.readUInt32LE(strings + 16);
    for (let symbol = start; symbol < start + size; symbol += entrySize) {
      if ((bytes[symbol + 12] & 15) !== 2) continue; // STT_FUNC
      if (bytes.readUInt16LE(symbol + 14) === 0) continue; // SHN_UNDEF import
      const nameStart = stringStart + bytes.readUInt32LE(symbol);
      const name = bytes.toString('utf8', nameStart, bytes.indexOf(0, nameStart));
      result.set(name, bytes.readUInt32LE(symbol + 8));
    }
  }
  return result;
}

function elf64FunctionNames(bytes) {
  assert.equal(bytes[4], 2, '64-bit guest shim');
  const sections = Number(bytes.readBigUInt64LE(40));
  const stride = bytes.readUInt16LE(58), count = bytes.readUInt16LE(60);
  const names = new Set();
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
      if (bytes.readUInt16LE(symbol + 6) === 0) continue; // SHN_UNDEF import
      const nameStart = stringStart + bytes.readUInt32LE(symbol);
      names.add(bytes.toString('utf8', nameStart, bytes.indexOf(0, nameStart)));
    }
  }
  return names;
}

test('shipped libgl32 exports real wrappers, not stubs, for the fixed-function set', () => {
  const sizes = elf32FunctionSizes(readFileSync(join(rootfs64, 'libgl32/libGL.so.1')));
  // The exact set found as 5-byte no-op stubs in the shipped binary by the
  // divergence audit (all are real trap wrappers in libgl64). A GLSTUB is
  // 5 bytes (xor eax,eax; ret); a real wrapper marshals args and traps.
  const watchlist = [
    'glAlphaFunc', 'glColor4ub', 'glColorPointer', 'glDisableClientState',
    'glEnableClientState', 'glFogf', 'glFogfv', 'glFogi', 'glFogiv',
    'glLoadMatrixd', 'glLoadMatrixf', 'glMultMatrixd', 'glTexCoordPointer',
    'glTexEnvf', 'glTexEnvfv', 'glTexEnvi', 'glTexEnviv', 'glVertexPointer',
  ];
  for (const name of watchlist) {
    assert.ok(sizes.get(name) > 8,
      `${name} is missing or a no-op stub in the bundled 32-bit guest libGL; rebuild tools/rootfs64/libgl32/libGL.so.1`);
  }
  // Both shims are built from the same source (libgl32.c is generated from
  // libgl64.c), so their export sets must be identical.
  const exports32 = new Set(sizes.keys());
  const exports64 = elf64FunctionNames(readFileSync(join(rootfs64, 'libgl64/libGL.so.1')));
  assert.deepEqual([...exports32].sort(), [...exports64].sort(),
    'libgl32 and libgl64 export different symbol sets; the 32-bit shim has drifted from the shared source');
});
