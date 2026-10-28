// jit64_tests.cpp — native unit-test harness for the phase-1 JIT framework
// (source/emulation/cpu/jit64.cpp + include/jit64.h).
//
// Build: g++ -std=c++17 -DBOXEDWINE_GUEST_X64=1 -I<repo>/include 
//     <repo>/source/emulation/cpu/jit64.cpp jit64_tests.cpp -o jit64_tests
// No SDL, no KMemory64, no emcc: feeds hand-built guest instruction streams
// into jit64DecodeOne/jit64CompileStream/Jit64BlockCache and checks the
// emitted decisions (kind/size/sub/len/plan, cache validity, plan text).
//
// Exit 0 = all pass; non-zero = failures (count printed).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../../../include/jit64.h"
#include "../../../../include/jit64wasm.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

// Defined below (with the emission tests); testFallbackRules uses it
// earlier in the file. The forward declaration was missing, so this
// harness did not compile at all before K-M3 -- repaired here so the
// phase-1 gate (scripts/build-jit-wasm.sh step 1) runs again.
static bool decodeOne(const std::vector<U8>& b, Jit64Op& op);

// Decode one stream and return the op (asserts decode succeeded).
static Jit64Op dec(U64 base, std::vector<U8> b) {
    Jit64Op op;
    bool ok = jit64DecodeOne(base, b.data(), (U32)b.size(), op);
    CHECK(ok, "decode failed for %zu bytes @0x%llx", b.size(), base);
    jit64Classify(op, false, false);
    return op;
}

static void testCoverageTable() {
    U32 n = 0;
    const Jit64CoveredOpcode* t = jit64CoveredOpcodes(&n);
    CHECK(n == 38, "coverage table has %u entries, want 38", n);
    CHECK(t != nullptr, "coverage table null");
    // Spot-check rank order matches the profile.
    CHECK(t[0].op == 0x89, "rank1 op=0x%02x want 0x89", t[0].op);
    CHECK(t[1].op == 0x8B, "rank2 op=0x%02x want 0x8B", t[1].op);
    CHECK(t[2].op == 0x83, "rank3 op=0x%02x want 0x83", t[2].op);
    CHECK(t[3].op == 0xE8, "rank4 op=0x%02x want 0xE8", t[3].op);
    CHECK(t[4].op == 0x8D, "rank5 op=0x%02x want 0x8D", t[4].op);
    CHECK(t[5].op == 0x58, "rank6 op=0x%02x want 0x58 (POP class)", t[5].op);
    CHECK(t[6].op == 0xB8, "rank7 op=0x%02x want 0xB8 (MOV r,imm class)", t[6].op);
    CHECK(t[8].op == 0x50, "rank9 op=0x%02x want 0x50 (PUSH class)", t[8].op);
    CHECK(t[10].op == 0xC1, "rank11 op=0x%02x want 0xC1", t[10].op);
    CHECK(t[14].op == 0x0F && t[14].op2 == 0xB6, "rank15 want 0FB6 got %02x/%02x", t[14].op, t[14].op2);
    CHECK(t[18].op == 0x0F && t[18].op2 == 0x84, "rank19 want 0F84 got %02x/%02x", t[18].op, t[18].op2);
    CHECK(t[20].op == 0x63, "rank21 op=0x%02x want 0x63 (MOVSXD)", t[20].op);
    CHECK(t[21].op == 0x88, "rank22 op=0x%02x want 0x88 (MOV r/m8,r8)", t[21].op);
    CHECK(t[22].op == 0xF7 && t[22].kind == J64_GRP3, "rank23 want F7/GRP3 got %02x/%d", t[22].op, (int)t[22].kind);
    CHECK(t[23].op == 0x98 && t[23].kind == J64_CBW, "rank24 want 98/CBW got %02x/%d", t[23].op, (int)t[23].kind);
    CHECK(t[25].op == 0xD3 && t[25].kind == J64_SHIFT_CL, "rank26 want D3/SHIFT_CL got %02x/%d", t[25].op, (int)t[25].kind);
    CHECK(t[27].op == 0x99, "rank28 want 99");
    CHECK(t[27].kind == J64_CQO, "rank28 kind");
    CHECK(t[28].op == 0xC6, "rank29 want C6");
    CHECK(t[29].op == 0x0F && t[29].op2 == 0x90, "rank30 want 0F90 got %02x/%02x", t[29].op, t[29].op2);
    CHECK(t[29].kind == J64_SETCC, "rank30 kind");
    CHECK(t[30].op == 0x0F && t[30].op2 == 0xA3, "rank31 want 0FA3 got %02x/%02x", t[30].op, t[30].op2);
    CHECK(t[30].kind == J64_BT, "rank31 kind");
    CHECK(t[31].op2 == 0x10 && t[31].kind == J64_SSE_MOV128, "sse rank1 want 0F10/MOV128 got %02x/%d", t[31].op2, (int)t[31].kind);
    CHECK(t[32].op2 == 0x28 && t[32].kind == J64_SSE_MOV128, "sse rank2 want 0F28/MOV128 got %02x/%d", t[32].op2, (int)t[32].kind);
    CHECK(t[33].op2 == 0x6F && t[33].kind == J64_SSE_MOV128, "sse rank3 want 0F6F/MOV128 got %02x/%d", t[33].op2, (int)t[33].kind);
    CHECK(t[34].op2 == 0xEF && t[34].kind == J64_SSE_PXOR, "sse rank4 want 0FEF/PXOR got %02x/%d", t[34].op2, (int)t[34].kind);
    CHECK(t[35].op2 == 0x6E && t[35].kind == J64_SSE_MOVD, "sse rank5 want 0F6E/MOVD got %02x/%d", t[35].op2, (int)t[35].kind);
    CHECK(t[36].op == 0xFF && t[36].kind == J64_CALL_IND, "ff rank1 want FF/CALL_IND got %02x/%d", t[36].op, (int)t[36].kind);
    CHECK(t[37].op == 0xFF && t[37].kind == J64_JMP_IND, "ff rank2 want FF/JMP_IND got %02x/%d", t[37].op, (int)t[37].kind);
}

static void testSseDecodeClassify() {
    // 66 0F 6F C1          movdqa xmm0, xmm1  (load, reg form)
    Jit64Op a = dec(0x400000, {0x66, 0x0F, 0x6F, 0xC1});
    CHECK(a.kind == J64_SSE_MOV128 && a.sub == 0x6F && a.size == 16,
          "movdqa: kind=%d sub=%02x size=%d", a.kind, a.sub, a.size);
    CHECK(a.regField == 0 && a.rmIndex == 1 && !a.isMem, "movdqa: rf=%d rm=%d mem=%d",
          a.regField, a.rmIndex, a.isMem);
    CHECK(a.plan == JIT64_FAST, "movdqa plan fallback");
    // 66 0F 7F D0          movdqa [rax], xmm2  (store, mem form)
    Jit64Op b = dec(0x400000, {0x66, 0x0F, 0x7F, 0x10});
    CHECK(b.kind == J64_SSE_MOV128 && b.sub == 0x7F && b.isMem && b.regField == 2,
          "movdqa-store: kind=%d sub=%02x mem=%d rf=%d", b.kind, b.sub, b.isMem, b.regField);
    CHECK(b.plan == JIT64_FAST, "movdqa-store plan fallback");
    // F3 0F 6F C8          movdqu xmm1, xmm0  (F3 form, no 66)
    Jit64Op c = dec(0x400000, {0xF3, 0x0F, 0x6F, 0xC8});
    CHECK(c.kind == J64_SSE_MOV128 && c.sub == 0x6F && c.rep == 0xF3 && !c.osize66,
          "movdqu: kind=%d sub=%02x rep=%02x 66=%d", c.kind, c.sub, c.rep, (int)c.osize66);
    CHECK(c.regField == 1 && c.rmIndex == 0 && c.plan == JIT64_FAST, "movdqu plan/regs");
    // 0F 10 03             movups xmm0, [rbx]
    Jit64Op d = dec(0x400000, {0x0F, 0x10, 0x03});
    CHECK(d.kind == J64_SSE_MOV128 && d.sub == 0x10 && d.isMem && d.plan == JIT64_FAST,
          "movups: kind=%d sub=%02x mem=%d", d.kind, d.sub, d.isMem);
    // 0F 11 4D 00          movups [rbp], xmm1
    Jit64Op e = dec(0x400000, {0x0F, 0x11, 0x4D, 0x00});
    CHECK(e.kind == J64_SSE_MOV128 && e.sub == 0x11 && e.isMem && e.regField == 1 && e.len == 4,
          "movups-store: sub=%02x mem=%d rf=%d len=%d", e.sub, e.isMem, e.regField, e.len);
    CHECK(e.plan == JIT64_FAST, "movups-store plan fallback");
    // 0F 28 C2             movaps xmm0, xmm2
    Jit64Op f = dec(0x400000, {0x0F, 0x28, 0xC2});
    CHECK(f.kind == J64_SSE_MOV128 && f.sub == 0x28 && f.plan == JIT64_FAST,
          "movaps: kind=%d sub=%02x", f.kind, f.sub);
    // 66 0F 29 23          movapd [rbx], xmm4 (66 allowed on the 28/29 family)
    Jit64Op g = dec(0x400000, {0x66, 0x0F, 0x29, 0x23});
    CHECK(g.kind == J64_SSE_MOV128 && g.sub == 0x29 && g.isMem && g.regField == 4,
          "movapd-store: kind=%d sub=%02x mem=%d rf=%d", g.kind, g.sub, g.isMem, g.regField);
    CHECK(g.plan == JIT64_FAST, "movapd-store plan fallback");
    // 41 0F 10 00          movups xmm0, [r8]  (REX.B on the EA base)
    Jit64Op h = dec(0x400000, {0x41, 0x0F, 0x10, 0x00});
    CHECK(h.kind == J64_SSE_MOV128 && h.isMem && h.plan == JIT64_FAST,
          "movups-r8: kind=%d mem=%d", h.kind, h.isMem);
    // 44 0F 28 C1          movaps xmm8, xmm1 (REX.R extends regField)
    Jit64Op i = dec(0x400000, {0x44, 0x0F, 0x28, 0xC1});
    CHECK(i.kind == J64_SSE_MOV128 && i.regField == 8 && i.rmIndex == 1 && i.plan == JIT64_FAST,
          "movaps-xmm8: rf=%d rm=%d", i.regField, i.rmIndex);
    // 66 0F EF C0          pxor xmm0, xmm0 (the zeroing idiom)
    Jit64Op j = dec(0x400000, {0x66, 0x0F, 0xEF, 0xC0});
    CHECK(j.kind == J64_SSE_PXOR && j.size == 16 && j.regField == 0 && j.rmIndex == 0,
          "pxor: kind=%d size=%d rf=%d rm=%d", j.kind, j.size, j.regField, j.rmIndex);
    CHECK(j.plan == JIT64_FAST, "pxor plan fallback");
    // 66 0F EF 0A          pxor xmm1, [rdx]
    Jit64Op k = dec(0x400000, {0x66, 0x0F, 0xEF, 0x0A});
    CHECK(k.kind == J64_SSE_PXOR && k.isMem && k.regField == 1 && k.plan == JIT64_FAST,
          "pxor-mem: kind=%d mem=%d rf=%d", k.kind, k.isMem, k.regField);
    // 66 0F 6E C8          movd xmm1, eax
    Jit64Op l = dec(0x400000, {0x66, 0x0F, 0x6E, 0xC8});
    CHECK(l.kind == J64_SSE_MOVD && l.sub == 0 && l.size == 4 && l.regField == 1 && l.rmIndex == 0,
          "movd: kind=%d sub=%d size=%d rf=%d rm=%d", l.kind, l.sub, l.size, l.regField, l.rmIndex);
    CHECK(l.plan == JIT64_FAST, "movd plan fallback");
    // 66 48 0F 6E D3       movq xmm2, rbx (REX.W form)
    Jit64Op m = dec(0x400000, {0x66, 0x48, 0x0F, 0x6E, 0xD3});
    CHECK(m.kind == J64_SSE_MOVD && m.sub == 0 && m.size == 8 && m.regField == 2 && m.rmIndex == 3,
          "movq: kind=%d sub=%d size=%d rf=%d rm=%d", m.kind, m.sub, m.size, m.regField, m.rmIndex);
    CHECK(m.plan == JIT64_FAST, "movq plan fallback");
    // 66 0F 7E 11          movd [rcx], xmm2 (store form)
    Jit64Op n = dec(0x400000, {0x66, 0x0F, 0x7E, 0x11});
    CHECK(n.kind == J64_SSE_MOVD && n.sub == 1 && n.size == 4 && n.isMem && n.regField == 2,
          "movd-store: kind=%d sub=%d size=%d mem=%d rf=%d", n.kind, n.sub, n.size, n.isMem, n.regField);
    CHECK(n.plan == JIT64_FAST, "movd-store plan fallback");
    // 66 48 0F 7E C2       movq rdx, xmm2 (store form, register dest)
    Jit64Op o = dec(0x400000, {0x66, 0x48, 0x0F, 0x7E, 0xC2});
    CHECK(o.kind == J64_SSE_MOVD && o.sub == 1 && o.size == 8 && !o.isMem && o.rmIndex == 2,
          "movq-store: kind=%d sub=%d size=%d mem=%d rm=%d", o.kind, o.sub, o.size, o.isMem, o.rmIndex);
    CHECK(o.plan == JIT64_FAST, "movq-store plan fallback");

    // Interpreter-only forms must NOT decode as SSE ops (the decode returns
    // false, so the stream truncates and the interpreter runs them):
    // - the legacy MMX forms (0F 6E/6F/7E/7F with no 66 and no F3 use the
    //   x87 register stack, not XMM);
    // - F3 0F 7E (MOVQ xmm, m64 — a different interpreter block);
    // - F2/F3 0F 10/11 (MOVSD/MOVSS scalar forms);
    // - 66+F3 stacked prefixes on 0F 6F.
    Jit64Op x;
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0x0F, 0x6E, 0xC0}, 3, x), "MMX movd must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0x0F, 0x6F, 0xC0}, 3, x), "MMX movq must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0x0F, 0x7F, 0xC0}, 3, x), "MMX movq-store must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0x0F, 0xEF, 0xC0}, 3, x), "MMX pxor must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0xF3, 0x0F, 0x7E, 0xC0}, 4, x), "F3 0F7E MOVQ must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0xF2, 0x0F, 0x10, 0xC0}, 4, x), "MOVSD must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0xF3, 0x0F, 0x10, 0xC0}, 4, x), "MOVSS must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0x66, 0xF3, 0x0F, 0x6F, 0xC0}, 5, x), "66+F3 movdqa must not decode");
    CHECK(!jit64DecodeOne(0x400000, (const U8[]){0xF0, 0x66, 0x0F, 0xEF, 0xC0}, 5, x), "LOCK pxor must not decode");
    // 64 0F 10 00 (FS-segment EA): decode succeeds, classify declines —
    // the FS base lives on the CPU object, not in Jit64WasmState.
    Jit64Op seg;
    CHECK(jit64DecodeOne(0x400000, (const U8[]){0x64, 0x0F, 0x10, 0x00}, 4, seg) &&
          seg.kind == J64_SSE_MOV128, "seg movups decode");
    jit64Classify(seg, false, false);
    CHECK(seg.plan == JIT64_FALLBACK, "seg movups must stay fallback");
    // Emitter validation (defense in depth past the decoder/classifier):
    // a hand-forged SSE op outside the family must be refused, and a valid
    // one must emit.
    {
        Jit64Op bad;
        bad.kind = J64_SSE_MOV128; bad.sub = 0x12; bad.size = 16;
        bad.regField = 0; bad.rmIndex = 1; bad.isMem = false;
        Jit64Block block; block.ops.assign(&bad, &bad + 1);
        std::vector<U8> m;
        CHECK(!jit64EmitWasm(block, m), "forged MOV128 sub=0x12 must not emit");
        Jit64Op badSize;
        badSize.kind = J64_SSE_MOVD; badSize.sub = 0; badSize.size = 2;
        badSize.regField = 0; badSize.rmIndex = 1; badSize.isMem = false;
        Jit64Block block2; block2.ops.assign(&badSize, &badSize + 1);
        CHECK(!jit64EmitWasm(block2, m), "forged MOVD size=2 must not emit");
        Jit64Block block3; block3.ops.assign(&a, &a + 1); // movdqa xmm0, xmm1
        CHECK(jit64EmitWasm(block3, m) && !m.empty(), "movdqa must emit");
    }
}

