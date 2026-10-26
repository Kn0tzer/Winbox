// web/tests/suite/unit/games-imports.test.mjs — bundled-games import pin.
//
// The binary/source divergence audit verified the four bundled games
// (tools/rootfs64/games/{snake,tetris,clipset,clipget}.exe, built from
// src/*.c by build-games.sh) have import tables matching their sources,
// but nothing pinned them: a rebuild with the wrong CRT, a source change
// that pulls in a new Win32 API, or a swapped binary would ship silently —
// and the import table IS the guest-visible contract (it is exactly the
// set of DLL functions the emulator + Wine must provide for the game).
//
// Two ties, mirroring the audit's method:
//  1. Golden pin: each committed .exe's FULL import table (per-DLL,
//     including the UCRT api-ms-win-crt-* sets) is pinned literally below
//     and re-parsed from the binary on every run.
//  2. Rebuild tie: build-games.sh runs unmodified in a scratch tree and
//     the rebuilt games' non-CRT imports must equal the committed ones —
//     with exactly one documented delta. This box's mingw targets msvcrt
//     while the committed games were built against UCRT; the CRT import
//     sets therefore differ structurally (and are covered by tie 1, not
//     this compare), and the UCRT startup objects pull in one extra
//     KERNEL32 import, IsDBCSLeadByte, that no game source references
//     (grep-verified CRT plumbing). If that delta ever changes size, this
//     test fails and prints it, rather than silently absorbing it.
// (build-games.sh calls the `file` utility only to describe its output;
// this box has no `file` package, so the scratch PATH carries a no-op
// stub for it. The compile commands themselves run exactly as scripted.)
//
// doom.exe is deliberately NOT pinned here: it is built from the external
// doomgeneric tree by a drift-fragile patch script (see build-games.sh),
// so there is no in-tree source to tie it to. It remains the audit's one
// accepted untied artifact.

