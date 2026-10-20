/* jitbenchmix.c — deterministic mixed CPU32/CPU64 browser workload.
 * Each sample combines a short integer kernel, a 256 KiB copy/compare, double
 * arithmetic with Newton square root, indirect calls, and a 64-way switch.
 */
#include <stdint.h>
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef JITBENCHMIX_PASSES
#define JITBENCHMIX_PASSES 4u
#endif

#define TABLE_ENTRIES 64u
#define TABLE_MASK (TABLE_ENTRIES - 1u)
#define MIX_INTEGER_STEPS 19u /* 19/64 of one jitbench32 pass (29.7%). */
#define BUFFER_BYTES (256u * 1024u)
#define BUFFER_WORDS (BUFFER_BYTES / sizeof(uint32_t))
#define MATH_COUNT 4096u

static uint32_t g_intTable[TABLE_ENTRIES];
static uint32_t g_source[BUFFER_WORDS];
static volatile uint32_t g_destination[BUFFER_WORDS];
static double g_mathA[MATH_COUNT];
static double g_mathB[MATH_COUNT];
static double g_mathC[MATH_COUNT];
static double g_arithmetic[MATH_COUNT];
static double g_roots[MATH_COUNT];

__attribute__((noinline, noclone))
static uint32_t jb_mix32(uint32_t hash, uint32_t value, uint32_t round) {
    uint32_t rotated = (hash << 11) | (hash >> 21);
    hash ^= rotated;
    hash += (value | 1u) * 0x9E3779B9u;
    hash ^= round * 0x85EBCA6Bu;
    return hash + 0xC2B2AE35u;
}

static uint64_t jb_integer_steps(uint32_t count, uint32_t pass,
        uint32_t* hashPointer, uint64_t* wideStatePointer) {
    uint32_t hash = *hashPointer;
    uint64_t wideState = *wideStatePointer;
    uint32_t index = pass & TABLE_MASK;
    for (uint32_t step = 0; step < count; step++) {
        uint32_t current = g_intTable[index];
        uint32_t next = g_intTable[(index + 1u) & TABLE_MASK];
        uint32_t mixed = jb_mix32(hash, current, step);
        uint32_t delta = current - next;
        uint32_t value = mixed + delta;
        uint32_t signedValue = current & 0x1FFFFFFFu;
        uint32_t signedBound = next & 0x1FFFFFFFu;
        int32_t scaled = (int32_t)signedValue * 3;
        int32_t bound = (int32_t)signedBound;
        uint32_t signedBits = 0;
        if (scaled > bound) signedBits |= 1u;
        else if (scaled < -bound) signedBits |= 2u;
        if (scaled == 0) signedBits |= 4u;
        if ((uint32_t)scaled > (uint32_t)bound) signedBits |= 8u;
        if ((uint32_t)scaled & 0x80000000u) signedBits |= 16u;
        uint32_t widened = (uint32_t)(uint8_t)current;
        uint32_t narrowByte = current & 0xFFu;
        int32_t narrowSigned = narrowByte < 0x80u
            ? (int32_t)narrowByte : (int32_t)narrowByte - 256;
        uint32_t highBits = current >> 24;
        uint32_t combined = (widened + 1u) ^ (uint32_t)narrowSigned;
        if (delta > current) value += 0x10000001u;
        else if (delta == 0) value ^= 0x20000003u;
        else if (delta & 0x80000000u) value += 0x40000007u;
        else value -= 0x8000000Bu;
        if (signedBits & 4u) value ^= 0x1000001Du;
        if (signedBits & 16u) value += 0x20000021u;
        uint64_t wide = ((uint64_t)current << 32) | next;
        wide += (((uint64_t)g_intTable[(index + 7u) & TABLE_MASK] << 32)
            | g_intTable[(index + 13u) & TABLE_MASK]) ^ 0x9E3779B97F4A7C15ull;
        wide -= (uint64_t)delta << 17;
        uint32_t shift = (current ^ step) & 63u;
        wide = (wide << shift) | (wide >> ((64u - shift) & 63u));
        wideState += wide ^ (wideState >> ((step & 31u) + 1u));
        value ^= (uint32_t)(wideState ^ (wideState >> 32));
        value ^= combined << 9;
        value += highBits * 3u;
        g_intTable[index] = value ^ next;
        hash = jb_mix32(hash, g_intTable[index], pass & 0xFFFFu);
        index = (index + 7u) & TABLE_MASK;
    }
    hash += pass << 1;
    *hashPointer = hash;
    *wideStatePointer = wideState;
    return ((uint64_t)hash << 32) | (uint32_t)wideState;
}