static void testMovForms() {
    // 48 89 C8          mov rax, rcx
    Jit64Op a = dec(0x400000, {0x48, 0x89, 0xC8});
    CHECK(a.kind == J64_MOV_RM_R && a.size == 8 && a.len == 3, "89: kind=%d size=%d len=%d", a.kind, a.size, a.len);
    CHECK(a.regField == 1 && a.rmIndex == 0 && !a.isMem, "89: rf=%d rm=%d mem=%d", a.regField, a.rmIndex, a.isMem);
    CHECK(a.plan == JIT64_FAST, "89 plan fallback");
    // 8B 45 FC          mov eax, [rbp-4]
    Jit64Op b = dec(0x400000, {0x8B, 0x45, 0xFC});
    CHECK(b.kind == J64_MOV_R_RM && b.size == 4 && b.len == 3, "8B: kind=%d size=%d len=%d", b.kind, b.size, b.len);
    CHECK(b.isMem && b.regField == 0, "8B: mem=%d rf=%d", b.isMem, b.regField);
    CHECK(b.plan == JIT64_FAST, "8B plan fallback");
    // 48 B8 imm64       mov rax, imm64
    Jit64Op c = dec(0x400000, {0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11});
    CHECK(c.kind == J64_MOV_R_IMM && c.size == 8 && c.len == 10, "B8: kind=%d size=%d len=%d", c.kind, c.size, c.len);
    CHECK(c.imm == 0x1122334455667788ULL, "B8: imm=0x%llx", c.imm);
    CHECK(c.plan == JIT64_FAST, "B8 plan fallback");
    // B9 0A 00 00 00    mov ecx, 10
    Jit64Op d = dec(0x400000, {0xB9, 0x0A, 0x00, 0x00, 0x00});
    CHECK(d.kind == J64_MOV_R_IMM && d.size == 4 && d.len == 5 && d.regField == 1, "B9 len=%d rf=%d", d.len, d.regField);
    CHECK(d.imm == 10, "B9 imm=%llu", d.imm);
    // C7 45 FC 2A 00 00 00  mov dword [rbp-4], 42
    Jit64Op e = dec(0x400000, {0xC7, 0x45, 0xFC, 0x2A, 0x00, 0x00, 0x00});
    CHECK(e.kind == J64_MOV_RM_IMM && e.len == 7 && e.imm == 42, "C7: kind=%d len=%d imm=%llu", e.kind, e.len, e.imm);
    // C6 C0 42          mov al, 0x42  (J64_MOV_RM_IMM, size 1)
    Jit64Op e6a = dec(0x400000, {0xC6, 0xC0, 0x42});
    CHECK(e6a.kind == J64_MOV_RM_IMM && e6a.size == 1 && e6a.len == 3, "C6 reg: kind=%d size=%d len=%d", e6a.kind, e6a.size, e6a.len);
    CHECK(e6a.imm == 0x42 && e6a.rmIndex == 0 && !e6a.isMem, "C6 reg: imm=%llu rm=%d mem=%d", e6a.imm, e6a.rmIndex, e6a.isMem);
    CHECK(e6a.plan == JIT64_FAST, "C6 reg plan fallback");
    // C6 45 FC 42       mov byte [rbp-4], 0x42  (mem form)
    Jit64Op e6b = dec(0x400000, {0xC6, 0x45, 0xFC, 0x42});
    CHECK(e6b.kind == J64_MOV_RM_IMM && e6b.size == 1 && e6b.len == 4, "C6 mem: kind=%d size=%d len=%d", e6b.kind, e6b.size, e6b.len);
    CHECK(e6b.imm == 0x42 && e6b.isMem, "C6 mem: imm=%llu mem=%d", e6b.imm, e6b.isMem);
    CHECK(e6b.plan == JIT64_FAST, "C6 mem plan fallback");
    // 41 C6 C0 42       mov r8b, 0x42  (REX.B: rm 8, not high-byte)
    Jit64Op e6c = dec(0x400000, {0x41, 0xC6, 0xC0, 0x42});
    CHECK(e6c.kind == J64_MOV_RM_IMM && e6c.rmIndex == 8 && e6c.rexPresent, "C6 rex: rm=%d rex=%d", e6c.rmIndex, e6c.rexPresent);
    CHECK(e6c.plan == JIT64_FAST, "C6 rex plan fallback");
    // C6 C4 42          mov ah, 0x42  (no REX, rm 4 => AH: interpreter-only)
    Jit64Op e6d = dec(0x400000, {0xC6, 0xC4, 0x42});
    CHECK(e6d.kind == J64_MOV_RM_IMM && e6d.size == 1, "C6 ah: kind=%d size=%d", e6d.kind, e6d.size);
    CHECK(e6d.plan != JIT64_FAST, "C6 ah should decline (high-byte)");
    // C6 C8 42          /1 is undefined: decode must not produce MOV_RM_IMM
    Jit64Op e6e;
    bool ok6e = jit64DecodeOne(0x400000, (const U8[]){0xC6, 0xC8, 0x42}, 3, e6e);
    CHECK(!ok6e || e6e.kind != J64_MOV_RM_IMM, "C6 /1: ok=%d kind=%d", ok6e, e6e.kind);
    // 0F B6 C0          movzx eax, al  (J64_MOVX, sub 0=zx8)
    Jit64Op f = dec(0x400000, {0x0F, 0xB6, 0xC0});
    CHECK(f.kind == J64_MOVX && f.sub == 0 && f.len == 3, "0FB6: kind=%d sub=%d len=%d", f.kind, f.sub, f.len);
    CHECK(f.plan == JIT64_FAST, "0FB6 plan fallback");
    // 0F BE C0          movsx eax, al  (J64_MOVX, sub 2=sx8)
    Jit64Op f2 = dec(0x400000, {0x0F, 0xBE, 0xC0});
    CHECK(f2.kind == J64_MOVX && f2.sub == 2 && f2.len == 3, "0FBE: kind=%d sub=%d", f2.kind, f2.sub);
    CHECK(f2.plan == JIT64_FAST, "0FBE plan fallback");
    // 0F B7 C0          movzx eax, ax  (J64_MOVX, sub 1=zx16)
    Jit64Op f3 = dec(0x400000, {0x0F, 0xB7, 0xC0});
    CHECK(f3.kind == J64_MOVX && f3.sub == 1, "0FB7: kind=%d sub=%d", f3.kind, f3.sub);
    // 0F BF C0          movsx eax, ax  (J64_MOVX, sub 3=sx16)
    Jit64Op f4 = dec(0x400000, {0x0F, 0xBF, 0xC0});
    CHECK(f4.kind == J64_MOVX && f4.sub == 3, "0FBF: kind=%d sub=%d", f4.kind, f4.sub);
    // 63 C0              movsxd eax, eax  (J64_MOVX, sub 4=sxd32)
    Jit64Op g = dec(0x400000, {0x63, 0xC0});
    CHECK(g.kind == J64_MOVX && g.sub == 4 && g.size == 4 && g.len == 2,
          "63: kind=%d sub=%d size=%d len=%d", g.kind, g.sub, g.size, g.len);
    CHECK(g.regField == 0 && g.rmIndex == 0 && !g.isMem, "63: rf=%d rm=%d mem=%d",
          g.regField, g.rmIndex, g.isMem);
    CHECK(g.plan == JIT64_FAST, "63 plan fallback");
    // 48 63 CA          movsxd rcx, edx  (REX.W: 64-bit sign-extend)
    Jit64Op g2 = dec(0x400000, {0x48, 0x63, 0xCA});
    CHECK(g2.kind == J64_MOVX && g2.sub == 4 && g2.size == 8 && g2.len == 3,
          "4863: sub=%d size=%d len=%d", g2.sub, g2.size, g2.len);
    CHECK(g2.regField == 1 && g2.rmIndex == 2, "4863: rf=%d rm=%d", g2.regField, g2.rmIndex);
    CHECK(g2.plan == JIT64_FAST, "4863 plan fallback");
    // 41 63 C8          movsxd rcx, r8d  (REX.B extends the source)
    Jit64Op g3 = dec(0x400000, {0x41, 0x63, 0xC8});
    CHECK(g3.rmIndex == 8 && g3.regField == 1 && g3.size == 4,
          "4163: rm=%d rf=%d size=%d", g3.rmIndex, g3.regField, g3.size);
    // 4D 63 C0          movsxd r8, r8d  (REX.W+B)
    Jit64Op g3b = dec(0x400000, {0x4D, 0x63, 0xC0});
    CHECK(g3b.regField == 8 && g3b.rmIndex == 8 && g3b.size == 8,
          "4D63: rf=%d rm=%d size=%d", g3b.regField, g3b.rmIndex, g3b.size);
    // 66 63 C0          movsxd (0x66): decoded as 32-bit (the interpreter
    // ignores 0x66) but stays fallback via the classifier's osize16 guard.
    {
        Jit64Op o; bool ok = jit64DecodeOne(0x400000, (const U8[]){0x66, 0x63, 0xC0}, 3, o);
        CHECK(ok && o.kind == J64_MOVX && o.sub == 4 && o.size == 4,
              "6663: kind=%d sub=%d size=%d", o.kind, o.sub, o.size);
        jit64Classify(o, false, true);
        CHECK(o.plan == JIT64_FALLBACK, "6663 must stay fallback (osize16 guard)");
    }
    // 63 03              movsxd eax, dword [rbx]: mem form decodes (isMem)
    // but stays interpreter-only (register-direct only at emission).
    Jit64Op g4 = dec(0x400000, {0x63, 0x03});
    CHECK(g4.kind == J64_MOVX && g4.sub == 4 && g4.isMem && g4.len == 2,
          "63mem: kind=%d sub=%d mem=%d len=%d", g4.kind, g4.sub, g4.isMem, g4.len);
}

static void testMov8Forms() {
    // 88 C8              mov al, cl  (reg-direct, no REX: low bytes)
    Jit64Op a = dec(0x400000, {0x88, 0xC8});
    CHECK(a.kind == J64_MOV_RM_R && a.size == 1 && a.len == 2,
          "88: kind=%d size=%d len=%d", a.kind, a.size, a.len);
    CHECK(a.regField == 1 && a.rmIndex == 0 && !a.isMem && !a.rexPresent,
          "88: rf=%d rm=%d mem=%d rex=%d", a.regField, a.rmIndex, a.isMem, a.rexPresent);
    CHECK(a.plan == JIT64_FAST, "88 plan fallback");
    // 88 E0              mov al, ah  (no REX: index 4 => high byte of rax)
    Jit64Op b = dec(0x400000, {0x88, 0xE0});
    CHECK(b.kind == J64_MOV_RM_R && b.size == 1 && b.regField == 4 && b.rmIndex == 0,
          "88E0: rf=%d rm=%d size=%d", b.regField, b.rmIndex, b.size);
    CHECK(!b.rexPresent && b.plan == JIT64_FAST, "88E0 rex/fast");
    // 40 88 E4           mov spl, ah->spl  (REX present: index 4 => SPL, not AH)
    Jit64Op c = dec(0x400000, {0x40, 0x88, 0xE4});
    CHECK(c.kind == J64_MOV_RM_R && c.size == 1 && c.len == 3,
          "4088: kind=%d size=%d len=%d", c.kind, c.size, c.len);
    CHECK(c.regField == 4 && c.rmIndex == 4 && c.rexPresent,
          "4088: rf=%d rm=%d rex=%d", c.regField, c.rmIndex, c.rexPresent);
    CHECK(c.plan == JIT64_FAST, "4088 plan fallback");
    // 45 88 C1           mov r9b, r8b  (REX.R+B extend both byte registers)
    Jit64Op d = dec(0x400000, {0x45, 0x88, 0xC1});
    CHECK(d.regField == 8 && d.rmIndex == 9 && d.size == 1 && d.rexPresent,
          "4588: rf=%d rm=%d", d.regField, d.rmIndex);
    CHECK(d.plan == JIT64_FAST, "4588 plan fallback");
    // 8A C3              mov al, bl  (reverse direction)
    Jit64Op e = dec(0x400000, {0x8A, 0xC3});
    CHECK(e.kind == J64_MOV_R_RM && e.size == 1 && e.len == 2,
          "8A: kind=%d size=%d len=%d", e.kind, e.size, e.len);
    CHECK(e.regField == 0 && e.rmIndex == 3 && !e.isMem,
          "8A: rf=%d rm=%d mem=%d", e.regField, e.rmIndex, e.isMem);
    CHECK(e.plan == JIT64_FAST, "8A plan fallback");
    // 66 88 C8           mov al, cl with 0x66: prefix ignored, still size 1 + fast
    {
        Jit64Op o; bool ok = jit64DecodeOne(0x400000, (const U8[]){0x66, 0x88, 0xC8}, 3, o);
        CHECK(ok && o.kind == J64_MOV_RM_R && o.size == 1,
              "6688: kind=%d size=%d", o.kind, o.size);
        jit64Classify(o, false, true);
        CHECK(o.plan == JIT64_FAST, "6688 must stay fast (0x66 ignored for 8-bit)");
    }
    // 88 45 FC           mov byte [rbp-4], al  (mem form: EA decoded)
    Jit64Op f = dec(0x400000, {0x88, 0x45, 0xFC});
    CHECK(f.kind == J64_MOV_RM_R && f.size == 1 && f.isMem && f.len == 3,
          "88mem: kind=%d size=%d mem=%d len=%d", f.kind, f.size, f.isMem, f.len);
    CHECK(f.ea.baseReg == 5 && f.ea.disp == (S64)(S8)0xFC,
          "88mem: ea base=%d disp=%lld", f.ea.baseReg, (long long)f.ea.disp);
    CHECK(f.plan == JIT64_FAST, "88mem plan fallback");
    // 8A 04 25 78 56 34 12  mov al, [0x12345678]  (disp32-only mem form)
    Jit64Op g = dec(0x400000, {0x8A, 0x04, 0x25, 0x78, 0x56, 0x34, 0x12});
    CHECK(g.kind == J64_MOV_R_RM && g.size == 1 && g.isMem && g.len == 7,
          "8Amem: kind=%d size=%d mem=%d len=%d", g.kind, g.size, g.isMem, g.len);
    CHECK(g.ea.baseReg == 0xFF && g.plan == JIT64_FAST, "8Amem disp32-only");
    // 66 89 C8           mov ax, cx: 16-bit MOV stays interpreter-only.
    {
        Jit64Op o; bool ok = jit64DecodeOne(0x400000, (const U8[]){0x66, 0x89, 0xC8}, 3, o);
        CHECK(ok && o.kind == J64_MOV_RM_R && o.size == 2, "6689 size=%d", o.size);
        jit64Classify(o, false, true);
        CHECK(o.plan == JIT64_FALLBACK, "6689 must stay fallback (16-bit MOV)");
    }
    // 88 C0              mov al, al (dest==src aliasing decodes cleanly)
    Jit64Op h = dec(0x400000, {0x88, 0xC0});
    CHECK(h.kind == J64_MOV_RM_R && h.regField == 0 && h.rmIndex == 0 && h.plan == JIT64_FAST,
          "88C0 aliasing");
}

static void testAluForms() {
    // 48 01 C8          add rax, rcx (rank-16 opcode 01)
    Jit64Op a = dec(0x400000, {0x48, 0x01, 0xC8});
    CHECK(a.kind == J64_ALU_RM_R && a.sub == 0 && a.size == 8 && a.len == 3, "01: kind=%d sub=%d", a.kind, a.sub);
    CHECK(a.plan == JIT64_FAST, "01 plan fallback");
    // 39 D8             cmp eax, ebx (rank-10 opcode 39)
    Jit64Op b = dec(0x400000, {0x39, 0xD8});
    CHECK(b.kind == J64_ALU_RM_R && b.sub == 7 && b.size == 4, "39: kind=%d sub=%d", b.kind, b.sub);
    // 48 29 D8          sub rax, rbx (rank-20 opcode 29)
    Jit64Op c = dec(0x400000, {0x48, 0x29, 0xD8});
    CHECK(c.kind == J64_ALU_RM_R && c.sub == 5 && c.size == 8, "29: kind=%d sub=%d", c.kind, c.sub);
    // 48 31 C0          xor rax, rax (rank-8 opcode 31)
    Jit64Op d = dec(0x400000, {0x48, 0x31, 0xC0});
    CHECK(d.kind == J64_ALU_RM_R && d.sub == 6, "31: kind=%d sub=%d", d.kind, d.sub);
    // 48 83 C0 03       add rax, 3 (rank-3 opcode 83)
    Jit64Op e = dec(0x400000, {0x48, 0x83, 0xC0, 0x03});
    CHECK(e.kind == J64_ALU_RM_IMM && e.sub == 0 && e.len == 4 && e.imm == 3, "83: kind=%d sub=%d len=%d imm=%llu", e.kind, e.sub, e.len, e.imm);
    // 48 83 E8 02       sub rax, 2 (sign-extended imm still positive)
    Jit64Op f = dec(0x400000, {0x48, 0x83, 0xE8, 0x02});
    CHECK(f.sub == 5 && f.imm == 2, "83-sub: sub=%d imm=%llu", f.sub, f.imm);
    // 48 83 F8 FF       cmp rax, -1 (imm8 0xFF sign-extends to all-ones)
    Jit64Op g = dec(0x400000, {0x48, 0x83, 0xF8, 0xFF});
    CHECK(g.sub == 7 && g.imm == 0xFFFFFFFFFFFFFFFFULL, "83-cmp-neg: sub=%d imm=0x%llx", g.sub, g.imm);
    // 05 34 12 00 00    add eax, 0x1234 (ACC-imm form)
    Jit64Op h = dec(0x400000, {0x05, 0x34, 0x12, 0x00, 0x00});
    CHECK(h.kind == J64_ALU_ACC_IMM && h.sub == 0 && h.len == 5 && h.imm == 0x1234, "05: kind=%d len=%d imm=0x%llx", h.kind, h.len, h.imm);
    // 85 C0             test eax, eax (rank-6 opcode 85)
    Jit64Op i = dec(0x400000, {0x85, 0xC0});
    CHECK(i.kind == J64_TEST_RM_R && i.size == 4 && i.len == 2, "85: kind=%d", i.kind);
    CHECK(i.plan == JIT64_FAST, "85 plan fallback");
}

