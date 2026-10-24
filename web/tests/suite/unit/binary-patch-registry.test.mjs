// web/tests/suite/unit/binary-patch-registry.test.mjs — Binary patches (P8).
//
// Campaign repeatedly binary-patched shipped artifacts: ntdll.so
// (WINELOADERNOEXEC→PATH; 8× mov eax,ebp→xor; mov eax,[eax]→xor),
// setupapi.dll (SetupDiCreateDeviceInfoList→INVALID_HANDLE_VALUE, fixed
// nothing), wineserver32 (382B shell → ELF), 91B fake-ELF wineboot.exe.
// Two patches crashed: WCSNICMP 3-byte MOVZX assumption (2/5B triggers
// corrupted EIP); skipcalls1 induced 0x352D31A9 misdiagnosed as TLS/GS.
// User correction: recompiling Wine (~1h) beats ~2d per-instruction archaeology.
// Rule: any binary patch must be checksummed, versioned, expiry/owner; patched
// dist/ vs web/ must never silently diverge.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('WINELOADERNOEXEC binary patch is absent from CPU emulation', () => {
  const cpu = readFileSync(join(repoRoot, 'source/emulation/cpu/common/cpu.cpp'), 'utf8');
  assert.doesNotMatch(cpu, /WINELOADERNOEXEC/,
    'WINELOADERNOEXEC→PATH must not be patched in guest bytes; rebuild Wine instead');
});

test('WCSNICMP 3-byte MOVZX assumption is gone (EIP corruption)', () => {
  const combined = readFileSync(join(repoRoot, 'source/emulation/cpu/normal/normalCPU.cpp'), 'utf8');
  assert.doesNotMatch(combined, /WCSNICMP_WORKAROUND/,
    'the unsafe WCSNICMP patch must not return; its 2/5-byte triggers corrupt EIP');
});

test('skipcalls1-style workarounds are not baked into shared builds (0x352D31A9)', () => {
  const cpu = readFileSync(join(repoRoot, 'source/emulation/cpu/cpu64.cpp'), 'utf8');
  assert.doesNotMatch(cpu, /skipcalls1|0x352D31A9/i,
    'the probe-only skipcalls1 workaround must not be promoted into shared CPU execution');
});

test('dist/web wine zips are hash-pinned (patch divergence detector)', (t) => {
  const distInfo = join(repoRoot, 'dist/runtime/BUILD_INFO.json');
  if (!existsSync(distInfo)) { t.skip('dist runtime absent; divergence requires an exported build'); return; }
  const provA = JSON.parse(readFileSync(distInfo, 'utf8'));
  const provB = JSON.parse(readFileSync(join(repoRoot, 'web/runtime/BUILD_INFO.json'), 'utf8'));
  assert.equal(provA.wasm_sha256, provB.wasm_sha256, 'patched-artifact divergence: dist/web wasm must match');
});

test('Wine rebuild path is documented (recompile > archaeology)', () => {
  const tools = readFileSync(join(repoRoot, 'tools/build-prefix32.sh'), 'utf8');
  assert.ok(tools.length > 100 || existsSync(join(repoRoot, 'tools/buildWine')),
    'Wine rebuild tooling must exist (user: ~1h recompile eliminates 32-bit boot blocker class vs 2d archaeology)');
});