static double jb_sqrt_newton(double value) {
    double guess = value * 0.5 + 1.0;
    for (unsigned i = 0; i < 10; i++) guess = (guess + value / guess) * 0.5;
    return guess;
}

typedef uint32_t (*DispatchFn)(uint32_t, uint32_t);
#define DISPATCH_FN(n, k) \
    __attribute__((noinline, noclone)) static uint32_t dispatch_##n(uint32_t v, uint32_t i) { \
        return (v ^ (i * (0x9E3779B9u + (k)))) + (0x85EBCA6Bu ^ (k)); \
    }
DISPATCH_FN(0, 0x00000011u)
DISPATCH_FN(1, 0x00000023u)
DISPATCH_FN(2, 0x00000035u)
DISPATCH_FN(3, 0x00000047u)
DISPATCH_FN(4, 0x00000059u)
DISPATCH_FN(5, 0x0000006Bu)
DISPATCH_FN(6, 0x0000007Du)
DISPATCH_FN(7, 0x0000008Fu)
static DispatchFn volatile g_dispatch[8] = {
    dispatch_0, dispatch_1, dispatch_2, dispatch_3,
    dispatch_4, dispatch_5, dispatch_6, dispatch_7
};

__attribute__((noinline, noclone))
static uint32_t jb_switch64(uint32_t selector, uint32_t value) {
    switch (selector & 63u) {
#define CASE(n) case n: return value + (0x9E3779B9u * ((n) + 1u)) ^ (0x85EBCA6Bu + (n))
        CASE(0); CASE(1); CASE(2); CASE(3); CASE(4); CASE(5); CASE(6); CASE(7);
        CASE(8); CASE(9); CASE(10); CASE(11); CASE(12); CASE(13); CASE(14); CASE(15);
        CASE(16); CASE(17); CASE(18); CASE(19); CASE(20); CASE(21); CASE(22); CASE(23);
        CASE(24); CASE(25); CASE(26); CASE(27); CASE(28); CASE(29); CASE(30); CASE(31);
        CASE(32); CASE(33); CASE(34); CASE(35); CASE(36); CASE(37); CASE(38); CASE(39);
        CASE(40); CASE(41); CASE(42); CASE(43); CASE(44); CASE(45); CASE(46); CASE(47);
        CASE(48); CASE(49); CASE(50); CASE(51); CASE(52); CASE(53); CASE(54); CASE(55);
        CASE(56); CASE(57); CASE(58); CASE(59); CASE(60); CASE(61); CASE(62); CASE(63);
#undef CASE
        default: return value;
    }
}

static void jb_initialize_integer_table(void) {
    for (uint32_t i = 0; i < TABLE_ENTRIES; i++)
        g_intTable[i] = 0x9E3779B9u ^ (i * 0x85EBCA6Bu) ^ (i * i << 7);
}

static void jb_initialize(void) {
    jb_initialize_integer_table();
    for (uint32_t i = 0; i < BUFFER_WORDS; i++)
        g_source[i] = 0xA5A5A5A5u ^ (i * 0x9E3779B9u) ^ (i >> 3);
    for (uint32_t i = 0; i < MATH_COUNT; i++) {
        g_mathA[i] = 1.0 + (double)(i % 97u) * 0.03125;
        g_mathB[i] = 0.75 + (double)(i % 53u) * 0.015625;
        g_mathC[i] = 0.125 + (double)(i % 29u) * 0.0078125;
    }
}