static void testEaDecode() {
    // 48 01 00          add [rax], rax
    Jit64Op a = dec(0x400000, {0x48, 0x01, 0x00});
    CHECK(a.kind == J64_ALU_RM_R && a.isMem && a.size == 8 && a.len == 3, "ea: kind=%d mem=%d", a.kind, a.isMem);
    CHECK(a.plan == JIT64_FAST, "ea: mem ALU plan fallback");
    CHECK(!a.ea.ripRel && a.ea.baseReg == 0 && a.ea.idxReg == 0xFF &&
          a.ea.scale == 0 && a.ea.disp == 0 && !a.ea.asize32 && a.ea.seg == 0,
          "ea: [rax] base=%d idx=%d", a.ea.baseReg, a.ea.idxReg);
    // 48 03 8C 90 10 00 00 00   add rcx, [rax + rdx*4 + 0x10]
    Jit64Op b = dec(0x400000, {0x48, 0x03, 0x8C, 0x90, 0x10, 0x00, 0x00, 0x00});
    CHECK(b.kind == J64_ALU_R_RM && b.isMem && b.len == 8, "ea: SIB kind=%d len=%d", b.kind, b.len);
    CHECK(b.ea.baseReg == 0 && b.ea.idxReg == 2 && b.ea.scale == 2 && b.ea.disp == 0x10,
          "ea: SIB base=%d idx=%d scale=%d disp=%lld", b.ea.baseReg, b.ea.idxReg, b.ea.scale, b.ea.disp);
    CHECK(b.plan == JIT64_FAST, "ea: SIB plan fallback");
    // 48 03 0D 78 56 34 12   add rcx, [rip + 0x12345678]  (RIP-relative)
    Jit64Op c = dec(0x400000, {0x48, 0x03, 0x0D, 0x78, 0x56, 0x34, 0x12});
    CHECK(c.isMem && c.len == 7 && c.ea.ripRel, "ea: riprel mem=%d rel=%d", c.isMem, c.ea.ripRel);
    CHECK(c.ea.ripRelTarget == 0x400007ULL + 0x12345678ULL, "ea: riprel target=0x%llx", c.ea.ripRelTarget);
    CHECK(c.plan == JIT64_FAST, "ea: riprel plan fallback");
    // 48 81 2D 78 56 34 12 05 00 00 00   sub [rip+disp32], 5 (imm after disp)
    Jit64Op d = dec(0x400000, {0x48, 0x81, 0x2D, 0x78, 0x56, 0x34, 0x12, 0x05, 0x00, 0x00, 0x00});
    CHECK(d.kind == J64_ALU_RM_IMM && d.isMem && d.sub == 5 && d.imm == 5 && d.len == 11,
          "ea: 81rip kind=%d sub=%d imm=%llu len=%d", d.kind, d.sub, d.imm, d.len);
    CHECK(d.ea.ripRel && d.ea.ripRelTarget == 0x40000BULL + 0x12345678ULL,
          "ea: 81rip target=0x%llx", d.ea.ripRelTarget);
    // 48 03 0C 25 00 10 00 00   add rcx, [0x1000]  (SIB disp32-only: no base/index)
    Jit64Op e = dec(0x400000, {0x48, 0x03, 0x0C, 0x25, 0x00, 0x10, 0x00, 0x00});
    CHECK(e.isMem && e.len == 8 && e.ea.baseReg == 0xFF && e.ea.idxReg == 0xFF &&
          e.ea.disp == 0x1000, "ea: sib32 base=%d idx=%d disp=%lld",
          e.ea.baseReg, e.ea.idxReg, e.ea.disp);
    // 48 03 4C 48 F0   add rcx, [rax + rcx*2 - 16]  (SIB + disp8)
    Jit64Op f = dec(0x400000, {0x48, 0x03, 0x4C, 0x48, 0xF0});
    CHECK(f.isMem && f.len == 5 && f.ea.baseReg == 0 && f.ea.idxReg == 1 &&
          f.ea.scale == 1 && f.ea.disp == -16, "ea: sibd8 disp=%lld", f.ea.disp);
    // 48 03 88 34 12 00 00   add rcx, [rax + 0x1234]  (mod=2 disp32, no SIB)
    Jit64Op g = dec(0x400000, {0x48, 0x03, 0x88, 0x34, 0x12, 0x00, 0x00});
    CHECK(g.isMem && g.len == 7 && g.ea.baseReg == 0 && g.ea.disp == 0x1234,
          "ea: d32 disp=%lld", g.ea.disp);
    // 67 48 03 00   add rcx, [eax]  (0x67 address-size)
    Jit64Op h = dec(0x400000, {0x67, 0x48, 0x03, 0x00});
    CHECK(h.isMem && h.ea.asize32 && h.ea.baseReg == 0, "ea: 0x67 asize=%d", h.ea.asize32);
    CHECK(h.plan == JIT64_FAST, "ea: 0x67 plan fallback");
    // 64 48 03 00   add rcx, fs:[rax]  -> interpreter-only
    Jit64Op i = dec(0x400000, {0x64, 0x48, 0x03, 0x00});
    CHECK(i.isMem && i.ea.seg == 0x64, "ea: FS seg=0x%x", i.ea.seg);
    CHECK(i.plan == JIT64_FALLBACK, "ea: FS must be fallback");
    // 65 48 03 00   add rcx, gs:[rax]  -> interpreter-only
    Jit64Op j = dec(0x400000, {0x65, 0x48, 0x03, 0x00});
    CHECK(j.isMem && j.ea.seg == 0x65 && j.plan == JIT64_FALLBACK, "ea: GS seg/fallback");
    // 4A 03 0C 24   add rcx, [rsp + r12]  (REX.X: idxF=4 means R12)
    Jit64Op k = dec(0x400000, {0x4A, 0x03, 0x0C, 0x24});
    CHECK(k.isMem && k.ea.baseReg == 4 && k.ea.idxReg == 12 && k.ea.scale == 0,
          "ea: REX.X base=%d idx=%d", k.ea.baseReg, k.ea.idxReg);
    // 49 03 08   add rcx, [r8]  (REX.B extends the base)
    Jit64Op l = dec(0x400000, {0x49, 0x03, 0x08});
    CHECK(l.isMem && l.ea.baseReg == 8, "ea: REX.B base=%d", l.ea.baseReg);
    // 85 00   test [rax], eax  (read-only memory TEST)
    Jit64Op m = dec(0x400000, {0x85, 0x00});
    CHECK(m.kind == J64_TEST_RM_R && m.isMem && m.size == 4 && m.ea.baseReg == 0,
          "ea: TEST kind=%d mem=%d", m.kind, m.isMem);
    CHECK(m.plan == JIT64_FAST, "ea: TEST plan fallback");
    // 83 00 05   add dword [rax], 5  (immediate-to-memory)
    Jit64Op n = dec(0x400000, {0x83, 0x00, 0x05});
    CHECK(n.kind == J64_ALU_RM_IMM && n.isMem && n.sub == 0 && n.imm == 5 &&
          n.len == 3 && n.ea.baseReg == 0, "ea: 83mem sub=%d imm=%llu", n.sub, n.imm);
    CHECK(n.plan == JIT64_FAST, "ea: 83mem plan fallback");
    // 80 30 07   xor byte [rax], 7
    Jit64Op o = dec(0x400000, {0x80, 0x30, 0x07});
    CHECK(o.kind == J64_ALU_RM_IMM && o.isMem && o.size == 1 && o.sub == 6 && o.imm == 7,
          "ea: 80mem size=%d sub=%d", o.size, o.sub);
    // 66 01 00   add [rax], ax  (16-bit memory ALU)
    Jit64Op p16 = dec(0x400000, {0x66, 0x01, 0x00});
    CHECK(p16.isMem && p16.size == 2 && p16.len == 3 && p16.ea.baseReg == 0,
          "ea: 16bit size=%d", p16.size);
    CHECK(p16.plan == JIT64_FAST, "ea: 16bit plan fallback");
    // F0 48 01 00   lock add [rax], rax  -> interpreter-only (atomicity)
    Jit64Op q = dec(0x400000, {0xF0, 0x48, 0x01, 0x00});
    jit64Classify(q, true, false);
    CHECK(q.plan == JIT64_FALLBACK, "ea: LOCK mem ALU must be fallback");
    // Register-direct forms still produce no EA.
    Jit64Op r = dec(0x400000, {0x48, 0x01, 0xC8});
    CHECK(!r.isMem && r.ea.baseReg == 0xFF && !r.ea.ripRel, "ea: reg form has no EA");
}

static void testStackDecode() {
    // PUSH/POP are always 64-bit in long mode (the interpreter ignores
    // 0x66); the decoder sets size=8 unconditionally.
    // 50+rd: PUSH reg (sub 0).
    Jit64Op a = dec(0x400000, {0x50});
    CHECK(a.kind == J64_PUSH && a.sub == 0 && a.size == 8 && a.regField == 0 && a.len == 1,
          "50: kind=%d sub=%d size=%d", a.kind, a.sub, a.size);
    // 41 57: PUSH r15 (REX.B).
    Jit64Op b = dec(0x400000, {0x41, 0x57});
    CHECK(b.kind == J64_PUSH && b.regField == 15 && b.len == 2, "4157: rf=%d", b.regField);
    // 66 50: 0x66 ignored for size (still 8), but the osize16 guard keeps
    // it fallback (conservative; the interpreter handles it as 64-bit).
    {
        Jit64Op o;
        bool ok = jit64DecodeOne(0x400000, (const U8[]){0x66, 0x50}, 2, o);
        CHECK(ok && o.kind == J64_PUSH && o.size == 8, "6650: size=%d", o.size);
        jit64Classify(o, false, true);
        CHECK(o.plan == JIT64_FALLBACK, "6650 must stay fallback");
    }
    // 58+rd: POP reg (sub 0).
    Jit64Op c = dec(0x400000, {0x5F});
    CHECK(c.kind == J64_POP && c.sub == 0 && c.size == 8 && c.regField == 7 && c.len == 1,
          "5F: kind=%d sub=%d", c.kind, c.sub);
    // 6A ib: PUSH imm8 sign-extended.
    Jit64Op d = dec(0x400000, {0x6A, 0xFE});
    CHECK(d.kind == J64_PUSH && d.sub == 1 && d.size == 8 && d.len == 2 &&
          d.imm == 0xFFFFFFFFFFFFFFFEULL, "6A: imm=0x%llx", (unsigned long long)d.imm);
    // 68 id: PUSH imm32 sign-extended.
    Jit64Op e = dec(0x400000, {0x68, 0x00, 0x00, 0x00, 0x80});
    CHECK(e.kind == J64_PUSH && e.sub == 1 && e.len == 5 &&
          e.imm == 0xFFFFFFFF80000000ULL, "68: imm=0x%llx", (unsigned long long)e.imm);
    // FF /6 mod=3: PUSH reg (register-direct r/m).
    Jit64Op f = dec(0x400000, {0xFF, 0xF3}); // mod=3, reg=6, rm=3 (RBX)
    CHECK(f.kind == J64_PUSH && f.sub == 2 && f.size == 8 && !f.isMem &&
          f.rmIndex == 3 && f.len == 2, "FFF3: sub=%d mem=%d rm=%d", f.sub, f.isMem, f.rmIndex);
    // FF /6 mod!=3: PUSH [rax+0x10].
    Jit64Op g = dec(0x400000, {0xFF, 0x70, 0x10});
    CHECK(g.kind == J64_PUSH && g.sub == 2 && g.isMem && g.len == 3,
          "FF70: sub=%d mem=%d", g.sub, g.isMem);
    CHECK(g.ea.baseReg == 0 && g.ea.disp == 16, "FF70: ea base=%d disp=%lld",
          g.ea.baseReg, (long long)g.ea.disp);
    // FF /0 is INC, not PUSH: must not decode as J64_PUSH.
    {
        Jit64Op o;
        bool ok = jit64DecodeOne(0x400000, (const U8[]){0xFF, 0xC0}, 2, o);
        CHECK(!ok || o.kind != J64_PUSH, "FF /0 must not decode as PUSH");
    }
    // 8F /0 mod=3: POP reg (register-direct r/m).
    Jit64Op h = dec(0x400000, {0x8F, 0xC5}); // mod=3, reg=0, rm=5 (RBP)
    CHECK(h.kind == J64_POP && h.sub == 1 && h.size == 8 && !h.isMem &&
          h.rmIndex == 5 && h.len == 2, "8FC5: sub=%d mem=%d rm=%d", h.sub, h.isMem, h.rmIndex);
    // 8F /0 mod!=3: POP [rbx].
    Jit64Op i = dec(0x400000, {0x8F, 0x03});
    CHECK(i.kind == J64_POP && i.sub == 1 && i.isMem && i.len == 2,
          "8F03: sub=%d mem=%d", i.sub, i.isMem);
    CHECK(i.ea.baseReg == 3, "8F03: ea base=%d", i.ea.baseReg);
    // 8F /1: not POP: must not decode as J64_POP.
    {
        Jit64Op o;
        bool ok = jit64DecodeOne(0x400000, (const U8[]){0x8F, 0xC8}, 2, o);
        CHECK(!ok || o.kind != J64_POP, "8F /1 must not decode as POP");
    }
    // Segment pushes are #UD in the interpreter: stay unknown.
    {
        Jit64Op o;
        bool ok = jit64DecodeOne(0x400000, (const U8[]){0x06}, 1, o);
        CHECK(!ok || o.kind == J64_UNKNOWN, "06 (PUSH ES) must stay unknown");
    }
    // LOCK on PUSH stays interpreter-only.
    {
        Jit64Op o;
        bool ok = jit64DecodeOne(0x400000, (const U8[]){0xF0, 0x50}, 2, o);
        CHECK(ok && o.kind == J64_PUSH, "F050 must decode");
        jit64Classify(o, true, false);
        CHECK(o.plan == JIT64_FALLBACK, "F050 LOCK must be fallback");
    }
    // REX.W PUSH/POP are FAST (the common case).
    {
        Jit64Op o = dec(0x400000, {0x48, 0x50});
        CHECK(o.plan == JIT64_FAST && o.size == 8, "4850: plan=%d", o.plan);
    }
}

static void testMiscFast() {
    // 48 8D 35 1A 00 00 00   lea rsi, [rip+0x1A]
    Jit64Op a = dec(0x400000, {0x48, 0x8D, 0x35, 0x1A, 0x00, 0x00, 0x00});
    CHECK(a.kind == J64_LEA && a.len == 7 && a.isMem, "8D: kind=%d len=%d mem=%d", a.kind, a.len, a.isMem);
    // C1 E0 04           shl eax, 4 (rank-17 opcode C1)
    Jit64Op b = dec(0x400000, {0xC1, 0xE0, 0x04});
    CHECK(b.kind == J64_SHIFT_IMM && b.sub == 4 && b.imm == 4 && b.len == 3, "C1: kind=%d sub=%d", b.kind, b.sub);
    // 50 / 5D            push rax, pop rbp (representative stack row)
    Jit64Op c = dec(0x400000, {0x50});
    CHECK(c.kind == J64_PUSH && c.regField == 0 && c.len == 1, "50: kind=%d", c.kind);
    Jit64Op d = dec(0x400000, {0x5D});
    CHECK(d.kind == J64_POP && d.regField == 5 && d.len == 1, "5D: kind=%d rf=%d", d.kind, d.regField);
    // E8 F3 FF FF FF     call -13
    Jit64Op e = dec(0x400010, {0xE8, 0xF3, 0xFF, 0xFF, 0xFF});
    CHECK(e.kind == J64_CALL_REL && e.len == 5 && e.delta == -13, "E8: kind=%d delta=%lld", e.kind, (long long)e.delta);
    // 74 07             jz +7
    Jit64Op f = dec(0x400000, {0x74, 0x07});
    CHECK(f.kind == J64_JCC && f.sub == 4 && f.delta == 7 && f.len == 2, "74: sub=%d delta=%lld", f.sub, (long long)f.delta);
    // 0F 84 10 00 00 00  jz rel32
    Jit64Op g = dec(0x400000, {0x0F, 0x84, 0x10, 0x00, 0x00, 0x00});
    CHECK(g.kind == J64_JCC && g.sub == 4 && g.delta == 16 && g.len == 6, "0F84: delta=%lld", (long long)g.delta);
    // 75 FB             jnz -5
    Jit64Op h = dec(0x400000, {0x75, 0xFB});
    CHECK(h.kind == J64_JCC && h.sub == 5 && h.delta == -5, "75: delta=%lld", (long long)h.delta);
    // C3 / C9 / 90
    CHECK(dec(0x400000, {0xC3}).kind == J64_RET, "C3 not RET");
    CHECK(dec(0x400000, {0xC9}).kind == J64_LEAVE, "C9 not LEAVE");
    CHECK(dec(0x400000, {0x90}).kind == J64_NOP, "90 not NOP");
    // E9 00 10 00 00     jmp rel32
    Jit64Op k = dec(0x400000, {0xE9, 0x00, 0x10, 0x00, 0x00});
    CHECK(k.kind == J64_JMP_REL && k.delta == 0x1000, "E9 delta=%lld", (long long)k.delta);
    // 0F 05 syscall decodes, but classifies fallback
    Jit64Op s;
    CHECK(jit64DecodeOne(0x400000, (const U8*)"\x0F\x05", 2, s) && s.kind == J64_SYSCALL, "0F05 not SYSCALL");
    jit64Classify(s, false, false);
    CHECK(s.plan == JIT64_FALLBACK, "syscall must be fallback");
}