import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { chmodSync, cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const gamesDir = join(repoRoot, 'tools/rootfs64/games');
const GAMES = ['snake', 'tetris', 'clipset', 'clipget'];

// Parse a PE32+ import table: { dllName: [sorted function names] }.
// Also records the file offset of each imported name (for the pin's
// negative test to corrupt a name at its real location).
function peImports(bytes) {
  assert.equal(bytes.toString('latin1', 0, 2), 'MZ');
  const pe = bytes.readUInt32LE(0x3c);
  assert.equal(bytes.toString('latin1', pe, pe + 4), 'PE\0\0');
  const nsec = bytes.readUInt16LE(pe + 6);
  const opt = pe + 24;
  const optSize = bytes.readUInt16LE(pe + 20);
  assert.equal(bytes.readUInt16LE(opt), 0x20b, 'games must be PE32+');
  const sections = [];
  for (let i = 0; i < nsec; i++) {
    const s = opt + optSize + i * 40;
    sections.push({
      va: bytes.readUInt32LE(s + 12), vsz: bytes.readUInt32LE(s + 8),
      roff: bytes.readUInt32LE(s + 20), rsz: bytes.readUInt32LE(s + 16),
    });
  }
  const r2o = (rva) => {
    for (const s of sections)
      if (rva >= s.va && rva < s.va + Math.max(s.vsz, s.rsz)) return s.roff + (rva - s.va);
    throw new Error(`RVA ${rva} not in any section`);
  };
  const cstr = (off) => bytes.toString('utf8', off, bytes.indexOf(0, off));
  const impRva = bytes.readUInt32LE(opt + 112 + 8); // data directory 1 = imports
  const out = {};
  const nameOffsets = new Map();
  let d = r2o(impRva);
  for (;;) {
    const oft = bytes.readUInt32LE(d);
    const nameRva = bytes.readUInt32LE(d + 12);
    const ft = bytes.readUInt32LE(d + 16);
    if (nameRva === 0) break;
    const dll = cstr(r2o(nameRva));
    const fns = [];
    let t = r2o(oft || ft);
    for (;;) {
      const val = bytes.readBigUInt64LE(t);
      if (val === 0n) break;
      if (val >> 63n) {
        fns.push(`#${Number(val & 0xffffn)}`);
      } else {
        const no = r2o(Number(val)) + 2; // skip the hint word
        nameOffsets.set(`${dll}!${cstr(no)}`, no);
        fns.push(cstr(no));
      }
      t += 8;
    }
    out[dll] = fns.sort();
    d += 20;
  }
  return { imports: out, nameOffsets };
}

// The committed games' exact import tables (audit-verified, re-derived
// from the shipped binaries when this pin was written, 2026-10-25).
const PINNED_IMPORTS = {
  snake: {
    'GDI32.dll': ['BitBlt', 'CreateCompatibleBitmap', 'CreateCompatibleDC', 'CreateSolidBrush', 'DeleteDC', 'DeleteObject', 'SelectObject', 'SetBkMode', 'SetTextColor', 'TextOutA'],
    'KERNEL32.dll': ['DeleteCriticalSection', 'EnterCriticalSection', 'GetLastError', 'GetStartupInfoA', 'InitializeCriticalSection', 'IsDBCSLeadByte', 'LeaveCriticalSection', 'SetUnhandledExceptionFilter', 'Sleep', 'TlsGetValue', 'VirtualProtect', 'VirtualQuery', 'lstrlenA'],
    'USER32.dll': ['AdjustWindowRect', 'BeginPaint', 'CreateWindowExA', 'DefWindowProcA', 'DispatchMessageA', 'EndPaint', 'FillRect', 'InvalidateRect', 'LoadCursorA', 'PeekMessageA', 'PostQuitMessage', 'RegisterClassA', 'SetTimer', 'TranslateMessage'],
    'api-ms-win-crt-environment-l1-1-0.dll': ['__p__environ'],
    'api-ms-win-crt-heap-l1-1-0.dll': ['_set_new_mode', 'calloc', 'free', 'malloc'],
    'api-ms-win-crt-locale-l1-1-0.dll': ['_configthreadlocale'],
    'api-ms-win-crt-math-l1-1-0.dll': ['__setusermatherr'],
    'api-ms-win-crt-private-l1-1-0.dll': ['__C_specific_handler', 'memcpy', 'memmove'],
    'api-ms-win-crt-runtime-l1-1-0.dll': ['__p___argc', '__p___argv', '__p__acmdln', '_cexit', '_configure_narrow_argv', '_crt_atexit', '_exit', '_initialize_narrow_environment', '_initterm', '_initterm_e', '_set_app_type', '_set_invalid_parameter_handler', 'abort', 'exit', 'signal'],
    'api-ms-win-crt-stdio-l1-1-0.dll': ['__acrt_iob_func', '__p__commode', '__p__fmode', '__stdio_common_vfprintf', 'fflush', 'setvbuf'],
    'api-ms-win-crt-string-l1-1-0.dll': ['strlen', 'strncmp'],
  },
  tetris: {
    'GDI32.dll': ['BitBlt', 'CreateCompatibleBitmap', 'CreateCompatibleDC', 'CreateSolidBrush', 'DeleteDC', 'DeleteObject', 'SelectObject', 'SetBkMode', 'SetTextColor', 'TextOutA'],
    'KERNEL32.dll': ['DeleteCriticalSection', 'EnterCriticalSection', 'GetLastError', 'GetStartupInfoA', 'InitializeCriticalSection', 'IsDBCSLeadByte', 'LeaveCriticalSection', 'SetUnhandledExceptionFilter', 'Sleep', 'TlsGetValue', 'VirtualProtect', 'VirtualQuery'],
    'USER32.dll': ['AdjustWindowRect', 'BeginPaint', 'CreateWindowExA', 'DefWindowProcA', 'DispatchMessageA', 'EndPaint', 'FillRect', 'InvalidateRect', 'LoadCursorA', 'PeekMessageA', 'PostQuitMessage', 'RegisterClassA', 'SetTimer', 'TranslateMessage'],
    'api-ms-win-crt-environment-l1-1-0.dll': ['__p__environ'],
    'api-ms-win-crt-heap-l1-1-0.dll': ['_set_new_mode', 'calloc', 'free', 'malloc'],
    'api-ms-win-crt-locale-l1-1-0.dll': ['_configthreadlocale'],
    'api-ms-win-crt-math-l1-1-0.dll': ['__setusermatherr'],
    'api-ms-win-crt-private-l1-1-0.dll': ['__C_specific_handler', 'memcpy', 'memmove'],
    'api-ms-win-crt-runtime-l1-1-0.dll': ['__p___argc', '__p___argv', '__p__acmdln', '_cexit', '_configure_narrow_argv', '_crt_atexit', '_exit', '_initialize_narrow_environment', '_initterm', '_initterm_e', '_set_app_type', '_set_invalid_parameter_handler', 'abort', 'exit', 'signal'],
    'api-ms-win-crt-stdio-l1-1-0.dll': ['__acrt_iob_func', '__p__commode', '__p__fmode', '__stdio_common_vfprintf', 'fflush', 'setvbuf'],
    'api-ms-win-crt-string-l1-1-0.dll': ['strlen', 'strncmp'],
  },
  clipset: {
    'KERNEL32.dll': ['DeleteCriticalSection', 'EnterCriticalSection', 'GetLastError', 'GetStartupInfoA', 'GlobalAlloc', 'GlobalFree', 'GlobalLock', 'GlobalUnlock', 'InitializeCriticalSection', 'IsDBCSLeadByte', 'LeaveCriticalSection', 'MultiByteToWideChar', 'SetUnhandledExceptionFilter', 'Sleep', 'TlsGetValue', 'VirtualProtect', 'VirtualQuery'],
    'USER32.dll': ['CloseClipboard', 'EmptyClipboard', 'OpenClipboard', 'SetClipboardData'],
    'api-ms-win-crt-environment-l1-1-0.dll': ['__p__environ'],
    'api-ms-win-crt-heap-l1-1-0.dll': ['_set_new_mode', 'calloc', 'free', 'malloc'],
    'api-ms-win-crt-locale-l1-1-0.dll': ['_configthreadlocale'],
    'api-ms-win-crt-math-l1-1-0.dll': ['__setusermatherr'],
    'api-ms-win-crt-private-l1-1-0.dll': ['__C_specific_handler', 'memcpy'],
    'api-ms-win-crt-runtime-l1-1-0.dll': ['__p___argc', '__p___argv', '__p__acmdln', '_cexit', '_configure_narrow_argv', '_crt_atexit', '_exit', '_initialize_narrow_environment', '_initterm', '_initterm_e', '_set_app_type', '_set_invalid_parameter_handler', 'abort', 'exit', 'signal'],
    'api-ms-win-crt-stdio-l1-1-0.dll': ['__acrt_iob_func', '__p__commode', '__p__fmode', '__stdio_common_vfprintf', 'fclose', 'fflush', 'fopen', 'fread', 'fseek', 'ftell', 'setvbuf'],
    'api-ms-win-crt-string-l1-1-0.dll': ['strlen', 'strncmp'],
  },
  clipget: {
    'KERNEL32.dll': ['DeleteCriticalSection', 'EnterCriticalSection', 'GetLastError', 'GetStartupInfoA', 'GlobalLock', 'GlobalUnlock', 'InitializeCriticalSection', 'IsDBCSLeadByte', 'LeaveCriticalSection', 'SetUnhandledExceptionFilter', 'Sleep', 'TlsGetValue', 'VirtualProtect', 'VirtualQuery', 'WideCharToMultiByte'],
    'USER32.dll': ['CloseClipboard', 'GetClipboardData', 'OpenClipboard'],
    'api-ms-win-crt-environment-l1-1-0.dll': ['__p__environ'],
    'api-ms-win-crt-filesystem-l1-1-0.dll': ['remove', 'rename'],
    'api-ms-win-crt-heap-l1-1-0.dll': ['_set_new_mode', 'calloc', 'free', 'malloc'],
    'api-ms-win-crt-locale-l1-1-0.dll': ['_configthreadlocale'],
    'api-ms-win-crt-math-l1-1-0.dll': ['__setusermatherr'],
    'api-ms-win-crt-private-l1-1-0.dll': ['__C_specific_handler', 'memcpy'],
    'api-ms-win-crt-runtime-l1-1-0.dll': ['__p___argc', '__p___argv', '__p__acmdln', '_cexit', '_configure_narrow_argv', '_crt_atexit', '_exit', '_initialize_narrow_environment', '_initterm', '_initterm_e', '_set_app_type', '_set_invalid_parameter_handler', 'abort', 'exit', 'signal'],
    'api-ms-win-crt-stdio-l1-1-0.dll': ['__acrt_iob_func', '__p__commode', '__p__fmode', '__stdio_common_vfprintf', 'fclose', 'fflush', 'fopen', 'fwrite', 'setvbuf'],
    'api-ms-win-crt-string-l1-1-0.dll': ['strlen', 'strncmp'],
  },
};

const isCrtDll = (dll) => dll === 'msvcrt.dll' || dll.startsWith('api-ms-win-crt');

function nonCrtImports(imports) {
  const out = {};
  for (const [dll, fns] of Object.entries(imports))
    if (!isCrtDll(dll)) out[dll] = fns;
  return out;
}

// The expected result of a box (msvcrt) rebuild: the committed non-CRT
// imports minus the one UCRT-startup-plumbing function (see header).
function expectedBoxRebuild(game) {
  const out = nonCrtImports(structuredClone(PINNED_IMPORTS[game]));
  out['KERNEL32.dll'] = out['KERNEL32.dll'].filter((f) => f !== 'IsDBCSLeadByte');
  return out;
}

function scratchGames(sabotageSnake) {
  const dir = mkdtempSync(join(tmpdir(), 'games-imports-'));
  const dest = join(dir, 'games');
  cpSync(join(gamesDir, 'build-games.sh'), join(dest, 'build-games.sh'));
  cpSync(join(gamesDir, 'src'), join(dest, 'src'), { recursive: true });
  if (sabotageSnake) {
    const src = join(dest, 'src/snake.c');
    writeFileSync(src, readFileSync(src, 'utf8') +
      '\nvoid bw64_pin_sabotage(void) { MessageBeep(0); }\n');
  }
  const bin = join(dir, 'bin');
  mkdirSync(bin, { recursive: true });
  writeFileSync(join(bin, 'file'), '#!/bin/sh\nexit 0\n'); // `file` stub, see header
  chmodSync(join(bin, 'file'), 0o755);
  execFileSync('bash', [join(dest, 'build-games.sh')],
    { stdio: 'pipe', env: { ...process.env, PATH: `${bin}:${process.env.PATH}` } });
  return { dir, dest };
}

for (const game of GAMES) {
  test(`committed ${game}.exe import table matches the pin`, () => {
    const { imports } = peImports(readFileSync(join(gamesDir, `${game}.exe`)));
    assert.deepEqual(imports, PINNED_IMPORTS[game],
      `${game}.exe import table drifted from its pin; rebuild via build-games.sh with a UCRT mingw or re-pin deliberately`);
  });
}

test('the pin is anchored: every committed game imports IsDBCSLeadByte via UCRT startup', () => {
  // Guards the rebuild-tie delta below from silently floating: if a future
  // committed game stops importing it, the delta assertion must be revisited,
  // not widened.
  for (const game of GAMES)
    assert.ok(PINNED_IMPORTS[game]['KERNEL32.dll'].includes('IsDBCSLeadByte'),
      `${game}: the documented UCRT/msvcrt delta changed; revisit expectedBoxRebuild`);
});

test('build-games.sh scratch rebuild reproduces the pinned non-CRT imports', () => {
  const { dir, dest } = scratchGames(false);
  try {
    for (const game of GAMES) {
      const { imports } = peImports(readFileSync(join(dest, `${game}.exe`)));
      assert.deepEqual(nonCrtImports(imports), expectedBoxRebuild(game),
        `${game}: a fresh build's Win32 imports differ from the committed game's (beyond the documented CRT delta); the source and the shipped binary have drifted`);
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('the import pin detects a flipped import-name byte in a copy', () => {
  const bytes = readFileSync(join(gamesDir, 'tetris.exe'));
  const { nameOffsets } = peImports(bytes);
  const at = nameOffsets.get('USER32.dll!CreateWindowExA');
  assert.ok(at !== undefined, 'CreateWindowExA must be an imported name in tetris.exe');
  bytes[at] ^= 0x01; // 'CreateWindowExA' -> 'BreateWindowExA' in the copy only
  assert.notDeepEqual(peImports(bytes).imports, PINNED_IMPORTS.tetris,
    'the pin must notice a corrupted import name');
});

test('a sabotaged game source breaks the rebuild tie', () => {
  const { dir, dest } = scratchGames(true);
  try {
    const { imports } = peImports(readFileSync(join(dest, 'snake.exe')));
    assert.ok(imports['USER32.dll'].includes('MessageBeep'),
      'sabotage must reach the rebuilt binary');
    assert.notDeepEqual(nonCrtImports(imports), expectedBoxRebuild('snake'),
      'a source change that alters what snake imports must fail the tie');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
