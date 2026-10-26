// web/tests/suite/unit/winmm32-build.test.mjs — guest winmm32 shim provenance.
//
// The binary/source divergence audit found tools/winmm32/winmm.dll shipped
// with NO recorded build recipe and no pin: its export set happened to match
// a rebuild, but nothing would have failed if it drifted. The recipe is now
// recorded in tools/winmm32/build-winmm.sh (recovered by measurement: the
// committed binary is a default-flags i686-w64-mingw32-gcc build, reproduced
// bit-for-bit except the PE build-time fields). This file is the tie,
// modelled on gl-shim-build.test.mjs: the committed binary's export set is
// pinned directly, and the script's --check (scratch rebuild; export-set
// compare + byte compare masked over the COFF/export TimeDateStamps and the
// PE CheckSum) must pass on the tree, leave it untouched, and reject both a
// source copy that lost an export and a one-byte-diverged binary copy.

import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const winmmDir = join(repoRoot, 'tools/winmm32');

function scratchWinmm() {
  const dir = mkdtempSync(join(tmpdir(), 'winmm32-build-'));
  const dest = join(dir, 'winmm32');
  for (const f of ['build-winmm.sh', 'winmm.c', 'winmm.dll'])
    cpSync(join(winmmDir, f), join(dest, f));
  return { dir, dest };
}

function pe32Sections(bytes) {
  assert.equal(bytes.toString('latin1', 0, 2), 'MZ');
  const pe = bytes.readUInt32LE(0x3c);
  assert.equal(bytes.toString('latin1', pe, pe + 4), 'PE\0\0');
  const count = bytes.readUInt16LE(pe + 6);
  const opt = pe + 24;
  const optSize = bytes.readUInt16LE(pe + 20);
  const sections = [];
  for (let i = 0; i < count; i++) {
    const s = opt + optSize + i * 40;
    sections.push({
      va: bytes.readUInt32LE(s + 12), vsz: bytes.readUInt32LE(s + 8),
      roff: bytes.readUInt32LE(s + 20), rsz: bytes.readUInt32LE(s + 16),
    });
  }
  return { opt, sections };
}

function rvaToOffset({ sections }, rva) {
  for (const s of sections)
    if (rva >= s.va && rva < s.va + Math.max(s.vsz, s.rsz)) return s.roff + (rva - s.va);
  throw new Error(`RVA ${rva} not in any section`);
}

function peExports(bytes) {
  const parsed = pe32Sections(bytes);
  assert.equal(bytes.readUInt16LE(parsed.opt), 0x10b, 'winmm.dll must be PE32');
  const dd = parsed.opt + 96; // data directories (PE32)
  const eo = rvaToOffset(parsed, bytes.readUInt32LE(dd)); // exports = directory 0
  const n = bytes.readUInt32LE(eo + 24);
  const namesOff = rvaToOffset(parsed, bytes.readUInt32LE(eo + 32));
  const out = [];
  for (let i = 0; i < n; i++) {
    const o = rvaToOffset(parsed, bytes.readUInt32LE(namesOff + i * 4));
    out.push(bytes.toString('utf8', o, bytes.indexOf(0, o)));
  }
  return out.sort();
}

// The whole guest-visible surface of the shim: the 11 waveOut/time entry
// points winmm.c defines with __declspec(dllexport), under the stdcall-
// decorated names the linker actually exports (name@stack-bytes).
const PINNED_EXPORTS = [
  'timeGetTime@0', 'waveOutClose@4', 'waveOutGetDevCapsA@12', 'waveOutGetNumDevs@0',
  'waveOutOpen@24', 'waveOutPause@4', 'waveOutPrepareHeader@12', 'waveOutReset@4',
  'waveOutRestart@4', 'waveOutUnprepareHeader@12', 'waveOutWrite@12',
];

test('committed winmm.dll exports exactly the pinned set', () => {
  assert.deepEqual(peExports(readFileSync(join(winmmDir, 'winmm.dll'))), PINNED_EXPORTS,
    'tools/winmm32/winmm.dll export set drifted; rebuild via tools/winmm32/build-winmm.sh or re-pin deliberately');
});

test('build-winmm.sh --check passes on the committed tree and leaves it untouched', () => {
  const artifacts = [join(winmmDir, 'winmm.dll'), join(winmmDir, 'winmm.c')];
  const before = artifacts.map((f) => readFileSync(f));
  execFileSync('bash', [join(winmmDir, 'build-winmm.sh'), '--check'], { stdio: 'pipe' });
  artifacts.forEach((f, i) => assert.ok(readFileSync(f).equals(before[i]),
    `--check modified the committed ${f}; the check path must build into a scratch dir only`));
});

test('build-winmm.sh --check rejects a source copy that lost an export', () => {
  const { dir, dest } = scratchWinmm();
  try {
    const src = join(dest, 'winmm.c');
    const text = readFileSync(src, 'utf8');
    assert.ok(text.includes('MMRESULT WINAPI waveOutReset('), 'sabotage anchor missing from winmm.c');
    writeFileSync(src, text.replace('MMRESULT WINAPI waveOutReset(', 'MMRESULT WINAPI waveOutResetGone('));
    assert.throws(
      () => execFileSync('bash', [join(dest, 'build-winmm.sh'), '--check'], { stdio: 'pipe' }),
      'a winmm.c copy missing the waveOutReset export must fail --check');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('build-winmm.sh --check rejects a one-byte-diverged binary copy', () => {
  const { dir, dest } = scratchWinmm();
  try {
    const binary = join(dest, 'winmm.dll');
    const bytes = readFileSync(binary);
    // .text raw data starts at file offset 0x600 in this DLL; a byte there is
    // outside every masked build-time field (136.., 216.., 12292..).
    bytes[0x610] ^= 0xff;
    writeFileSync(binary, bytes);
    assert.throws(
      () => execFileSync('bash', [join(dest, 'build-winmm.sh'), '--check'], { stdio: 'pipe' }),
      'a one-byte-diverged winmm.dll copy must fail --check');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