static void testFallbackRules() {
    // LOCK prefix forces fallback even on a fast opcode.
    Jit64Op a, b;
    const U8 lockAdd[] = {0xF0, 0x48, 0x01, 0xC8};
    CHECK(jit64DecodeOne(0x400000, lockAdd, 4, a) && a.kind == J64_ALU_RM_R, "F0-decode kind=%d", a.kind);
    jit64Classify(a, true, false);
    CHECK(a.plan == JIT64_FALLBACK, "LOCK add must be fallback");
    // HotSpot/glibc reference-count and mutex updates must stay on the CPU64
    // interpreter path: the JIT has no atomic RMW emission or shared-memory
    // host atomic lowering for these instructions.
    CHECK(!decodeOne({0xF0, 0x48, 0x0F, 0xB1, 0x08}, b),
          "LOCK CMPXCHG must stay interpreter-only");
    CHECK(!decodeOne({0xF0, 0x48, 0x0F, 0xC1, 0x08}, b),
          "LOCK XADD must stay interpreter-only");
    CHECK(!decodeOne({0x48, 0x87, 0x08}, b),
          "memory XCHG must stay interpreter-only (implicit LOCK)");
    // 66h operand size forces fallback.
    const U8 op16[] = {0x66, 0x89, 0xC8};
    CHECK(jit64DecodeOne(0x400000, op16, 3, b), "66-decode failed");
    jit64Classify(b, false, true);
    CHECK(b.plan == JIT64_FALLBACK, "o16 mov must be fallback");
    // 8-bit ALU decodes (size 1) and classifies fast.
    Jit64Op c = dec(0x400000, {0x00, 0xC8});
    CHECK(c.kind == J64_ALU_RM_R && c.size == 1 && c.sub == 0 && c.len == 2,
          "8-bit ADD: kind=%d size=%d sub=%d len=%d", c.kind, c.size, c.sub, c.len);
    CHECK(c.plan == JIT64_FAST, "8-bit ADD plan fallback");
    CHECK(!c.rexPresent, "8-bit ADD without REX");
    // 44 00 C8          add r8b, r9b (REX: no high-byte rule)
    Jit64Op c2 = dec(0x400000, {0x44, 0x00, 0xC8});
    CHECK(c2.kind == J64_ALU_RM_R && c2.size == 1 && c2.rexPresent, "REX add8: kind=%d", c2.kind);
    CHECK(c2.rmIndex == 0 && c2.regField == 9, "REX add8: rm=%d rf=%d", c2.rmIndex, c2.regField);
    // 00 E4             add ah, ah (no REX: high-byte registers)
    Jit64Op c3 = dec(0x400000, {0x00, 0xE4});
    CHECK(c3.kind == J64_ALU_RM_R && c3.size == 1 && !c3.rexPresent, "ah add: kind=%d", c3.kind);
    CHECK(c3.rmIndex == 4 && c3.regField == 4, "ah add: rm=%d rf=%d", c3.rmIndex, c3.regField);
    // 80 C0 05          add al, 5
    Jit64Op c4 = dec(0x400000, {0x80, 0xC0, 0x05});
    CHECK(c4.kind == J64_ALU_RM_IMM && c4.size == 1 && c4.sub == 0 && c4.imm == 5 && c4.len == 3,
          "80 add: kind=%d size=%d imm=%llu len=%d", c4.kind, c4.size, c4.imm, c4.len);
    CHECK(c4.plan == JIT64_FAST, "80 add plan fallback");
    // 84 C0             test al, al
    Jit64Op c5 = dec(0x400000, {0x84, 0xC0});
    CHECK(c5.kind == J64_TEST_RM_R && c5.size == 1 && c5.len == 2, "84: kind=%d", c5.kind);
    CHECK(c5.plan == JIT64_FAST, "84 plan fallback");
    // A8 0F             test al, 0x0F
    Jit64Op c6 = dec(0x400000, {0xA8, 0x0F});
    CHECK(c6.kind == J64_ALU_ACC_IMM && c6.sub == 8 && c6.size == 1 && c6.imm == 0x0F && c6.len == 2,
          "A8: kind=%d sub=%d imm=%llu", c6.kind, c6.sub, c6.imm);
    CHECK(c6.plan == JIT64_FAST, "A8 plan fallback");
    // C0 E0 04          shl al, 4
    Jit64Op c7 = dec(0x400000, {0xC0, 0xE0, 0x04});
    CHECK(c7.kind == J64_SHIFT_IMM && c7.size == 1 && c7.sub == 4 && c7.imm == 4 && c7.len == 3,
          "C0: kind=%d sub=%d imm=%llu", c7.kind, c7.sub, c7.imm);
    CHECK(c7.plan == JIT64_FAST, "C0 plan fallback");
    // F6 E8             imul al (one-operand, /5)
    Jit64Op c8 = dec(0x400000, {0xF6, 0xE8});
    CHECK(c8.kind == J64_IMUL_1OP && c8.size == 1 && c8.len == 2, "F6: kind=%d", c8.kind);
    CHECK(c8.plan == JIT64_FAST, "F6 plan fallback");
    // F6 E0             mul al (/4: J64_GRP3, unsigned one-operand MUL)
    Jit64Op c9 = dec(0x400000, {0xF6, 0xE0});
    CHECK(c9.kind == J64_GRP3 && c9.sub == 4 && c9.size == 1 && c9.len == 2,
          "F6 /4: kind=%d sub=%d", c9.kind, c9.sub);
    CHECK(c9.plan == JIT64_FAST, "F6 /4 plan fallback");
    // 98                cwde (AX -> EAX, default 32-bit)
    Jit64Op cbw1 = dec(0x400000, {0x98});
    CHECK(cbw1.kind == J64_CBW && cbw1.size == 4 && cbw1.len == 1,
          "98: kind=%d size=%d len=%d", cbw1.kind, cbw1.size, cbw1.len);
    CHECK(cbw1.plan == JIT64_FAST, "98 plan fallback");
    // 66 98             cbw (AL -> AX, 16-bit)
    Jit64Op cbw2 = dec(0x400000, {0x66, 0x98});
    CHECK(cbw2.kind == J64_CBW && cbw2.size == 2 && cbw2.len == 2,
          "66 98: kind=%d size=%d len=%d", cbw2.kind, cbw2.size, cbw2.len);
    CHECK(cbw2.plan == JIT64_FAST, "66 98 plan fallback");
    // 48 98             cdqe (EAX -> RAX, 64-bit)
    Jit64Op cbw3 = dec(0x400000, {0x48, 0x98});
    CHECK(cbw3.kind == J64_CBW && cbw3.size == 8 && cbw3.len == 2,
          "48 98: kind=%d size=%d len=%d", cbw3.kind, cbw3.size, cbw3.len);
    CHECK(cbw3.plan == JIT64_FAST, "48 98 plan fallback");
    // 99                cdq (EAX -> EDX, default 32-bit)
    Jit64Op cqo1 = dec(0x400000, {0x99});
    CHECK(cqo1.kind == J64_CQO, "99 kind");
    CHECK(cqo1.size == 4, "99 size");
    CHECK(cqo1.len == 1, "99 len");
    CHECK(cqo1.plan == JIT64_FAST, "99 plan fallback");
    // 66 99             cwd (AX -> DX, 16-bit)
    Jit64Op cqo2 = dec(0x400000, {0x66, 0x99});
    CHECK(cqo2.kind == J64_CQO, "66 99 kind");
    CHECK(cqo2.size == 2, "66 99 size");
    CHECK(cqo2.len == 2, "66 99 len");
    CHECK(cqo2.plan == JIT64_FAST, "66 99 plan fallback");
    // 48 99             cqo (RAX -> RDX, 64-bit)
    Jit64Op cqo3 = dec(0x400000, {0x48, 0x99});
    CHECK(cqo3.kind == J64_CQO, "48 99 kind");
    CHECK(cqo3.size == 8, "48 99 size");
    CHECK(cqo3.len == 2, "48 99 len");
    CHECK(cqo3.plan == JIT64_FAST, "48 99 plan fallback");
    // 0F 44 C1          cmove eax, ecx (reg-direct, 32-bit)
    Jit64Op cm1 = dec(0x400000, {0x0F, 0x44, 0xC1});
    CHECK(cm1.kind == J64_CMOV && cm1.sub == 4 && cm1.size == 4 && cm1.len == 3,
          "0F44: kind=%d sub=%d size=%d len=%d", cm1.kind, cm1.sub, cm1.size, cm1.len);
    CHECK(cm1.regField == 0 && cm1.rmIndex == 1 && !cm1.isMem, "0F44 operands");
    CHECK(cm1.plan == JIT64_FAST, "0F44 plan fallback");
    // 48 0F 4F C2       cmovg rax, rdx (64-bit)
    Jit64Op cm2 = dec(0x400000, {0x48, 0x0F, 0x4F, 0xC2});
    CHECK(cm2.kind == J64_CMOV && cm2.sub == 15 && cm2.size == 8 && cm2.len == 4,
          "0F4F: kind=%d sub=%d size=%d", cm2.kind, cm2.sub, cm2.size);
    CHECK(cm2.plan == JIT64_FAST, "0F4F plan fallback");
    // 66 0F 42 04 25 00 10 00 00  cmovb ax, [0x1000] (16-bit mem)
    Jit64Op cm3 = dec(0x400000, {0x66, 0x0F, 0x42, 0x04, 0x25, 0x00, 0x10, 0x00, 0x00});
    CHECK(cm3.kind == J64_CMOV && cm3.sub == 2 && cm3.size == 2 && cm3.isMem,
          "0F42 mem: kind=%d sub=%d size=%d isMem=%d", cm3.kind, cm3.sub, cm3.size, cm3.isMem);
    CHECK(cm3.plan == JIT64_FAST, "0F42 mem plan fallback");
    // 0F 94 C1          sete cl (reg-direct, 8-bit)
    Jit64Op sc1 = dec(0x400000, {0x0F, 0x94, 0xC1});
    CHECK(sc1.kind == J64_SETCC && sc1.sub == 4 && sc1.size == 1 && sc1.len == 3,
          "0F94: kind=%d sub=%d size=%d len=%d", sc1.kind, sc1.sub, sc1.size, sc1.len);
    CHECK(sc1.rmIndex == 1 && !sc1.isMem, "0F94 operands");
    CHECK(sc1.plan == JIT64_FAST, "0F94 plan fallback");
    // 0F 95 04 25 00 10 00 00  setne byte [0x1000] (mem)
    Jit64Op sc2 = dec(0x400000, {0x0F, 0x95, 0x04, 0x25, 0x00, 0x10, 0x00, 0x00});
    CHECK(sc2.kind == J64_SETCC && sc2.sub == 5 && sc2.size == 1 && sc2.isMem,
          "0F95 mem: kind=%d sub=%d size=%d isMem=%d", sc2.kind, sc2.sub, sc2.size, sc2.isMem);
    CHECK(sc2.plan == JIT64_FAST, "0F95 mem plan fallback");
    // 0F 9C C4          setl ah (high-byte reg: declined to interpreter)
    Jit64Op sc3 = dec(0x400000, {0x0F, 0x9C, 0xC4});
    CHECK(sc3.kind == J64_SETCC && sc3.sub == 12, "0F9C kind/sub");
    CHECK(sc3.plan != JIT64_FAST, "0F9C AH should be interpreter-only");
    // 0F A3 C1          bt ecx, eax (reg-direct, 32-bit)
    Jit64Op bt1 = dec(0x400000, {0x0F, 0xA3, 0xC1});
    CHECK(bt1.kind == J64_BT && bt1.sub == 0 && bt1.size == 4 && bt1.len == 3,
          "0FA3: kind=%d sub=%d size=%d len=%d", bt1.kind, bt1.sub, bt1.size, bt1.len);
    CHECK(bt1.regField == 0 && bt1.rmIndex == 1 && !bt1.isMem, "0FA3 operands");
    CHECK(bt1.plan == JIT64_FAST, "0FA3 plan fallback");
    // 0F AB 04 25 00 10 00 00  bts dword [0x1000], eax (mem, 32-bit)
    Jit64Op bt2 = dec(0x400000, {0x0F, 0xAB, 0x04, 0x25, 0x00, 0x10, 0x00, 0x00});
    CHECK(bt2.kind == J64_BT && bt2.sub == 1 && bt2.size == 4 && bt2.isMem,
          "0FAB mem: kind=%d sub=%d size=%d isMem=%d", bt2.kind, bt2.sub, bt2.size, bt2.isMem);
    CHECK(bt2.plan == JIT64_FAST, "0FAB mem plan fallback");
    // 0F B3 C1          btr ecx, eax (sub=2)
    Jit64Op bt3 = dec(0x400000, {0x0F, 0xB3, 0xC1});
    CHECK(bt3.kind == J64_BT && bt3.sub == 2, "0FB3 kind/sub");
    CHECK(bt3.plan == JIT64_FAST, "0FB3 plan fallback");
    // 66 0F BB C1       btc cx, ax (16-bit via 0x66)
    Jit64Op bt4 = dec(0x400000, {0x66, 0x0F, 0xBB, 0xC1});
    CHECK(bt4.kind == J64_BT && bt4.sub == 3 && bt4.size == 2, "0FBB 16-bit");
    CHECK(bt4.plan == JIT64_FAST, "0FBB plan fallback");
    // 6B C0 05          imul eax, eax, 5
    Jit64Op c10 = dec(0x400000, {0x6B, 0xC0, 0x05});
    CHECK(c10.kind == J64_IMUL_3OP && c10.size == 4 && c10.imm == 5 && c10.len == 3,
          "6B: kind=%d imm=%llu len=%d", c10.kind, c10.imm, c10.len);
    CHECK(c10.plan == JIT64_FAST, "6B plan fallback");
    // 69 C0 34 12 00 00 imul eax, eax, 0x1234
    Jit64Op c11 = dec(0x400000, {0x69, 0xC0, 0x34, 0x12, 0x00, 0x00});
    CHECK(c11.kind == J64_IMUL_3OP && c11.size == 4 && c11.imm == 0x1234 && c11.len == 6,
          "69: kind=%d imm=%llu len=%d", c11.kind, c11.imm, c11.len);
    // 66 01 C0          add ax, ax (16-bit)
    Jit64Op c12 = dec(0x400000, {0x66, 0x01, 0xC0});
    CHECK(c12.kind == J64_ALU_RM_R && c12.size == 2 && c12.len == 3, "66 ADD: size=%d len=%d", c12.size, c12.len);
    CHECK(c12.plan == JIT64_FAST, "66 ADD plan fallback");
    // Truncated stream must not decode.
    Jit64Op d;
    const U8 trunc[] = {0x48, 0x89};
    CHECK(!jit64DecodeOne(0x400000, trunc, 2, d), "truncated MOV must fail");
    const U8 trunc2[] = {0xE8, 0x01, 0x02};
    CHECK(!jit64DecodeOne(0x400000, trunc2, 3, d), "truncated CALL must fail");
    // REX.B-extended push r8.
    Jit64Op e = dec(0x400000, {0x41, 0x50});
    CHECK(e.kind == J64_PUSH && e.regField == 8, "REX.B push rf=%d", e.regField);
}

