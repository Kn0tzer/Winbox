#!/usr/bin/env node
// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
// gen-jit64.mjs — table-driven JIT emitter generator.
//
// Reads source/emulation/cpu/jit64table.mjs and emits
// source/emulation/cpu/jit64gen.inc (C++ emitter functions, included into
// jit64wasm.cpp's anonymous namespace).
//
// Usage:
//   node scripts/gen-jit64.mjs          # regenerate jit64gen.inc
//   node scripts/gen-jit64.mjs --check  # verify jit64gen.inc is in sync
//
// The byte-identical bar: every generated function must produce
// byte-identical wasm output to the hand-written emitter it replaces,
// proven by source/emulation/cpu/tests/jit64_gen_tests.cpp.

import { EMITTERS } from "../source/emulation/cpu/jit64table.mjs";
import { writeFileSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const OUT_PATH = join(ROOT, "source/emulation/cpu/jit64gen.inc");

// dest keyword -> C++ expression for the destination GPR index.
const DEST_EXPR = { rm: "op.rmIndex", reg: "op.regField", acc: "0" };

function condFor(kinds) {
  return kinds.map((k) => `op.kind == ${k}`).join(" || ");
}

// Group wiring entries by a key function, preserving first-seen order.
// Returns [[key, [entries]], ...].
function groupBy(wiring, keyFn) {
  const groups = new Map();
  for (const w of wiring) {
    const k = keyFn(w);
    if (!groups.has(k)) groups.set(k, []);
    groups.get(k).push(w);
  }
  return [...groups.entries()];
}

// "alu" template: register-direct ALU/TEST (emitAlu shape).
// L_A = width-masked lhs, L_B = width-masked rhs; compute via
// emitAluCompute; writeback per the standard rule.
function genAlu(e) {
  const L = [];

  L.push(`U32 ${e.func}(Bytes& body, const Jit64Op& op) {`);
  L.push(`    const bool isTest = ${e.isTest};`);
  L.push(
    `    const bool isLogic = isTest || op.sub == 1 || op.sub == 4 || op.sub == 6;`
  );
  L.push(
    `    const U8 effSub = isTest ? 4 : op.sub; // TEST computes flags like AND`
  );
  L.push(`    const U64 mask = widthMask(op.size);`);
  L.push(`    const U64 sb = widthSB(op.size);`);

  // Destination GPR selection. The chain is exhaustive by construction
  // (every kind appears in exactly one group), so the last group takes
  // the final else.
  L.push(`    U32 destReg;`);
  const destGroups = groupBy(e.wiring, (w) => w.dest);
  destGroups.forEach(([dest, ws], i) => {
    if (i === destGroups.length - 1) {
      L.push(`    else destReg = ${DEST_EXPR[dest]};`);
    } else {
      const kw = i === 0 ? "if" : "else if";
      L.push(
        `    ${kw} (${condFor(ws.map((w) => w.kind))}) destReg = ${DEST_EXPR[dest]};`
      );
    }
  });

  // L_A sourcing.
  L.push(`    // L_A = width-masked lhs.`);
  const aGroups = groupBy(e.wiring, (w) => w.a);
  if (aGroups.length === 1 && aGroups[0][0] === "dest") {
    L.push(`    emitReadOperand(body, op, destReg, L_A);`);
  } else {
    throw new Error("alu template: only a='dest' implemented so far");
  }

  // L_B sourcing.
  L.push(`    // L_B = width-masked rhs.`);
  const bGroups = groupBy(e.wiring, (w) =>
    w.b === "imm" ? "imm" : "reg"
  );
  const immGroup = bGroups.find(([k]) => k === "imm");
  const regGroup = bGroups.find(([k]) => k === "reg");
  if (immGroup && regGroup) {
    L.push(
      `    if (${condFor(immGroup[1].map((w) => w.kind))}) {`
    );
    L.push(`        i64Const(body, op.imm & mask);`);
    L.push(`        local(body, 0x21, L_B);`);
    L.push(`    } else {`);
    // srcReg selection among the reg kinds (exhaustive: every non-imm
    // kind is a reg kind, so the last group takes the final else).
    const regExprs = groupBy(regGroup[1], (w) => w.b);
    const regCpp = (bexpr) => bexpr.slice("reg:".length);
    if (regExprs.length === 1) {
      L.push(`        emitReadOperand(body, op, ${regCpp(regExprs[0][0])}, L_B);`);
    } else {
      L.push(`        U32 srcReg;`);
      regExprs.forEach(([bexpr, ws], i) => {
        if (i === regExprs.length - 1) {
          L.push(`        else srcReg = ${regCpp(bexpr)};`);
        } else {
          const kw = i === 0 ? "if" : "else if";
          L.push(
            `        ${kw} (${condFor(ws.map((w) => w.kind))}) srcReg = ${regCpp(bexpr)};`
          );
        }
      });
      L.push(`        emitReadOperand(body, op, srcReg, L_B);`);
    }
    L.push(`    }`);
  } else if (immGroup) {
    L.push(`    i64Const(body, op.imm & mask);`);
    L.push(`    local(body, 0x21, L_B);`);
  } else {
    throw new Error("alu template: no imm/reg b-groups?!");
  }

  L.push(`    if (op.sub == 2 || op.sub == 3) emitAluCarryFold(body, mask);`);
  L.push(`    emitAluCompute(body, effSub, isLogic, mask, sb);`);
  L.push(
    `    if (isTest || op.sub == 7) return 0xFF; // TEST/CMP write no register`
  );
  L.push(
    `    if (op.size == 1) { emitWriteByte(body, op, destReg, L_R); return 0xFF; }`
  );
  L.push(
    `    if (op.size == 2) { emitWriteWord(body, destReg, L_R); return 0xFF; }`
  );
  L.push(`    return destReg;`);
  L.push(`}`);
  return L.join("\n");
}

// "alu" template, mem variant: memory-operand ALU/TEST (emitAluMem shape).
// EA into L_T1 via emitEA; fault ordering memRead -> compute+flags ->
// memWrite. L_A/L_B sourcing has two patterns (regA vs memA).
function genAluMem(e) {
  const L = [];
  const regCpp = (x) => x.slice("reg:".length);

  const regAKinds = e.wiring.filter((w) => w.a.startsWith("reg:"));
  const memAKinds = e.wiring.filter((w) => w.a === "mem");
  const immKinds = e.wiring.filter((w) => w.b === "imm");
  const regBKinds = e.wiring.filter((w) => w.b.startsWith("reg:"));
  const regDestKinds = e.wiring.filter((w) => w.dest === "reg");
  if (!regAKinds.length || !memAKinds.length)
    throw new Error("alu/mem template needs both regA and memA kinds");
  const regAExprs = [...new Set(regAKinds.map((w) => w.a))];
  const regBExprs = [...new Set(regBKinds.map((w) => w.b))];
  if (regAExprs.length !== 1) throw new Error("alu/mem: want one regA expr");
  if (regBExprs.length !== 1) throw new Error("alu/mem: want one regB expr");
  // For a "reg" dest, the destination GPR is the register operand
  // (the same register sourced into L_A).
  const destRegExpr = regCpp(regAExprs[0]);

  L.push(`U32 ${e.func}(Bytes& body, const Jit64Op& op) {`);
  L.push(`    const bool isTest = ${e.isTest};`);
  L.push(
    `    const bool isLogic = isTest || op.sub == 1 || op.sub == 4 || op.sub == 6;`
  );
  L.push(
    `    const U8 effSub = isTest ? 4 : op.sub; // TEST computes flags like AND`
  );
  L.push(`    const U64 mask = widthMask(op.size);`);
  L.push(`    const U64 sb = widthSB(op.size);`);
  L.push(`    emitEA(body, op); // L_T1 = effective address`);
  // A/B sourcing.
  L.push(`    if (${condFor(regAKinds.map((w) => w.kind))}) {`);
  L.push(`        // L_A = reg (dest old value), L_B = mem.`);
  L.push(`        emitReadOperand(body, op, ${regCpp(regAExprs[0])}, L_A);`);
  L.push(`        emitMemRead(body, L_T1, op.size); // -> L_R (zero-extended)`);
  L.push(`        local(body, 0x20, L_R);`);
  L.push(`        i64Const(body, mask);`);
  L.push(`        body.push_back(0x83);`);
  L.push(`        local(body, 0x21, L_B);`);
  L.push(`    } else {`);
  L.push(`        // L_A = mem (dest old value), L_B = reg or imm.`);
  L.push(`        emitMemRead(body, L_T1, op.size); // -> L_R (zero-extended)`);
  L.push(`        local(body, 0x20, L_R);`);
  L.push(`        i64Const(body, mask);`);
  L.push(`        body.push_back(0x83);`);
  L.push(`        local(body, 0x21, L_A);`);
  if (immKinds.length) {
    L.push(`        if (${condFor(immKinds.map((w) => w.kind))}) {`);
    L.push(`            i64Const(body, op.imm & mask);`);
    L.push(`            local(body, 0x21, L_B);`);
    L.push(`        } else {`);
    L.push(
      `            emitReadOperand(body, op, ${regCpp(regBExprs[0])}, L_B);`
    );
    L.push(`        }`);
  } else {
    L.push(`        emitReadOperand(body, op, ${regCpp(regBExprs[0])}, L_B);`);
  }
  L.push(`    }`);
  L.push(`    if (op.sub == 2 || op.sub == 3) emitAluCarryFold(body, mask);`);
  L.push(`    emitAluCompute(body, effSub, isLogic, mask, sb);`);
  L.push(`    if (isTest || op.sub == 7) return 0xFF; // TEST/CMP write nothing`);
  if (regDestKinds.length) {
    L.push(`    if (${condFor(regDestKinds.map((w) => w.kind))}) {`);
    L.push(`        // Destination is the register.`);
    L.push(
      `        if (op.size == 1) { emitWriteByte(body, op, ${destRegExpr}, L_R); return 0xFF; }`
    );
    L.push(
      `        if (op.size == 2) { emitWriteWord(body, ${destRegExpr}, L_R); return 0xFF; }`
    );
    L.push(`        return ${destRegExpr};`);
    L.push(`    }`);
  }
  L.push(`    // Destination is memory: write back through the imported helper.`);
  L.push(`    emitMemWrite(body, L_T1, L_R, op.size);`);
  L.push(`    return 0xFF;`);
  L.push(`}`);
  return L.join("\n");
}

// "shift" template: register-direct SHL/SHR/SAR (emitShift shape).
// The op carries all variation (sub/size/imm); the template documents the
// contract and generates the proven body under the table-given name.
function genShift(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U64 count = op.imm; // already masked to 0x1F/0x3F at decode
    const U32 destReg = op.rmIndex;
    const U64 mask = widthMask(op.size);
    const U64 sb = widthSB(op.size);
    const U64 wbits = (U64)op.size * 8;
    const U8 sub = op.sub == 6 ? 4 : op.sub; // /6 aliases SHL
    if (count == 0) {
        if (op.size == 4) {
            // The interpreter zero-extends the 32-bit destination even when
            // the count is 0 (cpu64.cpp BK_SHIFT_IMM / step shift path do
            // reg[rm].setU32((U32)v)); flags are untouched. Publish the
            // width-masked value via L_R so the caller stores it back.
            emitReadOperand(body, op, destReg, L_R);
            return destReg;
        }
        return 0xFF; // count==0: pure no-op for 1/2/8-bit, value and flags untouched
    }
    // L_A = width-masked operand.
    emitReadOperand(body, op, destReg, L_A);
    // L_R = shift result. The shift amount is pushed after any
    // sign-extension so the operand stack stays [value, amount].
    local(body, 0x20, L_A);
    if (sub == 4) {
        i64Const(body, count);
        body.push_back(0x86); // i64.shl
    } else if (sub == 5) {
        i64Const(body, count);
        body.push_back(0x88); // i64.shr_u (L_A already width-masked)
    } else { // SAR: sign-extend the width, arithmetic shift, mask.
        emitSext(body, op.size);
        i64Const(body, count);
        body.push_back(0x87); // i64.shr_s
    }
    i64Const(body, mask);
    body.push_back(0x83); // width-mask the result
    local(body, 0x21, L_R);
    // Flags: L_F accumulates the five shift flag bits.
    i64Const(body, 0);
    local(body, 0x21, L_F);
    // CF: SHL: (v >> (wbits - count)) & 1; SHR/SAR: (v >> (count - 1)) & 1.
    // Stash the 0/1 bit in L_P: the SHL count==1 OF formula needs it below.
    local(body, 0x20, L_A);
    i64Const(body, sub == 4 ? wbits - count : count - 1);
    body.push_back(0x88); // i64.shr_u
    i64Const(body, 1);
    body.push_back(0x83); // i64.and
    local(body, 0x21, L_P);
    local(body, 0x20, L_P);
    body.push_back(0xA7); // i32.wrap_i64
    orFlagBit(body, F_CF);
    // OF: only defined for count == 1 (the bit stays 0 otherwise, matching
    // the interpreter clearing OF for count > 1).
    if (count == 1) {
        if (sub == 4) {
            // OF = MSB(result) XOR CF.
            local(body, 0x20, L_R);
            i64Const(body, sb);
            body.push_back(0x83);
            i64Const(body, 0);
            body.push_back(0x52); // i64.ne -> i32
            local(body, 0x20, L_P);
            body.push_back(0xA7); // i32.wrap_i64
            body.push_back(0x73); // i32.xor
            orFlagBit(body, F_OF);
        } else if (sub == 5) {
            // OF = old MSB.
            local(body, 0x20, L_A);
            i64Const(body, sb);
            body.push_back(0x83);
            i64Const(body, 0);
            body.push_back(0x52); // i64.ne -> i32
            orFlagBit(body, F_OF);
        }
        // SAR: OF = 0, nothing to emit.
    }
    // ZF.
    local(body, 0x20, L_R);
    body.push_back(0x50); // i64.eqz
    orFlagBit(body, F_ZF);
    // SF.
    local(body, 0x20, L_R);
    i64Const(body, sb);
    body.push_back(0x83);
    i64Const(body, 0);
    body.push_back(0x52);
    orFlagBit(body, F_SF);
    // PF: even parity of the low byte; PF = ((folded & 1) ^ 1).
    local(body, 0x20, L_R);
    i64Const(body, 0xFF);
    body.push_back(0x83);
    local(body, 0x21, L_P);
    const U32 shifts[] = {4, 2, 1};
    for (U32 s : shifts) {
        local(body, 0x20, L_P);
        local(body, 0x20, L_P);
        i64Const(body, s);
        body.push_back(0x88); // i64.shr_u
        body.push_back(0x85); // i64.xor
        local(body, 0x21, L_P);
    }
    local(body, 0x20, L_P);
    i64Const(body, 1);
    body.push_back(0x83); // & 1
    i64Const(body, 1);
    body.push_back(0x85); // ^ 1
    body.push_back(0xA7); // i32.wrap_i64: test results are i32
    orFlagBit(body, F_PF);
    // rflags = (rflags & ~SHIFT_MASK) | (i32)L_F; AF and the rest preserved.
    local(body, 0x20, L_RFLAGS);
    i32Const(body, F_SHIFT_MASK);
    body.push_back(0x72); // i32.or
    i32Const(body, F_SHIFT_MASK);
    body.push_back(0x73); // i32.xor
    local(body, 0x20, L_F);
    body.push_back(0xA7); // i32.wrap_i64
    body.push_back(0x72); // i32.or
    local(body, 0x21, L_RFLAGS);
    if (op.size == 1) { emitWriteByte(body, op, destReg, L_R); return 0xFF; }
    if (op.size == 2) { emitWriteWord(body, destReg, L_R); return 0xFF; }
    return destReg;
}`;
}

// "rotate" template: register-direct ROL/ROR/RCL/RCR (emitRotate shape).
function genRotate(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U64 count = op.imm; // already masked to 0x1F/0x3F at decode
    const U32 destReg = op.rmIndex;
    const U64 mask = widthMask(op.size);
    const U64 sb = widthSB(op.size);
    const U64 wbits = (U64)op.size * 8;
    const U8 sub = op.sub; // 0=ROL, 1=ROR, 2=RCL, 3=RCR
    if (count == 0) {
        if (op.size == 4) {
            // As for shifts: the interpreter zero-extends the 32-bit
            // destination even on count==0; flags untouched.
            emitReadOperand(body, op, destReg, L_R);
            return destReg;
        }
        return 0xFF; // count==0: pure no-op for 1/2/8-bit
    }
    // L_A = width-masked operand.
    emitReadOperand(body, op, destReg, L_A);
    if (sub == 0 || sub == 1) {
        // ROL/ROR: single rotate; c == count here (count already masked).
        const U64 c = count % wbits;
        local(body, 0x20, L_A);
        if (sub == 0) { // ROL: (v << c) | (v >> (wbits - c))
            i64Const(body, c);
            body.push_back(0x86); // i64.shl
            local(body, 0x20, L_A);
            i64Const(body, wbits - c);
            body.push_back(0x88); // i64.shr_u
        } else { // ROR: (v >> c) | (v << (wbits - c))
            i64Const(body, c);
            body.push_back(0x88); // i64.shr_u
            local(body, 0x20, L_A);
            i64Const(body, wbits - c);
            body.push_back(0x86); // i64.shl
        }
        body.push_back(0x84); // i64.or
        i64Const(body, mask);
        body.push_back(0x83); // width-mask
        local(body, 0x21, L_R);
        // CF: ROL: result & 1; ROR: (result & sb) != 0. Stash as i64 0/1 in L_P.
        local(body, 0x20, L_R);
        if (sub == 0) {
            i64Const(body, 1);
            body.push_back(0x83); // i64.and -> 0/1
        } else {
            i64Const(body, sb);
            body.push_back(0x83);
            i64Const(body, 0);
            body.push_back(0x52); // i64.ne -> i32
            body.push_back(0xAD); // i64.extend_i32_u
        }
        local(body, 0x21, L_P);
    } else {
        // RCL/RCR: rotate through carry, count iterations. The loop threads
        // the carry in L_P (incoming CF first) and the counter in L_B; L_R
        // holds the running result. L_A is free after the L_R init below.
        local(body, 0x20, L_RFLAGS);
        i32Const(body, 1);
        body.push_back(0x71); // i32.and -> incoming CF as 0/1
        body.push_back(0xAD); // i64.extend_i32_u
        local(body, 0x21, L_P);
        local(body, 0x20, L_A);
        local(body, 0x21, L_R);
        i64Const(body, count);
        local(body, 0x21, L_B);
        body.push_back(0x02); body.push_back(0x40); // block $done
        body.push_back(0x03); body.push_back(0x40); // loop $l
        local(body, 0x20, L_B);
        body.push_back(0x50); // i64.eqz
        body.push_back(0x0D); body.push_back(0x01); // br_if 1 ($done)
        if (sub == 2) { // RCL
            // newCf = (L_R & sb) != 0, stashed in L_A as i64 0/1.
            local(body, 0x20, L_R);
            i64Const(body, sb);
            body.push_back(0x83);
            i64Const(body, 0);
            body.push_back(0x52); // i64.ne
            body.push_back(0xAD); // i64.extend_i32_u
            local(body, 0x21, L_A);
            // L_R = ((L_R << 1) | L_P) & mask.
            local(body, 0x20, L_R);
            i64Const(body, 1);
            body.push_back(0x86); // i64.shl
            local(body, 0x20, L_P);
            body.push_back(0x84); // i64.or
            i64Const(body, mask);
            body.push_back(0x83); // width-mask
            local(body, 0x21, L_R);
            // Carry = newCf.
            local(body, 0x20, L_A);
            local(body, 0x21, L_P);
        } else { // RCR
            // newCf = L_R & 1, stashed in L_A.
            local(body, 0x20, L_R);
            i64Const(body, 1);
            body.push_back(0x83);
            local(body, 0x21, L_A);
            // L_R = (L_R >>u 1) | (L_P * sb); & mask for 32-bit.
            local(body, 0x20, L_R);
            i64Const(body, 1);
            body.push_back(0x88); // i64.shr_u
            local(body, 0x20, L_P);
            i64Const(body, sb);
            body.push_back(0x7E); // i64.mul: 0 or sb
            body.push_back(0x84); // i64.or
            i64Const(body, mask);
            body.push_back(0x83); // width-mask
            local(body, 0x21, L_R);
            local(body, 0x20, L_A);
            local(body, 0x21, L_P);
        }
        local(body, 0x20, L_B);
        i64Const(body, 1);
        body.push_back(0x7D); // i64.sub
        local(body, 0x21, L_B);
        body.push_back(0x0C); body.push_back(0x00); // br 0 ($l)
        body.push_back(0x0B); // end loop
        body.push_back(0x0B); // end block
    }
    // Flags: L_F accumulates CF and OF only.
    i64Const(body, 0);
    local(body, 0x21, L_F);
    local(body, 0x20, L_P);
    body.push_back(0xA7); // i32.wrap_i64
    orFlagBit(body, F_CF);
    // OF: only defined for count == 1 (stays 0 otherwise, matching the
    // interpreter clearing OF for count > 1).
    if (count == 1) {
        if (sub == 0 || sub == 2) {
            // OF = MSB(result) XOR CF.
            local(body, 0x20, L_R);
            i64Const(body, sb);
            body.push_back(0x83);
            i64Const(body, 0);
            body.push_back(0x52); // i64.ne -> i32
            local(body, 0x20, L_P);
            body.push_back(0xA7); // i32.wrap_i64
            body.push_back(0x73); // i32.xor
            orFlagBit(body, F_OF);
        } else {
            // OF = MSB(result) XOR MSB(result<<1).
            local(body, 0x20, L_R);
            i64Const(body, sb);
            body.push_back(0x83);
            i64Const(body, 0);
            body.push_back(0x52); // i64.ne -> i32
            local(body, 0x20, L_R);
            i64Const(body, 1);
            body.push_back(0x86); // i64.shl
            i64Const(body, sb);
            body.push_back(0x83);
            i64Const(body, 0);
            body.push_back(0x52); // i64.ne -> i32
            body.push_back(0x73); // i32.xor
            orFlagBit(body, F_OF);
        }
    }
    // rflags = (rflags & ~mask) | (i32)L_F; SZP/AF and the rest preserved.
    // OF is only defined for count==1; for count!=1 it is preserved from RFLAGS,
    // so the mask covers OF only when count==1.
    U32 rotMask = (count == 1) ? F_CFOF_MASK : (1u << F_CF);
    local(body, 0x20, L_RFLAGS);
    i32Const(body, rotMask);
    body.push_back(0x72); // i32.or
    i32Const(body, rotMask);
    body.push_back(0x73); // i32.xor
    local(body, 0x20, L_F);
    body.push_back(0xA7); // i32.wrap_i64
    body.push_back(0x72); // i32.or
    local(body, 0x21, L_RFLAGS);
    if (op.size == 1) { emitWriteByte(body, op, destReg, L_R); return 0xFF; }
    if (op.size == 2) { emitWriteWord(body, destReg, L_R); return 0xFF; }
    return destReg;
}`;
}

