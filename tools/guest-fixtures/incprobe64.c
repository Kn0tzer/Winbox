// incprobe64: INC/DEC (FF /0 /1, FE /0 /1) correctness battery +
// INC/DEC-dense timing loop, in the jitbench output protocol.
//
// The checksum must be identical under the interpreter and under the JIT.
// Before the INCDEC JIT round, every FF/FE op truncated the block and the
// interpreter served it; with INC/DEC emitted, the same bytes run as
// generated code. The battery folds results AND observed flag words
// (including CF, which INC/DEC must preserve) into the checksum; the
// timing loop is built from explicit INC/DEC asm so its rate exposes
// whether INC/DEC-dense blocks execute as emitted code or fall back.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t battery(void) {
    uint64_t h = 0x0BAD5EED0BAD5EEDULL;
    // Register cases on a fixed register (rax / al) loaded from and stored
    // to memory variables: no operand modifiers, identical at every
    // compiler. The incoming carry is set with stc/clc first: INC/DEC
    // must preserve it; pushfq captures the full flag word afterwards.
    #define CASE_R(OPINSN, VAL, CARRY) do { \
        uint64_t v = (uint64_t)(VAL); uint64_t fl = 0; \
        __asm__ volatile("movq %[vv], %%rax\n\t" CARRY "\n\t" OPINSN "\n\t" \
            "pushfq\n\t popq %[ff]\n\t movq %%rax, %[vv]" \
            : [vv] "+m"(v), [ff] "=m"(fl) : : "rax", "cc"); \
        h = mix(h, v); h = mix(h, fl & 0x8D5ULL); } while (0)
    CASE_R("incq %%rax", 0x7FFFFFFFFFFFFFFFULL, "stc");
    CASE_R("decq %%rax", 0x8000000000000000ULL, "clc");
    CASE_R("incq %%rax", 0xFFFFFFFFFFFFFFFFULL, "clc");
    CASE_R("decq %%rax", 0x0000000000000000ULL, "stc");
    CASE_R("incq %%rax", 0x0000000000000FFFULL, "clc");
    CASE_R("incl %%eax", 0x000000007FFFFFFFULL, "stc");
    CASE_R("decl %%eax", 0x0000000080000000ULL, "clc");
    CASE_R("incl %%eax", 0x00000000FFFFFFFFULL, "clc");
    CASE_R("decl %%eax", 0x0000000000000000ULL, "stc");
    CASE_R("incw %%ax", 0x0000000000007FFFULL, "clc");
    CASE_R("decw %%ax", 0x0000000000008000ULL, "stc");
    CASE_R("incw %%ax", 0x000000000000FFFFULL, "stc");
    CASE_R("decw %%ax", 0x0000000000000000ULL, "clc");
    CASE_R("incb %%al", 0x000000000000007FULL, "stc");
    CASE_R("decb %%al", 0x0000000000000080ULL, "clc");
    CASE_R("incb %%al", 0x00000000000000FFULL, "clc");
    CASE_R("decb %%al", 0x0000000000000000ULL, "stc");
    CASE_R("incb %%al", 0x000000000000000FULL, "clc");
    // CF preservation consumed by a following ADC: stc; inc/dec; adc 0
    // into a second fixed register. ADC must see the OLD carry (1) even
    // when the INC wraps or the DEC borrows.
    #define CASE_ADC(OPINSN, VAL) do { \
        uint64_t v = (uint64_t)(VAL); uint64_t adc = 0; \
        __asm__ volatile("movq %[vv], %%rax\n\t xorl %%ecx, %%ecx\n\t stc\n\t" OPINSN "\n\t" \
            "adcq $0, %%rcx\n\t movq %%rax, %[vv]\n\t movq %%rcx, %[aa]" \
            : [vv] "+m"(v), [aa] "=m"(adc) : : "rax", "rcx", "cc"); \
        h = mix(h, adc); h = mix(h, v); } while (0)
    CASE_ADC("incq %%rax", 0xFFFFFFFFFFFFFFFFULL);
    CASE_ADC("decq %%rax", 0x0000000000000000ULL);
    CASE_ADC("incl %%eax", 0x00000000FFFFFFFFULL);
    OutputDebugStringA("BW_INCPROBE64 battery reg ok\n");
    // Memory forms (FF /0 /1 and FE /0 /1 on based and based+disp EAs).
    static uint64_t memq[4];
    static uint32_t memd[4];
    static uint16_t memw[4];
    static uint8_t memb[8];
    for (int i = 0; i < 4; i++) {
        memq[i] = 0xFFFFFFFFFFFFFFFFULL - (uint64_t)i;
        memd[i] = 0x80000000u + (uint32_t)i;
        memw[i] = (uint16_t)(0x7FFF + i);
        memb[i] = (uint8_t)(0xFF - i);
        memb[i + 4] = (uint8_t)i;
    }
    uint64_t fl;
    __asm__ volatile("stc\n\t incq (%1)\n\t pushfq\n\t pop %0" : "=r"(fl) : "r"(memq) : "cc", "memory");
    h = mix(h, memq[0]); h = mix(h, fl & 0x8D5ULL);
    __asm__ volatile("clc\n\t decq 16(%1)\n\t pushfq\n\t pop %0" : "=r"(fl) : "r"(memq) : "cc", "memory");
    h = mix(h, memq[2]); h = mix(h, fl & 0x8D5ULL);
    __asm__ volatile("stc\n\t incl 4(%1)\n\t pushfq\n\t pop %0" : "=r"(fl) : "r"(memd) : "cc", "memory");
    h = mix(h, memd[1]); h = mix(h, fl & 0x8D5ULL);
    __asm__ volatile("clc\n\t decw 2(%1)\n\t pushfq\n\t pop %0" : "=r"(fl) : "r"(memw) : "cc", "memory");
    h = mix(h, memw[1]); h = mix(h, fl & 0x8D5ULL);
    __asm__ volatile("stc\n\t incb 3(%1)\n\t pushfq\n\t pop %0" : "=r"(fl) : "r"(memb) : "cc", "memory");
    h = mix(h, memb[3]); h = mix(h, fl & 0x8D5ULL);
    __asm__ volatile("clc\n\t decb (%1)\n\t pushfq\n\t pop %0" : "=r"(fl) : "r"(memb + 4) : "cc", "memory");
    h = mix(h, memb[4]); h = mix(h, fl & 0x8D5ULL);
    OutputDebugStringA("BW_INCPROBE64 battery mem ok\n");
    // C-level ++/-- sweeps at every width (encoding-agnostic differential
    // fodder), values folded after each step.
    for (uint32_t i = 0; i < 4096; i++) {
        uint64_t a = i * 2654435761u; a ^= a >> 29;
        uint32_t b = (uint32_t)a; uint16_t c = (uint16_t)a; uint8_t d = (uint8_t)a;
        a++; a--; ++a; b++; --b; c++; c--; d++; --d;
        h = mix(h, a); h = mix(h, b); h = mix(h, c); h = mix(h, d);
    }
    OutputDebugStringA("BW_INCPROBE64 battery sweep ok\n");
    return h;
}

