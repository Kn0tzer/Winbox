#!/usr/bin/env python3
"""Build and run a 32-bit x86 instruction batch for CPU differential tests."""

import argparse
import json
import pathlib
import struct
import subprocess
import sys

GPR_NAMES = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
RESULT_FIXED_SIZE = 32 + 4 + 4 + 8 * 16 + 8 * 8 + 64
MEMORY_OFFSET = 0x100
ARITH_FLAGS = 0x8D5
MEMORY_TAIL_MAX = 960


def little_bytes(value, size):
    return int(value).to_bytes(size, "little", signed=False)


def parse_case(case, index):
    name = case.get("name", f"case_{index}")
    code = case.get("bytes")
    if isinstance(code, str):
        code = bytes.fromhex(code)
    elif isinstance(code, list):
        code = bytes(code)
    else:
        raise ValueError(f"{name}: bytes must be a hex string or byte list")
    if not code or len(code) > 15:
        raise ValueError(f"{name}: instruction bytes must contain 1 to 15 bytes")
    gpr = case.get("gpr")
    if isinstance(gpr, dict):
        gpr = [int(gpr.get(reg, 0)) for reg in GPR_NAMES]
    if not isinstance(gpr, list) or len(gpr) != 8:
        raise ValueError(f"{name}: gpr must contain eax,ecx,edx,ebx,esp,ebp,esi,edi")
    xmm = case.get("xmm")
    if not isinstance(xmm, list) or len(xmm) != 8:
        raise ValueError(f"{name}: xmm must contain eight 16-byte hex strings")
    xmm_bytes = [bytes.fromhex(value) for value in xmm]
    if any(len(value) != 16 for value in xmm_bytes):
        raise ValueError(f"{name}: every xmm entry must be 16 bytes")
    mmx = case.get("mmx")
    if not isinstance(mmx, list) or len(mmx) != 8:
        raise ValueError(f"{name}: mmx must contain eight 64-bit values")
    memory = case.get("memory")
    if isinstance(memory, str):
        memory = bytes.fromhex(memory)
    elif isinstance(memory, list):
        memory = bytes(memory)
    if not isinstance(memory, bytes) or len(memory) != 64:
        raise ValueError(f"{name}: memory must be exactly 64 bytes (hex string or byte list)")
    memory_tail = case.get("memory_tail", b"")
    if isinstance(memory_tail, str):
        memory_tail = bytes.fromhex(memory_tail)
    elif isinstance(memory_tail, list):
        memory_tail = bytes(memory_tail)
    if not isinstance(memory_tail, bytes) or len(memory_tail) > MEMORY_TAIL_MAX:
        raise ValueError(f"{name}: memory_tail must contain at most {MEMORY_TAIL_MAX} bytes")
    memory_register = case.get("memory_register")
    if memory_register is not None:
        if isinstance(memory_register, str):
            memory_register = GPR_NAMES.index(memory_register.lower())
        if memory_register not in range(8) or memory_register == 4:
            raise ValueError(f"{name}: memory_register must be a GPR other than esp")
    return {
        "name": name,
        "bytes": code,
        "gpr": [int(value) & 0xFFFFFFFF for value in gpr],
        "eflags": int(case.get("eflags", 2)) & 0xFFFFFFFF,
        "xmm": xmm_bytes,
        "mmx": [int(value) & 0xFFFFFFFFFFFFFFFF for value in mmx],
        "memory": memory,
        "memory_tail": memory_tail,
        "memory_size": len(memory) + len(memory_tail),
        "mxcsr": int(case.get("mxcsr", 0x1F80)) & 0xFFFFFFFF,
        "memory_register": memory_register,
    }


def result_size(case):
    return RESULT_FIXED_SIZE + len(case["memory_tail"])


def bytes_directive(data):
    return ",".join(f"0x{value:02x}" for value in data)