// "imul2" template: two-operand IMUL r, r/m (emitImul shape)
function genImul2(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U32 destReg = op.regField;
    const U32 srcReg = op.rmIndex;
    const U64 m32 = 0xFFFFFFFFULL;
    // L_A = sign-extended dest operand, L_B = sign-extended source operand.
    // (For 64-bit the raw u64 bit pattern already is the S64 operand.)
    local(body, 0x20, destReg + 1);
    emitSext(body, op.size);
    local(body, 0x21, L_A);
    local(body, 0x20, srcReg + 1);
    emitSext(body, op.size);
    local(body, 0x21, L_B);
    // L_R = wrapped 64-bit product (exact for 16/32-bit: |operands| < 2^32).
    local(body, 0x20, L_A);
    local(body, 0x20, L_B);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_R);
    // Overflow detection -> i32 0/1 on the stack.
    if (op.size == 2 || op.size == 4) {
        // 16/32-bit: the 64-bit product is exact; overflow iff it differs
        // from its own width sign extension.
        local(body, 0x20, L_R);
        emitSext(body, op.size); // sext16/32(L_R)
        local(body, 0x20, L_R);
        body.push_back(0x52); // i64.ne
    } else {
        // 64-bit: the product needs 128 bits. Compute the high 64 bits of
        // the unsigned product from 32-bit limbs, then adjust for signedness.
        //
        // Write a = a1*2^32+a0, b = b1*2^32+b0 (32-bit limbs) and
        // t0=a0*b0, t1=a1*b0, t2=a0*b1, t3=a1*b1 (all exact 64-bit). Then
        //   hi_u = t3 + s_hi + m_hi
        // where s_hi = (t1+t2)>>32 and
        // m_hi = (((t1+t2) mod 2^32)+(t0>>32))>>32. The true high word is
        // < 2^64 and every term is non-negative, so no intermediate i64.add
        // wraps. Finally
        //   signed_hi = hi_u - (a<0 ? b : 0) - (b<0 ? a : 0)  (mod 2^64)
        // and overflow iff signed_hi != sign_extend_64(lo).
        i64Const(body, 0);
        local(body, 0x21, L_P); // hi_u = 0
        // t3 = a1*b1; hi_u += t3.
        local(body, 0x20, L_A);
        i64Const(body, 32);
        body.push_back(0x88); // i64.shr_u -> a1
        local(body, 0x20, L_B);
        i64Const(body, 32);
        body.push_back(0x88); // b1
        body.push_back(0x7E); // t3 (exact, < 2^64)
        local(body, 0x20, L_P);
        body.push_back(0x7C); // i64.add
        local(body, 0x21, L_P);
        // t1 = a1*b0 -> L_T1; t2 = a0*b1 -> L_T2.
        local(body, 0x20, L_A);
        i64Const(body, 32);
        body.push_back(0x88);
        local(body, 0x20, L_B);
        i64Const(body, m32);
        body.push_back(0x83);
        body.push_back(0x7E);
        local(body, 0x21, L_T1);
        local(body, 0x20, L_A);
        i64Const(body, m32);
        body.push_back(0x83);
        local(body, 0x20, L_B);
        i64Const(body, 32);
        body.push_back(0x88);
        body.push_back(0x7E);
        local(body, 0x21, L_T2);
        // s_hi = (t1>>32)+(t2>>32)+(((t1&m32)+(t2&m32))>>32); hi_u += s_hi.
        // (t1>>32),(t2>>32) < 2^32 and the carry < 2, so s_hi < 2^33: exact.
        local(body, 0x20, L_T1);
        i64Const(body, 32);
        body.push_back(0x88);
        local(body, 0x20, L_T2);
        i64Const(body, 32);
        body.push_back(0x88);
        body.push_back(0x7C);
        local(body, 0x20, L_T1);
        i64Const(body, m32);
        body.push_back(0x83);
        local(body, 0x20, L_T2);
        i64Const(body, m32);
        body.push_back(0x83);
        body.push_back(0x7C); // lo_sum (< 2^33, exact)
        local(body, 0x22, L_T1); // tee: stash lo_sum, keep on stack
        i64Const(body, 32);
        body.push_back(0x88); // carry_s
        body.push_back(0x7C); // s_hi
        local(body, 0x20, L_P);
        body.push_back(0x7C);
        local(body, 0x21, L_P);
        // m = (lo_sum&m32)+(t0>>32) (< 2^33, exact); m_hi = m>>32; hi_u += m_hi.
        local(body, 0x20, L_T1);
        i64Const(body, m32);
        body.push_back(0x83); // s_lo32
        local(body, 0x20, L_A);
        i64Const(body, m32);
        body.push_back(0x83); // a0
        local(body, 0x20, L_B);
        i64Const(body, m32);
        body.push_back(0x83); // b0
        body.push_back(0x7E); // t0 (exact)
        i64Const(body, 32);
        body.push_back(0x88); // t0_hi
        body.push_back(0x7C); // m
        i64Const(body, 32);
        body.push_back(0x88); // m_hi
        local(body, 0x20, L_P);
        body.push_back(0x7C);
        local(body, 0x21, L_P);
        // signed_hi = hi_u - a_s*b - b_s*a (mod 2^64); overflow iff it
        // differs from shr_s(lo, 63) (0 when lo >= 0, -1 when lo < 0).
        local(body, 0x20, L_P);
        local(body, 0x20, L_A);
        i64Const(body, 63);
        body.push_back(0x88); // a_s (0/1)
        local(body, 0x20, L_B);
        body.push_back(0x7E); // a_s * b (0 or b)
        body.push_back(0x7D); // i64.sub
        local(body, 0x20, L_B);
        i64Const(body, 63);
        body.push_back(0x88); // b_s
        local(body, 0x20, L_A);
        body.push_back(0x7E); // b_s * a
        body.push_back(0x7D);
        local(body, 0x20, L_R);
        i64Const(body, 63);
        body.push_back(0x87); // i64.shr_s -> 0 or -1
        body.push_back(0x52); // i64.ne -> i32 overflow bit
    }
    // L_F = overflow ? (CF|OF) : 0. The overflow bit is 0/1, so scale it
    // with i64.mul (1 & 0x801 would stay 1, not set both flag bits).
    body.push_back(0xAD); // i64.extend_i32_u
    i64Const(body, F_CFOF_MASK);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_F);
    // Publish: 32-bit zero-extends the destination (setU32); 16-bit
    // preserves the upper 48 (setU16, RMW).
    if (op.size == 4) {
        local(body, 0x20, L_R);
        i64Const(body, m32);
        body.push_back(0x83);
        local(body, 0x21, L_R);
    }
    // rflags = (rflags & ~CFOF_MASK) | (i32)L_F; everything else preserved.
    local(body, 0x20, L_RFLAGS);
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x72); // i32.or
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x73); // i32.xor
    local(body, 0x20, L_F);
    body.push_back(0xA7); // i32.wrap_i64
    body.push_back(0x72); // i32.or
    local(body, 0x21, L_RFLAGS);
    if (op.size == 2) { emitWriteWord(body, destReg, L_R); return 0xFF; }
    return destReg;
}`;
}

// "imul1" template: one-operand IMUL r/m (emitImul1 shape)
function genImul1(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U64 m32 = 0xFFFFFFFFULL;
    // L_A = sign-extended RAX operand at width; L_B = sign-extended r/m.
    if (op.size == 1) emitReadByte(body, op, 0, L_A);
    else {
        local(body, 0x20, 0 + 1); // RAX
        if (op.size == 2) {
            i64Const(body, 0xFFFFULL);
            body.push_back(0x83);
        } else if (op.size == 4) {
            i64Const(body, m32);
            body.push_back(0x83);
        }
        local(body, 0x21, L_A);
    }
    if (op.size == 1) emitReadByte(body, op, op.rmIndex, L_B);
    else {
        local(body, 0x20, op.rmIndex + 1);
        if (op.size == 2) {
            i64Const(body, 0xFFFFULL);
            body.push_back(0x83);
        } else if (op.size == 4) {
            i64Const(body, m32);
            body.push_back(0x83);
        }
        local(body, 0x21, L_B);
    }
    local(body, 0x20, L_A);
    emitSext(body, op.size == 1 ? 1 : op.size);
    local(body, 0x21, L_A);
    local(body, 0x20, L_B);
    emitSext(body, op.size == 1 ? 1 : op.size);
    local(body, 0x21, L_B);
    // L_R = wrapped product.
    local(body, 0x20, L_A);
    local(body, 0x20, L_B);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_R);
    // Overflow -> i32 0/1 on the stack; then write the halves.
    if (op.size == 8) {
        // 128-bit check via limbs (same method as emitImul).
        // Compute signed_hi in L_P using the limb method.
        i64Const(body, 0);
        local(body, 0x21, L_P);
        local(body, 0x20, L_A);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_B);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7E);
        local(body, 0x20, L_P); body.push_back(0x7C);
        local(body, 0x21, L_P);
        local(body, 0x20, L_A);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_B);
        i64Const(body, m32); body.push_back(0x83);
        body.push_back(0x7E);
        local(body, 0x21, L_T1);
        local(body, 0x20, L_A);
        i64Const(body, m32); body.push_back(0x83);
        local(body, 0x20, L_B);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7E);
        local(body, 0x21, L_T2);
        local(body, 0x20, L_T1);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_T2);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7C);
        local(body, 0x20, L_T1);
        i64Const(body, m32); body.push_back(0x83);
        local(body, 0x20, L_T2);
        i64Const(body, m32); body.push_back(0x83);
        body.push_back(0x7C);
        local(body, 0x22, L_T1);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7C);
        local(body, 0x20, L_P); body.push_back(0x7C);
        local(body, 0x21, L_P);
        local(body, 0x20, L_T1);
        i64Const(body, m32); body.push_back(0x83);
        local(body, 0x20, L_A);
        i64Const(body, m32); body.push_back(0x83);
        local(body, 0x20, L_B);
        i64Const(body, m32); body.push_back(0x83);
        body.push_back(0x7E);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7C);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_P); body.push_back(0x7C);
        local(body, 0x21, L_P);
        // signed_hi = hi_u - a_s*b - b_s*a; compare with sext64(lo).
        local(body, 0x20, L_P);
        local(body, 0x20, L_A);
        i64Const(body, 63); body.push_back(0x88);
        local(body, 0x20, L_B);
        body.push_back(0x7E);
        body.push_back(0x7D);
        local(body, 0x20, L_B);
        i64Const(body, 63); body.push_back(0x88);
        local(body, 0x20, L_A);
        body.push_back(0x7E);
        body.push_back(0x7D);
        // Stack: [signed_hi]. Save it (L_T1/L_T2 are dead after hi_u).
        local(body, 0x21, L_T1); // L_T1 = signed_hi. Stack: [].
        // Overflow: signed_hi != sext64(lo).
        local(body, 0x20, L_T1);
        local(body, 0x20, L_R);
        i64Const(body, 63); body.push_back(0x87);
        body.push_back(0x52); // i64.ne -> i32 overflow. Stack: [overflow].
        // Write RDX:RAX = signed_hi:lo.
        local(body, 0x20, L_T1);
        local(body, 0x21, 2 + 1); // RDX (GPR 2). Stack: [overflow].
        local(body, 0x20, L_R);
        local(body, 0x21, 0 + 1); // RAX (GPR 0). Stack: [overflow].
    } else {
        // 8/16/32-bit: product exact in 64 bits; overflow iff it differs
        // from its own width sign extension.
        local(body, 0x20, L_R);
        emitSext(body, op.size == 1 ? 1 : op.size);
        local(body, 0x20, L_R);
        body.push_back(0x52); // i64.ne -> i32 overflow
        if (op.size == 1) {
            // AX = prod (setU16, upper RAX preserved).
            emitWriteWord(body, 0, L_R);
        } else if (op.size == 2) {
            // DX:AX = prod (setU16 halves).
            local(body, 0x20, L_R);
            i64Const(body, 0xFFFFULL); body.push_back(0x83);
            local(body, 0x21, L_T1);
            emitWriteWord(body, 0, L_T1); // AX
            local(body, 0x20, L_R);
            i64Const(body, 16); body.push_back(0x88); // hi
            i64Const(body, 0xFFFFULL); body.push_back(0x83);
            local(body, 0x21, L_T1);
            emitWriteWord(body, 2, L_T1); // DX (GPR 2)
        } else {
            // EDX:EAX: full 64-bit writes of the zero-extended halves.
            local(body, 0x20, L_R);
            i64Const(body, m32); body.push_back(0x83);
            local(body, 0x21, 0 + 1); // RAX = lo
            local(body, 0x20, L_R);
            i64Const(body, 32); body.push_back(0x88);
            i64Const(body, m32); body.push_back(0x83);
            local(body, 0x21, 2 + 1); // RDX (GPR 2) = hi
        }
    }
    // L_F = overflow ? (CF|OF) : 0; merge preserving other flags.
    body.push_back(0xAD); // i64.extend_i32_u
    i64Const(body, F_CFOF_MASK);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_F);
    local(body, 0x20, L_RFLAGS);
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x72); // i32.or
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x73); // i32.xor
    local(body, 0x20, L_F);
    body.push_back(0xA7); // i32.wrap_i64
    body.push_back(0x72); // i32.or
    local(body, 0x21, L_RFLAGS);
    return 0xFF;
}`;
}

