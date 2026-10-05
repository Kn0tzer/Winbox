// Copyright (C) 2026 The BoxedWine Team. GPL-2.0-or-later.
// jit64table.mjs — semantics table for the table-driven JIT emitter.
//
// Each entry describes one GENERATED emitter function (see
// scripts/gen-jit64.mjs). The table drives the EMITTER (the per-group
// `emit*` functions in jit64wasm.cpp), not the decoder (jit64DecodeOne
// stays hand-written). New instruction coverage becomes table rows
// (+ a new template only when the composition is truly novel).
//
// Entry fields:
//   func:     generated C++ function name, e.g. "emitAluGen".
//   template: composition pattern implemented by the generator
//             ("alu", "shift", "rotate", "imul2", "imul1", "imul3",
//              "movx", "string").
//   mem:      for the "alu" template: false = register-direct (emitAlu
//             shape), true = memory-operand (emitAluMem shape).
//   kinds:    Jit64Kind values the emitter handles.
//   encoding: decode contract (opcode, ModRM.reg extension, prefixes
//             REX/0x66/0x67, REX.W/B) — documentation; the real decoder
//             is jit64DecodeOne.
//   operands: operand spec per role (sizes; reg-direct with the
//             AH/BH/CH/DH-vs-REX byte rule; ModRM r/m reg-or-memory;
//             immediates).
//   flags:    flag behavior (which flags are written, result width,
//             what is preserved).
//   isTest:   (alu template) C++ bool expression: the op computes flags
//             like AND but writes no register.
//   wiring:   (alu template) per-kind operand wiring, in order.
//             dest: "rm" (op.rmIndex) | "reg" (op.regField) | "acc" (0) |
//                   "mem" (memory writeback)
//             a:    "dest" (dest's old value) | "mem" | "reg:<C++ GPR expr>"
//             b:    "imm" (op.imm & mask) | "mem" | "reg:<C++ GPR expr>"

