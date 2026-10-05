// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';

const fixture = process.env.JIT64_WASM_FIXTURE;
assert.ok(fixture, 'JIT64_WASM_FIXTURE must point at the native fixture emitter');
const dir = mkdtempSync(join(tmpdir(), 'jit64-wasm-'));
let sequence = 0;
const mask64 = (1n << 64n) - 1n;
const little = (value, count) => Array.from({ length: count }, (_, i) => Number((value >> BigInt(8 * i)) & 255n));
const movabs = (reg, value) => [reg >= 8 ? 0x49 : 0x48, 0xB8 + reg % 8, ...little(value, 8)];

// Import object for emitted modules. memRead/memWrite are required imports
// (string ops); the stubs throw so a non-string test that touches them fails
// loudly instead of silently.
function baseEnv(memory) {
  return { env: {
    memory,
    memRead: () => { throw new Error('memRead called by non-string test'); },
    memWrite: () => { throw new Error('memWrite called by non-string test'); },
  } };
}

function emit(code) {
  const path = join(dir, String(sequence++));
  writeFileSync(path + '.bin', new Uint8Array(code));
  const result = spawnSync(fixture, [path + '.bin', path + '.wasm'], { encoding: 'utf8' });
  return { result, path: path + '.wasm' };
}

async function check(code, changed, rip = 0x12345678ABCDEFF0n, ptr = 512) {
  const { result, path } = emit(code);
  assert.equal(result.status, 0, result.stderr);
  const module = readFileSync(path);
  assert.ok(WebAssembly.validate(module), 'emitted module must validate');
  const memory = new WebAssembly.Memory({ initial: 1 });
  // Distinguish the wasm host pointer from guest RIP; unaligned host state
  // pointers are legal too (the wasm alignment immediate is only a hint).
  const view = new DataView(memory.buffer);
  const before = Array.from({ length: 16 }, (_, i) => 0xFEDCBA9876543200n + BigInt(i));
  before.forEach((v, i) => view.setBigUint64(ptr + i * 8, v, true));
  view.setBigUint64(ptr + 128, rip, true);
  view.setUint32(ptr + 136, 0x8D7, true);
  view.setUint32(ptr + 140, 0xA5A5A5A5, true);
  const { instance } = await WebAssembly.instantiate(module, baseEnv(memory));
  instance.exports.execute(ptr);
  const expected = [...before];
  changed(expected);
  for (let i = 0; i < 16; i++) assert.equal(view.getBigUint64(ptr + i * 8, true), expected[i], `GPR ${i}`);
  assert.equal(view.getBigUint64(ptr + 128, true), (rip + BigInt(code.length)) & mask64, '64-bit RIP');
  assert.equal(view.getUint32(ptr + 136, true), 0x8D7, 'MOV preserves flags');
  assert.equal(view.getUint32(ptr + 140, true), 0xA5A5A5A5, 'ABI reserved word');
}

test('all 16 registers retain full 64-bit immediate values', async () => {
  const values = Array.from({ length: 16 }, (_, i) => 0x8123456789ABCD00n + BigInt(i));
  await check(values.flatMap((v, i) => movabs(i, v)), regs => values.forEach((v, i) => { regs[i] = v; }));
});

for (const value of [0n, 63n, 64n, 127n, 128n, 0x7FFFFFFFFFFFFFFFn, 0x8000000000000000n, 0xFFFFFFFFFFFFFFFFn]) {
  test(`i64 signed LEB encoding preserves ${value.toString(16)}`, () => check(movabs(15, value), regs => { regs[15] = value; }));
}

test('32-bit immediate MOV zero-extends high registers', () => check(
  [0x41, 0xBF, 0xFF, 0xFF, 0xFF, 0xFF], regs => { regs[15] = 0xFFFFFFFFn; }));
test('MOV r/m64,r64 and MOV r64,r/m64 preserve high halves', () => check(
  [0x4D, 0x89, 0xF8, 0x4D, 0x8B, 0xC8], regs => { regs[8] = regs[15]; regs[9] = regs[8]; }));
test('32-bit register MOV zero-extends both encoding directions', () => check(
  [0x45, 0x89, 0xF8, 0x45, 0x8B, 0xC8], regs => { regs[8] = regs[15] & 0xFFFFFFFFn; regs[9] = regs[8]; }));
test('C7 imm32 sign-extends for a 64-bit register destination', () => check(
  [0x49, 0xC7, 0xC0, 0x80, 0xFF, 0xFF, 0xFF], regs => { regs[8] = 0xFFFFFFFFFFFFFF80n; }));
test('C7 imm32 zero-extends for a 32-bit register destination', () => check(
  [0x41, 0xC7, 0xC0, 0x80, 0xFF, 0xFF, 0xFF], regs => { regs[8] = 0xFFFFFF80n; }));
test('RIP advancement wraps modulo 64 bits', () => check(movabs(0, 1n), regs => { regs[0] = 1n; }, mask64 - 3n));
test('state at the end of imported memory stays in bounds', () => check(
  movabs(0, 7n), regs => { regs[0] = 7n; }, 0n, 65536 - 144));
test('unaligned state pointer executes correctly', () => check(
  movabs(0, 9n), regs => { regs[0] = 9n; }, 0n, 513));
test('out-of-bounds state traps before modifying any guest state', async () => {
  const { result, path } = emit(movabs(0, 1n));
  assert.equal(result.status, 0, result.stderr);
  const memory = new WebAssembly.Memory({ initial: 1 });
  const bytes = new Uint8Array(memory.buffer);
  bytes.fill(0xA5);
  const { instance } = await WebAssembly.instantiate(readFileSync(path), baseEnv(memory));
  for (const ptr of [65536 - 128, -1, -128]) {
    assert.throws(() => instance.exports.execute(ptr), WebAssembly.RuntimeError);
    assert.ok(bytes.every(value => value === 0xA5), 'trap must leave memory unchanged');
  }
});

for (const [name, code] of [
  ['empty block', []],
  ['guest-memory MOV', [0x48, 0x89, 0x00]],
  ['16-bit MOV', [0x66, 0xB8, 0x42, 0]], ['REP prefix', [0xF3, 0xB8, 0x42, 0, 0, 0]],
  ['memory ADD', [0x48, 0x01, 0x00]],
  ['unsupported suffix after MOV', [...movabs(0, 1n), 0x48, 0xC1, 0x20, 0x04]],
  ['block longer than 24 operations', Array.from({ length: 25 }, () => movabs(0, 1n)).flat()],
]) {
  test(`whole-block fallback rejects ${name}`, () => assert.equal(emit(code).result.status, 2));
}

// --- ALU reference model (mirrors cpu64.cpp flagsAdd/flagsSub/flagsLogic) ---
const ALU_MASK = 0x8D5;
function refAlu(sub, a, b, width, carryIn = 0n) {
  const mask = width === 8 ? mask64 : (1n << BigInt(width * 8)) - 1n;
  const sb = 1n << BigInt(width * 8 - 1);
  a &= mask; b &= mask;
  const isAdd = sub === 0 || sub === 2, isSub = sub === 3 || sub === 5 || sub === 7;
  const isLogic = !isAdd && !isSub;
  if (sub === 2 || sub === 3) b = (b + carryIn) & mask;
  let r, cf = 0n, of = 0n, af = 0n;
  if (isAdd) {
    r = (a + b) & mask;
    cf = r < a ? 1n : 0n;
    of = (((~(a ^ b)) & (a ^ r)) & sb) ? 1n : 0n;
  } else if (isSub) {
    r = (a - b) & mask;
    cf = a < b ? 1n : 0n;
    of = (((a ^ b) & (a ^ r)) & sb) ? 1n : 0n;
  } else {
    r = (sub === 1 ? a | b : sub === 4 ? a & b : a ^ b) & mask;
  }
  if (!isLogic) af = ((a ^ b ^ r) & 0x10n) ? 1n : 0n;
  const zf = r === 0n ? 1n : 0n, sf = (r & sb) ? 1n : 0n;
  let p = Number(r & 0xFFn); p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
  const pf = (p & 1) === 0 ? 1n : 0n;
  return { r, flags: cf | (pf << 2n) | (af << 4n) | (zf << 6n) | (sf << 7n) | (of << 11n) };
}

async function runAlu(code, regs, rflags, rip = 0x12345678ABCDEFF0n, ptr = 512) {
  const { result, path } = emit(code);
  assert.equal(result.status, 0, result.stderr);
  assert.ok(WebAssembly.validate(readFileSync(path)), 'emitted module must validate');
  const memory = new WebAssembly.Memory({ initial: 1 });
  const view = new DataView(memory.buffer);
  regs.forEach((v, i) => view.setBigUint64(ptr + i * 8, v, true));
  view.setBigUint64(ptr + 128, rip, true);
  view.setUint32(ptr + 136, rflags >>> 0, true);
  view.setUint32(ptr + 140, 0xA5A5A5A5, true);
  const { instance } = await WebAssembly.instantiate(readFileSync(path), baseEnv(memory));
  instance.exports.execute(ptr);
  return {
    regs: Array.from({ length: 16 }, (_, i) => view.getBigUint64(ptr + i * 8, true)),
    rflags: view.getUint32(ptr + 136, true),
    ripOut: view.getBigUint64(ptr + 128, true),
    reserved: view.getUint32(ptr + 140, true),
  };
}