// "imul3" template: three-operand IMUL r, r/m, imm (emitImul3 shape)
function genImul3(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U32 destReg = op.regField;
    // L_A = sign-extended source; L_B = sign-extended immediate.
    local(body, 0x20, op.rmIndex + 1);
    if (op.size == 2) {
        i64Const(body, 0xFFFFULL);
        body.push_back(0x83);
    } else if (op.size == 4) {
        i64Const(body, 0xFFFFFFFFULL);
        body.push_back(0x83);
    }
    emitSext(body, op.size);
    local(body, 0x21, L_A);
    i64Const(body, (U64)op.imm); // already sign-extended at decode
    local(body, 0x21, L_B);
    // L_R = wrapped product.
    local(body, 0x20, L_A);
    local(body, 0x20, L_B);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_R);
    if (op.size == 8) {
        // 128-bit overflow check (limb method, L_A/L_B/L_R as in emitImul).
        i64Const(body, 0);
        local(body, 0x21, L_P);
        local(body, 0x20, L_A);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_B);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7E);
        local(body, 0x20, L_P); body.push_back(0x7C);
        local(body, 0x21, L_P);
        local(body, 0x20, L_A);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_B);
        i64Const(body, 0xFFFFFFFFULL); body.push_back(0x83);
        body.push_back(0x7E);
        local(body, 0x21, L_T1);
        local(body, 0x20, L_A);
        i64Const(body, 0xFFFFFFFFULL); body.push_back(0x83);
        local(body, 0x20, L_B);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7E);
        local(body, 0x21, L_T2);
        local(body, 0x20, L_T1);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_T2);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7C);
        local(body, 0x20, L_T1);
        i64Const(body, 0xFFFFFFFFULL); body.push_back(0x83);
        local(body, 0x20, L_T2);
        i64Const(body, 0xFFFFFFFFULL); body.push_back(0x83);
        body.push_back(0x7C);
        local(body, 0x22, L_T1);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7C);
        local(body, 0x20, L_P); body.push_back(0x7C);
        local(body, 0x21, L_P);
        local(body, 0x20, L_T1);
        i64Const(body, 0xFFFFFFFFULL); body.push_back(0x83);
        local(body, 0x20, L_A);
        i64Const(body, 0xFFFFFFFFULL); body.push_back(0x83);
        local(body, 0x20, L_B);
        i64Const(body, 0xFFFFFFFFULL); body.push_back(0x83);
        body.push_back(0x7E);
        i64Const(body, 32); body.push_back(0x88);
        body.push_back(0x7C);
        i64Const(body, 32); body.push_back(0x88);
        local(body, 0x20, L_P); body.push_back(0x7C);
        local(body, 0x21, L_P);
        local(body, 0x20, L_P);
        local(body, 0x20, L_A);
        i64Const(body, 63); body.push_back(0x88);
        local(body, 0x20, L_B);
        body.push_back(0x7E);
        body.push_back(0x7D);
        local(body, 0x20, L_B);
        i64Const(body, 63); body.push_back(0x88);
        local(body, 0x20, L_A);
        body.push_back(0x7E);
        body.push_back(0x7D);
        local(body, 0x20, L_R);
        i64Const(body, 63); body.push_back(0x87);
        body.push_back(0x52); // i64.ne -> i32 overflow
    } else {
        local(body, 0x20, L_R);
        emitSext(body, op.size);
        local(body, 0x20, L_R);
        body.push_back(0x52); // i64.ne -> i32 overflow
    }
    body.push_back(0xAD); // i64.extend_i32_u
    i64Const(body, F_CFOF_MASK);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_F);
    // Publish: 16-bit preserves upper (RMW); 32-bit zero-extends.
    if (op.size == 2) emitWriteWord(body, destReg, L_R);
    else {
        local(body, 0x20, L_R);
        if (op.size == 4) {
            i64Const(body, 0xFFFFFFFFULL);
            body.push_back(0x83);
        }
        local(body, 0x21, destReg + 1);
    }
    local(body, 0x20, L_RFLAGS);
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x72); // i32.or
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x73); // i32.xor
    local(body, 0x20, L_F);
    body.push_back(0xA7); // i32.wrap_i64
    body.push_back(0x72); // i32.or
    local(body, 0x21, L_RFLAGS);
    return 0xFF; // destination already published inline above
}`;
}

// "movx" template: movx
function genMovx(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U32 destReg = op.regField;
    const U32 srcReg = op.rmIndex;
    const U8 srcSize = (op.sub == 0 || op.sub == 2) ? 1 : (op.sub == 4 ? 4 : 2);
    const bool isSigned = op.sub >= 2;
    // L_R = extended source value.
    if (srcSize == 1) emitReadByte(body, op, srcReg, L_R);
    else if (srcSize == 2) emitReadWord(body, srcReg, L_R);
    else {
        // MOVSXD (sub 4): 32-bit source; the register holds the operand in
        // its low 32 bits (the interpreter's loadRM(m, 4)).
        local(body, 0x20, srcReg + 1);
        i64Const(body, 0xFFFFFFFFULL);
        body.push_back(0x83); // i64.and
        local(body, 0x21, L_R);
    }
    if (isSigned) {
        local(body, 0x20, L_R);
        emitSext(body, srcSize);
        local(body, 0x21, L_R);
    }
    if (op.size == 2) {
        // setU16: low 16 bits, upper 48 preserved.
        emitWriteWord(body, destReg, L_R);
        return 0xFF;
    }
    // 32/64-bit: the value already fits (zero-extended by construction for
    // MOVZX; low 32 bits of the sign extension for MOVSX/MOVSXD); publish
    // via L_R.
    if (op.size == 4 && isSigned) {
        local(body, 0x20, L_R);
        i64Const(body, 0xFFFFFFFFULL);
        body.push_back(0x83); // i64.and
        local(body, 0x21, L_R);
    }
    return destReg;
}`;
}