export const EMITTERS = [
  {
    func: "emitAluGen",
    template: "alu",
    mem: false,
    kinds: [
      "J64_ALU_RM_R",
      "J64_ALU_R_RM",
      "J64_ALU_RM_IMM",
      "J64_ALU_ACC_IMM",
      "J64_TEST_RM_R",
    ],
    encoding:
      "ADD/ADC/SUB/SBB/AND/OR/XOR/CMP (/0../7): 0x00-0x05 r/m,r; " +
      "0x28-0x2D r,r/m; 0x80/0x81/0x83 r/m,imm; 0x04/0x05/.../0x3D acc,imm " +
      "(sub 8 = TEST); 0x84/0x85 TEST r/m,r. REX.W selects 64-bit, 0x66 16-bit.",
    operands:
      "sizes 1/2/4/8, reg-direct; 8-bit follows the AH/BH/CH/DH-vs-REX " +
      "rule (byteRegParts: no-REX idx 4..7 => high byte, else low byte).",
    flags: {
      writes: ["CF", "PF", "AF", "ZF", "SF", "OF"],
      preserves: "all other RFLAGS bits",
      note: "TEST/CMP compute flags but write no register",
    },
    isTest:
      "op.kind == J64_TEST_RM_R || (op.kind == J64_ALU_ACC_IMM && op.sub == 8)",
    wiring: [
      { kind: "J64_ALU_RM_R", dest: "rm", a: "dest", b: "reg:op.regField" },
      { kind: "J64_ALU_R_RM", dest: "reg", a: "dest", b: "reg:op.rmIndex" },
      { kind: "J64_ALU_RM_IMM", dest: "rm", a: "dest", b: "imm" },
      { kind: "J64_ALU_ACC_IMM", dest: "acc", a: "dest", b: "imm" },
      { kind: "J64_TEST_RM_R", dest: "rm", a: "dest", b: "reg:op.regField" },
    ],
  },
  {
    func: "emitAluMemGen",
    template: "alu",
    mem: true,
    kinds: [
      "J64_ALU_RM_R",
      "J64_ALU_R_RM",
      "J64_ALU_RM_IMM",
      "J64_TEST_RM_R",
    ],
    encoding:
      "ADD/ADC/SUB/SBB/AND/OR/XOR/CMP (/0../7) with ModRM mod != 3: " +
      "0x00-0x03 r/m,r; 0x28-0x2B r,r/m; 0x80/0x81/0x83 r/m,imm; " +
      "0x84/0x85 TEST r/m,r. EA via emitEA (SIB/RIP-relative/0x67/seg).",
    operands:
      "sizes 1/2/4/8, ModRM r/m names memory; fault ordering: memRead " +
      "before compute+flags, memWrite after (interpreter's loadRM/runAlu/" +
      "storeRM order).",
    flags: {
      writes: ["CF", "PF", "AF", "ZF", "SF", "OF"],
      preserves: "all other RFLAGS bits",
      note: "TEST/CMP compute flags but write nothing (no memWrite)",
    },
    isTest: "op.kind == J64_TEST_RM_R",
    wiring: [
      { kind: "J64_ALU_RM_R", dest: "mem", a: "mem", b: "reg:op.regField" },
      { kind: "J64_ALU_R_RM", dest: "reg", a: "reg:op.regField", b: "mem" },
      { kind: "J64_ALU_RM_IMM", dest: "mem", a: "mem", b: "imm" },
      { kind: "J64_TEST_RM_R", dest: "mem", a: "mem", b: "reg:op.regField" },
    ],
  },
  {
    func: "emitShiftGen",
    template: "shift",
    kinds: ["J64_SHIFT_IMM"],
    encoding:
      "SHL/SHR/SAR: 0xC1 /4,/5,/7 (0xC0 /4,/5,/7 for 8-bit; /6 aliases /4). " +
      "REX.W selects 64-bit, 0x66 16-bit. Count in imm8, masked to " +
      "0x1F/0x3F at decode.",
    operands:
      "sizes 1/2/4/8, register-direct; 8-bit follows the AH/BH/CH/DH-vs-REX rule.",
    flags: {
      writes: ["CF", "OF", "ZF", "SF", "PF"],
      preserves: "AF and all other RFLAGS bits (F_SHIFT_MASK merge)",
      note: "count==0: no-op (flags untouched); 32-bit count==0 still " +
            "zero-extends. OF defined only for count==1.",
    },
  },
  {
    func: "emitRotateGen",
    template: "rotate",
    kinds: ["J64_SHIFT_IMM"],
    encoding:
      "ROL/ROR/RCL/RCR: 0xC1 /0../3 (0xC0 /0../3 for 8-bit). REX.W selects " +
      "64-bit, 0x66 16-bit. Count in imm8, masked to 0x1F/0x3F at decode.",
    operands:
      "sizes 1/2/4/8, register-direct; 8-bit follows the AH/BH/CH/DH-vs-REX rule.",
    flags: {
      writes: ["CF", "OF"],
      preserves: "SZP/AF and all other RFLAGS bits (F_CFOF_MASK merge)",
      note: "count==0: no-op (flags untouched); 32-bit count==0 still " +
            "zero-extends. OF defined only for count==1.",
    },
  },
  {
    func: "emitImulGen",
    template: "imul2",
    kinds: ["J64_IMUL_R_RM"],
    encoding: "IMUL r32/64, r/m32/64: 0x0F 0xAF. REX.W selects 64-bit.",
    operands: "sizes 4/8, register-direct.",
    flags: {
      writes: ["CF", "OF"],
      preserves: "all other RFLAGS bits",
      note: "CF/OF set iff the signed result does not fit (128-bit limb product)",
    },
  },
  {
    func: "emitImul1Gen",
    template: "imul1",
    kinds: ["J64_IMUL_1OP"],
    encoding: "IMUL r/m: 0xF6 /5 (8-bit) or 0xF7 /5 (32/64-bit). rDX:rAX result.",
    operands: "sizes 1/2/4/8, register-direct.",
    flags: {
      writes: ["CF", "OF"],
      preserves: "all other RFLAGS bits",
      note: "32-bit limb product + sign correction; writes RAX and RDX",
    },
  },
  {
    func: "emitImul3Gen",
    template: "imul3",
    kinds: ["J64_IMUL_3OP"],
    encoding: "IMUL r, r/m, imm: 0x6B (imm8) or 0x69 (imm16/32). REX.W selects 64-bit.",
    operands: "sizes 2/4/8, register-direct.",
    flags: {
      writes: ["CF", "OF"],
      preserves: "all other RFLAGS bits",
      note: "CF/OF set iff the signed result does not fit",
    },
  },
  {
    func: "emitMovxGen",
    template: "movx",
    kinds: ["J64_MOVX"],
    encoding: "MOVZX: 0x0F 0xB6 (r/m8) / 0x0F 0xB7 (r/m16); MOVSX: 0x0F 0xBE / 0x0F 0xBF. REX.W/0x66 select dest size.",
    operands: "sizes 2/4/8 dest, 1/2 src; register-direct; 8-bit src follows AH/BH/CH/DH-vs-REX.",
    flags: {
      writes: [],
      preserves: "all RFLAGS bits (flags untouched)",
      note: "sub: 0=MOVZX8, 1=MOVZX16, 2=MOVSX8, 3=MOVSX16",
    },
  },
  {
    func: "emitStringGen",
    template: "string",
    kinds: ["J64_STRING"],
    encoding: "MOVS (0xA4/0xA5), STOS (0xAA/0xAB), CMPS (0xA6/0xA7), SCAS (0xAC/0xAD) with REP/REPE/REPNE (0xF2/0xF3).",
    operands: "sizes 1/2/4/8; RSI/RDI/RCX implicit; 0x67 addr-size.",
    flags: {
      writes: ["CF", "PF", "AF", "ZF", "SF", "OF"],
      preserves: "CMPS/SCAS write all six; MOVS/STOS preserve all flags",
      note: "sub: 0=MOVS, 1=STOS, 2=CMPS, 3=SCAS; rep loop with ZF early-exit",
    },
  },
];
