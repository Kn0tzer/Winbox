// sseprobe64: scalar SSE moves (MOVSD F2 0F 10/11, MOVSS F3 0F 10/11,
// MOVQ load F3 0F 7E) and packed FP bitwise logic (ANDPS/ANDPD 0F 54,
// ANDNPS/ANDNPD 0F 55, ORPS/ORPD 0F 56, XORPS/XORPD 0F 57) correctness
// battery + dense timing loops, in the jitbench output protocol.
//
// The checksum must be identical under the interpreter and under the
// JIT. Before the scalar-move/logic JIT round, every one of these ops
// truncated the block and the interpreter served it; with them emitted,
// the same bytes run as generated code. The battery folds the FULL
// 128-bit destination after each op, so the upper-bit rules are part of
// the checksum: MOVSD/MOVSS register forms preserve the destination's
// upper bits, their memory loads zero everything above the moved
// scalar, and the MOVQ load zeroes the high qword even in register
// form. ANDN cases use asymmetric patterns so an operand-order
// inversion (~src & dst instead of ~dst & src) changes the checksum.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

// 128-bit cells the asm cases work on. DINIT pre-loads the destination
// register, SRC the source register / memory source, RES receives the
// destination register after the op, MEMC is the memory destination.
// Inline-asm operands: %0 = RES or MEMC (the "=m" output), %1 = DINIT,
// %2 = SRC (inputs).
static uint64_t DINIT[2], SRC[2], RES[2], MEMC[2];

static const uint64_t PAT_A = 0xA5A5A5A5A5A5A5A5ULL;
static const uint64_t PAT_B = 0x5A5A5A5A5A5A5A5AULL;
static const uint64_t PAT_C = 0x0123456789ABCDEFULL;
static const uint64_t PAT_D = 0xFEDCBA9876543210ULL;