function genLea(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    // LEA r, m: the value IS the effective address. Mirrors cpu64.cpp
    // BK_LEA exactly: 32-bit zero-extends, 64-bit stores the full address.
    // (16-bit LEA never reaches emission: the classifier rejects it, and
    // the interpreter's block decoder breaks on it too.) No flags are
    // touched and no memory is read, so a bad address cannot fault.
    emitEA(body, op); // L_T1 = effective address
    local(body, 0x20, L_T1);
    if (op.size == 4) {
        i64Const(body, 0xFFFFFFFFULL);
        body.push_back(0x83); // i64.and: 32-bit zero extension
    }
    local(body, 0x21, L_R);
    return op.regField;
}`;
}

// "stack" template: PUSH/POP (emitStackGen).
// Always 64-bit. RSP is GPR local 5 (X64_RSP == 4; GPR local index = reg+1).
// The order of the RSP update vs the memory access mirrors cpu64.cpp exactly
// (reads return 0 and writes commit on unmapped pages, so the ORDER is the
// observable contract):
//   PUSH reg (sub 0): push64: value = old reg contents (PUSH RSP pushes the
//                     old RSP), RSP -= 8, [RSP] = value.
//   PUSH imm (sub 1): dsp_22: [RSP-8] = imm FIRST, then RSP -= 8.
//   PUSH r/m (sub 2): v = loadRM(m, 8); push64(v): read r/m (register or
//                     [decode-time EA]), RSP -= 8, [RSP] = v.
//   POP reg  (sub 0): v = readq(RSP); RSP += 8; reg = v. (POP RSP: the
//                     popped value overwrites the incremented RSP.)
//   POP r/m  (sub 1): v = pop64(); storeRM(m, 8, v): the EA is decode-time
//                     (pre-instruction registers), so emitEA runs BEFORE the
//                     RSP update; then v = [RSP], RSP += 8, r/m = v.
// No flags are touched. Returns 0xFF: RSP is updated inline in its local
// and the epilogue store-back publishes it.
function genStack(e) {
  const L = [];
  L.push(`U32 ${e.func}(Bytes& body, const Jit64Op& op) {`);
  L.push(`    constexpr U32 L_RSP = 5; // X64_RSP == 4; GPR local index = reg + 1`);
  L.push(`    if (op.kind == J64_PUSH) {`);
  L.push(`        if (op.sub == 0) {`);
  L.push(`            // PUSH reg: snapshot the source first (PUSH RSP pushes`);
  L.push(`            // the OLD rsp), then RSP -= 8, then [RSP] = value.`);
  L.push(`            local(body, 0x20, op.regField + 1);`);
  L.push(`            local(body, 0x21, L_A);`);
  L.push(`            local(body, 0x20, L_RSP);`);
  L.push(`            i64Const(body, 8);`);
  L.push(`            body.push_back(0x7D); // i64.sub`);
  L.push(`            local(body, 0x21, L_RSP);`);
  L.push(`            emitMemWrite(body, L_RSP, L_A, 8);`);
  L.push(`        } else if (op.sub == 1) {`);
  L.push(`            // PUSH imm: the interpreter writes [RSP-8] BEFORE`);
  L.push(`            // updating RSP (dsp_22).`);
  L.push(`            local(body, 0x20, L_RSP);`);
  L.push(`            i64Const(body, 8);`);
  L.push(`            body.push_back(0x7D); // i64.sub`);
  L.push(`            local(body, 0x21, L_T1);`);
  L.push(`            i64Const(body, (S64)op.imm); // sign-extended at decode`);
  L.push(`            local(body, 0x21, L_A);`);
  L.push(`            emitMemWrite(body, L_T1, L_A, 8);`);
  L.push(`            local(body, 0x20, L_T1);`);
  L.push(`            local(body, 0x21, L_RSP);`);
  L.push(`        } else {`);
  L.push(`            // PUSH r/m: read the operand first (register, or`);
  L.push(`            // [decode-time EA]), then RSP -= 8, then [RSP] = value.`);
  L.push(`            if (op.isMem) {`);
  L.push(`                emitEA(body, op); // L_T1 = effective address`);
  L.push(`                emitMemRead(body, L_T1, 8); // L_R = [EA]`);
  L.push(`                local(body, 0x20, L_R);`);
  L.push(`            } else {`);
  L.push(`                local(body, 0x20, op.rmIndex + 1);`);
  L.push(`            }`);
  L.push(`            local(body, 0x21, L_A);`);
  L.push(`            local(body, 0x20, L_RSP);`);
  L.push(`            i64Const(body, 8);`);
  L.push(`            body.push_back(0x7D); // i64.sub`);
  L.push(`            local(body, 0x21, L_RSP);`);
  L.push(`            emitMemWrite(body, L_RSP, L_A, 8);`);
  L.push(`        }`);
  L.push(`    } else {`);
  L.push(`        // POP. The r/m EA (if any) is decode-time, from`);
  L.push(`        // pre-instruction registers: compute it BEFORE the RSP`);
  L.push(`        // update so RSP-relative EAs see the old RSP.`);
  L.push(`        if (op.sub == 1 && op.isMem) emitEA(body, op); // L_T1 = EA`);
  L.push(`        emitMemRead(body, L_RSP, 8); // L_R = [RSP]`);
  L.push(`        local(body, 0x20, L_RSP);`);
  L.push(`        i64Const(body, 8);`);
  L.push(`        body.push_back(0x7C); // i64.add`);
  L.push(`        local(body, 0x21, L_RSP);`);
  L.push(`        local(body, 0x20, L_R);`);
  L.push(`        local(body, 0x21, L_A);`);
  L.push(`        if (op.sub == 0) {`);
  L.push(`            // POP reg: reg = v (POP RSP overwrites the +8).`);
  L.push(`            local(body, 0x20, L_A);`);
  L.push(`            local(body, 0x21, op.regField + 1);`);
  L.push(`        } else if (op.isMem) {`);
  L.push(`            // POP r/m (memory): [EA] = v.`);
  L.push(`            emitMemWrite(body, L_T1, L_A, 8);`);
  L.push(`        } else {`);
  L.push(`            // POP r/m (register-direct): reg = v.`);
  L.push(`            local(body, 0x20, L_A);`);
  L.push(`            local(body, 0x21, op.rmIndex + 1);`);
  L.push(`        }`);
  L.push(`    }`);
  L.push(`    return 0xFF;`);
  L.push(`}`);
  return L.join("\n");
}