static void testCompileStream() {
    // The loop-fixture from tools/buildLoopElf64.py:
    //   31 C0 | B9 0A000000 | 48 01 C8 | 48 FF C9 | 75 FB | 48 89 C7 | B8 3C000000 | 0F 05
    const U8 prog[] = {
        0x31, 0xC0,
        0xB9, 0x0A, 0x00, 0x00, 0x00,
        0x48, 0x01, 0xC8,
        0x48, 0xFF, 0xC9, // DEC rcx = FF /1 — interpreter handles, JIT: unknown->stop
    };
    Jit64Op ops[16];
    U32 n = jit64CompileStream(0x400000, prog, sizeof(prog), ops, 16, true);
    // 31C0 (xor, fast) + B9 (mov-imm, fast) + 4801C8 (add, fast), then stop at FF (unknown).
    CHECK(n == 3, "compileStream n=%u want 3", n);
    CHECK(ops[0].kind == J64_ALU_RM_R && ops[0].plan == JIT64_FAST, "op0 kind=%d", ops[0].kind);
    CHECK(ops[1].kind == J64_MOV_R_IMM, "op1 kind=%d", ops[1].kind);
    CHECK(ops[2].kind == J64_ALU_RM_R && ops[2].size == 8, "op2 kind=%d size=%d", ops[2].kind, ops[2].size);
    // Control-flow termination: stream ending in JNZ stops after it.
    const U8 prog2[] = {0x48, 0x31, 0xC0, 0x74, 0x07, 0x48, 0x89, 0xC7};
    n = jit64CompileStream(0x400000, prog2, sizeof(prog2), ops, 16, true);
    CHECK(n == 2 && ops[1].kind == J64_JCC, "cf-stop n=%u kind=%d", n, n >= 2 ? ops[1].kind : -1);
    // syscall terminates too.
    const U8 prog3[] = {0xB8, 0x3C, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x90};
    n = jit64CompileStream(0x400000, prog3, sizeof(prog3), ops, 16, true);
    CHECK(n == 2 && ops[1].kind == J64_SYSCALL, "syscall-stop n=%u", n);
}

static void testBlockCache() {
    Jit64BlockCache cache;
    Jit64Op ops[2];
    U32 n = jit64CompileStream(0x400000, (const U8*)"\x48\x89\xC8\x48\x01\xC8", 6, ops, 2, false);
    CHECK(n == 2, "cache-setup n=%u", n);
    // Miss before insert.
    CHECK(cache.lookup(0x400000, 1, 7, 1, 7) == nullptr, "lookup hit before insert");
    const Jit64Block* b = cache.insert(0x400000, 1, 7, 1, 7, ops, n);
    CHECK(b && b->executable && b->fastCount == 2, "insert executable=%d fast=%u", b ? b->executable : -1, b ? b->fastCount : 0);
    // Hit with matching gens.
    const Jit64Block* h = cache.lookup(0x400000, 1, 7, 1, 7);
    CHECK(h == b, "lookup miss after insert");
    // Stale gen -> miss (self-modifying code invalidation path).
    CHECK(cache.lookup(0x400000, 1, 8, 1, 7) == nullptr, "stale gen0 should miss");
    CHECK(cache.lookup(0x400000, 1, 7, 1, 9) == nullptr, "stale gen1 should miss");
    // Wrong rip -> miss.
    CHECK(cache.lookup(0x400008, 1, 7, 1, 7) == nullptr, "wrong rip should miss");
    // Block containing a fallback op is NOT executable (whole-block fallback rule).
    Jit64Op mixed[2];
    CHECK(jit64DecodeOne(0x500000, (const U8*)"\x48\x89\xC8", 3, mixed[0]), "mixed0");
    CHECK(jit64DecodeOne(0x500003, (const U8*)"\x0F\x05", 2, mixed[1]), "mixed1");
    jit64Classify(mixed[0], false, false);
    jit64Classify(mixed[1], false, false);
    const Jit64Block* nb = cache.insert(0x500000, 2, 1, 2, 1, mixed, 2);
    CHECK(nb && !nb->executable, "mixed block must not be executable");
    CHECK(cache.lookup(0x500000, 2, 1, 2, 1) == nullptr, "non-executable must not hit");
    // invalidate + stats.
    cache.invalidate(0x400000);
    CHECK(cache.lookup(0x400000, 1, 7, 1, 7) == nullptr, "invalidated entry should miss");
    CHECK(cache.lookups() >= 6 && cache.hits() == 1 && cache.inserts() == 2,
          "stats lookups=%llu hits=%llu inserts=%llu", cache.lookups(), cache.hits(), cache.inserts());
}

static void testPlanText() {
    Jit64BlockCache cache;
    Jit64Op ops[2];
    U32 n = jit64CompileStream(0x400000, (const U8*)"\x48\x89\xC8\x74\x02", 5, ops, 2, true);
    CHECK(n == 2, "plantext-setup n=%u", n);
    const Jit64Block* b = cache.insert(0x400000, 0, 0, 0, 0, ops, n);
    char buf[512];
    U32 used = jit64EmitPlanText(*b, buf, sizeof(buf));
    CHECK(used > 0, "plan text empty");
    CHECK(std::strstr(buf, "fast") != nullptr, "plan text missing 'fast': %s", buf);
    CHECK(std::strstr(buf, "kind=0") != nullptr, "plan text missing kind=0: %s", buf);
    // Too-small buffer reports 0 (no truncation).
    CHECK(jit64EmitPlanText(*b, buf, 8) == 0, "small buffer should report 0");
}

static void testJitFlag() {
    // Default (env unset in test runner): disabled. Explicit check only that
    // the accessor is stable (cached) — behavior change requires env.
    bool a = jit64Enabled();
    bool b = jit64Enabled();
    CHECK(a == b, "jit64Enabled not stable");
}

static void testWasmStateBridge() {
    // The staging ABI is the exact copy the runtime will hand to execute().
    U64 gpr[16];
    for (U32 i = 0; i < 16; i++) gpr[i] = 0xFEDCBA9876543210ULL + (U64)i * 0x0101010101010101ULL;
    Jit64WasmState st{};
    jit64WasmStateInit(st, gpr, 0x123456789ABCDEF0ULL, 0x8D5);
    // Emitted-code outputs start at 0 and a non-shared host memory is assumed.
    CHECK(st.blocksEntered == 0, "marshal blocksEntered=%u", st.blocksEntered);
    CHECK(st.execCount == 0, "marshal execCount=%u", st.execCount);
    CHECK(st.limit == JIT64_WASM_UNBOUNDED, "marshal limit=%u", st.limit);
    jit64WasmStateSetGens(st, 0x1234, 5, 6);
    CHECK(st.genBase == 0x1234 && st.gen0 == 5 && st.gen1 == 6, "marshal gens");
    for (U32 i = 0; i < 16; i++)
        CHECK(st.gpr[i] == gpr[i], "marshal gpr[%u]", i);
    CHECK(st.rip == 0x123456789ABCDEF0ULL, "marshal rip");
    CHECK(st.rflags == 0x8D5, "marshal rflags");
    // Register order follows the x86-64 encoding (RAX=0 .. R15=15).
    U64 order[16] = {};
    order[0] = 0xAAAAAAAAAAAAAAAAULL;   // RAX
    order[15] = 0xBBBBBBBBBBBBBBBBULL;  // R15
    jit64WasmStateInit(st, order, 0, 0);
    CHECK(st.gpr[0] == 0xAAAAAAAAAAAAAAAAULL && st.gpr[15] == 0xBBBBBBBBBBBBBBBBULL,
          "marshal register order");
    // Mutate through the struct (as the wasm module would) and read back.
    jit64WasmStateInit(st, gpr, 0x123456789ABCDEF0ULL, 0x202);
    for (U32 i = 0; i < 16; i++) st.gpr[i] ^= 0xFFFFFFFFFFFFFFFFULL;
    st.rip += 7;
    st.rflags = 0xA5A5A5A5;
    U64 out[16]; U64 rip = 0; U32 flags = 0;
    jit64WasmStateRead(st, out, rip, flags);
    for (U32 i = 0; i < 16; i++)
        CHECK(out[i] == (gpr[i] ^ 0xFFFFFFFFFFFFFFFFULL), "unmarshal gpr[%u]", i);
    CHECK(rip == 0x123456789ABCDEF7ULL, "unmarshal rip=0x%llx", rip);
    CHECK(flags == 0xA5A5A5A5, "unmarshal flags=0x%x", flags);
    // XMM staging area: appended at the struct end so every pre-existing
    // offset is unchanged; the Init/Read pair round-trips all 32 qwords.
    CHECK(offsetof(Jit64WasmState, xmm) == 176, "xmm offset");
    CHECK(sizeof(Jit64WasmState) == 432, "state size=%u", (unsigned)sizeof(Jit64WasmState));
    U64 xmmIn[32], xmmOut[32];
    for (U32 i = 0; i < 32; i++) xmmIn[i] = 0x0123456789ABCDEFULL ^ (U64)i * 0x1111111111111111ULL;
    jit64WasmStateInitXmm(st, xmmIn);
    for (U32 i = 0; i < 32; i++) CHECK(st.xmm[i] == xmmIn[i], "xmm marshal [%u]", i);
    st.xmm[5] ^= 0xFFFFFFFFFFFFFFFFULL; // as the module would publish
    jit64WasmStateReadXmm(st, xmmOut);
    for (U32 i = 0; i < 32; i++)
        CHECK(xmmOut[i] == (i == 5 ? (xmmIn[i] ^ 0xFFFFFFFFFFFFFFFFULL) : xmmIn[i]),
              "xmm unmarshal [%u]", i);
}

static void testWasmGlue() {
    // Native stubs: the table-index glue is emscripten-only. Natively every
    // entry point must be a safe no-op so cpu64.cpp links (and runs) without
    // the runtime TU — including the section-GC lifetime test.
    CHECK(!jit64WasmAvailable(), "native jit64WasmAvailable should be false");
    const U8 fake[] = {0x00, 0x61, 0x73, 0x6D};
    Jit64EmitOptions opts;
    CHECK(jit64WasmInstantiate(fake, sizeof(fake), opts) == -1, "native instantiate");
    CHECK(jit64WasmInstantiate(nullptr, 0, opts) == -1, "native instantiate null");
    CHECK(!jit64WasmHostAbiOk(), "native host ABI must report not-ok");
    CHECK(std::strcmp(jit64WasmEngineMode(), "unavailable") == 0, "native engine mode");
    jit64WasmCall(0, nullptr);      // must not crash
    jit64WasmCall(-1, nullptr);
    jit64WasmRelease(0);      // must not crash
    jit64WasmRelease(-1);
    // Cache owns the wasm slot lifecycle: a fresh block starts unattempted,
    // and eviction/invalidation reset the slot (release is a no-op here,
    // but the bookkeeping must hold).
    Jit64BlockCache cache;
    Jit64Op ops[1];
    U32 n = jit64CompileStream(0x400000, (const U8*)"\x48\x89\xC8", 3, ops, 1, false);
    CHECK(n == 1, "glue-setup n=%u", n);
    Jit64Block* b = cache.insert(0x400000, 1, 7, 1, 7, ops, n);
    CHECK(b && b->wasmIndex == -1, "fresh block wasmIndex=%d", b ? b->wasmIndex : -99);
    b->wasmIndex = -2; // simulate an emission rejection
    Jit64Block* b2 = cache.insert(0x400000, 1, 7, 1, 7, ops, n); // same slot: overwrite
    CHECK(b2 == b && b2->wasmIndex == -1, "overwrite must reset wasmIndex, got %d", b2->wasmIndex);
    b2->wasmIndex = 3; // simulate a live table index
    cache.invalidate(0x400000);
    Jit64Block* b3 = cache.insert(0x400000, 1, 7, 1, 7, ops, n);
    CHECK(b3->wasmIndex == -1, "invalidate must reset wasmIndex, got %d", b3->wasmIndex);
    b3->wasmIndex = 5;
    cache.clear();
    Jit64Block* b4 = cache.insert(0x400000, 1, 7, 1, 7, ops, n);
    CHECK(b4->wasmIndex == -1, "clear must reset wasmIndex, got %d", b4->wasmIndex);
}

// Compile a byte stream into a block and run the wasm emitter on it.
static bool emitOk(std::vector<U8> b) {
    Jit64Op ops[24];
    U32 n = jit64CompileStream(0x400000, b.data(), (U32)b.size(), ops, 24, true);
    if (!n) return false;
    U32 consumed = 0;
    for (U32 i = 0; i < n; i++) consumed += ops[i].len;
    if (consumed != b.size()) return false;
    Jit64Block block;
    block.ops.assign(ops, ops + n);
    std::vector<U8> module;
    return jit64EmitWasm(block, module) && !module.empty();
}

static void testShiftEmission() {
    // SHL/SHR/SAR (sub 4/5/7) 32/64-bit register-direct: accepted.
    CHECK(emitOk({0x48, 0xC1, 0xE0, 0x04}), "shl rax,4 rejected");
    CHECK(emitOk({0xC1, 0xE0, 0x04}), "shl eax,4 rejected");
    CHECK(emitOk({0x48, 0xC1, 0xE8, 0x01}), "shr rax,1 rejected");
    CHECK(emitOk({0x48, 0xC1, 0xF8, 0x01}), "sar rax,1 rejected");
    CHECK(emitOk({0xC1, 0xE8, 0x1F}), "shr eax,31 rejected");
    // /6 aliases SHL: accepted.
    CHECK(emitOk({0x48, 0xC1, 0xF0, 0x04}), "shl(/6) rax,4 rejected");
    // Count 0 is a no-op but still accepted (emits prologue/epilogue only).
    CHECK(emitOk({0x48, 0xC1, 0xE0, 0x00}), "shl rax,0 rejected");
    // Rotates (ROL/ROR/RCL/RCR, sub 0..3): accepted (see testRotateEmission).
    // Memory shift: accepted (D1 round added mem emitters; C1-mem uses them too).
    CHECK(emitOk({0x48, 0xC1, 0x20, 0x04}), "shl [rax],4 rejected");
    // Shift mixed with ALU in one block: accepted.
    CHECK(emitOk({0x48, 0xC1, 0xE0, 0x04, 0x48, 0x01, 0xC8}), "shl+add block rejected");
    // Memory shift no longer poisons the block (mem emitters added).
    CHECK(emitOk({0x48, 0xC1, 0xE0, 0x04, 0x48, 0xC1, 0x20, 0x04}), "shl+ror-mem block rejected");
}

static void testRotateEmission() {
    // ROL/ROR/RCL/RCR (sub 0..3) 32/64-bit register-direct: accepted.
    CHECK(emitOk({0x48, 0xC1, 0xC0, 0x04}), "rol rax,4 rejected");
    CHECK(emitOk({0xC1, 0xC0, 0x04}), "rol eax,4 rejected");
    CHECK(emitOk({0x48, 0xC1, 0xC8, 0x01}), "ror rax,1 rejected");
    CHECK(emitOk({0x48, 0xC1, 0xD0, 0x01}), "rcl rax,1 rejected");
    CHECK(emitOk({0x48, 0xC1, 0xD8, 0x3F}), "rcr rax,63 rejected");
    // Count 0 accepted (no-op with 32-bit zero-extend semantics).
    CHECK(emitOk({0x48, 0xC1, 0xC0, 0x00}), "rol rax,0 rejected");
    // Memory rotate: accepted (D1 round added mem emitters).
    CHECK(emitOk({0x48, 0xC1, 0x00, 0x04}), "rol [rax],4 rejected");
    // Rotate mixed with ALU in one block: accepted.
    CHECK(emitOk({0x48, 0xC1, 0xC0, 0x04, 0x48, 0x01, 0xC8}), "rol+add block rejected");
}

static void testImulEmission() {
    // Two-operand IMUL (0F AF) 32/64-bit register-direct: accepted.
    CHECK(emitOk({0x48, 0x0F, 0xAF, 0xD5}), "imul rdx,rbp rejected");
    CHECK(emitOk({0x0F, 0xAF, 0xD5}), "imul edx,ebp rejected");
    CHECK(emitOk({0x4D, 0x0F, 0xAF, 0xC8}), "imul r9,r8 rejected");
    // Memory IMUL: rejected.
    CHECK(!emitOk({0x48, 0x0F, 0xAF, 0x15}), "imul rdx,[rip] accepted");
    // IMUL mixed with ALU/rotate in one block: accepted.
    CHECK(emitOk({0x48, 0x0F, 0xAF, 0xD5, 0x48, 0xC1, 0xC0, 0x01}), "imul+rol block rejected");
}

static bool decodeOne(const std::vector<U8>& b, Jit64Op& op) {
    return jit64DecodeOne(0x400000, b.data(), (U32)b.size(), op);
}

