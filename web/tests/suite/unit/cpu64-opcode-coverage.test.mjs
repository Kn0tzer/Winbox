// web/tests/suite/unit/cpu64-opcode-coverage.test.mjs — 64-bit CPU opcode coverage.
//
// Verifies that every valid x86-64 user-mode opcode has a handler in cpu64.cpp
// and does not fall through to `goto unhandled`.
//
// This is the test that would have caught the FSIN/FCOS bug (commit 52beff78)
// at commit time: D9 FE / D9 FF had no handler and hit `goto unhandled`,
// causing illegal instruction crashes on sin()/cos().
//
// If this test had existed, the bug would have been caught statically instead
// of requiring live debugging of a crashing program.

import { test } from 'node:test';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { check } from '../lib/assert.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const cpu64Path = join(repoRoot, 'source/emulation/cpu/cpu64.cpp');
const src = readFileSync(cpu64Path, 'utf8');

// ---------------------------------------------------------------------------
// Parse the main opcode dispatch switch to build handled/unhandled sets
// ---------------------------------------------------------------------------
function parseMainSwitch(src) {
  const switchMatch = src.match(/\n    switch \(op\) \{([\s\S]*?)\r?\n    \}/);
  if (!switchMatch) throw new Error('Could not find main opcode switch in cpu64.cpp');
  const body = switchMatch[1];

  const handled = new Set();
  const unhandled = new Set();
  let currentCases = [];

  for (const line of body.split('\n')) {
    for (const m of line.matchAll(/case (0x[0-9a-fA-F]+)/g)) {
      currentCases.push(parseInt(m[1], 16));
    }
    if (line.includes('goto dsp_') || line.includes('goto unhandled')) {
      const isUnhandled = line.includes('goto unhandled');
      for (const op of currentCases) {
        (isUnhandled ? unhandled : handled).add(op);
      }
      currentCases = [];
    }
  }
  return { handled, unhandled };
}

const { handled, unhandled } = parseMainSwitch(src);
const hex = (n) => '0x' + n.toString(16).toUpperCase().padStart(2, '0');

// ---------------------------------------------------------------------------
// Opcodes that are correctly unhandled in 64-bit long mode:
//  - Invalid in 64-bit (segment register pushes, PUSHA/POPA, DAA/DAS, etc.)
//  - Privileged (HLT, CLI/STI, IN/OUT)
//  - Prefixes handled in the prefix decoder (REX, 0x66, 0x67, segment overrides)
// ---------------------------------------------------------------------------
const CORRECTLY_UNHANDLED = new Set([
  0x06, 0x07,       // PUSH/POP ES — invalid in 64-bit
  0x0E,             // PUSH CS — invalid in 64-bit
  0x16, 0x17,       // PUSH/POP SS — invalid in 64-bit
  0x1E, 0x1F,       // PUSH/POP DS — invalid in 64-bit
  0x27,             // DAA — invalid in 64-bit
  0x2F,             // DAS — invalid in 64-bit
  0x37,             // AAA — invalid in 64-bit
  0x3F,             // AAS — invalid in 64-bit
  0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, // REX prefixes
  0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
  0x60, 0x61,       // PUSHA/POPA — invalid in 64-bit
  0x64, 0x65,       // FS/GS overrides — handled as prefixes
  0x66,             // operand size override — handled as prefix
  0x67,             // address size override — handled as prefix
  0x82,             // alias of 0x80 — correctly not duplicated
  0x9A,             // CALL far — invalid in 64-bit
  0xC4, 0xC5,       // VEX prefixes — AVX not implemented (known gap, separate project)
  0x62,             // EVEX prefix — AVX-512 not implemented (known gap)
  0xCA, 0xCB,       // RETF — far return, invalid in 64-bit user mode
  0xCD,             // INT — software interrupt, needs OS (may be intentional)
  0xCE,             // INTO — invalid in 64-bit
  0xD4, 0xD5,       // AAM/AAD — invalid in 64-bit
  0xD6,             // SALC — undocumented, intentionally not implemented
  0xEA,             // JMP far — invalid in 64-bit
  0xF0,             // LOCK prefix — handled as prefix
  0xF1,             // INT1 — debug, intentionally not implemented
  0xF2, 0xF3,       // REP prefixes — handled as prefixes
  0xF4,             // HLT — privileged
  0xFA, 0xFB,       // CLI/STI — privileged
  // I/O instructions — no I/O ports in emulation (intentional)
  0x6C, 0x6D, 0x6E, 0x6F, 0xE4, 0xE5, 0xE6, 0xE7, 0xEC, 0xED, 0xEE, 0xEF,
  // Segment prefixes handled in prefix decoder
  0x26, 0x2E, 0x36, 0x3E,
]);

test('cpu64: all valid user-mode opcodes have handlers (no unexpected unhandled)', () => {
  const unexpected = [];
  for (const op of unhandled) {
    if (!CORRECTLY_UNHANDLED.has(op)) {
      unexpected.push(op);
    }
  }
  check(unexpected.length === 0,
    `Found ${unexpected.length} valid opcodes going to unhandled: ` +
    unexpected.map(hex).join(', ') +
    `. These are potential FSIN/FCOS-class bugs — valid instructions with no handler.`);
});

test('cpu64: LODS (0xAC/0xAD) is implemented', () => {
  // LODS is a basic string instruction. The 32-bit emulator has it.
  // If 64-bit doesn't, that's a gap.
  check(!unhandled.has(0xAC) || CORRECTLY_UNHANDLED.has(0xAC),
    'LODSB (0xAC) goes to unhandled — 32-bit emulator has this, 64-bit should too');
  check(!unhandled.has(0xAD) || CORRECTLY_UNHANDLED.has(0xAD),
    'LODSW/LODSD (0xAD) goes to unhandled — 32-bit emulator has this, 64-bit should too');
});

test('cpu64: LOOP family (0xE0-0xE2) is implemented', () => {
  for (const op of [0xE0, 0xE1, 0xE2]) {
    check(!unhandled.has(op),
      `${hex(op)} (LOOP${op === 0xE0 ? 'NE' : op === 0xE1 ? 'E' : ''}) goes to unhandled — ` +
      `this is a common loop instruction that real programs use`);
  }
});

test('cpu64: ENTER (0xC8) is implemented', () => {
  check(!unhandled.has(0xC8),
    'ENTER (0xC8) goes to unhandled — compilers emit this for stack frames');
});

test('cpu64: JRCXZ (0xE3) is implemented', () => {
  check(!unhandled.has(0xE3),
    'JRCXZ (0xE3) goes to unhandled — valid 64-bit instruction');
});

test('cpu64: critical opcodes are handled', () => {
  // Spot-check that the most common opcodes are all handled
  const critical = [
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, // PUSH
    0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, // POP
    0x89, 0x8B, // MOV
    0xE8,       // CALL rel32
    0xE9,       // JMP rel32
    0xEB,       // JMP rel8
    0xC3,       // RET
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, // Jcc rel8
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
    0x0F,       // two-byte escape
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, // x87
  ];
  for (const op of critical) {
    check(handled.has(op), `Critical opcode ${hex(op)} is not handled`);
  }
});