// "mov8" template: 8-bit MOV (emitMov8Gen).
// 0x88 MOV r/m8, r8; 0x8A MOV r8, r/m8. Mirrors cpu64.cpp dsp_9/dsp_10
// exactly: no flags are touched; 8-bit register access follows the
// AH/BH/CH/DH-vs-REX rule via emitReadByte/emitWriteByte; memory forms
// go through the imported helpers with the interpreter's fault ordering:
//   88 mem: readReg8 (cannot fault), then [EA] = byte (may fault);
//   8A mem: byte = [EA] (may fault), then writeReg8 (cannot fault).
// The EA is computed from pre-instruction registers (decode-time), so
// emitEA runs before any register write. Returns 0xFF: GPR writes happen
// inline in the GPR locals and the epilogue store-back publishes them.
function genMov8(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U32 src = op.kind == J64_MOV_RM_R ? op.regField : op.rmIndex;
    const U32 dest = op.kind == J64_MOV_RM_R ? op.rmIndex : op.regField;
    if (op.isMem) {
        emitEA(body, op); // L_T1 = effective address
        if (op.kind == J64_MOV_RM_R) {
            emitReadByte(body, op, src, L_A);
            emitMemWrite(body, L_T1, L_A, 1);
        } else {
            emitMemRead(body, L_T1, 1); // L_R = [EA]
            emitWriteByte(body, op, dest, L_R);
        }
    } else {
        emitReadByte(body, op, src, L_A);
        emitWriteByte(body, op, dest, L_A);
    }
    return 0xFF;
}`;
}

// "grp3" template: F6/F7 group 3 (emitGrp3Gen).
// TEST r/m,imm (/0): flags like AND of (r/m & imm), no writeback.
// NOT r/m (/2): r/m = ~r/m, no flags touched.
// NEG r/m (/3): r/m = 0 - r/m, flags like SUB (CF = (r/m != 0)).
// MUL r/m (/4, unsigned): RDX:RAX = RAX * r/m (DX:AX / EDX:EAX / AX for
// smaller widths); CF/OF set iff the high half is non-zero, all other
// flags preserved. Mirrors cpu64.cpp dsp_31 exactly.
// The r/m operand is sourced into L_A (width-masked): register-direct via
// emitReadOperand (the 8-bit high-byte rule included), memory via emitEA +
// emitMemRead. Fault ordering for the storing sub-ops (NOT/NEG) is
// memRead -> compute+flags -> memWrite, matching loadRM/storeRM.
// Returns 0xFF when everything was published inline (TEST, MUL, memory
// forms, 8/16-bit writebacks); otherwise the destination GPR index for
// L_R (NOT/NEG register-direct 4/8-bit, following the ALU convention).
function genGrp3(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U64 mask = widthMask(op.size);
    const U64 sb = widthSB(op.size);
    const U64 m32 = 0xFFFFFFFFULL;
    // L_A = width-masked r/m operand.
    if (op.isMem) {
        emitEA(body, op); // L_T1 = effective address
        emitMemRead(body, L_T1, op.size); // -> L_R (zero-extended)
        local(body, 0x20, L_R);
        i64Const(body, mask);
        body.push_back(0x83); // i64.and
        local(body, 0x21, L_A);
    } else {
        emitReadOperand(body, op, op.rmIndex, L_A);
    }
    if (op.sub == 0) {
        // TEST: flags like AND of (a & imm); no writeback.
        i64Const(body, op.imm & mask);
        local(body, 0x21, L_B);
        emitAluCompute(body, 4, true, mask, sb);
        return 0xFF;
    }
    if (op.sub == 2 || op.sub == 3) {
        // NOT / NEG.
        if (op.sub == 2) {
            local(body, 0x20, L_A);
            i64Const(body, mask);
            body.push_back(0x85); // i64.xor -> (~a) & mask
            local(body, 0x21, L_R);
        } else {
            // NEG via the SUB path: L_A = 0, L_B = a. CF = (R <u 0)
            // = (a != 0); OF/AF/SZ/P per flagsSub(0, a, r).
            local(body, 0x20, L_A);
            local(body, 0x21, L_B);
            i64Const(body, 0);
            local(body, 0x21, L_A);
            emitAluCompute(body, 7, false, mask, sb);
        }
        if (op.isMem) {
            emitMemWrite(body, L_T1, L_R, op.size);
            return 0xFF;
        }
        if (op.size == 1) { emitWriteByte(body, op, op.rmIndex, L_R); return 0xFF; }
        if (op.size == 2) { emitWriteWord(body, op.rmIndex, L_R); return 0xFF; }
        return op.rmIndex;
    }
    // MUL r/m (unsigned). On entry L_A = width-masked r/m operand; move it
    // to L_B, then L_A = zero-extended RAX at width.
    local(body, 0x20, L_A);
    local(body, 0x21, L_B); // L_B = operand (L_A is clobbered below)
    if (op.size == 1) emitReadByte(body, op, 0, L_A);
    else {
        local(body, 0x20, 0 + 1); // RAX
        if (op.size == 2) {
            i64Const(body, 0xFFFFULL);
            body.push_back(0x83);
        } else if (op.size == 4) {
            i64Const(body, m32);
            body.push_back(0x83);
        }
        local(body, 0x21, L_A);
    }
    local(body, 0x20, L_A);
    local(body, 0x20, L_B);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_R); // L_R = low half (exact for widths < 8)
    if (op.size == 8) {
        // 128-bit product via 32-bit limbs (unsigned long multiplication):
        // hi = a_hi*b_hi + (a_lo*b_hi >> 32) + (a_hi*b_lo >> 32) +
        //      (((a_lo*b_lo >> 32) + (a_lo*b_hi & m32) +
        //        (a_hi*b_lo & m32)) >> 32).
        // Every partial sum fits in 64 bits, so wrapping is exact.
        local(body, 0x20, L_A);
        i64Const(body, m32);
        body.push_back(0x83);
        local(body, 0x21, L_T1); // a_lo
        local(body, 0x20, L_A);
        i64Const(body, 32);
        body.push_back(0x88);
        local(body, 0x21, L_T2); // a_hi
        local(body, 0x20, L_B);
        i64Const(body, m32);
        body.push_back(0x83);
        local(body, 0x21, L_P); // b_lo
        local(body, 0x20, L_B);
        i64Const(body, 32);
        body.push_back(0x88);
        local(body, 0x21, L_A); // b_hi (L_B dead)
        local(body, 0x20, L_T2);
        local(body, 0x20, L_A);
        body.push_back(0x7E); // a_hi*b_hi
        local(body, 0x20, L_T1);
        local(body, 0x20, L_A);
        body.push_back(0x7E);
        i64Const(body, 32);
        body.push_back(0x88); // (a_lo*b_hi) >> 32
        local(body, 0x20, L_T2);
        local(body, 0x20, L_P);
        body.push_back(0x7E);
        i64Const(body, 32);
        body.push_back(0x88); // (a_hi*b_lo) >> 32
        local(body, 0x20, L_T1);
        local(body, 0x20, L_P);
        body.push_back(0x7E);
        i64Const(body, 32);
        body.push_back(0x88); // (a_lo*b_lo) >> 32
        local(body, 0x20, L_T1);
        local(body, 0x20, L_A);
        body.push_back(0x7E);
        i64Const(body, m32);
        body.push_back(0x83); // (a_lo*b_hi) & m32
        local(body, 0x20, L_T2);
        local(body, 0x20, L_P);
        body.push_back(0x7E);
        i64Const(body, m32);
        body.push_back(0x83); // (a_hi*b_lo) & m32
        body.push_back(0x7C); // +
        body.push_back(0x7C); // +
        i64Const(body, 32);
        body.push_back(0x88); // carry >> 32
        body.push_back(0x7C); // +
        body.push_back(0x7C); // +
        body.push_back(0x7C); // + -> hi
        local(body, 0x21, L_P); // L_P = hi
        // overflow = (hi != 0); RDX:RAX = hi:lo.
        local(body, 0x20, L_P);
        i64Const(body, 0);
        body.push_back(0x52); // i64.ne -> i32 overflow
        local(body, 0x20, L_P);
        local(body, 0x21, 2 + 1); // RDX
        local(body, 0x20, L_R);
        local(body, 0x21, 0 + 1); // RAX
    } else {
        // 8/16/32-bit: the product is exact in 64 bits; split hi/lo.
        local(body, 0x20, L_R);
        if (op.size == 1) {
            i64Const(body, 8);
            body.push_back(0x88); // hi = prod >> 8
        } else if (op.size == 2) {
            i64Const(body, 16);
            body.push_back(0x88);
            i64Const(body, 0xFFFFULL);
            body.push_back(0x83); // hi = (prod >> 16) & 0xFFFF
        } else {
            i64Const(body, 32);
            body.push_back(0x88); // hi = prod >> 32
        }
        local(body, 0x21, L_T1); // L_T1 = hi
        local(body, 0x20, L_T1);
        i64Const(body, 0);
        body.push_back(0x52); // i64.ne -> i32 overflow
        if (op.size == 1) {
            // AX = prod (setU16, upper RAX preserved).
            emitWriteWord(body, 0, L_R);
        } else if (op.size == 2) {
            // DX:AX = hi:lo (setU16 halves).
            local(body, 0x20, L_R);
            i64Const(body, 0xFFFFULL);
            body.push_back(0x83);
            local(body, 0x21, L_T2); // lo
            emitWriteWord(body, 0, L_T2); // AX
            emitWriteWord(body, 2, L_T1); // DX
        } else {
            // EDX:EAX: full 64-bit writes of the zero-extended halves.
            local(body, 0x20, L_R);
            i64Const(body, m32);
            body.push_back(0x83);
            local(body, 0x21, 0 + 1); // RAX = lo
            local(body, 0x20, L_T1);
            local(body, 0x21, 2 + 1); // RDX = hi
        }
    }
    // L_F = overflow ? (CF|OF) : 0; merge preserving other flags.
    body.push_back(0xAD); // i64.extend_i32_u
    i64Const(body, F_CFOF_MASK);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_F);
    local(body, 0x20, L_RFLAGS);
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x72); // i32.or
    i32Const(body, F_CFOF_MASK);
    body.push_back(0x73); // i32.xor
    local(body, 0x20, L_F);
    body.push_back(0xA7); // i32.wrap_i64
    body.push_back(0x72); // i32.or
    local(body, 0x21, L_RFLAGS);
    return 0xFF;
}`;
}