static void testShiftCLEmission() {
    // D3/D2 shift/rotate by CL: decode + classifier + wasm acceptance.
    Jit64Op op;
    // D3 SHL r/m64,CL decodes: kind, sub, size, len (no immediate).
    CHECK(decodeOne({0x48, 0xD3, 0xE0}, op) &&
          op.kind == J64_SHIFT_CL && op.sub == 4 && op.size == 8 &&
          op.len == 3 && !op.isMem, "D3 shl rax,cl decode");
    // D3 ROR r/m32,CL: 32-bit default size.
    CHECK(decodeOne({0xD3, 0xC9}, op) &&
          op.kind == J64_SHIFT_CL && op.sub == 1 && op.size == 4 &&
          op.len == 2, "D3 ror ecx,cl decode");
    // D2 SHL r/m8,CL: 8-bit form.
    CHECK(decodeOne({0xD2, 0xE0}, op) &&
          op.kind == J64_SHIFT_CL && op.sub == 4 && op.size == 1 &&
          op.len == 2, "D2 shl al,cl decode");
    // 0x66 D3: 16-bit form.
    CHECK(decodeOne({0x66, 0xD3, 0xF8}, op) &&
          op.kind == J64_SHIFT_CL && op.sub == 7 && op.size == 2,
          "D3 sar ax,cl decode");
    // D3 mem form decodes with EA.
    CHECK(decodeOne({0x48, 0xD3, 0x20}, op) &&
          op.kind == J64_SHIFT_CL && op.isMem && op.len == 3,
          "D3 shl [rax],cl decode");
    // Wasm acceptance: all sub-ops x sizes, reg-direct.
    CHECK(emitOk({0x48, 0xD3, 0xE0}), "shl rax,cl rejected");
    CHECK(emitOk({0xD3, 0xE8}), "shr eax,cl rejected");
    CHECK(!emitOk({0x66, 0xD3, 0xF8}), "sar ax,cl accepted");
    CHECK(emitOk({0xD2, 0xE0}), "shl al,cl rejected");
    CHECK(emitOk({0x48, 0xD3, 0xC0}), "rol rax,cl rejected");
    CHECK(emitOk({0x48, 0xD3, 0xC8}), "ror rax,cl rejected");
    CHECK(emitOk({0x48, 0xD3, 0xD0}), "rcl rax,cl rejected");
    CHECK(emitOk({0x48, 0xD3, 0xD8}), "rcr rax,cl rejected");
    // /6 aliases SHL: accepted.
    CHECK(emitOk({0x48, 0xD3, 0xF0}), "shl(/6) rax,cl rejected");
    // Memory forms: declined (interpreter-only; mem flag-merge bug).
    CHECK(!emitOk({0x48, 0xD3, 0x20}), "shl [rax],cl accepted");
    CHECK(!emitOk({0x48, 0xD3, 0x00}), "rol [rax],cl accepted");
    // High-byte (AH/BH/CH/DH, D2 rm 4..7, no REX): declined to interpreter.
    CHECK(!emitOk({0xD2, 0xE4}), "shl ah,cl accepted");
    // ... but with REX it's fine (SPL, not AH).
    CHECK(emitOk({0x40, 0xD2, 0xE4}), "shl spl,cl rejected");
    // Shift-by-CL mixed with ALU in one block: accepted.
    CHECK(emitOk({0x48, 0xD3, 0xE0, 0x48, 0x01, 0xC8}), "shl-cl+add block rejected");
}


static void testGrp3Emission() {
    Jit64Op op;
    // TEST r/m,imm decodes: kind, sub, size, len, finalized immediate.
    CHECK(decodeOne({0xF7, 0xC0, 0x78, 0x56, 0x34, 0x12}, op) &&
          op.kind == J64_GRP3 && op.sub == 0 && op.size == 4 && op.len == 6 &&
          op.imm == 0x12345678, "f7 test eax,imm32 decode");
    CHECK(decodeOne({0xF6, 0xC0, 0x7F}, op) &&
          op.kind == J64_GRP3 && op.sub == 0 && op.size == 1 && op.len == 3 &&
          op.imm == 0x7F, "f6 test al,imm8 decode");
    CHECK(decodeOne({0x66, 0xF7, 0xC0, 0x34, 0x12}, op) &&
          op.kind == J64_GRP3 && op.sub == 0 && op.size == 2 && op.len == 5 &&
          op.imm == 0x1234, "66 f7 test ax,imm16 decode");
    // 64-bit TEST: imm32 sign-extended at decode (mirrors dsp_31).
    CHECK(decodeOne({0x48, 0xF7, 0xC0, 0x80, 0xFF, 0xFF, 0xFF}, op) &&
          op.kind == J64_GRP3 && op.sub == 0 && op.size == 8 && op.len == 7 &&
          op.imm == 0xFFFFFFFFFFFFFF80ULL, "rex.w test rax,imm32 decode");
    // F6 ignores 0x66 for the size (interpreter hardcodes 1).
    CHECK(decodeOne({0x66, 0xF6, 0xC0, 0x01}, op) &&
          op.kind == J64_GRP3 && op.sub == 0 && op.size == 1 && op.len == 4,
          "66 f6 size=%d", op.size);
    // NOT / NEG / MUL.
    CHECK(decodeOne({0xF7, 0xD0}, op) &&
          op.kind == J64_GRP3 && op.sub == 2 && op.size == 4 && op.len == 2 &&
          !op.isMem, "f7 not eax decode");
    CHECK(decodeOne({0xF7, 0x18}, op) &&
          op.kind == J64_GRP3 && op.sub == 3 && op.size == 4 && op.len == 2 &&
          op.isMem && op.ea.baseReg == 0, "f7 neg [rax] decode");
    CHECK(decodeOne({0x48, 0xF7, 0xE0}, op) &&
          op.kind == J64_GRP3 && op.sub == 4 && op.size == 8 && op.len == 3,
          "rex.w mul rax decode");
    CHECK(decodeOne({0xF6, 0xE0}, op) &&
          op.kind == J64_GRP3 && op.sub == 4 && op.size == 1 && op.len == 2,
          "f6 mul al decode");
    // /5 keeps the proven IMUL path.
    CHECK(decodeOne({0xF7, 0xE8}, op) && op.kind == J64_IMUL_1OP, "f7 /5 kind=%d", op.kind);
    // /1 invalid; /6 /7 DIV/IDIV decode as J64_GRP3 (guard-bail fast path).
    CHECK(!decodeOne({0xF7, 0xC8}, op), "f7 /1 decoded");
    CHECK(decodeOne({0xF7, 0xF0}, op) &&
          op.kind == J64_GRP3 && op.sub == 6 && op.size == 4 && op.len == 2 &&
          !op.isMem, "f7 div eax decode");
    CHECK(decodeOne({0xF7, 0xF8}, op) &&
          op.kind == J64_GRP3 && op.sub == 7 && op.size == 4 && op.len == 2,
          "f7 idiv eax decode");
    CHECK(decodeOne({0xF6, 0xF0}, op) &&
          op.kind == J64_GRP3 && op.sub == 6 && op.size == 1 && op.len == 2,
          "f6 div al decode");
    CHECK(decodeOne({0xF6, 0xFE}, op) &&
          op.kind == J64_GRP3 && op.sub == 7 && op.size == 1 && op.len == 2,
          "f6 idiv al decode");
    CHECK(decodeOne({0x48, 0xF7, 0xF0}, op) &&
          op.kind == J64_GRP3 && op.sub == 6 && op.size == 8 && op.len == 3,
          "rex.w div rax decode");
    CHECK(decodeOne({0x66, 0xF7, 0xF8}, op) &&
          op.kind == J64_GRP3 && op.sub == 7 && op.size == 2 && op.len == 3,
          "66 f7 idiv ax decode");
    CHECK(decodeOne({0xF7, 0x30}, op) &&
          op.kind == J64_GRP3 && op.sub == 6 && op.size == 4 && op.len == 2 &&
          op.isMem, "f7 div [rax] decode");
    // TEST RIP-relative: the EA target includes the immediate length
    // (the interpreter adds immLen to effAddr for RIP-rel TEST).
    CHECK(decodeOne({0xF7, 0x05, 0x10, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12}, op) &&
          op.kind == J64_GRP3 && op.isMem && op.ea.ripRel &&
          op.ea.ripRelTarget == 0x400000 + 10 + 0x10 && op.len == 10,
          "f7 test [rip] ea");
    // Plans: all four sub-ops fast; LOCK falls back.
    Jit64Op t = dec(0x400000, {0xF7, 0xC0, 0x01, 0x00, 0x00, 0x00});
    CHECK(t.plan == JIT64_FAST, "test plan fallback");
    Jit64Op nn = dec(0x400000, {0xF7, 0xD3});
    CHECK(nn.plan == JIT64_FAST, "not plan fallback");
    Jit64Op ng = dec(0x400000, {0x48, 0xF7, 0xDB});
    CHECK(ng.plan == JIT64_FAST, "neg plan fallback");
    Jit64Op mu = dec(0x400000, {0x48, 0xF7, 0xE3});
    CHECK(mu.plan == JIT64_FAST, "mul plan fallback");
    Jit64Op dv = dec(0x400000, {0xF7, 0xF3});
    CHECK(dv.plan == JIT64_FAST, "div plan fallback");
    Jit64Op id = dec(0x400000, {0x48, 0xF7, 0xFB});
    CHECK(id.plan == JIT64_FAST, "idiv plan fallback");
    Jit64Op d8 = dec(0x400000, {0xF6, 0xF6});
    CHECK(d8.plan == JIT64_FAST, "div8 plan fallback");
    // Emission accept/reject.
    CHECK(emitOk({0xF7, 0xC0, 0x78, 0x56, 0x34, 0x12}), "test eax,imm32 rejected");
    CHECK(emitOk({0xF6, 0xC0, 0x7F}), "test al,imm8 rejected");
    CHECK(emitOk({0x48, 0xF7, 0xC0, 0x80, 0xFF, 0xFF, 0xFF}), "test rax,imm32 rejected");
    CHECK(emitOk({0xF7, 0xD0}), "not eax rejected");
    CHECK(emitOk({0xF7, 0x18}), "neg [rax] rejected");
    CHECK(emitOk({0x48, 0xF7, 0xE3}), "mul rbx rejected");
    CHECK(emitOk({0xF6, 0xE0}), "mul al rejected");
    CHECK(emitOk({0xF7, 0x05, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}), "test [rip] rejected");
    CHECK(emitOk({0xF7, 0xF3}), "div ebx rejected");
    CHECK(emitOk({0xF7, 0xFB}), "idiv ebx rejected");
    CHECK(emitOk({0x48, 0xF7, 0xF0}), "div rax rejected");
    CHECK(emitOk({0xF6, 0xF0}), "div al rejected");
    CHECK(emitOk({0x66, 0xF7, 0xF8}), "idiv ax rejected");
    CHECK(emitOk({0xF7, 0x30}), "div [rax] rejected");
    // DIV mixed with ALU in one block: accepted (guard bail is mid-block).
    CHECK(emitOk({0xF7, 0xF3, 0x48, 0x01, 0xC8}), "div+add block rejected");
    CHECK(!emitOk({0xF7, 0xC8}), "/1 accepted");
    CHECK(!emitOk({0xF0, 0xF7, 0xD0}), "lock not accepted");
    // GRP3 mixed with ALU in one block: accepted.
    CHECK(emitOk({0xF7, 0xD0, 0x48, 0x01, 0xC8}), "not+add block rejected");
}

static void testStringEmission() {
    Jit64Op op;
    // MOVS/STOS decode: kind, sub, size, len.
    CHECK(decodeOne({0xA4}, op) && op.kind == J64_STRING && op.sub == 0 && op.size == 1 && op.len == 1, "movsb decode");
    CHECK(decodeOne({0xA5}, op) && op.kind == J64_STRING && op.sub == 0 && op.size == 4 && op.len == 1, "movsd decode");
    CHECK(decodeOne({0x66, 0xA5}, op) && op.kind == J64_STRING && op.sub == 0 && op.size == 2 && op.len == 2, "movsw decode");
    CHECK(decodeOne({0xAA}, op) && op.kind == J64_STRING && op.sub == 1 && op.size == 1, "stosb decode");
    CHECK(decodeOne({0xAB}, op) && op.kind == J64_STRING && op.sub == 1 && op.size == 4, "stosd decode");
    CHECK(decodeOne({0xA6}, op) && op.kind == J64_STRING && op.sub == 2 && op.size == 1, "cmpsb decode");
    CHECK(decodeOne({0xA7}, op) && op.kind == J64_STRING && op.sub == 2 && op.size == 4, "cmpsd decode");
    CHECK(decodeOne({0xAE}, op) && op.kind == J64_STRING && op.sub == 3 && op.size == 1, "scasb decode");
    CHECK(decodeOne({0xAF}, op) && op.kind == J64_STRING && op.sub == 3 && op.size == 4, "scasd decode");
    // REP/REPE/REPNE and 0x67 decode into the op.
    CHECK(decodeOne({0xF3, 0xA4}, op) && op.kind == J64_STRING && op.rep == 0xF3 && op.len == 2, "rep movsb decode");
    CHECK(decodeOne({0xF3, 0xA6}, op) && op.kind == J64_STRING && op.rep == 0xF3, "repe cmpsb decode");
    CHECK(decodeOne({0xF2, 0xAE}, op) && op.kind == J64_STRING && op.rep == 0xF2, "repne scasb decode");
    CHECK(decodeOne({0x67, 0xA5}, op) && op.kind == J64_STRING && op.asize32 && op.len == 2, "67 movsd decode");
    CHECK(decodeOne({0xF3, 0x67, 0xAB}, op) && op.kind == J64_STRING && op.rep == 0xF3 && op.asize32 && op.len == 3, "rep67 stosd decode");
    // REX.W selects 8-byte size (opSize), matching the interpreter.
    CHECK(decodeOne({0x48, 0xA5}, op) && op.kind == J64_STRING && op.size == 8 && op.len == 2, "rex.w movsq decode");
    CHECK(decodeOne({0xF3, 0x48, 0xAB}, op) && op.kind == J64_STRING && op.sub == 1 && op.size == 8 && op.rep == 0xF3, "rep rex.w stosq decode");
    // LODS (AC/AD) is unimplemented by the interpreter: stays unknown.
    CHECK(!decodeOne({0xAC}, op) || op.kind != J64_STRING, "lodsb decoded as string");
    CHECK(!decodeOne({0xAD}, op) || op.kind != J64_STRING, "lodsd decoded as string");
    // A8/A9 are TEST, not string ops.
    CHECK(!decodeOne({0xA8, 0x01}, op) || op.kind != J64_STRING, "test al decoded as string");
    // LOCK+string is rejected (falls back to the interpreter).
    CHECK(!decodeOne({0xF0, 0xA4}, op) || op.kind != J64_STRING, "lock movsb decoded as string");
    // Emission accept/reject.
    CHECK(emitOk({0xA4}), "movsb rejected");
    CHECK(emitOk({0xF3, 0xA5}), "rep movsd rejected");
    CHECK(emitOk({0xF3, 0xAB}), "rep stosd rejected");
    CHECK(emitOk({0xF3, 0x48, 0xAB}), "rep stosq rejected");
    CHECK(emitOk({0xF3, 0xA6}), "repe cmpsb rejected");
    CHECK(emitOk({0xF2, 0xAF}), "repne scasd rejected");
    CHECK(emitOk({0x67, 0xA5}), "67 movsd rejected");
    CHECK(emitOk({0x66, 0xAB}), "movsw rejected");
    // String op mixed with ALU in one block: accepted.
    CHECK(emitOk({0xA4, 0x48, 0x01, 0xC8}), "movsb+add block rejected");
    CHECK(emitOk({0xF3, 0xA5, 0x48, 0x01, 0xC8}), "rep movsd+add block rejected");
    // LODS poisons the block (unknown op).
    CHECK(!emitOk({0xAC}), "lodsb block accepted");
    CHECK(!emitOk({0xA4, 0xAC}), "movsb+lodsb block accepted");
}

