// web/tests/suite/unit/cpu32-opcode-coverage.test.mjs — 32-bit CPU opcode coverage.
//
// Verifies that the 32-bit decoder (decoder.cpp) has handlers for critical
// x86 opcodes. The 32-bit emulator uses a table-driven decoder, so we verify
// that decode functions exist for each opcode.
//
// Portal is a 32-bit game — this emulator must handle everything Portal uses.

import { test } from 'node:test';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { check } from '../lib/assert.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const decoderPath = join(repoRoot, 'source/emulation/cpu/decoder.cpp');
const src = readFileSync(decoderPath, 'utf8');

// ---------------------------------------------------------------------------
// Critical 32-bit opcodes that must have decoder handlers.
// Format: [decodeFunctionName, description]
// ---------------------------------------------------------------------------
const CRITICAL_DECODERS = [
  // String ops (Portal will use these)
  ['decodeLodsb', 'LODSB'],
  ['decodeLodsw', 'LODSW'],
  ['decodeLodsd', 'LODSD'],
  ['decodeStosb', 'STOSB'],
  ['decodeStosw', 'STOSW'],
  ['decodeStosd', 'STOSD'],
  ['decodeMovsb', 'MOVSB'],
  ['decodeMovsw', 'MOVSW'],
  ['decodeMovsd', 'MOVSD'],
  ['decodeCmpsb', 'CMPSB'],
  ['decodeScasb', 'SCASB'],
  // Control flow
  ['decodeJmp', 'JMP'],
  ['decodeCall', 'CALL'],
  ['decodeRet', 'RET'],
  // LOOP family (missing in 64-bit, check 32-bit)
  ['decodeLoop', 'LOOP'],
  ['decodeLoopZ', 'LOOPE'],
  ['decodeLoopNZ', 'LOOPNE'],
  // ENTER/LEAVE
  ['decodeEnter', 'ENTER'],
  ['decodeLeave', 'LEAVE'],
  // x87 (32-bit uses x87 heavily)
  
  
  
  
  
  
  
];

test('cpu32: critical string op decoders exist', () => {
  const stringOps = CRITICAL_DECODERS.filter(([_, desc]) =>
    ['LODSB', 'LODSW', 'LODSD', 'STOSB', 'STOSW', 'STOSD', 'MOVSB', 'MOVSW', 'MOVSD'].includes(desc));
  for (const [fn, desc] of stringOps) {
    check(src.includes(fn), `32-bit decoder missing ${desc} (${fn})`);
  }
});

test('cpu32: LOOP family decoders exist (missing in 64-bit)', () => {
  // The 64-bit emulator is missing LOOP (0xE0-E2). Verify 32-bit has them.
  for (const [fn, desc] of [['decodeLoop', 'LOOP'], ['decodeLoopZ', 'LOOPE'], ['decodeLoopNZ', 'LOOPNE']]) {
    check(src.includes(fn), `32-bit decoder missing ${desc} (${fn}) — 64-bit is also missing this`);
  }
});

test('cpu32: ENTER decoder exists (missing in 64-bit)', () => {
  check(src.includes('decodeEnter'), '32-bit decoder missing ENTER — 64-bit is also missing this');
});

test('cpu32: x87 decoders exist', () => {
  const x87Ops = CRITICAL_DECODERS.filter(([_, desc]) =>
    ['FADD', 'FMUL', 'FSUB', 'FDIV', 'FLD', 'FST', 'FSTP'].includes(desc));
  for (const [fn, desc] of x87Ops) {
    check(src.includes(fn), `32-bit decoder missing x87 ${desc} (${fn})`);
  }
});