static uint64_t battery(void) {
    uint64_t h = 0x0BAD5EED0BAD5EEDULL;
    #define SETUP(DLO, DHI, SLO, SHI) do { \
        DINIT[0] = (DLO); DINIT[1] = (DHI); SRC[0] = (SLO); SRC[1] = (SHI); \
        RES[0] = 0; RES[1] = 0; MEMC[0] = PAT_C; MEMC[1] = PAT_D; } while (0)
    #define FOLDREG() do { h = mix(h, RES[0]); h = mix(h, RES[1]); } while (0)
    #define FOLDCELL() do { h = mix(h, MEMC[0]); h = mix(h, MEMC[1]); } while (0)
    // Load destination xmm0 from DINIT (%1), source xmm1 from SRC (%2)
    // (both via the already-covered MOVDQA load), run OP, store xmm0
    // to RES (%0).
    #define CASE_RR(OP) do { SETUP(PAT_A, PAT_B, PAT_C, PAT_D); \
        __asm__ volatile("movdqa %1, %%xmm0\n\t movdqa %2, %%xmm1\n\t" OP "\n\t movdqa %%xmm0, %0" \
            : "=m"(RES) : "m"(DINIT), "m"(SRC) : "memory"); FOLDREG(); } while (0)
    // Destination xmm0 from DINIT (%1), op reads memory SRC (%2),
    // store xmm0 to RES (%0).
    #define CASE_RM(OP) do { SETUP(PAT_A, PAT_B, PAT_C, PAT_D); \
        __asm__ volatile("movdqa %1, %%xmm0\n\t" OP "\n\t movdqa %%xmm0, %0" \
            : "=m"(RES) : "m"(DINIT), "m"(SRC) : "memory"); FOLDREG(); } while (0)
    // Source xmm0 from SRC (%2), op writes memory MEMC (%0).
    #define CASE_MR(OP) do { SETUP(PAT_A, PAT_B, PAT_C, PAT_D); \
        __asm__ volatile("movdqa %2, %%xmm0\n\t" OP \
            : "=m"(MEMC) : "m"(DINIT), "m"(SRC) : "memory"); FOLDCELL(); } while (0)

    // ---- MOVSD (F2 0F 10 load / F2 0F 11 store) ----
    CASE_RR("movsd %%xmm1, %%xmm0");            // reg load: lo=src.lo, hi PRESERVED (PAT_B)
    CASE_RM("movsd %2, %%xmm0");               // mem load: lo=SRC[0], hi ZEROED
    CASE_MR("movsd %%xmm0, %0");               // mem store: MEMC[0]=SRC[0], MEMC[1] untouched
    CASE_RR("movsd %%xmm1, %%xmm0\n\t movsd %%xmm0, %%xmm1\n\t movdqa %%xmm1, %%xmm0"); // reg store form, folded via xmm1
    // ---- MOVSS (F3 0F 10 load / F3 0F 11 store) ----
    CASE_RR("movss %%xmm1, %%xmm0");            // reg load: low32 replaced, rest PRESERVED
    CASE_RM("movss %2, %%xmm0");               // mem load: lo=zero-extended dword, hi ZEROED
    CASE_MR("movss %%xmm0, %0");               // mem store: low dword only, rest of MEMC untouched
    CASE_RR("movss %%xmm1, %%xmm0\n\t movss %%xmm0, %%xmm1\n\t movdqa %%xmm1, %%xmm0");
    // ---- MOVQ load (F3 0F 7E): high qword ZEROED even in register form ----
    CASE_RR("movq %%xmm1, %%xmm0");             // reg load: lo=src.lo, hi ZEROED (unlike MOVSD)
    CASE_RM("movq %2, %%xmm0");                // mem load: lo=SRC[0], hi ZEROED
    OutputDebugStringA("BW_SSEPROBE64 battery moves ok\n");
    // ---- Packed logic, register source (PS and PD prefix forms) ----
    CASE_RR("andps %%xmm1, %%xmm0");
    CASE_RR("andpd %%xmm1, %%xmm0");
    CASE_RR("andnps %%xmm1, %%xmm0");           // ~PAT_A & PAT_C per qword; inverted order differs
    CASE_RR("andnpd %%xmm1, %%xmm0");
    CASE_RR("orps %%xmm1, %%xmm0");
    CASE_RR("orpd %%xmm1, %%xmm0");
    CASE_RR("xorps %%xmm1, %%xmm0");
    CASE_RR("xorpd %%xmm1, %%xmm0");
    CASE_RR("xorps %%xmm0, %%xmm0");            // self-xor zeroing idiom
    // ---- Packed logic, memory source ----
    CASE_RM("andps %2, %%xmm0");
    CASE_RM("andnps %2, %%xmm0");
    CASE_RM("orps %2, %%xmm0");
    CASE_RM("xorpd %2, %%xmm0");
    OutputDebugStringA("BW_SSEPROBE64 battery logic ok\n");
    // A short realistic chain: zero, load a double, mask the sign bit
    // (the fabs idiom), fold the result. Operands: %0=RES, %1=DINIT,
    // %2=SRC (the double), %3=MEMC (the mask cell).
    SETUP(PAT_A, PAT_B, PAT_C, PAT_D);
    __asm__ volatile(
        "xorps %%xmm0, %%xmm0\n\t"
        "movsd %2, %%xmm0\n\t"
        "andpd %3, %%xmm0\n\t"
        "movdqa %%xmm0, %0"
        : "=m"(RES) : "m"(DINIT), "m"(SRC), "m"(MEMC) : "memory");
    FOLDREG();
    // C-level double/float sweeps (encoding-agnostic differential
    // fodder; the compiler spills these through the scalar moves).
    {
        volatile double dv = 0.5;
        volatile float fv = 0.25f;
        for (uint32_t i = 0; i < 4096; i++) {
            dv = dv * 1.0000001 + 0.0000001;
            fv = fv * 0.9999999f + 0.0000002f;
            if ((i & 0x3FF) == 0) { h = mix(h, (uint64_t)(dv * 1e9)); h = mix(h, (uint64_t)(fv * 1e9f)); }
        }
        h = mix(h, (uint64_t)(dv * 1e9));
        h = mix(h, (uint64_t)(fv * 1e9f));
    }
    OutputDebugStringA("BW_SSEPROBE64 battery sweep ok\n");
    return h;
}

#ifndef SSEPROBE_PASSES
#define SSEPROBE_PASSES 100000u
#endif

// Timing loop A: scalar-move dense. 8 moves per pass between a small
// set of memory cells and registers; results folded afterwards so the
// loop body is the whole measurement.
static uint64_t cellsA[8];
static uint64_t timing_scalar(uint64_t seed) {
    for (int i = 0; i < 8; i++) cellsA[i] = seed ^ (0x1111111111111111ULL * (uint64_t)(i + 1));
    uint64_t base = (uint64_t)cellsA;
    for (uint32_t i = 0; i < SSEPROBE_PASSES; i++) {
        __asm__ volatile(
            "movsd (%0), %%xmm0\n\t"
            "movsd %%xmm0, 8(%0)\n\t"
            "movss 16(%0), %%xmm1\n\t"
            "movss %%xmm1, 20(%0)\n\t"
            "movsd %%xmm0, %%xmm2\n\t"
            "movss %%xmm1, %%xmm3\n\t"
            "movq 24(%0), %%xmm4\n\t"
            "movsd %%xmm4, 32(%0)\n\t"
            : : "r"(base) : "memory");
    }
    uint64_t h = seed;
    for (int i = 0; i < 8; i++) h = mix(h, cellsA[i]);
    return h;
}

