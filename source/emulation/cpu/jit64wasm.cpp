// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
#include "jit64wasm.h"

#ifdef BOXEDWINE_GUEST_X64
namespace {
using Bytes = std::vector<U8>;

void uleb(Bytes& bytes, U32 value) {
    do {
        U8 part = value & 0x7F;
        value >>= 7;
        bytes.push_back(part | (value ? 0x80 : 0));
    } while (value);
}

void i32Const(Bytes& bytes, U32 value) {
    bytes.push_back(0x41); // i32.const (signed LEB128)
    const bool negative = (value >> 31) != 0;
    for (;;) {
        U8 part = value & 0x7F;
        value >>= 7;
        if (negative) value |= 0xFE000000u;
        bool last = (value == 0 && !(part & 0x40)) ||
                    (value == ~0u && (part & 0x40));
        bytes.push_back(part | (last ? 0 : 0x80));
        if (last) return;
    }
}

// Interpret the U64 bit pattern as signed, without implementation-defined
// conversion to signed or right-shifting a negative C++ integer.
void i64Const(Bytes& bytes, U64 value) {
    bytes.push_back(0x42);
    const bool negative = (value >> 63) != 0;
    for (;;) {
        U8 part = value & 0x7F;
        value >>= 7;
        if (negative) value |= 0xFE00000000000000ULL;
        bool last = (value == 0 && !(part & 0x40)) ||
                    (value == ~0ULL && (part & 0x40));
        bytes.push_back(part | (last ? 0 : 0x80));
        if (last) return;
    }
}

void local(Bytes& bytes, U8 opcode, U32 index) {
    bytes.push_back(opcode);
    uleb(bytes, index);
}

void memoryOp(Bytes& bytes, U8 opcode, U32 align, U32 offset) {
    bytes.push_back(opcode);
    uleb(bytes, align);
    uleb(bytes, offset);
}

void section(Bytes& module, U8 id, const Bytes& contents) {
    module.push_back(id);
    uleb(module, (U32)contents.size());
    module.insert(module.end(), contents.begin(), contents.end());
}

// RFLAGS bits written by ALU ops (same numeric layout as X64_* in cpu64.h).
constexpr U32 F_CF = 0, F_PF = 2, F_AF = 4, F_ZF = 6, F_SF = 7, F_OF = 11;
constexpr U32 F_ALU_MASK = (1u << F_CF) | (1u << F_PF) | (1u << F_AF) |
                          (1u << F_ZF) | (1u << F_SF) | (1u << F_OF); // 0x8D5
// Shift flag bits written by SHL/SHR/SAR: CF/PF/ZF/SF/OF. AF is NOT touched
// by shifts (unlike ALU ops), so it stays out of the merge mask.
constexpr U32 F_SHIFT_MASK = (1u << F_CF) | (1u << F_PF) |
                             (1u << F_ZF) | (1u << F_SF) | (1u << F_OF); // 0x8C5
// Rotates and IMUL write only CF and OF. The interpreter clears just those
// two (doShift for sub <= 3 skips setSZP; IMUL leaves SZP/AF undefined), so
// PF/ZF/SF/AF and every other RFLAGS bit are preserved.
constexpr U32 F_CFOF_MASK = (1u << F_CF) | (1u << F_OF); // 0x801

// Module locals: 0 = state ptr param, 1..16 = i64 GPRs, 17 = i64 RIP,
// 18 = i32 RFLAGS, 19..25 = i64 scratch (A, B, R, flag bits, parity/carry
// temp, plus T1/T2 for the 64-bit IMUL 128-bit product limbs), 26..27 =
// i64 string-op scratch (STEP: DF-selected +/-size; CNT: REP counter).
constexpr U32 L_RFLAGS = 18, L_A = 19, L_B = 20, L_R = 21, L_F = 22, L_P = 23;
constexpr U32 L_T1 = 24, L_T2 = 25, L_STEP = 26, L_CNT = 27;
// Function indices in the emitted module: the two mem imports first, then
// the defined execute().
constexpr U32 F_MEMREAD = 0, F_MEMWRITE = 1;

// OR the stack-top 0/1 value, shifted left by `shift`, into L_F. The
// value comes from a comparison/test operator, which yields i32.
void orFlagBit(Bytes& body, U32 shift) {
    body.push_back(0xAD); // i64.extend_i32_u
    if (shift) {
        i64Const(body, shift);
        body.push_back(0x86); // i64.shl
    }
    local(body, 0x20, L_F);
    body.push_back(0x84); // i64.or
    local(body, 0x21, L_F);
}

// Compute the six ALU flag bits into L_F, then merge into L_RFLAGS while
// preserving the other RFLAGS bits. L_A/L_B/L_R hold width-masked operand
// values; sb is the width sign bit. Mirrors cpu64.cpp flagsAdd/flagsSub/
// flagsLogic exactly: CF/OF/AF formulas, SZP on the masked result, and AF
// cleared for logic ops.
void emitFlags(Bytes& body, U8 sub, U64 sb, bool isLogic) {
    const bool isAdd = !isLogic && (sub == 0 || sub == 2);
    const bool isSub = !isLogic && (sub == 3 || sub == 5 || sub == 7);
    i64Const(body, 0);
    local(body, 0x21, L_F);
    if (isAdd || isSub) {
        // CF: ADD/ADC: R <u A; SUB/SBB/CMP: A <u B.
        local(body, 0x20, isAdd ? L_R : L_A);
        local(body, 0x20, isAdd ? L_A : L_B);
        body.push_back(0x54); // i64.lt_u
        orFlagBit(body, F_CF);
        // OF: ADD: (~(A^B) & (A^R)); SUB: ((A^B) & (A^R)); tested at sb.
        local(body, 0x20, L_A);
        local(body, 0x20, L_B);
        body.push_back(0x85); // i64.xor
        if (isAdd) {
            i64Const(body, ~0ULL);
            body.push_back(0x85); // i64.xor -> ~(A^B)
        }
        local(body, 0x20, L_A);
        local(body, 0x20, L_R);
        body.push_back(0x85); // i64.xor -> A^R
        body.push_back(0x83); // i64.and
        i64Const(body, sb);
        body.push_back(0x83); // i64.and
        i64Const(body, 0);
        body.push_back(0x52); // i64.ne
        orFlagBit(body, F_OF);
        // AF: ((A^B^R) & 0x10) != 0.
        local(body, 0x20, L_A);
        local(body, 0x20, L_B);
        body.push_back(0x85);
        local(body, 0x20, L_R);
        body.push_back(0x85);
        i64Const(body, 0x10);
        body.push_back(0x83);
        i64Const(body, 0);
        body.push_back(0x52);
        orFlagBit(body, F_AF);
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
    // rflags = ((rflags | MASK) ^ MASK) | (i32)L_F  ==  (rflags & ~MASK) | L_F.
    local(body, 0x20, L_RFLAGS);
    i32Const(body, F_ALU_MASK);
    body.push_back(0x72); // i32.or
    i32Const(body, F_ALU_MASK);
    body.push_back(0x73); // i32.xor
    local(body, 0x20, L_F);
    body.push_back(0xA7); // i32.wrap_i64
    body.push_back(0x72); // i32.or
    local(body, 0x21, L_RFLAGS);
}

// Emit one register-direct ALU/TEST op. Returns the destination GPR index,
// or 0xFF when the op writes no register (TEST/CMP).
U32 emitAlu(Bytes& body, const Jit64Op& op) {
    const bool isTest = op.kind == J64_TEST_RM_R;
    const bool isLogic = isTest || op.sub == 1 || op.sub == 4 || op.sub == 6;
    const U8 effSub = isTest ? 4 : op.sub; // TEST computes flags like AND
    const U64 mask = op.size == 4 ? 0xFFFFFFFFULL : 0xFFFFFFFFFFFFFFFFULL;
    const U64 sb = op.size == 4 ? 0x80000000ULL : 0x8000000000000000ULL;
    U32 destReg;
    if (op.kind == J64_ALU_RM_R || isTest) destReg = op.rmIndex;
    else if (op.kind == J64_ALU_R_RM) destReg = op.regField;
    else if (op.kind == J64_ALU_RM_IMM) destReg = op.rmIndex;
    else destReg = 0; // J64_ALU_ACC_IMM: RAX
    // L_A = width-masked lhs (dest's old value).
    local(body, 0x20, destReg + 1);
    if (op.size == 4) {
        i64Const(body, mask);
        body.push_back(0x83); // i64.and
    }
    local(body, 0x21, L_A);
    // L_B = width-masked rhs; ADC/SBB fold the incoming carry in.
    if (op.kind == J64_ALU_RM_IMM || op.kind == J64_ALU_ACC_IMM) {
        i64Const(body, op.imm);
    } else {
        const U32 srcReg = (op.kind == J64_ALU_RM_R || isTest) ? op.regField : op.rmIndex;
        local(body, 0x20, srcReg + 1);
    }
    if (op.size == 4) {
        i64Const(body, mask);
        body.push_back(0x83);
    }
    if (op.sub == 2 || op.sub == 3) {
        local(body, 0x20, L_RFLAGS);
        i32Const(body, 1);
        body.push_back(0x71); // i32.and -> incoming CF as 0/1
        body.push_back(0xAD); // i64.extend_i32_u
        body.push_back(0x7C); // i64.add
        if (op.size == 4) {
            i64Const(body, mask);
            body.push_back(0x83);
        }
    }
    local(body, 0x21, L_B);
    // L_R = L_A <op> L_B, width-masked.
    local(body, 0x20, L_A);
    local(body, 0x20, L_B);
    switch (effSub) {
    case 0:
    case 2: body.push_back(0x7C); break; // i64.add
    case 3:
    case 5:
    case 7: body.push_back(0x7D); break; // i64.sub
    case 4: body.push_back(0x83); break; // i64.and
    case 1: body.push_back(0x84); break; // i64.or
    case 6: body.push_back(0x85); break; // i64.xor
    default: return 0xFF; // unreachable: filtered by the acceptance check
    }
    if (op.size == 4) {
        i64Const(body, mask);
        body.push_back(0x83);
    }
    local(body, 0x21, L_R);
    emitFlags(body, effSub, sb, isLogic);
    if (isTest || op.sub == 7) return 0xFF; // TEST/CMP write no register
    return destReg;
}

// Emit one register-direct SHL/SHR/SAR (0xC1 /4,/5,/7; /6 aliases /4).
// Returns the destination GPR index, or 0xFF when the op is a no-op.
// Mirrors cpu64.cpp doShift exactly for the non-rotate cases: count == 0
// leaves the value and all flags untouched; otherwise CF is the last bit
// shifted out, OF is defined only for count == 1 (SHL: MSB(result) XOR CF;
// SHR: old MSB; SAR: 0), and SZP is set from the result. AF and all other
// RFLAGS bits are preserved.
U32 emitShift(Bytes& body, const Jit64Op& op) {
    const U64 count = op.imm; // already masked to 0x1F/0x3F at decode
    const U32 destReg = op.rmIndex;
    const U64 mask = op.size == 4 ? 0xFFFFFFFFULL : 0xFFFFFFFFFFFFFFFFULL;
    const U64 sb = op.size == 4 ? 0x80000000ULL : 0x8000000000000000ULL;
    const U64 wbits = op.size == 4 ? 32 : 64;
    const U8 sub = op.sub == 6 ? 4 : op.sub; // /6 aliases SHL
    if (count == 0) {
        if (op.size == 4) {
            // The interpreter zero-extends the 32-bit destination even when
            // the count is 0 (cpu64.cpp BK_SHIFT_IMM / step shift path do
            // reg[rm].setU32((U32)v)); flags are untouched. Publish the
            // width-masked value via L_R so the caller stores it back.
            local(body, 0x20, destReg + 1);
            i64Const(body, mask);
            body.push_back(0x83); // i64.and
            local(body, 0x21, L_R);
            return destReg;
        }
        return 0xFF; // 64-bit count==0: pure no-op, value and flags untouched
    }
    // L_A = width-masked operand.
    local(body, 0x20, destReg + 1);
    if (op.size == 4) {
        i64Const(body, mask);
        body.push_back(0x83); // i64.and
    }
    local(body, 0x21, L_A);
    // L_R = shift result. The shift amount is pushed after any
    // sign-extension so the operand stack stays [value, amount].
    local(body, 0x20, L_A);
    if (sub == 4) {
        i64Const(body, count);
        body.push_back(0x86); // i64.shl
        if (op.size == 4) {
            i64Const(body, mask);
            body.push_back(0x83);
        }
    } else if (sub == 5) {
        i64Const(body, count);
        body.push_back(0x88); // i64.shr_u (L_A already width-masked)
    } else { // SAR: sign-extend, arithmetic shift, mask.
        if (op.size == 4) {
            i64Const(body, 32);
            body.push_back(0x86); // i64.shl
            i64Const(body, 32);
            body.push_back(0x87); // i64.shr_s
        }
        i64Const(body, count);
        body.push_back(0x87); // i64.shr_s
        if (op.size == 4) {
            i64Const(body, mask);
            body.push_back(0x83);
        }
    }
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
    return destReg;
}

// Emit one register-direct ROL/ROR/RCL/RCR (0xC1 /0../3). Returns the
// destination GPR index, or 0xFF when the op is a pure no-op (64-bit
// count==0). Mirrors cpu64.cpp doShift exactly for the rotate cases: count
// is already masked at decode, so c == count > 0 below; CF is the last bit
// rotated out (for RCL/RCR the loop threads the incoming CF through L_P),
// OF is defined only for count == 1 (ROL/RCL: MSB(result) XOR CF;
// ROR/RCR: MSB(result) XOR MSB(result<<1)), and SZP/AF are never touched --
// the flag merge writes only CF|OF (F_CFOF_MASK), preserving everything else.
U32 emitRotate(Bytes& body, const Jit64Op& op) {
    const U64 count = op.imm; // already masked to 0x1F/0x3F at decode
    const U32 destReg = op.rmIndex;
    const U64 mask = op.size == 4 ? 0xFFFFFFFFULL : 0xFFFFFFFFFFFFFFFFULL;
    const U64 sb = op.size == 4 ? 0x80000000ULL : 0x8000000000000000ULL;
    const U64 wbits = op.size == 4 ? 32 : 64;
    const U8 sub = op.sub; // 0=ROL, 1=ROR, 2=RCL, 3=RCR
    if (count == 0) {
        if (op.size == 4) {
            // As for shifts: the interpreter zero-extends the 32-bit
            // destination even on count==0; flags untouched.
            local(body, 0x20, destReg + 1);
            i64Const(body, mask);
            body.push_back(0x83); // i64.and
            local(body, 0x21, L_R);
            return destReg;
        }
        return 0xFF; // 64-bit count==0: pure no-op
    }
    // L_A = width-masked operand.
    local(body, 0x20, destReg + 1);
    if (op.size == 4) {
        i64Const(body, mask);
        body.push_back(0x83); // i64.and
    }
    local(body, 0x21, L_A);
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
        if (op.size == 4) {
            i64Const(body, mask);
            body.push_back(0x83);
        }
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
            if (op.size == 4) {
                i64Const(body, mask);
                body.push_back(0x83);
            }
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
            if (op.size == 4) {
                i64Const(body, mask);
                body.push_back(0x83);
            }
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
    return destReg;
}

// Emit one register-direct IMUL r, r/m (0F AF). Returns the destination GPR
// index. Mirrors cpu64.cpp BK_IMUL_R_RM exactly: the destination is the
// ModRM reg field and the source is the r/m register; both operands are
// sign-extended to 64 bits, the wrapped 64-bit product is stored
// (zero-extended for 32-bit), and CF/OF are set iff the signed result does
// not fit in the operand width. All other flags are preserved (SF/ZF/AF/PF
// are undefined per Intel; the interpreter leaves them alone).
U32 emitImul(Bytes& body, const Jit64Op& op) {
    const U32 destReg = op.regField;
    const U32 srcReg = op.rmIndex;
    const U64 m32 = 0xFFFFFFFFULL;
    // L_A = sign-extended dest operand, L_B = sign-extended source operand.
    // (For 64-bit the raw u64 bit pattern already is the S64 operand.)
    local(body, 0x20, destReg + 1);
    if (op.size == 4) {
        i64Const(body, 32);
        body.push_back(0x86); // i64.shl
        i64Const(body, 32);
        body.push_back(0x87); // i64.shr_s: sign-extends the low 32 bits
    }
    local(body, 0x21, L_A);
    local(body, 0x20, srcReg + 1);
    if (op.size == 4) {
        i64Const(body, 32);
        body.push_back(0x86);
        i64Const(body, 32);
        body.push_back(0x87);
    }
    local(body, 0x21, L_B);
    // L_R = wrapped 64-bit product (exact for 32-bit: |operands| < 2^32).
    local(body, 0x20, L_A);
    local(body, 0x20, L_B);
    body.push_back(0x7E); // i64.mul
    local(body, 0x21, L_R);
    // Overflow detection -> i32 0/1 on the stack.
    if (op.size == 4) {
        // 32-bit: the 64-bit product is exact; overflow iff it differs from
        // its own 32-bit sign extension.
        local(body, 0x20, L_R);
        i64Const(body, 32);
        body.push_back(0x86);
        i64Const(body, 32);
        body.push_back(0x87); // sext32(L_R)
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
    // Publish: 32-bit zero-extends the destination (setU32).
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
    return destReg;
}
} // namespace

void jit64WasmStateInit(Jit64WasmState& state, const U64 gpr[16], U64 rip, U32 rflags) {
    for (U32 i = 0; i < 16; i++) state.gpr[i] = gpr[i];
    state.rip = rip;
    state.rflags = rflags;
    state.reserved = 0;
}

void jit64WasmStateRead(const Jit64WasmState& state, U64 gpr[16], U64& rip, U32& rflags) {
    for (U32 i = 0; i < 16; i++) gpr[i] = state.gpr[i];
    rip = state.rip;
    rflags = state.rflags;
}

// Emit a call to env.memRead(addrLocal, size): splits the i64 address to
// lo/hi i32s, passes the size and the memScratch address, then loads the
// 64-bit result into L_R. All import params are i32 (no BigInt dependence).
void emitMemRead(Bytes& body, U32 addrLocal, U32 size) {
    local(body, 0x20, addrLocal);
    body.push_back(0xA7); // i32.wrap_i64 (lo)
    local(body, 0x20, addrLocal);
    i64Const(body, 32);
    body.push_back(0x88); // i64.shr_u
    body.push_back(0xA7); // i32.wrap_i64 (hi)
    i32Const(body, size);
    local(body, 0x20, 0); // state ptr
    i32Const(body, (U32)offsetof(Jit64WasmState, memScratch));
    body.push_back(0x6A); // i32.add (retPtr)
    body.push_back(0x10); // call
    uleb(body, F_MEMREAD);
    local(body, 0x20, 0);
    memoryOp(body, 0x29, 3, (U32)offsetof(Jit64WasmState, memScratch)); // i64.load
    local(body, 0x21, L_R);
}

// Emit a call to env.memWrite(addrLocal, valLocal, size).
void emitMemWrite(Bytes& body, U32 addrLocal, U32 valLocal, U32 size) {
    local(body, 0x20, addrLocal);
    body.push_back(0xA7); // lo
    local(body, 0x20, addrLocal);
    i64Const(body, 32);
    body.push_back(0x88); // i64.shr_u
    body.push_back(0xA7); // hi
    i32Const(body, size);
    local(body, 0x20, valLocal);
    body.push_back(0xA7); // valLo
    local(body, 0x20, valLocal);
    i64Const(body, 32);
    body.push_back(0x88); // i64.shr_u
    body.push_back(0xA7); // valHi
    body.push_back(0x10); // call
    uleb(body, F_MEMWRITE);
}

// Emit one string op (MOVS/STOS/CMPS/SCAS). Returns 0xFF: RSI/RDI/RCX are
// updated in their locals and published by the epilogue's store-back.
// Mirrors CPU64::runStringOp exactly: DF (bit 10) selects the +/-size step,
// the effective address is masked to 32 bits under 0x67 but the register
// update is full 64-bit wrapping, REP takes its count from RCX (masked
// under 0x67), CMPS/SCAS set flags through the SUB path and honor
// REPE/REPNE early exit. Memory goes through the imported helpers so the
// host's KMemory64 accessors (fault/commit/generation semantics) apply.
U32 emitString(Bytes& body, const Jit64Op& op) {
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
}

bool jit64EmitWasm(const Jit64Block& block, std::vector<U8>& output) {
    output.clear();
    if (block.ops.empty() || block.ops.size() > 24) return false;
    U32 byteCount = 0;
    for (const auto& op : block.ops) {
        if (op.plan != JIT64_FAST || !op.len || op.len > 15) return false;
        if (op.kind == J64_STRING) {
            // String ops use 1/2/4/8-byte operands and touch memory through
            // the imported helpers (never raw linear memory).
            if (op.isMem || (op.size != 1 && op.size != 2 && op.size != 4 && op.size != 8)) return false;
            if (op.sub > 3) return false;
            if (op.rep != 0 && op.rep != 0xF2 && op.rep != 0xF3) return false;
        } else if (op.isMem || (op.size != 4 && op.size != 8)) {
            return false;
        }
        switch (op.kind) {
        case J64_MOV_R_IMM:
            if (op.regField >= 16) return false;
            break;
        case J64_MOV_RM_IMM:
            if (op.rmIndex >= 16) return false;
            break;
        case J64_MOV_RM_R:
        case J64_MOV_R_RM:
            if (op.sub != 0 || op.regField >= 16 || op.rmIndex >= 16) return false;
            break;
        case J64_ALU_RM_R:
        case J64_ALU_R_RM:
        case J64_TEST_RM_R:
            if (op.regField >= 16 || op.rmIndex >= 16 || op.sub > 7) return false;
            break;
        case J64_ALU_RM_IMM:
            if (op.rmIndex >= 16 || op.sub > 7) return false;
            break;
        case J64_ALU_ACC_IMM:
            if (op.sub > 7) return false;
            break;
        case J64_SHIFT_IMM:
            // SHL/SHR/SAR (/4,/5,/6,/7) and rotates (/0../3), 0xC1 only.
            if (op.rmIndex >= 16 || op.sub > 7) return false;
            break;
        case J64_IMUL_R_RM:
            // Two-operand IMUL r, r/m (0F AF), register-direct only.
            if (op.regField >= 16 || op.rmIndex >= 16) return false;
            break;
        case J64_STRING:
            break; // validated above
        default:
            return false; // MOVZX, control flow, and the rest
        }
        byteCount += op.len;
    }

    // Param local 0 is the wasm32 host pointer. Locals 1..16 are i64 guest
    // GPRs, 17 the i64 guest RIP, 18 the i32 RFLAGS, 19..25 i64 scratch.
    // Guest values are never wasm host addresses.
    Bytes body = {3, 17, 0x7E, 1, 0x7F, 9, 0x7E}; // +L_STEP/L_CNT
    for (U32 reg = 0; reg < 16; reg++) {
        local(body, 0x20, 0);
        memoryOp(body, 0x29, 3, reg * 8); // i64.load
        local(body, 0x21, reg + 1);
    }
    local(body, 0x20, 0);
    memoryOp(body, 0x29, 3, (U32)offsetof(Jit64WasmState, rip));
    local(body, 0x21, 17);
    local(body, 0x20, 0);
    memoryOp(body, 0x28, 2, (U32)offsetof(Jit64WasmState, rflags)); // i32.load
    local(body, 0x21, L_RFLAGS);

    for (const auto& op : block.ops) {
        switch (op.kind) {
        case J64_MOV_R_IMM:
        case J64_MOV_RM_IMM: {
            U32 dest = op.kind == J64_MOV_RM_IMM ? op.rmIndex : op.regField;
            i64Const(body, op.size == 4 ? op.imm & 0xFFFFFFFFULL : op.imm);
            local(body, 0x21, dest + 1);
            break;
        }
        case J64_MOV_RM_R:
        case J64_MOV_R_RM: {
            U32 dest = op.kind == J64_MOV_RM_R ? op.rmIndex : op.regField;
            U32 src = op.kind == J64_MOV_RM_R ? op.regField : op.rmIndex;
            local(body, 0x20, src + 1);
            if (op.size == 4) {
                i64Const(body, 0xFFFFFFFFULL);
                body.push_back(0x83); // i64.and, x86 32-bit zero extension
            }
            local(body, 0x21, dest + 1);
            break;
        }
        case J64_SHIFT_IMM: {
            // Register-direct shifts and rotates: emitShift/emitRotate leave
            // the result in L_R; publish it to the GPR local so the final
            // store-back writes it (and later ops observe it). A 0xFF return
            // is the 64-bit count==0 pure no-op (32-bit count==0 still
            // zero-extends via L_R and returns the register).
            U32 written = (op.sub <= 3) ? emitRotate(body, op) : emitShift(body, op);
            if (written != 0xFF) {
                local(body, 0x20, L_R);
                local(body, 0x21, written + 1);
            }
            break;
        }
        case J64_IMUL_R_RM: {
            // Register-direct IMUL r, r/m: emitImul leaves the result in
            // L_R; publish it to the destination GPR local.
            U32 written = emitImul(body, op);
            if (written != 0xFF) {
                local(body, 0x20, L_R);
                local(body, 0x21, written + 1);
            }
            break;
        }
        case J64_STRING: {
            // String ops update RSI/RDI/RCX in their locals; the epilogue
            // store-back publishes them. emitString returns 0xFF.
            emitString(body, op);
            break;
        }
        default: {
            // Register-direct ALU/TEST: emitAlu leaves the result in L_R;
            // publish it to the GPR local so the final store-back writes it
            // (and later ops in the block observe it).
            U32 written = emitAlu(body, op);
            if (written != 0xFF) {
                local(body, 0x20, L_R);
                local(body, 0x21, written + 1);
            }
            break;
        }
        }
    }

    for (U32 reg = 0; reg < 16; reg++) {
        local(body, 0x20, 0);
        local(body, 0x20, reg + 1);
        memoryOp(body, 0x37, 3, reg * 8); // i64.store
    }
    local(body, 0x20, 0);
    local(body, 0x20, 17);
    i64Const(body, byteCount);
    body.push_back(0x7C); // i64.add, modulo 64 bits
    memoryOp(body, 0x37, 3, (U32)offsetof(Jit64WasmState, rip));
    local(body, 0x20, 0);
    local(body, 0x20, L_RFLAGS);
    memoryOp(body, 0x36, 2, (U32)offsetof(Jit64WasmState, rflags)); // i32.store
    body.push_back(0x0B); // end execute

    Bytes module = {0, 0x61, 0x73, 0x6D, 1, 0, 0, 0};
    // Type section: 0: (i32)->() [execute], 1: (i32 x4)->() [memRead],
    // 2: (i32 x5)->() [memWrite].
    Bytes types = {3,
        0x60, 1, 0x7F, 0,
        0x60, 4, 0x7F, 0x7F, 0x7F, 0x7F, 0,
        0x60, 5, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0};
    section(module, 1, types);
    // Import section: env.memory, env.memRead (func 0, type 1),
    // env.memWrite (func 1, type 2).
    Bytes imports = {3,
        3, 'e', 'n', 'v', 6, 'm', 'e', 'm', 'o', 'r', 'y', 2, 0, 1,
        3, 'e', 'n', 'v', 7, 'm', 'e', 'm', 'R', 'e', 'a', 'd', 0, 1,
        3, 'e', 'n', 'v', 8, 'm', 'e', 'm', 'W', 'r', 'i', 't', 'e', 0, 2};
    section(module, 2, imports);
    section(module, 3, {1, 0}); // one function, type 0 (function index 2)
    section(module, 7, {1, 7, 'e', 'x', 'e', 'c', 'u', 't', 'e', 0, 2}); // export func 2
    Bytes code = {1};
    uleb(code, (U32)body.size());
    code.insert(code.end(), body.begin(), body.end());
    section(module, 10, code);
    output.swap(module);
    return true;
}
#endif
