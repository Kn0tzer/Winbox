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
  const { instance } = await WebAssembly.instantiate(module, { env: { memory } });
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
  const { instance } = await WebAssembly.instantiate(readFileSync(path), { env: { memory } });
  for (const ptr of [65536 - 128, -1, -128]) {
    assert.throws(() => instance.exports.execute(ptr), WebAssembly.RuntimeError);
    assert.ok(bytes.every(value => value === 0xA5), 'trap must leave memory unchanged');
  }
});

for (const [name, code] of [
  ['empty block', []],
  ['guest-memory MOV', [0x48, 0x89, 0x00]], ['MOVZX source width', [0x0F, 0xB6, 0xC0]],
  ['16-bit MOV', [0x66, 0xB8, 0x42, 0]], ['REP prefix', [0xF3, 0xB8, 0x42, 0, 0, 0]],
  ['16-bit ADD', [0x66, 0x01, 0xC0]], ['memory ADD', [0x48, 0x01, 0x00]],
  ['rotate group (ROL)', [0x48, 0xC1, 0xC0, 0x04]], ['rotate group (ROR)', [0x48, 0xC1, 0xC8, 0x04]],
  ['rotate group (RCL)', [0x48, 0xC1, 0xD0, 0x04]], ['rotate group (RCR)', [0x48, 0xC1, 0xD8, 0x04]],
  ['IMUL', [0x48, 0x0F, 0xAF, 0xC3]],
  ['unsupported suffix after MOV', [...movabs(0, 1n), 0x48, 0xC1, 0xC8, 0x04]],
  ['block longer than 24 operations', Array.from({ length: 25 }, () => movabs(0, 1n)).flat()],
]) {
  test(`whole-block fallback rejects ${name}`, () => assert.equal(emit(code).result.status, 2));
}

// --- ALU reference model (mirrors cpu64.cpp flagsAdd/flagsSub/flagsLogic) ---
const ALU_MASK = 0x8D5;
function refAlu(sub, a, b, width, carryIn = 0n) {
  const mask = width === 4 ? 0xFFFFFFFFn : mask64;
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
  const { instance } = await WebAssembly.instantiate(readFileSync(path), { env: { memory } });
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
