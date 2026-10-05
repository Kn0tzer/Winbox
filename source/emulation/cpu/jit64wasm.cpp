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
    bytes.push_back(0x41); // i32.const
    uleb(bytes, value);
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

// Module locals: 0 = state ptr param, 1..16 = i64 GPRs, 17 = i64 RIP,
// 18 = i32 RFLAGS, 19..23 = i64 scratch (A, B, R, flag bits, parity temp).
constexpr U32 L_RFLAGS = 18, L_A = 19, L_B = 20, L_R = 21, L_F = 22, L_P = 23;

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
        body.push_back(0x87); // i64.shr_u
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

bool jit64EmitWasm(const Jit64Block& block, std::vector<U8>& output) {
    output.clear();
    if (block.ops.empty() || block.ops.size() > 24) return false;
    U32 byteCount = 0;
    for (const auto& op : block.ops) {
        if (op.plan != JIT64_FAST || op.isMem ||
            (op.size != 4 && op.size != 8) || !op.len || op.len > 15) return false;
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
        default:
            return false; // MOVZX, shifts, control flow, and the rest
        }
        byteCount += op.len;
    }

    // Param local 0 is the wasm32 host pointer. Locals 1..16 are i64 guest
    // GPRs, 17 the i64 guest RIP, 18 the i32 RFLAGS, 19..23 i64 scratch.
    // Guest values are never wasm host addresses.
    Bytes body = {3, 17, 0x7E, 1, 0x7F, 5, 0x7E};
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
    section(module, 1, {1, 0x60, 1, 0x7F, 0}); // (i32) -> ()
    section(module, 2, {1, 3, 'e', 'n', 'v', 6, 'm', 'e', 'm', 'o', 'r', 'y', 2, 0, 1});
    section(module, 3, {1, 0}); // one function, type 0
    section(module, 7, {1, 7, 'e', 'x', 'e', 'c', 'u', 't', 'e', 0, 0});
    Bytes code = {1};
    uleb(code, (U32)body.size());
    code.insert(code.end(), body.begin(), body.end());
    section(module, 10, code);
    output.swap(module);
    return true;
}
#endif