const baseRegs = () => Array.from({ length: 16 }, (_, i) => 0xFEDCBA9876543200n + BigInt(i));
const modrm = (reg, rm) => 0xC0 | (reg << 3) | rm;
function expectAlu(out, initFlags, newBits, codeLen, changed) {
  assert.equal(out.rflags, (((initFlags & ~ALU_MASK) | Number(newBits)) >>> 0), 'rflags');
  assert.equal(out.ripOut, (0x12345678ABCDEFF0n + BigInt(codeLen)) & mask64, 'RIP advances');
  assert.equal(out.reserved, 0xA5A5A5A5, 'ABI reserved word');
  changed(out.regs);
}

test('ADD r64,r64 wraps with carry out', async () => {
  const regs = baseRegs(); regs[0] = mask64; regs[3] = 1n;
  const { r, flags } = refAlu(0, regs[0], regs[3], 8);
  assert.equal(flags & 0x8D5n, 0x1n | 0x40n | 0x4n | 0x10n, 'CF+ZF+PF+AF set');
  const out = await runAlu([0x48, 0x01, modrm(3, 0)], regs, 0x202);
  expectAlu(out, 0x202, flags, 3, got => {
    assert.equal(got[0], r);
    assert.equal(got[3], 1n, 'source register untouched');
  });
});

test('ADD r32,r32 uses 32-bit flags and zero-extends', async () => {
  const regs = baseRegs(); regs[0] = 0x80000000n; regs[3] = 0x80000000n;
  const { r, flags } = refAlu(0, regs[0], regs[3], 4);
  assert.equal(flags & 0x8D5n, 0x1n | 0x40n | 0x4n | 0x800n, 'CF+ZF+PF+OF set');
  const out = await runAlu([0x01, modrm(3, 0)], regs, 0x202);
  expectAlu(out, 0x202, flags, 2, got => assert.equal(got[0], r));
});

test('SUB r64,r64 borrows', async () => {
  const regs = baseRegs(); regs[0] = 0n; regs[3] = 1n;
  const { r, flags } = refAlu(5, regs[0], regs[3], 8);
  assert.equal(flags & 0x8D5n, 0x1n | 0x80n | 0x10n | 0x4n, 'CF+SF+AF+PF set');
  const out = await runAlu([0x48, 0x29, modrm(3, 0)], regs, 0x202);
  expectAlu(out, 0x202, flags, 3, got => assert.equal(got[0], r));
});

test('XOR r64,r64 self-zeroes with logic flags', async () => {
  const regs = baseRegs(); regs[0] = 0x123456789ABCDEF0n;
  const { r, flags } = refAlu(6, regs[0], regs[0], 8);
  assert.equal(flags & 0x8D5n, 0x40n | 0x4n, 'only ZF+PF set');
  const out = await runAlu([0x48, 0x31, modrm(0, 0)], regs, 0x8D5);
  expectAlu(out, 0x8D5, flags, 3, got => assert.equal(got[0], r));
});

test('AND/OR r64,r64 clear CF/OF/AF', async () => {
  const regs = baseRegs(); regs[0] = 0xF0F0F0F0F0F0F0F0n; regs[3] = 0x0FF00FF00FF00FF0n;
  for (const [op, sub] of [[0x21, 4], [0x09, 1]]) {
    const { r, flags } = refAlu(sub, regs[0], regs[3], 8);
    assert.equal(flags & 0x811n, 0n, 'CF/OF/AF clear');
    const out = await runAlu([0x48, op, modrm(3, 0)], [...regs], 0x8D5);
    expectAlu(out, 0x8D5, flags, 3, got => assert.equal(got[0], r));
  }
});

test('ADD r64,imm32 sign-extends the immediate', async () => {
  const regs = baseRegs(); regs[0] = 5n;
  const imm = 0xFFFFFFFFFFFFFFFBn; // -5
  const { r, flags } = refAlu(0, regs[0], imm, 8);
  const out = await runAlu([0x48, 0x81, modrm(0, 0), 0xFB, 0xFF, 0xFF, 0xFF], regs, 0x202);
  expectAlu(out, 0x202, flags, 7, got => assert.equal(got[0], r));
});

test('ADD r64,imm8 sign-extends and SUB r64,imm8 borrows', async () => {
  const regs = baseRegs(); regs[0] = 0n;
  let m = refAlu(0, regs[0], 0xFFFFFFFFFFFFFFFFn, 8); // +(-1)
  let out = await runAlu([0x48, 0x83, modrm(0, 0), 0xFF], regs, 0x202);
  expectAlu(out, 0x202, m.flags, 4, got => assert.equal(got[0], m.r));
  m = refAlu(5, regs[0], 1n, 8);
  out = await runAlu([0x48, 0x83, modrm(5, 0), 0x01], [...regs], 0x202);
  expectAlu(out, 0x202, m.flags, 4, got => assert.equal(got[0], m.r));
});

test('ADD rax,imm32 accumulator form', async () => {
  const regs = baseRegs(); regs[0] = 0x7FFFFFFFFFFFFFFFn;
  const { r, flags } = refAlu(0, regs[0], 1n, 8);
  assert.equal(flags & 0x8D5n, 0x800n | 0x80n | 0x10n | 0x4n, 'OF+SF+AF+PF set');
  const out = await runAlu([0x48, 0x05, 0x01, 0x00, 0x00, 0x00], regs, 0x202);
  expectAlu(out, 0x202, flags, 6, got => assert.equal(got[0], r));
});

test('CMP r64,r64 writes no register', async () => {
  const regs = baseRegs(); regs[0] = 10n; regs[3] = 10n;
  const { flags } = refAlu(7, regs[0], regs[3], 8);
  const out = await runAlu([0x48, 0x39, modrm(3, 0)], regs, 0x202);
  expectAlu(out, 0x202, flags, 3, got => {
    assert.equal(got[0], 10n, 'CMP leaves the destination alone');
    assert.equal(got[3], 10n);
  });
});

test('TEST r64,r64 writes no register', async () => {
  const regs = baseRegs(); regs[0] = 0xF0F0n; regs[3] = 0x0F0Fn;
  const { flags } = refAlu(4, regs[0], regs[3], 8);
  const out = await runAlu([0x48, 0x85, modrm(3, 0)], regs, 0x202);
  expectAlu(out, 0x202, flags, 3, got => {
    assert.equal(got[0], 0xF0F0n, 'TEST leaves the destination alone');
  });
});

test('ADC/SBB fold the incoming carry', async () => {
  const regs = baseRegs(); regs[0] = mask64; regs[3] = 0n;
  let m = refAlu(2, regs[0], regs[3], 8, 1n);
  let out = await runAlu([0x48, 0x11, modrm(3, 0)], [...regs], 0x203);
  expectAlu(out, 0x203, m.flags, 3, got => assert.equal(got[0], m.r));
  regs[0] = 5n; regs[3] = 5n;
  m = refAlu(3, regs[0], regs[3], 8, 1n);
  out = await runAlu([0x48, 0x19, modrm(3, 0)], [...regs], 0x203);
  expectAlu(out, 0x203, m.flags, 3, got => assert.equal(got[0], m.r));
});

test('ALU on extended registers (r8/r9)', async () => {
  const regs = baseRegs(); regs[8] = 2n; regs[9] = 3n;
  const { r, flags } = refAlu(0, regs[9], regs[8], 8);
  const out = await runAlu([0x4D, 0x01, modrm(0, 1)], regs, 0x202); // add r9, r8
  expectAlu(out, 0x202, flags, 3, got => {
    assert.equal(got[9], r);
    assert.equal(got[8], 2n);
  });
});

test('ALU preserves non-written RFLAGS bits', async () => {
  const regs = baseRegs(); regs[0] = 1n;
  const { flags } = refAlu(6, regs[0], regs[0], 8);
  const out = await runAlu([0x48, 0x31, modrm(0, 0)], regs, 0xA5A5A5A5);
  expectAlu(out, 0xA5A5A5A5, flags, 3, () => {});
});

test('mixed MOV+ADD block', async () => {
  const regs = baseRegs(); regs[3] = 8n;
  const code = [...movabs(0, 7n), 0x48, 0x01, modrm(3, 0)];
  const { r, flags } = refAlu(0, 7n, 8n, 8);
  const out = await runAlu(code, regs, 0x202);
  expectAlu(out, 0x202, flags, code.length, got => assert.equal(got[0], r));
});

// --- Shift reference model (mirrors cpu64.cpp doShift; count pre-masked) ---
const SHIFT_MASK = 0x8C5;
function refShift(sub, v, count, width, rflagsIn) {
  const mask = width === 4 ? 0xFFFFFFFFn : mask64;
  const sb = 1n << BigInt(width * 8 - 1);
  const wbits = BigInt(width * 8);
  v &= mask;
  if (count === 0n) return { r: v, flags: BigInt(rflagsIn) & BigInt(SHIFT_MASK) };
  let r, cf;
  if (sub === 4 || sub === 6) {          // SHL
    cf = (v >> (wbits - count)) & 1n;
    r = (v << count) & mask;
  } else if (sub === 5) {                // SHR
    cf = (v >> (count - 1n)) & 1n;
    r = (v >> count) & mask;
  } else {                               // SAR: arithmetic shift of sign-extended value
    cf = (v >> (count - 1n)) & 1n;
    const signed = (v & sb) ? v - (1n << wbits) : v;
    r = (signed >> count) & mask;        // BigInt >> is arithmetic
  }
  let of = 0n;
  if (count === 1n) {
    if (sub === 4 || sub === 6) of = (((r & sb) ? 1n : 0n) ^ cf) & 1n;
    else if (sub === 5) of = (v & sb) ? 1n : 0n;
  }
  const zf = r === 0n ? 1n : 0n, sf = (r & sb) ? 1n : 0n;
  let q = Number(r & 0xFFn); q ^= q >> 4; q ^= q >> 2; q ^= q >> 1;
  const pf = (q & 1) === 0 ? 1n : 0n;
  return { r, flags: cf | (pf << 2n) | (zf << 6n) | (sf << 7n) | (of << 11n) };
}