#ifndef INCPROBE_PASSES
#define INCPROBE_PASSES 100000u
#endif

static uint64_t timing_loop(uint64_t seed) {
    static uint64_t tm[4];
    static uint32_t td[4];
    tm[0] = seed; tm[1] = seed ^ 0x1111111111111111ULL;
    tm[2] = seed ^ 0x2222222222222222ULL; tm[3] = seed ^ 0x3333333333333333ULL;
    td[0] = (uint32_t)seed; td[1] = (uint32_t)(seed >> 7);
    td[2] = (uint32_t)(seed >> 13); td[3] = (uint32_t)(seed >> 21);
    uint64_t a = seed, b = seed + 1, c = seed + 2;
    uint32_t e = (uint32_t)seed;
    for (uint32_t i = 0; i < INCPROBE_PASSES; i++) {
        __asm__ volatile(
            "incq %0\n\t decq %1\n\t incq %2\n\t decl %3\n\t"
            "incq (%4)\n\t decq 8(%4)\n\t incl (%5)\n\t decl 4(%5)\n\t"
            : "+r"(a), "+r"(b), "+r"(c), "+r"(e)
            : "r"(tm), "r"(td)
            : "cc", "memory");
    }
    uint64_t h = seed;
    h = mix(h, a); h = mix(h, b); h = mix(h, c); h = mix(h, e);
    h = mix(h, tm[0]); h = mix(h, tm[1]); h = mix(h, td[0]); h = mix(h, td[1]);
    return h;
}

int main(void) {
    OutputDebugStringA("BW_INCPROBE64 battery begin\n");
    uint64_t seed = battery();
    {
        char bline[80];
        snprintf(bline, sizeof(bline), "BW_INCPROBE64 battery checksum=%016llx\n", (unsigned long long)seed);
        OutputDebugStringA(bline);
    }
    char hex[17];
    LARGE_INTEGER frequency, start, end;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    for (int sample = 0; sample < 4; sample++) {
        QueryPerformanceCounter(&start);
        uint64_t sum = timing_loop(seed);
        QueryPerformanceCounter(&end);
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)sum);
        char line[160];
        snprintf(line, sizeof(line), "BW_INCPROBE64 sample=%d ms=%.3f checksum=%s passes=%u\n",
                 sample, (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)frequency.QuadPart,
                 hex, (unsigned)INCPROBE_PASSES);
        OutputDebugStringA(line);
        printf("%s", line);
        fflush(stdout);
    }
    return 0;
}