static void testX87NeverJitted() {
    // x87 (D8-DF) is interpreter-only by design (plan section 12: the 64-bit
    // guest mandates 80-bit SoftFloat; wasm f64 emission is provably not
    // bit-exact). It must decode to J64_FPU, classify fallback, truncate the
    // compile stream, and never be accepted by the wasm emitter.
    Jit64Op op;
    // Register forms (mod==11).
    CHECK(decodeOne({0xD9, 0xE0}, op) && op.kind == J64_FPU && op.len == 2, "fchs decode");
    CHECK(decodeOne({0xD9, 0xC0}, op) && op.kind == J64_FPU && op.len == 2, "fld st0 decode");
    CHECK(decodeOne({0xDD, 0xD8}, op) && op.kind == J64_FPU && op.len == 2, "fstp st0 decode");
    CHECK(decodeOne({0xDE, 0xC0}, op) && op.kind == J64_FPU && op.len == 2, "faddp decode");
    CHECK(decodeOne({0xD9, 0xE8}, op) && op.kind == J64_FPU && op.len == 2, "fld1 decode");
    CHECK(decodeOne({0xDA, 0xE9}, op) && op.kind == J64_FPU && op.len == 2, "fucompp decode");
    CHECK(decodeOne({0xDF, 0xE0}, op) && op.kind == J64_FPU && op.len == 2, "fnstsw ax decode");
    CHECK(decodeOne({0xDC, 0xC8}, op) && op.kind == J64_FPU && op.len == 2, "fmul st0 decode");
    // Memory forms: [rax], [disp32], [rax+rcx*2+disp32] (SIB).
    CHECK(decodeOne({0xD8, 0x00}, op) && op.kind == J64_FPU && op.len == 2 && op.isMem, "fadd m32 decode");
    CHECK(decodeOne({0xD9, 0x38}, op) && op.kind == J64_FPU && op.len == 2 && op.isMem, "fstp m32 decode");
    CHECK(decodeOne({0xDD, 0x1D, 0x78, 0x56, 0x34, 0x12}, op) && op.kind == J64_FPU && op.len == 6 && op.isMem, "fstp m64 disp32 decode");
    CHECK(decodeOne({0xDB, 0x2D, 0x78, 0x56, 0x34, 0x12}, op) && op.kind == J64_FPU && op.len == 6 && op.isMem, "fist m32 disp32 decode");
    CHECK(decodeOne({0xDF, 0x28}, op) && op.kind == J64_FPU && op.len == 2 && op.isMem, "fild m64 decode");
    CHECK(decodeOne({0xD8, 0x84, 0x48, 0x78, 0x56, 0x34, 0x12}, op) && op.kind == J64_FPU && op.len == 7 && op.isMem, "fadd sib decode");
    // Truncated x87 (ModRM cut off) must not decode.
    CHECK(!decodeOne({0xD8}, op), "lone D8 decoded");
    CHECK(!decodeOne({0xD8, 0x84, 0x48}, op), "truncated sib x87 decoded");
    // Classification: always fallback, even without hostile prefixes.
    CHECK(decodeOne({0xD9, 0xE0}, op) && op.kind == J64_FPU, "fchs re-decode");
    jit64Classify(op, false, false);
    CHECK(op.plan == JIT64_FALLBACK, "x87 classified fast");
    // Stream truncation: the fast prefix survives, the x87 op is excluded.
    {
        const U8 prog[] = {0x48, 0x89, 0xC8, 0x48, 0x01, 0xC8, 0xD9, 0xE0, 0x48, 0x89, 0xD0};
        Jit64Op ops[8];
        U32 n = jit64CompileStream(0x400000, prog, sizeof(prog), ops, 8, false);
        CHECK(n == 2, "x87 trunc n=%u want 2", n);
        CHECK(ops[0].kind == J64_MOV_RM_R && ops[0].plan == JIT64_FAST, "x87 trunc op0 kind=%d", ops[0].kind);
        CHECK(ops[1].kind == J64_ALU_RM_R && ops[1].plan == JIT64_FAST, "x87 trunc op1 kind=%d", ops[1].kind);
    }
    // Emitter boundary: any byte stream containing x87 is rejected wholesale.
    CHECK(!emitOk({0xD9, 0xE0}), "fchs block accepted");
    CHECK(!emitOk({0x48, 0x89, 0xC8, 0xD9, 0xE0}), "mov+fchs block accepted");
    CHECK(!emitOk({0xD8, 0x00}), "fadd m32 block accepted");
}

// --- emitted-block structure: terminators, hotness, generation lookups ---

static void testFFIndirectEmission() {
    Jit64Op op;
    // FF /2 register form: call *rax (the census's ff d0 shape).
    CHECK(decodeOne({0xFF, 0xD0}, op) &&
          op.kind == J64_CALL_IND && op.sub == 2 && op.size == 8 && op.len == 2 &&
          !op.isMem && op.rmIndex == 0, "ff d0 call *rax decode");
    // FF /4 register form: jmp *rax.
    CHECK(decodeOne({0xFF, 0xE0}, op) &&
          op.kind == J64_JMP_IND && op.sub == 4 && op.size == 8 && op.len == 2 &&
          !op.isMem && op.rmIndex == 0, "ff e0 jmp *rax decode");
    // REX.B-extended registers: call *r15, jmp *r8.
    CHECK(decodeOne({0x49, 0xFF, 0xD7}, op) &&
          op.kind == J64_CALL_IND && !op.isMem && op.rmIndex == 15 && op.len == 3,
          "49 ff d7 call *r15 rm=%d", op.rmIndex);
    CHECK(decodeOne({0x49, 0xFF, 0xE0}, op) &&
          op.kind == J64_JMP_IND && !op.isMem && op.rmIndex == 8 && op.len == 3,
          "49 ff e0 jmp *r8 rm=%d", op.rmIndex);
    // No REX.W: the operand stays a full 64-bit target (the interpreter
    // hardcodes loadRM(m, 8)); size is 8 either way.
    CHECK(decodeOne({0x41, 0xFF, 0xD0}, op) &&
          op.kind == J64_CALL_IND && op.size == 8 && op.rmIndex == 8,
          "41 ff d0 call *r8 (no REX.W) size=%d rm=%d", op.size, op.rmIndex);
    // Memory forms: call *[rbx]; call *[rax+0x10].
    CHECK(decodeOne({0xFF, 0x13}, op) &&
          op.kind == J64_CALL_IND && op.isMem && op.len == 2 && op.ea.baseReg == 3,
          "ff 13 call *[rbx] decode");
    CHECK(decodeOne({0xFF, 0x50, 0x10}, op) &&
          op.kind == J64_CALL_IND && op.isMem && op.len == 3 &&
          op.ea.baseReg == 0 && op.ea.disp == 16, "ff 50 10 call *[rax+16] decode");
    // The census's jump-table shape: jmp qword [rax*8 + disp32]
    // (FF 24 C5: SIB scale 3, index rax, base field 5 + mod 0 = disp32).
    CHECK(decodeOne({0xFF, 0x24, 0xC5, 0x78, 0x56, 0x34, 0x12}, op) &&
          op.kind == J64_JMP_IND && op.isMem && op.len == 7 &&
          op.ea.baseReg == 0xFF && op.ea.idxReg == 0 && op.ea.scale == 3 &&
          op.ea.disp == 0x12345678,
          "ff 24 c5 jmp table ea base=%d idx=%d scale=%d disp=%lld",
          op.ea.baseReg, op.ea.idxReg, op.ea.scale, (long long)op.ea.disp);
    // Stack-relative forms: jmp *[rsp] (SIB base rsp, no index);
    // call *[rsp+8].
    CHECK(decodeOne({0xFF, 0x24, 0x24}, op) &&
          op.kind == J64_JMP_IND && op.isMem && op.len == 3 &&
          op.ea.baseReg == 4 && op.ea.idxReg == 0xFF, "ff 24 24 jmp *[rsp] decode");
    CHECK(decodeOne({0xFF, 0x54, 0x24, 0x08}, op) &&
          op.kind == J64_CALL_IND && op.isMem && op.len == 4 &&
          op.ea.baseReg == 4 && op.ea.disp == 8, "ff 54 24 08 call *[rsp+8] decode");
    // RIP-relative: call *[rip+0x10] at 0x400000: target = base + len + disp.
    CHECK(decodeOne({0xFF, 0x15, 0x10, 0x00, 0x00, 0x00}, op) &&
          op.kind == J64_CALL_IND && op.isMem && op.len == 6 && op.ea.ripRel &&
          op.ea.ripRelTarget == 0x400000 + 6 + 0x10, "ff 15 call *[rip] ea");
    // The other FF groups must NOT decode: /0 INC, /1 DEC, /3 far CALL,
    // /5 far JMP, /7 — the interpreter serves /0 /1 and #GP-faults /3 /5
    // at decode; none may reach the emitter.
    CHECK(!decodeOne({0xFF, 0xC0}, op), "ff /0 inc decoded");
    CHECK(!decodeOne({0xFF, 0xC8}, op), "ff /1 dec decoded");
    CHECK(!decodeOne({0xFF, 0xD8}, op), "ff /3 far call reg decoded");
    CHECK(!decodeOne({0xFF, 0x18}, op), "ff /3 far call mem decoded");
    CHECK(!decodeOne({0xFF, 0xE8}, op), "ff /5 far jmp reg decoded");
    CHECK(!decodeOne({0xFF, 0x28}, op), "ff /5 far jmp mem decoded");
    CHECK(!decodeOne({0xFF, 0xF8}, op), "ff /7 decoded");
    // Plans: plain forms fast; LOCK and 0x66 fall back (the interpreter
    // ignores both for the transfer itself, but admission follows the
    // FF /6 PUSH precedent — conservative, never wrong); FS/GS memory
    // operands fall back; an FS-prefixed REGISTER form is exact (the
    // segment override names no memory operand) and stays fast.
    Jit64Op c = dec(0x400000, {0xFF, 0xD0});
    CHECK(c.plan == JIT64_FAST, "call ind plan fallback");
    Jit64Op j = dec(0x400000, {0xFF, 0x24, 0xC5, 0, 0, 0, 0});
    CHECK(j.plan == JIT64_FAST, "jmp table plan fallback");
    {
        Jit64Op o = dec(0x400000, {0xFF, 0xD0});
        jit64Classify(o, true, false);
        CHECK(o.plan == JIT64_FALLBACK, "lock call ind plan fast");
    }
    {
        Jit64Op o = dec(0x400000, {0x66, 0xFF, 0xD0});
        jit64Classify(o, false, true);
        CHECK(o.plan == JIT64_FALLBACK, "66 call ind plan fast");
    }
    {
        Jit64Op o = dec(0x400000, {0x64, 0xFF, 0x13}); // call *fs:[rbx]
        CHECK(o.isMem && o.ea.seg != 0 && o.plan == JIT64_FALLBACK,
              "fs call ind mem plan fast seg=%d", o.ea.seg);
    }
    {
        Jit64Op o = dec(0x400000, {0x64, 0xFF, 0xD0}); // fs: call *rax
        CHECK(!o.isMem && o.plan == JIT64_FAST, "fs call ind reg plan fallback");
    }
    // Compile stream: the stream stops AFTER an indirect transfer, like
    // the other terminators; bytes past it belong to the next block.
    {
        Jit64Op ops[24];
        U32 n = jit64CompileStream(0x400000,
            (const U8[]){0x48, 0x89, 0xC8, 0xFF, 0xD0, 0x90}, 6, ops, 24, true);
        CHECK(n == 2 && ops[1].kind == J64_CALL_IND, "stream stop after call ind n=%u", n);
        n = jit64CompileStream(0x400000,
            (const U8[]){0x48, 0x89, 0xC8, 0xFF, 0xE0, 0x90}, 6, ops, 24, true);
        CHECK(n == 2 && ops[1].kind == J64_JMP_IND, "stream stop after jmp ind n=%u", n);
        // A truncated ModRM (stream ends mid-instruction) ends the stream
        // BEFORE the transfer, never with a half-decoded op.
        n = jit64CompileStream(0x400000,
            (const U8[]){0x48, 0x89, 0xC8, 0xFF}, 4, ops, 24, true);
        CHECK(n == 1, "truncated ff stream n=%u", n);
    }
    // Emission: blocks ending in every served form are accepted.
    CHECK(emitOk({0x48, 0x89, 0xC8, 0xFF, 0xD0}), "call *rax block rejected");
    CHECK(emitOk({0x48, 0x89, 0xC8, 0xFF, 0xE0}), "jmp *rax block rejected");
    CHECK(emitOk({0x48, 0x89, 0xC8, 0xFF, 0x13}), "call *[rbx] block rejected");
    CHECK(emitOk({0x48, 0x89, 0xC8, 0xFF, 0x24, 0xC5, 0, 0, 0, 0}), "jmp table block rejected");
    CHECK(emitOk({0x48, 0x89, 0xC8, 0xFF, 0x15, 0, 0, 0, 0}), "call *[rip] block rejected");
    CHECK(emitOk({0xFF, 0xD0, 0x90}) == false, "call ind not last accepted via stream");
    // A hand-built block with the transfer NOT last is refused by the
    // emitter (owns-RIP mid-block), even when every op is FAST.
    {
        Jit64Op ops[24];
        U32 n = jit64CompileStream(0x400000,
            (const U8[]){0xFF, 0xD0, 0x90}, 3, ops, 24, false);
        CHECK(n == 2, "no-stop stream n=%u", n);
        Jit64Block block;
        block.ops.assign(ops, ops + n);
        std::vector<U8> module;
        CHECK(!jit64EmitWasm(block, module), "mid-block call ind emitted");
    }
    // Store classification (§14.3): CALL_IND stores (the return-address
    // push); JMP_IND never does.
    {
        Jit64Op ops[1];
        ops[0] = dec(0x400000, {0xFF, 0xD0});
        CHECK(jit64BlockMayStore(ops, 1), "call ind mayStore false");
        ops[0] = dec(0x400000, {0xFF, 0xE0});
        CHECK(!jit64BlockMayStore(ops, 1), "jmp ind mayStore true");
    }
}

static void testTerminatorEmission() {
    // INSN after INSN then a terminator: every terminator kind the decoder
    // marks FAST must be emittable now (the archive defect was that the
    // emitter rejected exactly the ops the decoder appends last).
    const std::vector<std::vector<U8>> accepted = {
        {0x48, 0x89, 0xC8, 0x90},                      // mov rax,rcx; nop
        {0x48, 0x89, 0xC8, 0x74, 0x10},                // ...; jz +16
        {0x48, 0x89, 0xC8, 0x0F, 0x85, 0x10, 0, 0, 0}, // ...; jnz rel32
        {0x48, 0x89, 0xC8, 0xE9, 0x10, 0, 0, 0},       // ...; jmp rel32
        {0x48, 0x89, 0xC8, 0xEB, 0x10},                // ...; jmp rel8
        {0x48, 0x89, 0xC8, 0xE8, 0x10, 0, 0, 0},       // ...; call rel32
        {0x48, 0x89, 0xC8, 0xC3},                      // ...; ret
        {0x48, 0x89, 0xC8, 0xC9},                      // ...; leave
        {0x48, 0x89, 0xC8, 0xC9, 0xC3},                // ...; leave; ret
        {0x48, 0x89, 0xC8, 0x50, 0x5B, 0xC3},          // push; pop; ret
    };
    for (const auto& code : accepted) CHECK(emitOk(code), "terminator block rejected");

    // A terminator BEFORE the last instruction cannot be represented by a
    // straight-line module (a taken branch continues at another RIP), so it
    // must be rejected rather than mis-executed.
    CHECK(!emitOk({0x48, 0x89, 0xC8, 0x74, 0x02, 0x48, 0x89, 0xC8}), "mid-block jcc accepted");
    CHECK(!emitOk({0x48, 0x89, 0xC8, 0xE9, 0x02, 0, 0, 0, 0x90}), "mid-block jmp accepted");
    CHECK(!emitOk({0x48, 0x89, 0xC8, 0xC3, 0x90}), "mid-block ret accepted");
    CHECK(!emitOk({0x48, 0x89, 0xC8, 0xE8, 0x02, 0, 0, 0, 0x90}), "mid-block call accepted");

    // A hand-built block whose op list carries a stale FALLBACK plan must be
    // refused wholesale (the runtime's whole-block fallback rule).
    {
        Jit64Op ops[2];
        U32 n = jit64CompileStream(0x400000, (const U8*)"\x48\x89\xC8\xC3", 4, ops, 2, true);
        CHECK(n == 2, "terminator stream n=%u", n);
        ops[0].plan = JIT64_FALLBACK;
        Jit64Block block;
        block.ops.assign(ops, ops + n);
        std::vector<U8> module = {0xAA};
        CHECK(!jit64EmitWasm(block, module) && module.empty(), "fallback op not refused");
    }
}

static void testHotness() {
    Jit64Hotness hot;
    const U64 rip = 0x400000;
    CHECK(!hot.bump(rip, 4), "hotness fired early");
    CHECK(!hot.bump(rip, 4), "hotness fired early (2)");
    CHECK(!hot.bump(rip, 4), "hotness fired early (3)");
    CHECK(hot.bump(rip, 4), "hotness should fire on the 4th entry");
    // Once hot, every later entry stays hot (an evicted block is re-emitted).
    CHECK(hot.bump(rip, 4), "hotness lost its hot state");
    // threshold 0 = compile immediately.
    CHECK(hot.bump(0x500000, 0), "threshold 0 must emit immediately");
    CHECK(hot.bump(0, 4) == false, "rip 0 must never be counted");
    // A different rip has its own counter.
    CHECK(!hot.bump(0x600000, 4), "hotness aliased another rip");
    hot.clear();
    CHECK(!hot.bump(rip, 4), "clear did not reset the counters");
    // The env-provided threshold is cached and stable.
    U16 t1 = jit64HotThreshold();
    CHECK(t1 == jit64HotThreshold(), "hot threshold not cached");
}

static void testCompileLimit() {
    Jit64State state;
    state.maxBlocks = 2;
    CHECK(state.canCompile(), "compile limit blocked the first block");
    state.blocksCompiled = 1;
    CHECK(state.canCompile(), "compile limit blocked before the cap");
    state.blocksCompiled = 2;
    CHECK(!state.canCompile(), "compile limit allowed a block past the cap");
    state.maxBlocks = 0;
    state.blocksCompiled = 0;
    CHECK(!state.canCompile(), "zero compile limit allowed a block");
}