// Timing loop B: packed-logic dense. 8 logic ops per pass, register
// sources plus one memory source; final register values are stored
// and folded so nothing is dead.
static uint64_t cellsB[2];
static uint64_t outB[4];
static uint64_t timing_logic(uint64_t seed) {
    cellsB[0] = seed; cellsB[1] = seed ^ 0x0F0F0F0F0F0F0F0FULL;
    uint64_t base = (uint64_t)cellsB;
    uint64_t obase = (uint64_t)outB;
    __asm__ volatile(
        "movdqa (%0), %%xmm1\n\t movdqa (%0), %%xmm2\n\t movdqa (%0), %%xmm3\n\t"
        "movdqa (%0), %%xmm5\n\t movdqa (%0), %%xmm6\n\t movdqa (%0), %%xmm7\n\t"
        : : "r"(base) : "memory");
    for (uint32_t i = 0; i < SSEPROBE_PASSES; i++) {
        __asm__ volatile(
            "andps (%0), %%xmm0\n\t"
            "orps %%xmm1, %%xmm0\n\t"
            "xorps %%xmm2, %%xmm0\n\t"
            "andnps %%xmm3, %%xmm0\n\t"
            "andpd (%0), %%xmm4\n\t"
            "orpd %%xmm5, %%xmm4\n\t"
            "xorpd %%xmm6, %%xmm4\n\t"
            "andnpd %%xmm7, %%xmm4\n\t"
            : : "r"(base) : "memory");
    }
    __asm__ volatile("movdqa %%xmm0, (%0)\n\t movdqa %%xmm4, 16(%0)" : : "r"(obase) : "memory");
    uint64_t h = seed;
    for (int i = 0; i < 4; i++) h = mix(h, outB[i]);
    return h;
}

int main(void) {
    OutputDebugStringA("BW_SSEPROBE64 battery begin\n");
    uint64_t seed = battery();
    {
        char bline[96];
        snprintf(bline, sizeof(bline), "BW_SSEPROBE64 battery checksum=%016llx\n", (unsigned long long)seed);
        OutputDebugStringA(bline);
    }
    LARGE_INTEGER frequency, start, end;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    // Integer millisecond math ONLY: the double ms expression printed
    // garbage (1e66+) for every sample after the first on the shipped
    // build, in both engines - the guest FP conversion/format path is
    // not trusted inside a timing report. ticks= carries the raw QPC
    // delta so the figure can be re-derived from the log.
    {
        char fline[96];
        snprintf(fline, sizeof(fline), "BW_SSEPROBE64 qpcfreq=%lld\n", (long long)frequency.QuadPart);
        OutputDebugStringA(fline);
        printf("%s", fline);
        fflush(stdout);
    }
    for (int sample = 0; sample < 4; sample++) {
        char line[224]; char hex[17];
        QueryPerformanceCounter(&start);
        uint64_t sumA = timing_scalar(seed);
        QueryPerformanceCounter(&end);
        long long ticksA = (long long)(end.QuadPart - start.QuadPart);
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)sumA);
        snprintf(line, sizeof(line), "BW_SSEPROBE64 sample=%d loop=scalar ms=%lld checksum=%s passes=%u ticks=%lld\n",
                 sample, (long long)(ticksA * 1000 / frequency.QuadPart),
                 hex, (unsigned)SSEPROBE_PASSES, ticksA);
        OutputDebugStringA(line);
        printf("%s", line);
        fflush(stdout);
        QueryPerformanceCounter(&start);
        uint64_t sumB = timing_logic(seed);
        QueryPerformanceCounter(&end);
        long long ticksB = (long long)(end.QuadPart - start.QuadPart);
        snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)sumB);
        snprintf(line, sizeof(line), "BW_SSEPROBE64 sample=%d loop=logic ms=%lld checksum=%s passes=%u ticks=%lld\n",
                 sample, (long long)(ticksB * 1000 / frequency.QuadPart),
                 hex, (unsigned)SSEPROBE_PASSES, ticksB);
        OutputDebugStringA(line);
        printf("%s", line);
        fflush(stdout);
    }
    return 0;
}
