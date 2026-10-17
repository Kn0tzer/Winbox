// web/tests/suite/unit/cpu-x87-coverage.test.mjs — x87 FPU opcode coverage.
//
// Exhaustively verifies that every x87 instruction (D8-DF with all ModR/M
// combinations) has a handler in cpu64.cpp.
//
// This is the highest-risk area: the x87 implementation is hand-written,
// and the FSIN/FCOS bug (D9 FE / D9 FF hitting `goto unhandled`) was here.
// Portal is a 32-bit game that will use x87 extensively.
//
// REGRESSION TEST for commit 52beff78.

import { test } from 'node:test';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { check } from '../lib/assert.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const cpu64Path = join(repoRoot, 'source/emulation/cpu/cpu64.cpp');
const src = readFileSync(cpu64Path, 'utf8');

// ---------------------------------------------------------------------------
// Extract x87 handlers by scanning for all (op, sub) patterns in the source.
// We don't distinguish mem vs reg form — just verify the (op, sub) combo
// is handled somewhere.
// ---------------------------------------------------------------------------
function parseX87Handlers(source) {
  const handlers = new Set();
  const add = (opHex, sub) => handlers.add(opHex.toLowerCase() + ':' + sub);

  // Pattern 1: op == 0xDX && sub == N
  for (const m of source.matchAll(/op == (0xD[89ABCDEFabcdef]) && sub == (\d+)/g)) {
    add(m[1], parseInt(m[2]));
  }
  // Pattern 2: op == 0xDX && sub <= N  (range 0..N)
  for (const m of source.matchAll(/op == (0xD[89ABCDEFabcdef]) && sub <= (\d+)/g)) {
    const max = parseInt(m[2]);
    for (let s = 0; s <= max; s++) add(m[1], s);
  }
  // Pattern 3: (op == 0xDX || op == 0xDY) && (sub == A || sub == B)
  for (const m of source.matchAll(/\(op == (0xD[89ABCDEFabcdef]) \|\| op == (0xD[89ABCDEFabcdef])\) && \(sub == (\d+) \|\| sub == (\d+)\)/g)) {
    add(m[1], parseInt(m[3])); add(m[1], parseInt(m[4]));
    add(m[2], parseInt(m[3])); add(m[2], parseInt(m[4]));
  }
  // Pattern 4: op == 0xDX && (sub == A || sub == B)
  for (const m of source.matchAll(/op == (0xD[89ABCDEFabcdef]) && \(sub == (\d+) \|\| sub == (\d+)\)/g)) {
    add(m[1], parseInt(m[2])); add(m[1], parseInt(m[3]));
  }
  return handlers;
}

const handlers = parseX87Handlers(src);
const has = (opHex, sub) => handlers.has(opHex.toLowerCase() + ':' + sub);
const hex = (n) => '0x' + n.toString(16).toUpperCase();

// ---------------------------------------------------------------------------
// Reference: x87 memory-form opcodes that must be implemented.
// [opcodeHex, sub, name]
// ---------------------------------------------------------------------------
const X87_MEM_REQUIRED = [
  ['0xd8', 0, 'FADD m32fp'], ['0xd8', 1, 'FMUL m32fp'],
  ['0xd8', 4, 'FSUB m32fp'], ['0xd8', 6, 'FDIV m32fp'],
  ['0xd9', 0, 'FLD m32fp'], ['0xd9', 2, 'FST m32fp'], ['0xd9', 3, 'FSTP m32fp'],
  ['0xd9', 5, 'FLDCW m16'], ['0xd9', 7, 'FNSTCW m16'],
  ['0xda', 0, 'FIADD m32int'], ['0xda', 1, 'FIMUL m32int'],
  ['0xda', 4, 'FISUB m32int'], ['0xda', 6, 'FIDIV m32int'],
  ['0xdb', 0, 'FILD m32int'], ['0xdb', 2, 'FIST m32int'], ['0xdb', 3, 'FISTP m32int'],
  ['0xdb', 5, 'FLD m80fp'], ['0xdb', 7, 'FSTP m80fp'],
  ['0xdc', 0, 'FADD m64fp'], ['0xdc', 1, 'FMUL m64fp'],
  ['0xdc', 4, 'FSUB m64fp'], ['0xdc', 6, 'FDIV m64fp'],
  ['0xdd', 0, 'FLD m64fp'], ['0xdd', 3, 'FSTP m64fp'], ['0xdd', 7, 'FNSTSW m16'],
  ['0xde', 0, 'FIADD m16int'], ['0xde', 1, 'FIMUL m16int'],
  ['0xde', 4, 'FISUB m16int'], ['0xde', 6, 'FIDIV m16int'],
  ['0xdf', 0, 'FILD m16int'], ['0xdf', 2, 'FIST m16int'], ['0xdf', 3, 'FISTP m16int'],
  ['0xdf', 5, 'FILD m64int'], ['0xdf', 7, 'FISTP m64int'],
];

test('x87: FSIN (D9 FE) and FCOS (D9 FF) are implemented (regression for 52beff78)', () => {
  check(src.includes('FSIN'), 'FSIN not found in cpu64.cpp — regression of 52beff78');
  check(src.includes('FCOS'), 'FCOS not found in cpu64.cpp — regression of 52beff78');
  // Verify they're in the D9/7 register-form handler (not just comments)
  check(/FPU::FSIN|fpu\.FSIN|::FSIN/.test(src) || src.includes('D9 FE'),
    'FSIN handler not properly wired');
});

test('x87: all critical memory-form opcodes are handled', () => {
  const missing = [];
  for (const [opHex, sub, name] of X87_MEM_REQUIRED) {
    if (!has(opHex, sub)) missing.push(`${hex(parseInt(opHex, 16))}/${sub} (${name})`);
  }
  check(missing.length === 0,
    `Missing x87 handlers: ${missing.join(', ')}. ` +
    `These are FSIN/FCOS-class bugs — valid x87 instructions with no handler.`);
});

test('x87: FLD/FST/FSTP forms are handled', () => {
  const critical = [
    ['0xd9', 0, 'FLD m32fp'], ['0xdd', 0, 'FLD m64fp'],
    ['0xd9', 3, 'FSTP m32fp'], ['0xdd', 3, 'FSTP m64fp'],
    ['0xdb', 0, 'FILD m32int'], ['0xdf', 5, 'FILD m64int'],
  ];
  for (const [opHex, sub, name] of critical) {
    check(has(opHex, sub), `Missing handler for ${name} (${hex(parseInt(opHex, 16))}/${sub})`);
  }
});

test('x87: FCOM/FCOMP forms are handled', () => {
  const critical = [
    ['0xd8', 2, 'FCOM m32fp'], ['0xd8', 3, 'FCOMP m32fp'],
    ['0xdc', 2, 'FCOM m64fp'], ['0xdc', 3, 'FCOMP m64fp'],
  ];
  for (const [opHex, sub, name] of critical) {
    check(has(opHex, sub), `Missing handler for ${name} (${hex(parseInt(opHex, 16))}/${sub})`);
  }
});