function expectShift(out, initFlags, newBits, codeLen, changed) {
  assert.equal(out.rflags, (((initFlags & ~SHIFT_MASK) | Number(newBits)) >>> 0), 'rflags');
  assert.equal(out.ripOut, (0x12345678ABCDEFF0n + BigInt(codeLen)) & mask64, 'RIP advances');
  assert.equal(out.reserved, 0xA5A5A5A5, 'ABI reserved word');
  changed(out.regs);
}

test('SHL r64 by 4 shifts out CF and zeroes', async () => {
  const regs = baseRegs(); regs[0] = 0xF000000000000000n;
  const { r, flags } = refShift(4, regs[0], 4n, 8, 0x202);
  assert.equal(flags & 0x8C5n, 0x1n | 0x40n | 0x4n, 'CF+ZF+PF set, OF clear');
  const out = await runAlu([0x48, 0xC1, modrm(4, 0), 0x04], regs, 0x202);
  expectShift(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('SHL r64 by 1 sets OF from MSB xor CF', async () => {
  const regs = baseRegs(); regs[0] = 0x4000000000000000n;
  const { r, flags } = refShift(4, regs[0], 1n, 8, 0x202);
  assert.equal(flags & 0x8C5n, 0x800n | 0x80n | 0x4n, 'OF+SF+PF set');
  const out = await runAlu([0x48, 0xC1, modrm(4, 0), 0x01], regs, 0x202);
  expectShift(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('SHR r64 by 1 shifts into CF, OF is old MSB', async () => {
  const regs = baseRegs(); regs[0] = 0x8000000000000001n;
  const { r, flags } = refShift(5, regs[0], 1n, 8, 0x202);
  assert.equal(flags & 0x8C5n, 0x1n | 0x800n | 0x4n, 'CF+OF+PF set');
  const out = await runAlu([0x48, 0xC1, modrm(5, 0), 0x01], regs, 0x202);
  expectShift(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('SAR r64 by 1 sign-extends and clears OF', async () => {
  const regs = baseRegs(); regs[0] = 0x8000000000000000n;
  const { r, flags } = refShift(7, regs[0], 1n, 8, 0x202);
  assert.equal(r, 0xC000000000000000n);
  assert.equal(flags & 0x8C5n, 0x80n | 0x4n, 'only SF+PF set');
  const out = await runAlu([0x48, 0xC1, modrm(7, 0), 0x01], regs, 0x202);
  expectShift(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('SAR r64 by 63 of -1 stays -1 with CF set', async () => {
  const regs = baseRegs(); regs[0] = mask64;
  const { r, flags } = refShift(7, regs[0], 63n, 8, 0x202);
  assert.equal(r, mask64);
  assert.equal(flags & 0x8C5n, 0x1n | 0x80n | 0x4n, 'CF+SF+PF set');
  const out = await runAlu([0x48, 0xC1, modrm(7, 0), 0x3F], regs, 0x202);
  expectShift(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('SHL r32 uses 32-bit width, flags and zero-extension', async () => {
  const regs = baseRegs(); regs[0] = 0x80000001n;
  const { r, flags } = refShift(4, regs[0], 1n, 4, 0x202);
  assert.equal(r, 0x2n);
  assert.equal(flags & 0x8C5n, 0x1n | 0x800n, 'CF+OF set');
  const out = await runAlu([0xC1, modrm(4, 0), 0x01], regs, 0x202);
  expectShift(out, 0x202, flags, 3, got => assert.equal(got[0], r));
});

test('SAR r32 sign-extends from 32 bits', async () => {
  const regs = baseRegs(); regs[0] = 0x80000000n;
  const { r, flags } = refShift(7, regs[0], 1n, 4, 0x202);
  assert.equal(r, 0xC0000000n);
  const out = await runAlu([0xC1, modrm(7, 0), 0x01], regs, 0x202);
  expectShift(out, 0x202, flags, 3, got => assert.equal(got[0], r));
});

test('shift by 0 is a no-op preserving value and flags', async () => {
  const regs = baseRegs(); regs[0] = 0x123n;
  const out = await runAlu([0x48, 0xC1, modrm(4, 0), 0x00], regs, 0x8C5);
  assert.equal(out.rflags, 0x8C5, 'flags untouched');
  assert.equal(out.regs[0], 0x123n, 'value untouched');
});

test('shift preserves AF and non-shift RFLAGS bits', async () => {
  const regs = baseRegs(); regs[0] = 1n;
  const { flags } = refShift(4, regs[0], 1n, 8, 0xA5B5);
  const out = await runAlu([0x48, 0xC1, modrm(4, 0), 0x01], regs, 0xA5B5);
  expectShift(out, 0xA5B5, flags, 4, () => {});
  assert.equal(out.rflags & 0x10, 0x10, 'AF preserved');
});

test('SHL /6 alias behaves like /4', async () => {
  const regs = baseRegs(); regs[0] = 0x4000000000000000n;
  const { r, flags } = refShift(6, regs[0], 1n, 8, 0x202);
  const out = await runAlu([0x48, 0xC1, modrm(6, 0), 0x01], regs, 0x202);
  expectShift(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('shift on extended register r8', async () => {
  const regs = baseRegs(); regs[8] = 3n;
  const { r, flags } = refShift(4, regs[8], 2n, 8, 0x202);
  const out = await runAlu([0x49, 0xC1, modrm(4, 0), 0x02], regs, 0x202);
  expectShift(out, 0x202, flags, 4, got => {
    assert.equal(got[8], r);
    assert.equal(got[0], regs[0], 'rax untouched');
  });
});

test('mixed shift+ALU block observes shift result', async () => {
  const regs = baseRegs(); regs[0] = 1n; regs[3] = 0n;
  const code = [0x48, 0xC1, modrm(4, 0), 0x03, 0x48, 0x01, modrm(3, 0)];
  const s = refShift(4, 1n, 3n, 8, 0x202);
  const a = refAlu(0, s.r, 0n, 8);
  const out = await runAlu(code, regs, 0x202);
  // Final flags come from the ADD; check the value chain instead.
  assert.equal(out.regs[0], a.r, 'add saw the shifted value');
  assert.equal(out.rflags & 0x8C5, Number(a.flags & 0x8D5n) & 0x8C5, 'ADD flags');
});

// --- Rotate reference model (mirrors cpu64.cpp doShift sub 0..3; count pre-masked) ---
const ROT_MASK = 0x801;
function refRotate(sub, v, count, width, rflagsIn) {
  const mask = width === 4 ? 0xFFFFFFFFn : mask64;
  const sb = 1n << BigInt(width * 8 - 1);
  const wbits = BigInt(width * 8);
  const mbits = width === 4 ? 31n : 63n;
  v &= mask;
  const c = count & mbits; // decoder pre-masks, but mirror it
  if (c === 0n) return { r: v, flags: BigInt(rflagsIn) }; // flags untouched
  let r, cf, of = 0n;
  if (sub === 0) { // ROL
    r = ((v << c) | (v >> (wbits - c))) & mask;
    cf = (v >> (wbits - c)) & 1n;
    if (c === 1n) of = (((r & sb) ? 1n : 0n) ^ cf) & 1n;
  } else if (sub === 1) { // ROR
    r = ((v >> c) | (v << (wbits - c))) & mask;
    cf = (v >> (c - 1n)) & 1n;
    if (c === 1n) of = (((r & sb) ? 1n : 0n) ^ ((r >> (wbits - 2n)) & 1n)) & 1n;
  } else if (sub === 2) { // RCL: loop-carried carry
    cf = BigInt(rflagsIn) & 1n;
    r = v;
    for (let i = 0n; i < c; i++) {
      const ncf = (r & sb) ? 1n : 0n;
      r = ((r << 1n) | cf) & mask;
      cf = ncf;
    }
    if (c === 1n) of = (((r & sb) ? 1n : 0n) ^ cf) & 1n;
  } else { // RCR
    cf = BigInt(rflagsIn) & 1n;
    r = v;
    for (let i = 0n; i < c; i++) {
      const ncf = r & 1n;
      r = ((r >> 1n) | (cf << (wbits - 1n))) & mask;
      cf = ncf;
    }
    if (c === 1n) of = (((r & sb) ? 1n : 0n) ^ ((r >> (wbits - 2n)) & 1n)) & 1n;
  }
  // OF is only defined for count==1; otherwise it is preserved.
  const fmask = c === 1n ? ~0x801n : ~0x001n;
  return { r, flags: (BigInt(rflagsIn) & fmask) | cf | (of << 11n) };
}

function expectRotate(out, initFlags, newBits, codeLen, changed) {
  assert.equal(out.rflags, (((initFlags & ~ROT_MASK) | Number(newBits)) >>> 0), 'rflags');
  assert.equal(out.ripOut, (0x12345678ABCDEFF0n + BigInt(codeLen)) & mask64, 'RIP advances');
  assert.equal(out.reserved, 0xA5A5A5A5, 'ABI reserved word');
  changed(out.regs);
}

// --- IMUL reference model (two-operand 0F AF; mirrors cpu64.cpp BK_IMUL_R_RM) ---
const IMUL_MASK = 0x801;
function refImul(a, b, width) {
  const mask = width === 4 ? 0xFFFFFFFFn : mask64;
  const wbits = BigInt(width * 8);
  const sb = 1n << (wbits - 1n);
  // Sign-extend operands to the operation width, then to BigInt full precision.
  let sa = a & mask, sbv = b & mask;
  sa = (sa & sb) ? sa - (1n << wbits) : sa;
  sbv = (sbv & sb) ? sbv - (1n << wbits) : sbv;
  const prod = sa * sbv;
  const r = prod & mask;
  // Overflow iff the full product differs from its sign-extended low half.
  let rs = r & mask;
  rs = (rs & sb) ? rs - (1n << wbits) : rs;
  const ovf = prod !== rs ? 1n : 0n;
  return { r, flags: ovf ? 0x801n : 0n };
}

function expectImul(out, initFlags, newBits, codeLen, changed) {
  assert.equal(out.rflags, (((initFlags & ~IMUL_MASK) | Number(newBits)) >>> 0), 'rflags');
  assert.equal(out.ripOut, (0x12345678ABCDEFF0n + BigInt(codeLen)) & mask64, 'RIP advances');
  assert.equal(out.reserved, 0xA5A5A5A5, 'ABI reserved word');
  changed(out.regs);
}

// ===== Rotate execution tests (0xC1 /0../3) =====

test('ROL r64 by 1 rotates MSB into CF and LSB', async () => {
  const regs = baseRegs(); regs[0] = 0x8000000000000001n;
  const { r, flags } = refRotate(0, regs[0], 1n, 8, 0x202);
  assert.equal(r, 0x0000000000000003n);
  assert.equal(flags & 0x801n, 0x801n, 'CF+OF set');
  const out = await runAlu([0x48, 0xC1, modrm(0, 0), 0x01], regs, 0x202);
  expectRotate(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('ROL r64 by 4, OF preserved when count!=1', async () => {
  const regs = baseRegs(); regs[0] = 0x123456789ABCDEF0n;
  const { r, flags } = refRotate(0, regs[0], 4n, 8, 0xADB5);
  const out = await runAlu([0x48, 0xC1, modrm(0, 0), 0x04], regs, 0xADB5);
  expectRotate(out, 0xADB5, flags, 4, got => {
    assert.equal(got[0], r);
    assert.equal(out.rflags & 0x800, 0x800, 'OF preserved from input (count!=1)');
  });
});

test('ROR r64 by 1 shifts LSB into CF', async () => {
  const regs = baseRegs(); regs[0] = 0x8000000000000001n;
  const { r, flags } = refRotate(1, regs[0], 1n, 8, 0x202);
  assert.equal(r, 0xC000000000000000n);
  const out = await runAlu([0x48, 0xC1, modrm(1, 0), 0x01], regs, 0x202);
  expectRotate(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('RCL r64 by 1 threads carry through CF', async () => {
  const regs = baseRegs(); regs[0] = 0x8000000000000000n;
  // CF=1 in: bit0 becomes 1, MSB goes to CF.
  const { r, flags } = refRotate(2, regs[0], 1n, 8, 0x203);
  assert.equal(r, 0x0000000000000001n);
  assert.equal(flags & 1n, 1n, 'CF set from old MSB');
  const out = await runAlu([0x48, 0xC1, modrm(2, 0), 0x01], regs, 0x203);
  expectRotate(out, 0x203, flags, 4, got => assert.equal(got[0], r));
});

test('RCR r64 by 1 threads carry through CF', async () => {
  const regs = baseRegs(); regs[0] = 0x0000000000000001n;
  const { r, flags } = refRotate(3, regs[0], 1n, 8, 0x202);
  assert.equal(r, 0x0000000000000000n);
  assert.equal(flags & 1n, 1n, 'CF set from old LSB');
  const out = await runAlu([0x48, 0xC1, modrm(3, 0), 0x01], regs, 0x202);
  expectRotate(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('ROL r32 by 1 zero-extends and uses 32-bit width', async () => {
  const regs = baseRegs(); regs[0] = 0xDEADBEEF80000001n;
  const { r, flags } = refRotate(0, regs[0], 1n, 4, 0x202);
  assert.equal(r, 0x00000003n); // ROL(0x80000001,1): bit31->CF, bit0=1
  const out = await runAlu([0xC1, modrm(0, 0), 0x01], regs, 0x202);
  expectRotate(out, 0x202, flags, 3, got => assert.equal(got[0], r));
});

test('rotate by 0 leaves value and flags untouched', async () => {
  const regs = baseRegs(); regs[0] = 0x123456789ABCDEF0n;
  for (const sub of [0, 1, 2, 3]) {
    const { r, flags } = refRotate(sub, regs[0], 0n, 8, 0xA5B5);
    assert.equal(r, regs[0]);
    assert.equal(flags, 0xA5B5n);
    const out = await runAlu([0x48, 0xC1, modrm(sub, 0), 0x00], regs, 0xA5B5);
    expectRotate(out, 0xA5B5, flags, 4, got => assert.equal(got[0], r));
  }
});

test('RCL r64 by 63 iterates the full carry chain', async () => {
  const regs = baseRegs(); regs[0] = 0xFFFFFFFFFFFFFFFFn;
  const { r, flags } = refRotate(2, regs[0], 63n, 8, 0x202);
  const out = await runAlu([0x48, 0xC1, modrm(2, 0), 0x3F], regs, 0x202);
  expectRotate(out, 0x202, flags, 4, got => assert.equal(got[0], r));
});

test('rotate preserves SZP/AF (only CF/OF change)', async () => {
  const regs = baseRegs(); regs[0] = 0x8000000000000000n;
  const { flags } = refRotate(0, regs[0], 1n, 8, 0xA5B5);
  const out = await runAlu([0x48, 0xC1, modrm(0, 0), 0x01], regs, 0xA5B5);
  assert.equal(out.rflags & ~0x801, 0xA5B5 & ~0x801, 'SZP/AF preserved');
  assert.equal(out.rflags & 0x801, Number(flags & 0x801n), 'CF/OF from rotate');
});

// ===== IMUL execution tests (0F AF) =====

test('IMUL r64,r64 without overflow clears CF/OF', async () => {
  const regs = baseRegs(); regs[2] = 6n; regs[5] = 7n;
  const { r, flags } = refImul(6n, 7n, 8);
  assert.equal(r, 42n); assert.equal(flags, 0n);
  const out = await runAlu([0x48, 0x0F, 0xAF, modrm(2, 5)], regs, 0xA5B5);
  expectImul(out, 0xA5B5, flags, 4, got => {
    assert.equal(got[2], 42n, 'rdx = 6*7');
    assert.equal(got[5], 7n, 'rbp untouched');
  });
});

test('IMUL r64,r64 overflow sets CF and OF', async () => {
  const regs = baseRegs(); regs[2] = 0x7000000000000000n; regs[5] = 2n;
  const { r, flags } = refImul(0x7000000000000000n, 2n, 8);
  assert.equal(r, 0xE000000000000000n); assert.equal(flags, 0x801n);
  const out = await runAlu([0x48, 0x0F, 0xAF, modrm(2, 5)], regs, 0x202);
  expectImul(out, 0x202, flags, 4, got => assert.equal(got[2], r));
});

test('IMUL r64,r64 INT64_MIN * -1 overflows', async () => {
  const regs = baseRegs(); regs[2] = 0x8000000000000000n; regs[5] = 0xFFFFFFFFFFFFFFFFn;
  const { r, flags } = refImul(0x8000000000000000n, 0xFFFFFFFFFFFFFFFFn, 8);
  assert.equal(r, 0x8000000000000000n); assert.equal(flags, 0x801n);
  const out = await runAlu([0x48, 0x0F, 0xAF, modrm(2, 5)], regs, 0x202);
  expectImul(out, 0x202, flags, 4, got => assert.equal(got[2], r));
});

test('IMUL r32,r32 sign-extends and detects 32-bit overflow', async () => {
  const regs = baseRegs(); regs[2] = 0xDEADBEEF00010000n; regs[5] = 0x0000000000020000n;
  const { r, flags } = refImul(0x10000n, 0x20000n, 4);
  assert.equal(r, 0x00000000n); // 2^32 truncated to 0
  assert.equal(flags, 0x801n, '2^32 does not fit in i32');
  const out = await runAlu([0x0F, 0xAF, modrm(2, 5)], regs, 0x202);
  expectImul(out, 0x202, flags, 3, got => assert.equal(got[2], r));
});

test('IMUL r32,r32 negative operands without overflow', async () => {
  const regs = baseRegs(); regs[2] = 0xFFFFFFFFFFFFFFF6n; regs[5] = 0x0000000000000007n;
  const { r, flags } = refImul(0xFFFFFFF6n, 7n, 4); // (-10) * 7 = -70
  assert.equal(r, 0xFFFFFFBAn); assert.equal(flags, 0n);
  const out = await runAlu([0x0F, 0xAF, modrm(2, 5)], regs, 0xA5B5);
  expectImul(out, 0xA5B5, flags, 3, got => assert.equal(got[2], r));
});

test('IMUL preserves SZP/AF, only CF/OF change', async () => {
  const regs = baseRegs(); regs[2] = 3n; regs[5] = 4n;
  const out = await runAlu([0x48, 0x0F, 0xAF, modrm(2, 5)], regs, 0xA5B5);
  assert.equal(out.rflags & ~0x801, 0xA5B5 & ~0x801, 'SZP/AF preserved');
  assert.equal(out.rflags & 0x801, 0, 'no overflow');
  assert.equal(out.regs[2], 12n);
});
// --- String-op tests (memory-touching emission) ---

// Guest memory model: sparse byte map; unmapped reads return 0, matching
// KMemory64::readb. Writes commit (populate the map).
function makeGuestMem() {
  const bytes = new Map(); // BigInt addr -> Number byte
  return {
    read(addr, size) {
      let v = 0n;
      for (let i = 0; i < size; i++) v |= BigInt(bytes.get(addr + BigInt(i)) || 0) << BigInt(8 * i);
      return v;
    },
    write(addr, size, val) {
      for (let i = 0; i < size; i++) bytes.set(addr + BigInt(i), Number((val >> BigInt(8 * i)) & 0xFFn));
    },
    clone() {
      const c = makeGuestMem();
      for (const [k, v] of bytes) c.bytes.set(k, v);
      return c;
    },
    bytes,
  };
}

// Import object for string-op modules: real memRead/memWrite backed by the
// JS guest memory. Protocol: (addrLo, addrHi, size, retPtr) and
// (addrLo, addrHi, size, valLo, valHi); memRead writes 8 bytes to retPtr.
function stringEnv(memory, guestMem) {
  const view = new DataView(memory.buffer);
  const u32 = n => Number(BigInt.asUintN(32, BigInt(n)));
  return { env: {
    memory,
    memRead: (addrLo, addrHi, size, retPtr) => {
      const addr = (BigInt(u32(addrHi)) << 32n) | BigInt(u32(addrLo));
      view.setBigUint64(retPtr, guestMem.read(addr, size), true);
    },
    memWrite: (addrLo, addrHi, size, valLo, valHi) => {
      const addr = (BigInt(u32(addrHi)) << 32n) | BigInt(u32(addrLo));
      const val = (BigInt(u32(valHi)) << 32n) | BigInt(u32(valLo));
      guestMem.write(addr, size, val);
    },
  } };
}

// JS reference model of CPU64::runStringOp (transcribed from cpu64.cpp).
// regs: array of 16 BigInts, modified in place. Returns new rflags (Number).
function refString(sub, size, rep, asize32, regs, rflags, guestMem) {
  const DF = 0x400n, ALU_MASK = 0x8D5n;
  const sizeMask = (1n << BigInt(size * 8)) - 1n;
  let flags = BigInt(rflags);
  const step = (flags & DF) ? -BigInt(size) : BigInt(size);
  const eff = a => asize32 ? a & 0xFFFFFFFFn : a & mask64;
  const readM = a => guestMem.read(eff(a), size);
  const writeM = (a, v) => guestMem.write(eff(a), size, v & sizeMask);
  if (sub <= 1) {
    const isStos = sub === 1;
    let count = rep !== 0 ? regs[1] : 1n;
    if (asize32) count &= 0xFFFFFFFFn;
    // while (count--) { ... }
    for (;;) {
      const old = count; count = (count - 1n) & mask64;
      if (old === 0n) break;
      const val = isStos ? regs[0] & sizeMask : readM(regs[6]);
      writeM(regs[7], val);
      if (!isStos) regs[6] = (regs[6] + step) & mask64;
      regs[7] = (regs[7] + step) & mask64;
    }
    if (rep !== 0) regs[1] = 0n;
  } else {
    const isScas = sub === 3;
    let count = rep !== 0 ? regs[1] : 1n;
    if (asize32) count &= 0xFFFFFFFFn;
    while (count > 0n) {
      count--;
      const lhs = isScas ? regs[0] & sizeMask : readM(regs[6]);
      const rhs = readM(regs[7]);
      const { flags: nf } = refAlu(5, lhs, rhs, size);
      flags = (flags & ~ALU_MASK) | nf;
      if (!isScas) regs[6] = (regs[6] + step) & mask64;
      regs[7] = (regs[7] + step) & mask64;
      if (rep !== 0) {
        const zf = (flags & 0x40n) !== 0n;
        if ((rep === 0xF3 && !zf) || (rep === 0xF2 && zf)) break;
      }
    }
    if (rep !== 0) regs[1] = count;
  }
  return Number(flags & 0xFFFFFFFFn);
}

async function runString(code, { regs, rflags = 0x202, memInit = new Map(), ptr = 512 }) {
  const { result, path } = emit(code);
  assert.equal(result.status, 0, result.stderr);
  assert.ok(WebAssembly.validate(readFileSync(path)), 'emitted module must validate');
  const memory = new WebAssembly.Memory({ initial: 1 });
  const guestMem = makeGuestMem();
  for (const [k, v] of memInit) guestMem.bytes.set(k, v);
  const view = new DataView(memory.buffer);
  regs.forEach((v, i) => view.setBigUint64(ptr + i * 8, v, true));
  view.setBigUint64(ptr + 128, 0x12345678ABCDEFF0n, true);
  view.setUint32(ptr + 136, rflags >>> 0, true);
  view.setUint32(ptr + 140, 0xA5A5A5A5, true);
  const { instance } = await WebAssembly.instantiate(readFileSync(path), stringEnv(memory, guestMem));
  instance.exports.execute(ptr);
  return {
    regs: Array.from({ length: 16 }, (_, i) => view.getBigUint64(ptr + i * 8, true)),
    rflags: view.getUint32(ptr + 136, true),
    ripOut: view.getBigUint64(ptr + 128, true),
    reserved: view.getUint32(ptr + 140, true),
    guestMem,
  };
}

// Compare wasm execution against the reference model.
async function checkString(code, sub, size, rep, asize32, regs, rflags, memInit) {
  const refRegs = [...regs];
  const refMem = makeGuestMem();
  for (const [k, v] of memInit) refMem.bytes.set(k, v);
  const refFlags = refString(sub, size, rep, asize32, refRegs, rflags, refMem);
  const out = await runString(code, { regs: [...regs], rflags, memInit });
  assert.deepEqual(out.regs, refRegs, 'GPRs');
  assert.equal(out.rflags, refFlags >>> 0, 'RFLAGS');
  assert.equal(out.ripOut, (0x12345678ABCDEFF0n + BigInt(code.length)) & mask64, 'RIP');
  assert.equal(out.reserved, 0xA5A5A5A5, 'ABI reserved word');
  // Memory: compare the full byte maps.
  const refKeys = [...refMem.bytes.keys()].map(String).sort();
  const outKeys = [...out.guestMem.bytes.keys()].map(String).sort();
  assert.deepEqual(outKeys, refKeys, 'written addresses');
  for (const k of refKeys) assert.equal(out.guestMem.bytes.get(BigInt(k)), refMem.bytes.get(BigInt(k)), `mem[${k}]`);
}

const SBASE = 0x1000n; // test scratch region
function memBytes(base, arr) {
  const m = new Map();
  arr.forEach((v, i) => m.set(base + BigInt(i), v));
  return m;
}

test('STOSB stores AL and advances RDI', async () => {
  const regs = baseRegs(); regs[0] = 0xABn; regs[7] = SBASE;
  await checkString([0xAA], 1, 1, 0, false, regs, 0x202, new Map());
});

test('STOSD stores EAX and advances RDI by 4', async () => {
  const regs = baseRegs(); regs[0] = 0xDEADBEEFn; regs[7] = SBASE;
  await checkString([0xAB], 1, 4, 0, false, regs, 0x202, new Map());
});

test('REP STOSD fills RCX dwords and zeroes RCX', async () => {
  const regs = baseRegs(); regs[0] = 0x11223344n; regs[7] = SBASE; regs[1] = 4n;
  await checkString([0xF3, 0xAB], 1, 4, 0xF3, false, regs, 0x202, new Map());
});

test('MOVSB copies byte and advances RSI/RDI', async () => {
  const regs = baseRegs(); regs[6] = SBASE; regs[7] = SBASE + 0x100n;
  await checkString([0xA4], 0, 1, 0, false, regs, 0x202, memBytes(SBASE, [0x7E]));
});

test('REP MOVSD copies RCX dwords', async () => {
  const regs = baseRegs(); regs[6] = SBASE; regs[7] = SBASE + 0x100n; regs[1] = 3n;
  await checkString([0xF3, 0xA5], 0, 4, 0xF3, false, regs, 0x202,
    memBytes(SBASE, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]));
});

test('STD makes STOSB decrement RDI', async () => {
  const regs = baseRegs(); regs[0] = 0x5An; regs[7] = SBASE + 0x10n;
  await checkString([0xAA], 1, 1, 0, false, regs, 0x202 | 0x400, new Map());
});

test('0x67 masks addresses to 32 bits but not register updates', async () => {
  const regs = baseRegs(); regs[6] = 0x1_00001000n; regs[7] = 0x2_00002000n;
  await checkString([0x67, 0xA5], 0, 4, 0, true, regs, 0x202, memBytes(0x1000n, [9, 9, 9, 9]));
});

test('REP with RCX=0 does nothing but still clears RCX', async () => {
  const regs = baseRegs(); regs[6] = SBASE; regs[7] = SBASE + 0x100n; regs[1] = 0n;
  await checkString([0xF3, 0xA5], 0, 4, 0xF3, false, regs, 0x202, new Map());
});

test('SCASB finds AL and sets ZF', async () => {
  const regs = baseRegs(); regs[0] = 0x42n; regs[7] = SBASE;
  await checkString([0xAE], 3, 1, 0, false, regs, 0x202, memBytes(SBASE, [0x42]));
});

test('REPE CMPSB stops at first mismatch with RCX remainder', async () => {
  const regs = baseRegs(); regs[6] = SBASE; regs[7] = SBASE + 0x100n; regs[1] = 8n;
  const mem = memBytes(SBASE, [1, 2, 3, 4, 5, 6, 7, 8]);
  for (const [k, v] of memBytes(SBASE + 0x100n, [1, 2, 9, 4, 5, 6, 7, 8])) mem.set(k, v);
  await checkString([0xF3, 0xA6], 2, 1, 0xF3, false, regs, 0x202, mem);
});

test('REPNE SCASD stops at match', async () => {
  const regs = baseRegs(); regs[0] = 0x99n; regs[7] = SBASE; regs[1] = 8n;
  await checkString([0xF2, 0xAF], 3, 4, 0xF2, false, regs, 0x202,
    memBytes(SBASE, [0, 0, 0, 0, 0x99, 0, 0, 0]));
});

test('REP STOSQ writes 8 bytes per iteration', async () => {
  const regs = baseRegs(); regs[0] = 0x1122334455667788n; regs[7] = SBASE; regs[1] = 2n;
  await checkString([0xF3, 0x48, 0xAB], 1, 8, 0xF3, false, regs, 0x202, new Map());
});

test('string op preserves non-flag RFLAGS bits', async () => {
  const regs = baseRegs(); regs[6] = SBASE; regs[7] = SBASE + 0x100n;
  await checkString([0xA6], 2, 1, 0, false, regs, 0x202 | 0x400 | 0x200, memBytes(SBASE, [5]));
});

// --- String-op differential fuzzing vs the JS reference model ---
// Deterministic PRNG (mulberry32) for reproducible fuzzing.
function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

test('string ops differential fuzz (reference model)', async () => {
  const rand = mulberry32(0x5EED);
  const pick = arr => arr[Math.floor(rand() * arr.length)];
  const r64 = () => BigInt(Math.floor(rand() * 0xFFFFFFFF)) << 32n | BigInt(Math.floor(rand() * 0xFFFFFFFF));
  const CASES = 300;
  for (let c = 0; c < CASES; c++) {
    const sub = Math.floor(rand() * 4);
    const size = pick([1, 2, 4, 8]);
    const rep = pick([0, 0xF2, 0xF3]);
    const asize32 = rand() < 0.3;
    const df = rand() < 0.3;
    // Addresses: mostly in the scratch region, sometimes with high bits set
    // (to exercise 0x67 masking vs full 64-bit update).
    const addrBase = SBASE + BigInt(Math.floor(rand() * 256));
    const hi = rand() < 0.2 ? BigInt(Math.floor(rand() * 0xFFFF)) << 32n : 0n;
    const regs = baseRegs();
    regs[0] = r64(); // RAX (STOS value / SCAS comparand)
    regs[1] = BigInt(Math.floor(rand() * 12)); // RCX (REP count)
    regs[6] = addrBase + hi; // RSI
    regs[7] = addrBase + 0x200n + hi; // RDI (separate region)
    const rflags = 0x202 | (df ? 0x400 : 0) | (rand() < 0.5 ? 0x1 : 0);
    // Random memory contents in both regions.
    const memInit = new Map();
    for (let i = 0; i < 64; i++) {
      if (rand() < 0.7) memInit.set(SBASE + BigInt(i), Math.floor(rand() * 256));
      if (rand() < 0x7) memInit.set(SBASE + 0x200n + BigInt(i), Math.floor(rand() * 256));
    }
    // Encode the instruction.
    const opByte = [0xA4, 0xAA, 0xA6, 0xAE][sub] + (size === 1 ? 0 : 1);
    // A4/A5, AA/AB, A6/A7, AE/AF: byte opcode for size 1, +1 for size 2/4/8.
    // 0x66 prefix selects size 2, REX.W selects size 8.
    const code = [];
    if (rep) code.push(rep);
    if (asize32) code.push(0x67);
    if (size === 2) code.push(0x66);
    if (size === 8) code.push(0x48);
    code.push(opByte);
    await checkString(code, sub, size, rep, asize32, regs, rflags, memInit);
  }
});

// --- 8-bit ALU emission ---
// 8-bit register model: without REX, indices 4..7 are AH/CH/DH/BH;
// with any REX they are SPL/BPL/SIL/DIL.
function byteAccess(regs, idx, rex) {
  let phys = idx, shift = 0n;
  if (!rex && idx >= 4 && idx <= 7) { phys = idx - 4; shift = 8n; }
  return {
    get: () => (regs[phys] >> shift) & 0xFFn,
    set: (v) => { regs[phys] = (regs[phys] & ~(0xFFn << shift)) | ((v & 0xFFn) << shift); },
  };
}

async function checkAlu8(code, destIdx, srcIdx, rex, sub, rflagsIn = 0x202) {
  const regs = baseRegs();
  const dest = byteAccess(regs, destIdx, rex);
  const src = byteAccess(regs, srcIdx, rex);
  const { r, flags } = refAlu(sub, dest.get(), src.get(), 1);
  const out = await runAlu(code, regs, rflagsIn);
  expectAlu(out, rflagsIn, flags, code.length, got => {
    const exp = [...regs];
    if (sub !== 7) byteAccess(exp, destIdx, rex).set(r);
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
}

for (const sub of [0, 1, 2, 3, 4, 5, 6, 7]) {
  test(`8-bit ALU r/m,r sub=${sub} (AL,CL)`, async () => {
    await checkAlu8([sub * 8, modrm(1, 0)], 0, 1, false, sub);
  });
  test(`8-bit ALU r,r/m sub=${sub} (CL,AL)`, async () => {
    const regs = baseRegs();
    const dest = byteAccess(regs, 1, false), src = byteAccess(regs, 0, false);
    const { r, flags } = refAlu(sub, dest.get(), src.get(), 1);
    const out = await runAlu([sub * 8 + 2, modrm(1, 0)], regs, 0x202);
    expectAlu(out, 0x202, flags, 2, got => {
      const exp = [...regs];
      if (sub !== 7) byteAccess(exp, 1, false).set(r);
      for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
    });
  });
}

test('8-bit ADD AH,BH (high-byte registers, no REX)', async () => {
  await checkAlu8([0x00, modrm(7, 4)], 4, 7, false, 0);
});
test('8-bit SUB DH,AH preserves neighbors', async () => {
  const regs = baseRegs();
  regs[0] = 0x1122334455667788n; regs[2] = 0xAABBCCDDEEFF0011n;
  const dest = byteAccess(regs, 6, false), src = byteAccess(regs, 4, false);
  const { r, flags } = refAlu(5, dest.get(), src.get(), 1);
  const out = await runAlu([0x28, modrm(4, 6)], regs, 0x202);
  expectAlu(out, 0x202, flags, 2, got => {
    const exp = [...regs];
    byteAccess(exp, 6, false).set(r);
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
});
test('8-bit ADD SPL,SPL under REX (not AH)', async () => {
  const regs = baseRegs();
  regs[4] = 0x1122334455667788n; // RSP: SPL is the low byte
  const dest = byteAccess(regs, 4, true), src = byteAccess(regs, 4, true);
  const { r, flags } = refAlu(0, dest.get(), src.get(), 1);
  const out = await runAlu([0x40, 0x00, modrm(4, 4)], regs, 0x202);
  expectAlu(out, 0x202, flags, 3, got => {
    const exp = [...regs];
    byteAccess(exp, 4, true).set(r);
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
});
test('8-bit ADD AL,imm8 (acc-imm)', async () => {
  const regs = baseRegs();
  const { r, flags } = refAlu(0, regs[0] & 0xFFn, 0x7Fn, 1);
  const out = await runAlu([0x04, 0x7F], regs, 0x202);
  expectAlu(out, 0x202, flags, 2, got => {
    const exp = [...regs]; exp[0] = (exp[0] & ~0xFFn) | r;
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
});
test('8-bit OR r/m8,imm8 (0x80 group)', async () => {
  const regs = baseRegs();
  const { r, flags } = refAlu(1, regs[2] & 0xFFn, 0xF0n, 1);
  const out = await runAlu([0x80, modrm(1, 2), 0xF0], regs, 0x202);
  expectAlu(out, 0x202, flags, 3, got => {
    const exp = [...regs]; exp[2] = (exp[2] & ~0xFFn) | r;
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
});
test('8-bit TEST r/m8,r8 sets flags only', async () => {
  const regs = baseRegs();
  const { flags } = refAlu(4, regs[0] & 0xFFn, regs[1] & 0xFFn, 1);
  const out = await runAlu([0x84, modrm(1, 0)], regs, 0x202);
  expectAlu(out, 0x202, flags, 2, got => {
    for (let i = 0; i < 16; i++) assert.equal(got[i], regs[i], `GPR ${i} untouched`);
  });
});
test('8-bit TEST AL,imm8 (A8)', async () => {
  const regs = baseRegs();
  const { flags } = refAlu(4, regs[0] & 0xFFn, 0x0Fn, 1);
  const out = await runAlu([0xA8, 0x0F], regs, 0x202);
  expectAlu(out, 0x202, flags, 2, got => {
    for (let i = 0; i < 16; i++) assert.equal(got[i], regs[i], `GPR ${i} untouched`);
  });
});

// --- 16-bit ALU emission ---
async function checkAlu16(code, sub, rflagsIn = 0x202) {
  const regs = baseRegs();
  const { r, flags } = refAlu(sub, regs[0] & 0xFFFFn, regs[1] & 0xFFFFn, 2);
  const out = await runAlu(code, regs, rflagsIn);
  expectAlu(out, rflagsIn, flags, code.length, got => {
    const exp = [...regs];
    if (sub !== 7) exp[0] = (exp[0] & ~0xFFFFn) | r; // setU16: upper preserved
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
}
for (const [name, byte] of [['ADD', 0x01], ['OR', 0x09], ['ADC', 0x11], ['SBB', 0x19], ['AND', 0x21], ['SUB', 0x29], ['XOR', 0x31], ['CMP', 0x39]]) {
  test(`16-bit ${name} AX,CX preserves upper 48`, async () => {
    await checkAlu16([0x66, byte, modrm(1, 0)], (byte >> 3) & 7);
  });
}
test('16-bit ADD AX,imm16 (acc-imm)', async () => {
  const regs = baseRegs();
  const { r, flags } = refAlu(0, regs[0] & 0xFFFFn, 0x1234n, 2);
  const out = await runAlu([0x66, 0x05, 0x34, 0x12], regs, 0x202);
  expectAlu(out, 0x202, flags, 4, got => {
    const exp = [...regs]; exp[0] = (exp[0] & ~0xFFFFn) | r;
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
});
test('16-bit SHL AX,1 with flags', async () => {
  const regs = baseRegs(); regs[0] = 0xFEDCBA987654C001n;
  const v = regs[0] & 0xFFFFn;
  const r = (v << 1n) & 0xFFFFn;
  const cf = (v >> 15n) & 1n, of = (((r >> 15n) & 1n) ^ cf);
  const zf = r === 0n ? 1n : 0n, sf = (r >> 15n) & 1n;
  let p = Number(r & 0xFFn); p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
  const flags = cf | (BigInt((p & 1) === 0 ? 1 : 0) << 2n) | (zf << 6n) | (sf << 7n) | (of << 11n);
  const out = await runAlu([0x66, 0xC1, modrm(4, 0), 0x01], regs, 0x202);
  expectAlu(out, 0x202, flags, 4, got => {
    const exp = [...regs]; exp[0] = (exp[0] & ~0xFFFFn) | r;
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
});

// --- MOVZX/MOVSX emission ---
async function checkMovx(op2, destSize, rexW) {
  const sub = [0xB6, 0xB7, 0xBE, 0xBF].indexOf(op2);
  const srcSize = (sub === 0 || sub === 2) ? 1 : 2; // B6/BE: 8-bit source
  const signed = sub >= 2;
  const regs = baseRegs();
  regs[1] = 0xFEDCBA9876543281n; // src: low byte 0x81, low word 0x3281
  let v = srcSize === 1 ? regs[1] & 0xFFn : regs[1] & 0xFFFFn;
  if (signed) v = srcSize === 1 ? BigInt.asIntN(8, v) : BigInt.asIntN(16, v);
  v = BigInt.asUintN(64, v);
  const code = [];
  if (destSize === 2) code.push(0x66);
  if (rexW) code.push(0x48);
  code.push(0x0F, op2, modrm(0, 1));
  const out = await runAlu(code, regs, 0x202);
  const exp = [...regs];
  if (destSize === 2) exp[0] = (exp[0] & ~0xFFFFn) | (v & 0xFFFFn);
  else if (destSize === 4) exp[0] = v & 0xFFFFFFFFn;
  else exp[0] = v;
  expectAlu(out, 0x202, 0n, code.length, got => {
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
  assert.equal(out.rflags, 0x202, 'MOVX preserves flags');
}
for (const op2 of [0xB6, 0xB7, 0xBE, 0xBF]) {
  for (const [ds, rw] of [[2, false], [4, false], [8, true]]) {
    test(`MOVX 0F${op2.toString(16).toUpperCase()} dest${ds * 8}`, async () => {
      await checkMovx(op2, ds, rw);
    });
  }
}

// --- one-operand IMUL emission ---
async function checkImul1(size, rexW) {
  const regs = baseRegs();
  regs[0] = 0xFEDCBA9876543211n; regs[1] = 0x1122334455667788n;
  const w = size * 8;
  const wmask = (1n << BigInt(w)) - 1n;
  const sa = BigInt.asIntN(w, regs[0] & wmask);
  const sb = BigInt.asIntN(w, regs[1] & wmask);
  const prod = sa * sb;
  const lo = BigInt.asUintN(w, prod);
  const overflow = BigInt.asIntN(w, lo) !== prod ? 1n : 0n;
  const code = [];
  if (rexW) code.push(0x48); else if (size === 2) code.push(0x66);
  code.push(size === 1 ? 0xF6 : 0xF7, modrm(5, 1));
  const out = await runAlu(code, regs, 0x202);
  const exp = [...regs];
  // 8-bit IMUL writes the 16-bit product to AX (interpreter setU16).
  if (size === 1) exp[0] = (exp[0] & ~0xFFFFn) | BigInt.asUintN(16, prod);
  else if (size === 2) {
    exp[0] = (exp[0] & ~0xFFFFn) | lo;
    exp[2] = (exp[2] & ~0xFFFFn) | ((prod >> BigInt(w)) & 0xFFFFn);
  } else if (size === 4) {
    exp[0] = lo; exp[2] = (prod >> 32n) & 0xFFFFFFFFn;
  } else { exp[0] = BigInt.asUintN(64, prod); exp[2] = BigInt.asUintN(64, prod >> 64n); }
  const flags = overflow ? 0x801n : 0n;
  expectAlu(out, 0x202, flags, code.length, got => {
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
  assert.equal(out.rflags & ~0x801, 0x202 & ~0x801, 'other flags preserved');
}
for (const [size, rexW] of [[1, false], [2, false], [4, false], [8, true]]) {
  test(`IMUL 1-op ${size * 8}-bit`, async () => { await checkImul1(size, rexW); });
}

// --- three-operand IMUL emission ---
async function checkImul3(size, useImm8) {
  const regs = baseRegs();
  regs[1] = 0xFEDCBA9876543211n;
  const w = size * 8;
  const sa = BigInt.asIntN(w, regs[1] & ((1n << BigInt(w)) - 1n));
  const imm = useImm8 ? -5n : 0x12345n;
  const prod = sa * imm;
  const r = BigInt.asUintN(w, prod);
  const overflow = BigInt.asIntN(w, r) !== prod ? 1n : 0n;
  const code = [];
  if (size === 8) code.push(0x48); else if (size === 2) code.push(0x66);
  if (useImm8) code.push(0x6B, modrm(0, 1), 0xFB);
  else if (size === 2) code.push(0x69, modrm(0, 1), 0x45, 0x23);
  else code.push(0x69, modrm(0, 1), 0x45, 0x23, 0x01, 0x00);
  const out = await runAlu(code, regs, 0x202);
  const exp = [...regs];
  if (size === 2) exp[0] = (exp[0] & ~0xFFFFn) | r;
  else if (size === 4) exp[0] = r;
  else exp[0] = BigInt.asUintN(64, prod);
  const flags = overflow ? 0x801n : 0n;
  expectAlu(out, 0x202, flags, code.length, got => {
    for (let i = 0; i < 16; i++) assert.equal(got[i], exp[i], `GPR ${i}`);
  });
  assert.equal(out.rflags & ~0x801, 0x202 & ~0x801, 'other flags preserved');
}
for (const size of [2, 4, 8]) {
  test(`IMUL 3-op ${size * 8}-bit imm8`, async () => { await checkImul3(size, true); });
  test(`IMUL 3-op ${size * 8}-bit imm${size === 2 ? 16 : 32}`, async () => { await checkImul3(size, false); });
}

// --- Differential fuzzing for new groups ---
function makeRand(seed) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
  };
}
function r64fuzz(rand) {
  let v = 0n;
  for (let i = 0; i < 8; i++) v = (v << 8n) | BigInt(Math.floor(rand() * 256));
  return v;
}

test('fuzz 8-bit ALU vs reference (500 cases, high-byte adversarial)', async () => {
  const rand = makeRand(0x8A1F);
  for (let i = 0; i < 500; i++) {
    const sub = Math.floor(rand() * 8);
    const useRex = rand() < 0.4;
    // Register indices in the modrm byte (0..7, or 0..15 with REX).
    let d = Math.floor(rand() * 8), s = Math.floor(rand() * 8);
    if (useRex && rand() < 0.5) d += 8;
    if (useRex && rand() < 0.5) s += 8;
    // Adversarial: force high-byte (4..7) without REX half the time.
    if (!useRex && rand() < 0.5) d = 4 + Math.floor(rand() * 4);
    if (!useRex && rand() < 0.5) s = 4 + Math.floor(rand() * 4);
    const isRRM = rand() < 0.5; // true: r8,r/m8 (02); false: r/m8,r8 (00)
    const code = [];
    let rex = 0;
    if (useRex) {
      rex = 0x40;
      if (d >= 8) { rex |= 0x04; d -= 8; } // REX.R extends reg field
      if (s >= 8) { rex |= 0x01; s -= 8; } // REX.B extends rm field
      // Note: for r/m8,r8, reg=s, rm=d. For r8,r/m8, reg=d, rm=s.
      // We set REX based on d/s as reg/rm; adjust below.
    }
    // Recompute: for opcode 00 (r/m,r): reg=s, rm=d. For 02 (r,r/m): reg=d, rm=s.
    let regF = isRRM ? d : s, rmF = isRRM ? s : d;
    if (useRex) {
      rex = 0x40;
      if (regF >= 8) { rex |= 0x04; regF -= 8; }
      if (rmF >= 8) { rex |= 0x01; rmF -= 8; }
      code.push(rex);
    }
    code.push(sub * 8 + (isRRM ? 2 : 0), 0xC0 | (regF << 3) | rmF);
    const regs = baseRegs();
    for (let r = 0; r < 16; r++) regs[r] = r64fuzz(rand);
    // Physical accessors: byteAccess maps 4..7 to high-byte iff !rex.
    const destIdx = isRRM ? d : d; // d is the destination modrm index (0..15)
    const srcIdx = isRRM ? s : s;
    // Reconstruct full indices (with high bits) for byteAccess.
    const dFull = isRRM ? (regF + (rex & 0x04 ? 8 : 0)) : (rmF + (rex & 0x01 ? 8 : 0));
    const sFull = isRRM ? (rmF + (rex & 0x01 ? 8 : 0)) : (regF + (rex & 0x04 ? 8 : 0));
    const hasRex = code[0] === rex && useRex;
    const dAcc = byteAccess(regs, dFull, hasRex);
    const sAcc = byteAccess(regs, sFull, hasRex);
    const { r, flags } = refAlu(sub, dAcc.get(), sAcc.get(), 1);
    const out = await runAlu(code, regs, 0x202);
    const exp = [...regs];
    if (sub !== 7) {
      const eAcc = byteAccess(exp, dFull, hasRex);
      eAcc.set(r);
    }
    const got = out.regs;
    for (let g = 0; g < 16; g++) {
      assert.equal(got[g], exp[g], `fuzz8 i=${i} sub=${sub} GPR${g}`);
    }
    assert.equal(out.rflags, (0x202 & ~0x8D5) | Number(flags), `fuzz8 i=${i} flags`);
    assert.equal(out.ripOut, (0x12345678ABCDEFF0n + BigInt(code.length)) & mask64, `fuzz8 i=${i} rip`);
  }
});

test('fuzz 16-bit ALU vs reference (300 cases)', async () => {
  const rand = makeRand(0x16B1);
  for (let i = 0; i < 300; i++) {
    const sub = Math.floor(rand() * 8);
    const isRRM = rand() < 0.5;
    const d = Math.floor(rand() * 8), s = Math.floor(rand() * 8);
    const regF = isRRM ? d : s, rmF = isRRM ? s : d;
    const code = [0x66, sub * 8 + (isRRM ? 3 : 1), 0xC0 | (regF << 3) | rmF];
    const regs = baseRegs();
    for (let r = 0; r < 16; r++) regs[r] = r64fuzz(rand);
    const dv = regs[d] & 0xFFFFn, sv = regs[s] & 0xFFFFn;
    const { r, flags } = refAlu(sub, dv, sv, 2);
    const out = await runAlu(code, regs, 0x202);
    const exp = [...regs];
    if (sub !== 7) exp[d] = (exp[d] & ~0xFFFFn) | r;
    const got = out.regs;
    for (let g = 0; g < 16; g++) assert.equal(got[g], exp[g], `fuzz16 i=${i} GPR${g}`);
    assert.equal(out.rflags, (0x202 & ~0x8D5) | Number(flags), `fuzz16 i=${i} flags`);
  }
});

test('fuzz MOVX vs reference (240 cases)', async () => {
  const rand = makeRand(0x0F5E);
  const ops = [0xB6, 0xB7, 0xBE, 0xBF];
  for (let i = 0; i < 240; i++) {
    const op2 = ops[Math.floor(rand() * 4)];
    const sub = ops.indexOf(op2);
    const srcSize = (sub === 0 || sub === 2) ? 1 : 2;
    const signed = sub >= 2;
    const destSize = [2, 4, 8][Math.floor(rand() * 3)];
    const d = Math.floor(rand() * 8), s = Math.floor(rand() * 8);
    const code = [];
    if (destSize === 2) code.push(0x66);
    if (destSize === 8) code.push(0x48);
    code.push(0x0F, op2, 0xC0 | (d << 3) | s);
    const regs = baseRegs();
    for (let r = 0; r < 16; r++) regs[r] = r64fuzz(rand);
    // Source may be a high-byte register (AH/BH/CH/DH) if s in 4..7 and no REX.
    // REX.W is present for 64-bit dest, so pass rex=true in that case.
    const hasRex = destSize === 8;
    let v;
    if (srcSize === 1) v = byteAccess(regs, s, hasRex).get();
    else v = regs[s] & 0xFFFFn;
    if (signed) v = srcSize === 1 ? BigInt.asIntN(8, v) : BigInt.asIntN(16, v);
    v = BigInt.asUintN(64, v);
    const out = await runAlu(code, regs, 0x202);
    const exp = [...regs];
    if (destSize === 2) exp[d] = (exp[d] & ~0xFFFFn) | (v & 0xFFFFn);
    else if (destSize === 4) exp[d] = v & 0xFFFFFFFFn;
    else exp[d] = v;
    const got = out.regs;
    for (let g = 0; g < 16; g++) assert.equal(got[g], exp[g], `fuzzMovx i=${i} GPR${g}`);
    assert.equal(out.rflags, 0x202, `fuzzMovx i=${i} flags preserved`);
  }
});

test('fuzz IMUL 1-op vs reference (200 cases)', async () => {
  const rand = makeRand(0xF607);
  for (let i = 0; i < 200; i++) {
    const size = [1, 2, 4, 8][Math.floor(rand() * 4)];
    const s = Math.floor(rand() * 8);
    const code = [];
    if (size === 8) code.push(0x48); else if (size === 2) code.push(0x66);
    code.push(size === 1 ? 0xF6 : 0xF7, 0xC0 | (5 << 3) | s);
    const regs = baseRegs();
    for (let r = 0; r < 16; r++) regs[r] = r64fuzz(rand);
    const w = size * 8, wmask = (1n << BigInt(w)) - 1n;
    const sa = BigInt.asIntN(w, regs[0] & wmask);
    // 8-bit source may be high-byte (no REX in this fuzz).
    const sbRaw = size === 1 ? byteAccess(regs, s, false).get() : regs[s] & wmask;
    const sb = BigInt.asIntN(w, sbRaw);
    const prod = sa * sb;
    const out = await runAlu(code, regs, 0x202);
    const exp = [...regs];
    let overflow;
    if (size === 1) {
      exp[0] = (exp[0] & ~0xFFFFn) | BigInt.asUintN(16, prod);
      overflow = BigInt.asIntN(8, BigInt.asUintN(8, prod)) !== prod;
    } else if (size === 2) {
      exp[0] = (exp[0] & ~0xFFFFn) | BigInt.asUintN(16, prod);
      exp[2] = (exp[2] & ~0xFFFFn) | BigInt.asUintN(16, prod >> 16n);
      overflow = BigInt.asIntN(16, BigInt.asUintN(16, prod)) !== prod;
    } else if (size === 4) {
      exp[0] = BigInt.asUintN(32, prod);
      exp[2] = BigInt.asUintN(32, prod >> 32n);
      overflow = BigInt.asIntN(32, BigInt.asUintN(32, prod)) !== prod;
    } else {
      exp[0] = BigInt.asUintN(64, prod);
      exp[2] = BigInt.asUintN(64, prod >> 64n);
      overflow = BigInt.asIntN(64, BigInt.asUintN(64, prod)) !== prod;
    }
    const got = out.regs;
    for (let g = 0; g < 16; g++) assert.equal(got[g], exp[g], `fuzzImul1 i=${i} GPR${g}`);
    const expFlags = (0x202 & ~0x801) | (overflow ? 0x801 : 0);
    assert.equal(out.rflags, expFlags, `fuzzImul1 i=${i} flags`);
  }
});

test('fuzz IMUL 3-op vs reference (200 cases)', async () => {
  const rand = makeRand(0x69B1);
  for (let i = 0; i < 200; i++) {
    const size = [2, 4, 8][Math.floor(rand() * 3)];
    const useImm8 = rand() < 0.5;
    const d = Math.floor(rand() * 8), s = Math.floor(rand() * 8);
    const imm = useImm8 ? BigInt(Math.floor(rand() * 256) - 128)
                       : BigInt(Math.floor(rand() * 4294967296) - 2147483648);
    const code = [];
    if (size === 8) code.push(0x48); else if (size === 2) code.push(0x66);
    if (useImm8) {
      code.push(0x6B, 0xC0 | (d << 3) | s, Number(imm & 0xFFn));
    } else if (size === 2) {
      code.push(0x69, 0xC0 | (d << 3) | s, Number(imm & 0xFFn), Number((imm >> 8n) & 0xFFn));
    } else {
      code.push(0x69, 0xC0 | (d << 3) | s,
        Number(imm & 0xFFn), Number((imm >> 8n) & 0xFFn),
        Number((imm >> 16n) & 0xFFn), Number((imm >> 24n) & 0xFFn));
    }
    const regs = baseRegs();
    for (let r = 0; r < 16; r++) regs[r] = r64fuzz(rand);
    const w = size * 8;
    const sa = BigInt.asIntN(w, regs[s] & ((1n << BigInt(w)) - 1n));
    const prod = sa * imm;
    const out = await runAlu(code, regs, 0x202);
    const exp = [...regs];
    const r = BigInt.asUintN(w, prod);
    if (size === 2) exp[d] = (exp[d] & ~0xFFFFn) | r;
    else exp[d] = size === 4 ? r : BigInt.asUintN(64, prod);
    const overflow = BigInt.asIntN(w, r) !== prod;
    const got = out.regs;
    for (let g = 0; g < 16; g++) assert.equal(got[g], exp[g], `fuzzImul3 i=${i} GPR${g}`);
    const expFlags = (0x202 & ~0x801) | (overflow ? 0x801 : 0);
    assert.equal(out.rflags, expFlags, `fuzzImul3 i=${i} flags`);
  }
});