def build_assembly(cases):
    lines = [
        ".section .text", ".globl _start", "_start:", "    movl %esp, startup_esp",
    ]
    for index, case in enumerate(cases):
        lines += [f"case_{index}:"]
        lines += [f"    movl $xmm_{index}, %eax"]
        for reg in range(8):
            lines += [f"    movdqu {reg * 16}(%eax), %xmm{reg}"]
        lines += [f"    movl $mmx_{index}, %eax"]
        for reg in range(8):
            lines += [f"    movq {reg * 8}(%eax), %mm{reg}"]
        lines += [f"    movl $0x{case['mxcsr']:08x}, mxcsr_{index}", f"    ldmxcsr mxcsr_{index}"]
        lines += ["    movl startup_esp, %esp", f"    movl ${case['eflags'] | 2}, %eax", "    pushl %eax", "    popfl"]
        for reg, value in enumerate(case["gpr"]):
            if reg != case["memory_register"]:
                lines += [f"    movl $0x{value:08x}, %{GPR_NAMES[reg]}"]
        if case["memory_register"] is not None:
            reg = GPR_NAMES[case["memory_register"]]
            lines += [f"    movl $memory_{index}, %{reg}"]
        lines += [f"instruction_{index}:", f"    .byte {bytes_directive(case['bytes'])}"]
        for reg, name in enumerate(GPR_NAMES):
            lines += [f"    movl %{name}, result_{index}+{reg * 4}"]
        lines += ["    seto %cl", "    lahf", "    movzbl %ah, %eax", f"    andl $0xd5, %eax", "    movzbl %cl, %edx", "    shll $11, %edx", "    orl %edx, %eax", f"    movl %eax, result_{index}+32"]
        lines += [f"    stmxcsr result_{index}+36"]
        for reg in range(8):
            lines += [f"    movdqu %xmm{reg}, result_{index}+{40 + reg * 16}"]
        for reg in range(8):
            lines += [f"    movq %mm{reg}, result_{index}+{168 + reg * 8}"]
        lines += [f"    movl $memory_{index}, %esi", f"    movl $result_{index}+232, %edi", f"    movl ${case['memory_size']}, %ecx", "    cld", "    rep movsb"]
        lines += ["    movl $4, %eax", "    movl $1, %ebx", f"    movl $result_{index}, %ecx", f"    movl ${result_size(case)}, %edx", "    int $0x80"]
    lines += ["    movl $1, %eax", "    xorl %ebx, %ebx", "    int $0x80"]
    lines += [".section .data", "startup_esp:", "    .long 0"]
    for index, case in enumerate(cases):
        lines += [".balign 16", f"xmm_{index}:", f"    .byte {bytes_directive(b''.join(case['xmm']))}"]
        lines += [f"mmx_{index}:", f"    .byte {bytes_directive(b''.join(little_bytes(v, 8) for v in case['mmx']))}"]
        lines += [f"mxcsr_{index}:", f"    .long {case['mxcsr']}"]
        lines += [".balign 16", f"memory_{index}:", f"    .byte {bytes_directive(case['memory'] + case['memory_tail'])}"]
        lines += [f"result_{index}:", f"    .zero {result_size(case)}"]
    return "\n".join(lines) + "\n"


def unpack_results(cases, output):
    expected_size = sum(result_size(case) for case in cases)
    if len(output) != expected_size:
        raise RuntimeError(f"qemu returned {len(output)} bytes; expected {expected_size}")
    results = []
    offset = 0
    for index, case in enumerate(cases):
        size = result_size(case)
        data = output[offset:offset + size]
        offset += size
        gpr = list(struct.unpack_from("<8I", data, 0))
        eflags = struct.unpack_from("<I", data, 32)[0] & ARITH_FLAGS
        mxcsr = struct.unpack_from("<I", data, 36)[0]
        xmm = [data[40 + reg * 16:56 + reg * 16].hex() for reg in range(8)]
        mmx = list(struct.unpack_from("<8Q", data, 168))
        memory = data[232:232 + case["memory_size"]].hex()
        results.append({"name": case["name"], "gpr": gpr, "eflags": eflags, "mxcsr": mxcsr, "xmm": xmm, "mmx": mmx, "memory": memory})
    return results