static uint64_t jb_run(uint32_t passes) {
    uint64_t checksum = 0x243F6A8885A308D3ull;
    uint32_t integerHash = 0x243F6A88u;
    uint64_t integerWideState = 0x13198A2E03707344ull;
    jb_initialize_integer_table();
    for (uint32_t pass = 0; pass < passes; pass++) {
        checksum ^= jb_integer_steps(MIX_INTEGER_STEPS, pass, &integerHash, &integerWideState);

        uint32_t memoryChecksum = 0;
        volatile uint32_t* destination = g_destination;
        for (uint32_t i = 0; i < BUFFER_WORDS; i++)
            destination[i] = g_source[i] ^ (0x01010101u * pass);
        for (uint32_t i = 0; i < BUFFER_BYTES; i++) {
            const volatile uint8_t* sourceBytes = (const volatile uint8_t*)g_source;
            const volatile uint8_t* destinationBytes = (const volatile uint8_t*)g_destination;
            uint8_t expected = sourceBytes[i] ^ (uint8_t)(0x01010101u * pass);
            if (destinationBytes[i] == expected) memoryChecksum++;
            else memoryChecksum ^= ((uint32_t)i << 8) | destinationBytes[i];
        }
        checksum += memoryChecksum;

        double arithmeticTotal = 0.0;
        double rootTotal = 0.0;
        uint32_t comparisons = 0;
        for (uint32_t i = 0; i < MATH_COUNT; i++) {
            double arithmetic = (g_mathA[i] * g_mathB[i] + g_mathC[i]) / (g_mathB[i] + 2.0);
            double root = jb_sqrt_newton(g_mathA[i] + g_mathC[i]);
            g_arithmetic[i] = arithmetic;
            g_roots[i] = root;
            arithmeticTotal += arithmetic;
            rootTotal += root;
            if (arithmetic > root) comparisons++;
            if (arithmetic < 4.0 && root > 1.0) comparisons++;
        }
        /* One explicitly rounded integer conversion for each double array. */
        uint64_t arithmeticInteger = (uint64_t)(arithmeticTotal * 1024.0 + 0.5);
        uint64_t rootInteger = (uint64_t)(rootTotal * 1024.0 + 0.5);
        checksum ^= arithmeticInteger + (rootInteger << 1) + comparisons;

        uint32_t dispatchValue = (uint32_t)checksum;
        for (uint32_t i = 0; i < 256u; i++)
            dispatchValue = g_dispatch[(i + pass) & 7u](dispatchValue, i);
        checksum += dispatchValue;

        uint32_t switchValue = (uint32_t)(checksum >> 32);
        for (uint32_t i = 0; i < 64u; i++)
            switchValue = jb_switch64(i + pass, switchValue);
        checksum ^= ((uint64_t)switchValue << 32) | memoryChecksum;
    }
    return checksum;
}

static void jb_hex(uint64_t value, char out[17]) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; i++) out[15u - i] = digits[(value >> (i * 4u)) & 0xFu];
    out[16] = '\0';
}

int main(void) {
#if defined(__i386__) || defined(_M_IX86)
    const char* arch = "x86";
#else
    const char* arch = "x64";
#endif
    LARGE_INTEGER frequency, start, end;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    jb_initialize();
    for (int sample = 0; sample < 4; sample++) {
        QueryPerformanceCounter(&start);
        uint64_t checksum = jb_run(JITBENCHMIX_PASSES);
        QueryPerformanceCounter(&end);
        char line[192], hex[17];
        jb_hex(checksum, hex);
        snprintf(line, sizeof(line), "BW_JITBENCHMIX arch=%s sample=%d ms=%.3f checksum=%s\n",
            arch, sample,
            (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)frequency.QuadPart,
            hex);
        OutputDebugStringA(line);
    }
    return 0;
}