static void testCacheCollisions() {
    Jit64BlockCache cache;
    Jit64Op op;
    CHECK(jit64CompileStream(0x400000, (const U8*)"\x90", 1, &op, 1, false) == 1,
          "collision fixture decode");
    // These distinct hot blocks collide in the original direct-mapped cache.
    for (U64 i = 0; i < 4; ++i)
        cache.insert(0x400000 + i * 0x100000, 1, 7, 1, 7, &op, 1);
    for (U64 i = 0; i < 4; ++i)
        CHECK(cache.lookup(0x400000 + i * 0x100000, 1, 7, 1, 7),
              "a colliding hot block was evicted");
    cache.lookup(0x400000, 1, 7, 1, 7); // keep the first block hot
    cache.insert(0x800000, 1, 7, 1, 7, &op, 1);
    CHECK(cache.lookup(0x400000, 1, 7, 1, 7), "hot entry lost to eviction");
    CHECK(!cache.lookup(0x500000, 1, 7, 1, 7), "least recently used entry survived");
    cache.invalidate(0x700000);
    cache.insert(0x900000, 1, 7, 1, 7, &op, 1);
    CHECK(cache.lookup(0x600000, 1, 7, 1, 7), "insertion did not reuse the empty slot");
    CHECK(cache.lookup(0x800000, 1, 7, 1, 7), "invalidation removed a different block");
}

static U32 g_fakeGen[4096];
static U32 fakePageGenRead(U64 pageNum) { return g_fakeGen[jit64BlockSlot(pageNum)]; }

static void testLookupByRip() {
    // lookupByRip needs the installed page-generation reader; without one it
    // must fail closed (the caller then stays on the interpreter).
    jit64SetPageGenFn(nullptr);
    Jit64BlockCache cache;
    Jit64Op ops[1];
    U32 n = jit64CompileStream(0x400000, (const U8*)"\x48\x89\xC8", 3, ops, 1, false);
    CHECK(n == 1, "lookup-setup n=%u", n);
    Jit64Block* b = cache.insert(0x400000, 1, g_fakeGen[jit64BlockSlot(1)], 1, g_fakeGen[jit64BlockSlot(1)], ops, n);
    CHECK(b != nullptr, "insert failed");
    CHECK(cache.lookupByRip(0x400000) == nullptr, "no page-gen reader must fail closed");
    jit64SetPageGenFn(&fakePageGenRead);
    CHECK(cache.lookupByRip(0x400000) == b, "valid entry should hit");
    CHECK(cache.lookupByRip(0x400008) == nullptr, "wrong rip must miss");
    // A write to the page (generation bump) invalidates without a decode.
    g_fakeGen[jit64BlockSlot(1)]++;
    CHECK(cache.lookupByRip(0x400000) == nullptr, "stale generation must miss");
    CHECK(cache.at(0x400000)->startRip == 0x400000, "entry must survive a stale lookup");
    jit64SetPageGenFn(nullptr);
}

static void testEngineAndStats() {
    // Engine names are stable strings the metrics panel switches on.
    CHECK(std::strcmp(jit64EngineName(JIT64_ENGINE_WASM), "wasm") == 0, "engine name wasm");
    CHECK(std::strcmp(jit64EngineName(JIT64_ENGINE_INTERP), "interp") == 0, "engine name interp");
    CHECK(std::strcmp(jit64EngineName(JIT64_ENGINE_OFF), "off") == 0, "engine name off");
    CHECK(std::strcmp(jit64WasmEngineMode(), "unavailable") == 0, "native engine mode");
    // Metrics seam: a null state must still produce valid, honest JSON.
    char buf[1024];
    U32 used = jit64StatsJson(nullptr, buf, sizeof(buf));
    CHECK(used > 0, "stats json produced nothing");
    if (used) {
        std::string json(buf, used);
        CHECK(json.find("\"cpu64\"") != std::string::npos, "stats missing cpu64 section");
        CHECK(json.find("\"cpu32\"") != std::string::npos, "stats missing cpu32 section");
        CHECK(json.find("\"blocksEntered\":0") != std::string::npos, "stats blocksEntered");
        CHECK(json.find("\"engine\":\"off\"") != std::string::npos, "stats engine");
        CHECK(json.find("\"available\":0") != std::string::npos, "stats available");
        CHECK(json.back() == '}', "stats json must end with }");
    }
    // Too-small buffers report 0 instead of truncating into invalid JSON.
    CHECK(jit64StatsJson(nullptr, buf, 8) == 0, "small stats buffer must report 0");
}

static void testRejectedOpcodeStats() {
    char buf[4096];
    const U8 prefixed0F[] = {0xF3, 0x66, 0x0F, 0xAF, 0xC0};
    U8 opcode = 0, opcode2 = 0;
    CHECK(jit64OpcodeBytes(prefixed0F, sizeof(prefixed0F), opcode, opcode2) &&
          opcode == 0x0F && opcode2 == 0xAF, "opcode bytes skip prefixes for 0F map");
    const U8 prefixedOneByte[] = {0x48, 0xF7, 0xE0};
    CHECK(jit64OpcodeBytes(prefixedOneByte, sizeof(prefixedOneByte), opcode, opcode2) &&
          opcode == 0xF7 && opcode2 == 0, "opcode bytes skip REX for one-byte map");

    Jit64State state;
    state.rejectOpcode[0xF7].store(2);
    state.reject0FOpcode[0xAF].store(9);
    U32 used = jit64StatsJson(&state, buf, sizeof(buf));
    CHECK(used > 0, "reject stats JSON produced nothing");
    if (used) {
        std::string json(buf, used);
        CHECK(json.find("\"rejectTop\":[{\"op\":\"0F AF\",\"count\":9},{\"op\":\"F7\",\"count\":2}]") != std::string::npos,
              "rejectTop must combine and order one-byte and 0F opcodes");
    }

    Jit64State published;
    jit64RegisterState(&published);
    published.rejectOpcode[0xF7].store(3);
    published.reject0FOpcode[0xAF].store(12);
    jit64PublishState(&published);
    published.rejectOpcode[0xF7].store(4);
    published.reject0FOpcode[0xAF].store(13);
    CHECK(jit64GlobalStatsJson(buf, sizeof(buf)) > 0, "aggregate reject JSON produced nothing");
    CHECK(std::strstr(buf, "\"rejectTop\":[{\"op\":\"0F AF\",\"count\":13},{\"op\":\"F7\",\"count\":4}]") != nullptr,
          "aggregate reject stats must read live atomic counts");
    jit64ReleaseState(&published);
    CHECK(jit64GlobalStatsJson(buf, sizeof(buf)) > 0, "retired reject JSON produced nothing");
    CHECK(std::strstr(buf, "\"rejectTop\":[{\"op\":\"0F AF\",\"count\":13},{\"op\":\"F7\",\"count\":4}]") != nullptr,
          "aggregate reject stats must retain retired counts");
}

static void testRejectAttribution() {
    // Negative cache: the attributed add round-trips through rejectedInfo;
    // the plain add reports "rejected, no attribution" (0xFFFF key).
    Jit64NegativeCache cache;
    U16 key = 0; U64 failRip = 0;
    CHECK(!cache.rejectedInfo(0x500000, 7, key, failRip), "empty cache must miss");
    cache.add(0x500000, 7, 0x1FF, 0x500010);
    CHECK(cache.rejected(0x500000, 7), "attributed entry must reject");
    CHECK(cache.rejectedInfo(0x500000, 7, key, failRip) && key == 0x1FF && failRip == 0x500010,
          "attribution round-trip: key=%x rip=%llx", key, (unsigned long long)failRip);
    CHECK(!cache.rejected(0x500000, 8), "wrong generation must miss");
    cache.add(0x600000, 3);
    CHECK(cache.rejectedInfo(0x600000, 3, key, failRip) && key == 0xFFFF && failRip == 0,
          "plain add must carry no attribution: key=%x", key);

    // Reject sites: dedup by failing RIP, byte evidence captured on the
    // compile-path record and backfilled after negative-cache-only hits.
    Jit64State state;
    const U8 ffBytes[] = {0xFF, 0xD0, 0x90, 0x90}; // call rax; nop; nop
    state.recordRejectSite(0x700000, 0x700000, 0xFF, ffBytes, sizeof(ffBytes), 1);
    state.recordRejectSite(0x700000, 0x700000, 0xFF, nullptr, 0, 41);
    CHECK(state.rejectSiteCount == 1, "site dedup: count=%u", state.rejectSiteCount);
    CHECK(state.rejectSites[0].entries == 42, "site entries accumulate: %llu",
          (unsigned long long)state.rejectSites[0].entries);
    CHECK(state.rejectSites[0].byteCount == 4 && state.rejectSites[0].bytes[0] == 0xFF &&
          state.rejectSites[0].bytes[1] == 0xD0, "site bytes captured");
    state.recordRejectSite(0x800000, 0x800020, 0x1EF, nullptr, 0, 5);
    state.recordRejectSite(0x800000, 0x800020, 0x1EF, ffBytes, sizeof(ffBytes), 1);
    CHECK(state.rejectSiteCount == 2 && state.rejectSites[1].byteCount == 4,
          "site bytes backfilled from a later compile-path record");

    // JSON: entry-weighted top + sites ride alongside the distinct census.
    state.rejectEntryOpcode[0xFF].store(42);
    state.rejectEntry0FOpcode[0xEF].store(100);
    char buf[8192];
    U32 used = jit64StatsJson(&state, buf, sizeof(buf));
    CHECK(used > 0, "attribution JSON produced nothing");
    if (used) {
        std::string json(buf, used);
        CHECK(json.find("\"rejectEntryTop\":[{\"op\":\"0F EF\",\"count\":100},{\"op\":\"FF\",\"count\":42}]") != std::string::npos,
              "rejectEntryTop must order entry-weighted opcodes");
        CHECK(json.find("\"rip\":\"0x700000\"") != std::string::npos &&
              json.find("\"bytes\":\"ffd09090\"") != std::string::npos,
              "rejectSites must carry the failing RIP and its bytes");
    }

    // Block-may-store truthfulness for the SSE store forms.
    Jit64Op store128 = dec(0x400000, {0x66, 0x0F, 0x7F, 0x10}); // movdqa [rax], xmm2
    CHECK(jit64BlockMayStore(&store128, 1), "movdqa store must report may-store");
    Jit64Op load128 = dec(0x400000, {0x66, 0x0F, 0x6F, 0x10});  // movdqa xmm2, [rax]
    CHECK(!jit64BlockMayStore(&load128, 1), "movdqa load must not report may-store");
    Jit64Op storeMovd = dec(0x400000, {0x66, 0x0F, 0x7E, 0x11}); // movd [rcx], xmm2
    CHECK(jit64BlockMayStore(&storeMovd, 1), "movd store must report may-store");
}

static void testPublishedStats() {
    char buf[2048];
    Jit64State state;
    jit64RegisterState(&state);
    state.wasmBlocks = 3;
    jit64PublishState(&state);
    state.wasmBlocks = 10; // not yet published: readers must not inspect it
    CHECK(jit64GlobalStatsJson(buf, sizeof(buf)) > 0, "aggregate JSON");
    CHECK(std::strstr(buf, "\"blocksEntered\":3,") != nullptr, "read published snapshot");
    jit64ReleaseState(&state);
    CHECK(jit64GlobalStatsJson(buf, sizeof(buf)) > 0, "retired aggregate JSON");
    CHECK(std::strstr(buf, "\"blocksEntered\":10,") != nullptr, "retain counts on CPU exit");
}

// K-M3 fault gate pins: every emitted guest-memory access is followed by a
// check of Jit64WasmState::faultPending (offset 172): when the host import
// recorded a denied access there, the module stores the faulting op index
// to execCount and branches to the epilogue, which publishes only the
// completed ops. String-loop accesses exit with depth 3 (check-if, loop,
// done-block, outer block); the epilogue conditional execCount store uses
// a different byte shape and must not be counted as an access gate.
static bool emitModule(std::vector<U8> b, std::vector<U8>& module) {
    Jit64Op ops[24];
    U32 n = jit64CompileStream(0x400000, b.data(), (U32)b.size(), ops, 24, true);
    if (!n) return false;
    U32 consumed = 0;
    for (U32 i = 0; i < n; i++) consumed += ops[i].len;
    if (consumed != b.size()) return false;
    Jit64Block block;
    block.ops.assign(ops, ops + n);
    return jit64EmitWasm(block, module) && !module.empty();
}

static U32 countPattern(const std::vector<U8>& m, const std::vector<U8>& pat) {
    U32 count = 0;
    if (m.size() < pat.size()) return 0;
    for (size_t i = 0; i + pat.size() <= m.size(); i++) {
        bool eq = true;
        for (size_t j = 0; j < pat.size(); j++) if (m[i + j] != pat[j]) { eq = false; break; }
        if (eq) count++;
    }
    return count;
}

static void testWasmFaultGate() {
    CHECK(offsetof(Jit64WasmState, faultPending) == 172, "faultPending offset");
    Jit64WasmState st{};
    U64 gpr[16] = {};
    jit64WasmStateInit(st, gpr, 0, 0);
    CHECK(st.faultPending == 0, "state init must clear faultPending");
    // Full access-gate sequence: i32.load off=172; if; local.get 34 /
    // local.set 18 (restore the op-start RFLAGS snapshot); local.get 0;
    // local.get 33 (current op); i32.store off=164 (execCount); br depth.
    const std::vector<U8> gate = {0x28, 0x02, 0xAC, 0x01, 0x04, 0x40, 0x20, 0x22,
                                  0x21, 0x12, 0x20, 0x00, 0x20, 0x21, 0x36, 0x02,
                                  0xA4, 0x01, 0x0C};
    // String-loop gates skip the flags restore (their flags are partial
    // progress) and exit with depth 3.
    const std::vector<U8> gateStr = {0x28, 0x02, 0xAC, 0x01, 0x04, 0x40, 0x20, 0x00,
                                     0x20, 0x21, 0x36, 0x02, 0xA4, 0x01, 0x0C, 0x03};
    std::vector<U8> m;
    CHECK(emitModule({0x48, 0x8B, 0x03}, m), "emit mov rax,[rbx]");
    CHECK(countPattern(m, gate) == 1, "load gate count=%u", countPattern(m, gate));
    CHECK(countPattern(m, gateStr) == 0, "plain load must not use the string gate");
    CHECK(emitModule({0x48, 0x8B, 0x03, 0x48, 0x89, 0x0B, 0x48, 0x8B, 0x13}, m),
          "emit 3-access block");
    CHECK(countPattern(m, gate) == 3, "3-access gate count=%u", countPattern(m, gate));
    CHECK(emitModule({0xF3, 0x48, 0xA5}, m), "emit rep movsq");
    CHECK(countPattern(m, gate) == 0, "rep movsq must not use the restoring gate");
    CHECK(countPattern(m, gateStr) == 2, "rep movsq string gates=%u", countPattern(m, gateStr));
    // The by-CL memory shift is classifier-declined (interpreter-only), so
    // no compiled stream reaches its emitter; hand-feed the decoded op.
    // Its store sits inside the count!=0 guard, so the gate must branch
    // with depth 2. (If the classifier ever enables the form, the Node
    // harness gains the behavioural version of this pin.)
    {
        Jit64Op op;
        const U8 bytes[] = {0x48, 0xD3, 0x23}; // shl qword [rbx], cl
        CHECK(jit64DecodeOne(0x400000, bytes, sizeof(bytes), op), "decode shl [rbx],cl");
        op.plan = JIT64_FAST; // bypass the classifier decline: emitter unit test
        Jit64Block block;
        block.ops.assign(&op, &op + 1);
        std::vector<U8> mm;
        CHECK(jit64EmitWasm(block, mm) && !mm.empty(), "hand-fed shl [rbx],cl must emit");
        std::vector<U8> gateD2 = gate; gateD2.push_back(0x02);
        CHECK(countPattern(mm, gate) == 2, "shl-cl-mem gates=%u", countPattern(mm, gate));
        CHECK(countPattern(mm, gateD2) == 1, "shl-cl-mem depth-2 gates=%u", countPattern(mm, gateD2));
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--stats-json") == 0) {
        char buf[2048];
        if (!jit64GlobalStatsJson(buf, sizeof(buf))) return 1;
        puts(buf);
        return 0;
    }
    printf("jit64 phase-1 unit tests\\n");
    testCoverageTable();
    testSseDecodeClassify();
    testRejectAttribution();
    testMovForms();
    testMov8Forms();
    testAluForms();
    testEaDecode();
    testStackDecode();
    testMiscFast();
    testFallbackRules();
    testCompileStream();
    testBlockCache();
    testPlanText();
    testJitFlag();
    testWasmStateBridge();
    testWasmGlue();
    testWasmFaultGate();
    testShiftEmission();
    testRotateEmission();
    testShiftCLEmission();
    testImulEmission();
    testGrp3Emission();
    testFFIndirectEmission();
    testStringEmission();
    testX87NeverJitted();
    testTerminatorEmission();
    testHotness();
    testCompileLimit();
    testCacheCollisions();
    testLookupByRip();
    testEngineAndStats();
    testRejectedOpcodeStats();
    testPublishedStats();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