// "string" template: string
function genString(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    const U32 size = op.size; // 1, 2, or 4
    const U32 sub = op.sub;   // 0=MOVS, 1=STOS, 2=CMPS, 3=SCAS
    const bool isStos = (sub == 1);
    const bool isScas = (sub == 3);
    const bool isMovsStos = (sub <= 1);
    const U64 mask = size == 1 ? 0xFFULL : size == 2 ? 0xFFFFULL :
                       size == 4 ? 0xFFFFFFFFULL : 0xFFFFFFFFFFFFFFFFULL;
    const U64 sb = (U64)1 << (size * 8 - 1);
    const U32 RSI = 6 + 1, RDI = 7 + 1, RCX = 1 + 1, RAX = 0 + 1;

    // L_CNT = rep ? RCX : 1 (masked to 32 bits under 0x67).
    if (op.rep == 0) {
        i64Const(body, 1);
    } else {
        local(body, 0x20, RCX);
        if (op.asize32) {
            i64Const(body, 0xFFFFFFFFULL);
            body.push_back(0x83); // i64.and
        }
    }
    local(body, 0x21, L_CNT);

    // L_STEP = DF ? -size : +size.
    local(body, 0x20, L_RFLAGS);
    i32Const(body, 0x400); // X64_DF
    body.push_back(0x71);  // i32.and
    body.push_back(0x45);  // i32.eqz
    body.push_back(0x04);  // if
    body.push_back(0x7E);  //   result i64
    i64Const(body, size);
    body.push_back(0x05);  // else
    i64Const(body, (U64)(-(S64)size));
    body.push_back(0x0B);  // end
    local(body, 0x21, L_STEP);

    body.push_back(0x02); body.push_back(0x40); // block $done
    body.push_back(0x03); body.push_back(0x40); // loop $rep
    local(body, 0x20, L_CNT);
    body.push_back(0x50); // i64.eqz
    body.push_back(0x0D); body.push_back(0x01); // br_if 1 ($done)
    local(body, 0x20, L_CNT);
    i64Const(body, 1);
    body.push_back(0x7D); // i64.sub
    local(body, 0x21, L_CNT);

    if (isMovsStos) {
        // dst = RDI (& 0xFFFFFFFF under 0x67) -> L_A.
        local(body, 0x20, RDI);
        if (op.asize32) {
            i64Const(body, 0xFFFFFFFFULL);
            body.push_back(0x83);
        }
        local(body, 0x21, L_A);
        if (isStos) {
            local(body, 0x20, RAX);
            i64Const(body, mask);
            body.push_back(0x83);
            local(body, 0x21, L_R);
        } else {
            local(body, 0x20, RSI);
            if (op.asize32) {
                i64Const(body, 0xFFFFFFFFULL);
                body.push_back(0x83);
            }
            local(body, 0x21, L_A);
            emitMemRead(body, L_A, size); // -> L_R
            local(body, 0x20, RDI);
            if (op.asize32) {
                i64Const(body, 0xFFFFFFFFULL);
                body.push_back(0x83);
            }
            local(body, 0x21, L_A);
        }
        emitMemWrite(body, L_A, L_R, size);
        if (!isStos) {
            local(body, 0x20, RSI);
            local(body, 0x20, L_STEP);
            body.push_back(0x7C); // i64.add
            local(body, 0x21, RSI);
        }
        local(body, 0x20, RDI);
        local(body, 0x20, L_STEP);
        body.push_back(0x7C);
        local(body, 0x21, RDI);
    } else {
        // lhs -> L_A.
        if (isScas) {
            local(body, 0x20, RAX);
            i64Const(body, mask);
            body.push_back(0x83);
            local(body, 0x21, L_A);
        } else {
            local(body, 0x20, RSI);
            if (op.asize32) {
                i64Const(body, 0xFFFFFFFFULL);
                body.push_back(0x83);
            }
            local(body, 0x21, L_P); // temp addr (L_P is emitFlags scratch)
            emitMemRead(body, L_P, size); // -> L_R
            local(body, 0x20, L_R);
            local(body, 0x21, L_A);
        }
        // rhs = mem[RDI (& mask)] -> L_B.
        local(body, 0x20, RDI);
        if (op.asize32) {
            i64Const(body, 0xFFFFFFFFULL);
            body.push_back(0x83);
        }
        local(body, 0x21, L_P); // temp addr
        emitMemRead(body, L_P, size); // -> L_R
        local(body, 0x20, L_R);
        local(body, 0x21, L_B);
        // L_R = (L_A - L_B) & mask; flags through the SUB path.
        local(body, 0x20, L_A);
        local(body, 0x20, L_B);
        body.push_back(0x7D); // i64.sub
        i64Const(body, mask);
        body.push_back(0x83);
        local(body, 0x21, L_R);
        emitFlags(body, 5, sb, false);
        if (!isScas) {
            local(body, 0x20, RSI);
            local(body, 0x20, L_STEP);
            body.push_back(0x7C);
            local(body, 0x21, RSI);
        }
        local(body, 0x20, RDI);
        local(body, 0x20, L_STEP);
        body.push_back(0x7C);
        local(body, 0x21, RDI);
        // REP early exit on ZF.
        if (op.rep == 0xF3) { // REPE: break if !ZF
            local(body, 0x20, L_RFLAGS);
            i32Const(body, 0x40); // ZF
            body.push_back(0x71); // i32.and
            body.push_back(0x45); // i32.eqz
            body.push_back(0x0D); body.push_back(0x01); // br_if 1
        } else if (op.rep == 0xF2) { // REPNE: break if ZF
            local(body, 0x20, L_RFLAGS);
            i32Const(body, 0x40);
            body.push_back(0x71);
            body.push_back(0x0D); body.push_back(0x01); // br_if 1
        }
    }

    body.push_back(0x0C); body.push_back(0x00); // br 0 ($rep)
    body.push_back(0x0B); // end loop
    body.push_back(0x0B); // end block

    // REP updates RCX: MOVS/STOS leave 0, CMPS/SCAS leave the remainder.
    if (op.rep != 0) {
        if (isMovsStos) i64Const(body, 0);
        else local(body, 0x20, L_CNT);
        local(body, 0x21, RCX);
    }
    return 0xFF;
}`;
}

// "cbw" template: CBW/CWDE/CDQE (emitCbwGen).
// 0x98: sign-extend AL->AX (16-bit/CBW), AX->EAX (32-bit/CWDE),
// EAX->RAX (64-bit/CDQE). Mirrors cpu64.cpp dsp_20 exactly: no flags are
// touched, no memory is accessed, RAX is the only operand. RAX is GPR
// local 1 (X64_RAX == 0; GPR local index = reg + 1).
// Returns 0 (RAX) for 32/64-bit (epilogue publishes L_R), 0xFF for 16-bit
// (emitWriteWord does the RMW inline, preserving the upper 48 bits).
function genCbw(e) {
  return `U32 ${e.func}(Bytes& body, const Jit64Op& op) {
    // RAX is the only operand; the value is already in GPR local 1.
    if (op.size == 2) {
        // CBW: AL -> AX, upper 48 bits of RAX preserved (setU16).
        local(body, 0x20, 1); // RAX
        i64Const(body, 0xFF);
        body.push_back(0x83); // i64.and: AL
        local(body, 0x21, L_R);
        local(body, 0x20, L_R);
        emitSext(body, 1); // sign-extend 8 -> 64 (low 16 are the result)
        local(body, 0x21, L_R);
        emitWriteWord(body, 0, L_R); // low 16 bits, upper 48 kept
        return 0xFF;
    }
    if (op.size == 4) {
        // CWDE: AX -> EAX, then zero-extended to 64 (setU64 of the
        // 32-bit sign extension).
        emitReadWord(body, 0, L_R); // L_R = AX (0..65535)
        local(body, 0x20, L_R);
        emitSext(body, 2); // sign-extend 16 -> 64
        local(body, 0x21, L_R);
        local(body, 0x20, L_R);
        i64Const(body, 0xFFFFFFFFULL);
        body.push_back(0x83); // i64.and: keep low 32 (zero-extend)
        local(body, 0x21, L_R);
        return 0; // RAX
    }
    // CDQE (size == 8): EAX -> RAX sign extension.
    local(body, 0x20, 1); // RAX
    i64Const(body, 0xFFFFFFFFULL);
    body.push_back(0x83); // i64.and: EAX
    local(body, 0x21, L_R);
    local(body, 0x20, L_R);
    emitSext(body, 4); // sign-extend 32 -> 64
    local(body, 0x21, L_R);
    return 0; // RAX
}`;
}

const TEMPLATES = { alu: (e) => (e.mem ? genAluMem(e) : genAlu(e)), shift: genShift, rotate: genRotate, imul2: genImul2, imul1: genImul1, imul3: genImul3, movx: genMovx, string: genString, lea: genLea, stack: genStack, mov8: genMov8, grp3: genGrp3, cbw: genCbw };

function generate() {
  const out = [];
  out.push(
    `// GENERATED by scripts/gen-jit64.mjs from source/emulation/cpu/jit64table.mjs`
  );
  out.push(`// DO NOT EDIT — regenerate with: node scripts/gen-jit64.mjs`);
  out.push(`//`);
  out.push(
    `// Table-driven JIT emitters. Included into jit64wasm.cpp's anonymous`
  );
  out.push(
    `// namespace so the generated functions can use the emission primitives.`
  );
  out.push(``);
  for (const e of EMITTERS) {
    const gen = TEMPLATES[e.template];
    if (!gen) throw new Error(`unknown template: ${e.template}`);
    out.push(gen(e));
    out.push(``);
  }
  return out.join("\n");
}

const check = process.argv.includes("--check");
const text = generate();
if (check) {
  const cur = readFileSync(OUT_PATH, "utf8");
  if (cur !== text) {
    console.error(
      `jit64gen.inc is out of sync with jit64table.mjs; run: node scripts/gen-jit64.mjs`
    );
    process.exit(1);
  }
  console.log("jit64gen.inc in sync.");
} else {
  writeFileSync(OUT_PATH, text);
  console.log(`wrote ${OUT_PATH} (${EMITTERS.length} emitter(s))`);
}