def cpp_u64(value):
    return f"0x{value:016x}ULL"


def cpp_bytes(data):
    return ", ".join(f"0x{value:02x}" for value in data)


def build_cpp(cases, results):
    lines = [
        '#include "boxedwine.h"',
        '#ifdef __TEST',
        '#include "testCPU.h"',
        '',
        'namespace {',
        'struct CpuDiff32Case { const char* name; U32 inputGpr[8]; U32 gpr[8]; U32 inputFlags; U32 flags; U32 inputMxcsr; U32 mxcsr; U64 inputXmm[8][2]; U64 xmm[8][2]; U64 inputMmx[8]; U64 mmx[8]; U8 inputMemory[64]; U8 memory[64]; U8 inputMemoryTail[960]; U8 memoryTail[960]; U8 code[15]; U8 codeLength; S8 memoryRegister; U16 memorySize; };',
        'const CpuDiff32Case CASES[] = {'
    ]
    for case, result in zip(cases, results):
        expected_gpr = result["gpr"][:]
        if case["memory_register"] is not None:
            expected_gpr[case["memory_register"]] = MEMORY_OFFSET
        input_xmm = []
        output_xmm = []
        for input_value, output_value in zip(case["xmm"], result["xmm"]):
            input_raw = bytes(input_value)
            output_raw = bytes.fromhex(output_value)
            input_xmm += [cpp_u64(int.from_bytes(input_raw[:8], "little")), cpp_u64(int.from_bytes(input_raw[8:], "little"))]
            output_xmm += [cpp_u64(int.from_bytes(output_raw[:8], "little")), cpp_u64(int.from_bytes(output_raw[8:], "little"))]
        output_memory = bytes.fromhex(result["memory"])
        input_memory = case["memory"] + case["memory_tail"]
        code = list(case["bytes"])
        code += [0] * (15 - len(code))
        lines += ['    {', f'        "{case["name"]}",']
        lines += [f'        {{{", ".join(f"0x{v:08x}" for v in case["gpr"])}}},']
        lines += [f'        {{{", ".join(f"0x{v:08x}" for v in expected_gpr)}}},']
        lines += [f'        0x{case["eflags"] & ARITH_FLAGS:08x}, 0x{result["eflags"]:08x},']
        lines += [f'        0x{case["mxcsr"]:08x}, 0x{result["mxcsr"]:08x},']
        lines += [f'        {{{", ".join("{" + input_xmm[i] + ", " + input_xmm[i + 1] + "}" for i in range(0, 16, 2))}}},']
        lines += [f'        {{{", ".join("{" + output_xmm[i] + ", " + output_xmm[i + 1] + "}" for i in range(0, 16, 2))}}},']
        lines += [f'        {{{", ".join(cpp_u64(v) for v in case["mmx"])}}},']
        lines += [f'        {{{", ".join(cpp_u64(v) for v in result["mmx"])}}},']
        lines += [f'        {{{cpp_bytes(input_memory[:64])}}},', f'        {{{cpp_bytes(output_memory[:64])}}},']
        lines += [f'        {{{cpp_bytes(input_memory[64:])}}},', f'        {{{cpp_bytes(output_memory[64:])}}},']
        lines += [f'        {{{cpp_bytes(bytes(code))}}}, {len(case["bytes"])}, {case["memory_register"] if case["memory_register"] is not None else -1}, {case["memory_size"]}']
        lines += ['    },']
    lines += [
        '};',
        '}',
        '',
        'void testCpuDiff32Generated() {',
        '    auto& context = testContext();',
        '    for (const auto& test : CASES) {',
        '        testNewInstruction(0);',
        '        CPU* cpu = context.cpu;',
        '        for (int i = 0; i < 8; ++i) {',
        '            cpu->reg[i].u32 = test.inputGpr[i];',
        '            cpu->xmm[i].pi.u64[0] = test.inputXmm[i][0];',
        '            cpu->xmm[i].pi.u64[1] = test.inputXmm[i][1];',
        '            cpu->fpu.getMMX(i)->q = test.inputMmx[i];',
        '        }',
        '        cpu->mxcsr = test.inputMxcsr;',
        '        cpu->setFlags(test.inputFlags | 2, FMASK_ALL);',
        '        for (int i = 0; i < 64; ++i) context.memory->writeb(TEST_HEAP_ADDRESS + 0x100 + i, test.inputMemory[i]);',
        '        for (int i = 64; i < test.memorySize; ++i) context.memory->writeb(TEST_HEAP_ADDRESS + 0x100 + i, test.inputMemoryTail[i - 64]);',
        '        if (test.memoryRegister >= 0) cpu->reg[test.memoryRegister].u32 = 0x100;',
        '        for (int i = 0; i < test.codeLength; ++i) testPushCode8(test.code[i]);',
        '        testRunCPU();',
        '        cpu->fillFlags();',
        '        for (int i = 0; i < 8; ++i) {',
        '            if (cpu->reg[i].u32 != test.gpr[i]) testFail("%s gpr%d expected=%08x actual=%08x", test.name, i, test.gpr[i], cpu->reg[i].u32);',
        '            if (cpu->xmm[i].pi.u64[0] != test.xmm[i][0] || cpu->xmm[i].pi.u64[1] != test.xmm[i][1]) testFail("%s xmm%d expected=%016llx:%016llx actual=%016llx:%016llx", test.name, i, (unsigned long long)test.xmm[i][1], (unsigned long long)test.xmm[i][0], (unsigned long long)cpu->xmm[i].pi.u64[1], (unsigned long long)cpu->xmm[i].pi.u64[0]);',
        '            if (cpu->fpu.getMMX(i)->q != test.mmx[i]) testFail("%s mm%d expected=%016llx actual=%016llx", test.name, i, (unsigned long long)test.mmx[i], (unsigned long long)cpu->fpu.getMMX(i)->q);',
        '        }',
        '        if ((cpu->flags & 0x8d5) != test.flags) testFail("%s flags expected=%08x actual=%08x", test.name, test.flags, cpu->flags & 0x8d5);',
        '        if ((cpu->mxcsr & 0x6000) != (test.mxcsr & 0x6000)) testFail("%s MXCSR.RC expected=%08x actual=%08x", test.name, test.mxcsr & 0x6000, cpu->mxcsr & 0x6000);',
        '        for (int i = 0; i < test.memorySize; ++i) { U8 expected = i < 64 ? test.memory[i] : test.memoryTail[i - 64]; U8 actual = context.memory->readb(TEST_HEAP_ADDRESS + 0x100 + i); if (actual != expected) testFail("%s memory[%d] expected=%02x actual=%02x", test.name, i, expected, actual); }',
        '    }',
        '}',
        '#endif',
        ''
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cases", type=pathlib.Path)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--run-qemu", action="store_true")
    parser.add_argument("--cpp", type=pathlib.Path)
    args = parser.parse_args()
    raw = json.loads(args.cases.read_text())
    source_cases = raw["cases"] if isinstance(raw, dict) else raw
    cases = [parse_case(case, i) for i, case in enumerate(source_cases)]
    args.out_dir.mkdir(parents=True, exist_ok=True)
    asm = args.out_dir / "batch.s"
    elf = args.out_dir / "batch.elf"
    asm.write_text(build_assembly(cases))
    subprocess.run(["i686-linux-gnu-gcc", "-m32", "-static", "-nostdlib", "-no-pie", "-Wl,-e,_start", "-Wl,--build-id=none", str(asm), "-o", str(elf)], check=True)
    if not args.run_qemu:
        print(elf)
        return 0
    output = subprocess.run(["qemu-i386-static", str(elf)], check=True, stdout=subprocess.PIPE).stdout
    results = unpack_results(cases, output)
    (args.out_dir / "expected.json").write_text(json.dumps(results, indent=2) + "\n")
    if args.cpp:
        args.cpp.parent.mkdir(parents=True, exist_ok=True)
        args.cpp.write_text(build_cpp(cases, results))
    print(f"{elf}: {len(results)} case(s)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
