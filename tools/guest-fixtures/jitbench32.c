/* jitbench32.c — deterministic CPU32 integer/branch workload for the WASM JIT.
 *
 * The timed loop follows jitbench64.c: one cold and three warm QueryPerformanceCounter
 * samples, with a checksum and pass count in every OutputDebugStringA record.
 * Unsigned wrap-around is intentional; signed arithmetic stays in range.
 */
#include <stdint.h>
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef JITBENCH32_PASSES
#define JITBENCH32_PASSES 4096u
#endif

#define TABLE_ENTRIES 64u
#define TABLE_MASK (TABLE_ENTRIES - 1u)

static uint32_t g_table[TABLE_ENTRIES];
static uint64_t g_checksum;

__attribute__((noinline, noclone))
static uint32_t jb32_mix(uint32_t hash, uint32_t value, uint32_t round) {
    uint32_t rotated = (hash << 11) | (hash >> 21);
    uint32_t product = (value | 1u) * 0x9E3779B9u;
    hash ^= rotated;
    hash += product;
    hash ^= round * 0x85EBCA6Bu;
    hash += 0xC2B2AE35u;
    return hash;
}

__attribute__((noinline, noclone))
static uint32_t jb32_signed_step(uint32_t left, uint32_t right) {
    int32_t value = (int32_t)(left & 0x1FFFFFFFu);
    int32_t bound = (int32_t)(right & 0x1FFFFFFFu);
    int32_t scaled = value * 3;
    uint32_t flags = 0;
    if (scaled > bound) flags |= 1u;
    else if (scaled < -bound) flags |= 2u;
    if (scaled == 0) flags |= 4u;
    if ((uint32_t)scaled > (uint32_t)bound) flags |= 8u;
    if ((uint32_t)scaled & 0x80000000u) flags |= 16u;
    return flags;
}

static void jb32_run(uint32_t passes) {
    uint32_t hash = 0x243F6A88u;
    uint32_t work = 0;
    uint64_t wideState = 0x13198A2E03707344ull;

    for (uint32_t i = 0; i < TABLE_ENTRIES; i++) {
        g_table[i] = 0x9E3779B9u ^ (i * 0x85EBCA6Bu);
        g_table[i] += (i * i) << 7;
    }

    for (uint32_t pass = 0; pass < passes; pass++) {
        uint32_t index = pass & TABLE_MASK;
        for (uint32_t step = 0; step < TABLE_ENTRIES; step++) {
            uint32_t current = g_table[index];
            uint32_t next = g_table[(index + 1u) & TABLE_MASK];
            uint32_t mixed = jb32_mix(hash, current, step);
            uint32_t delta = current - next;
            uint32_t sum = mixed + delta;
            uint32_t signedBits = jb32_signed_step(current, next);
            uint32_t widened = (uint32_t)(uint8_t)current;
            uint32_t narrowByte = current & 0xFFu;
            int32_t narrowSigned = narrowByte < 0x80u
                ? (int32_t)narrowByte : (int32_t)narrowByte - 256;
            uint32_t highBits = current >> 24;
            uint32_t combined = (widened + 1u) ^ (uint32_t)narrowSigned;

            if (delta > current) sum += 0x10000001u;
            else if (delta == 0) sum ^= 0x20000003u;
            else if (delta & 0x80000000u) sum += 0x40000007u;
            else sum -= 0x8000000Bu;
            if (signedBits & 4u) sum ^= 0x1000001Du;
            if (signedBits & 16u) sum += 0x20000021u;

            /* Keep 64-bit add/sub/variable-shift pairs in the i686 guest. */
            uint64_t wide = ((uint64_t)current << 32) | next;
            uint64_t other = ((uint64_t)g_table[(index + 7u) & TABLE_MASK] << 32)
                | g_table[(index + 13u) & TABLE_MASK];
            wide += other ^ 0x9E3779B97F4A7C15ull;
            wide -= (uint64_t)delta << 17;
            uint32_t shift = (current ^ step) & 63u;
            wide = (wide << shift) | (wide >> ((64u - shift) & 63u));
            wideState += wide ^ (wideState >> ((step & 31u) + 1u));
            sum ^= (uint32_t)(wideState ^ (wideState >> 32));
            sum ^= combined << 9;
            sum += highBits * 3u;

            g_table[index] = sum ^ next;
            hash = jb32_mix(hash, g_table[index], pass & 0xFFFFu);
            index = (index + 7u) & TABLE_MASK;
            work += 6u;
        }
        hash += pass << 1;
    }

    for (uint32_t i = 0; i < TABLE_ENTRIES; i += 2) {
        uint32_t value = g_table[i];
        hash ^= value >> 13;
        hash += g_table[i + 1u];
        hash = jb32_mix(hash, value, i);
    }
    g_checksum = ((uint64_t)hash << 32) | (uint32_t)(work ^ passes ^ (uint32_t)wideState);
}

int main(void) {
    LARGE_INTEGER frequency, start, end;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    for (int sample = 0; sample < 4; sample++) {
        QueryPerformanceCounter(&start);
        jb32_run(JITBENCH32_PASSES);
        QueryPerformanceCounter(&end);
        char line[192];
        snprintf(line, sizeof(line),
            "BW_JITBENCH32 sample=%d ms=%.3f checksum=%016llx passes=%u\n",
            sample,
            (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)frequency.QuadPart,
            (unsigned long long)g_checksum, (unsigned)JITBENCH32_PASSES);
        OutputDebugStringA(line);
    }
    return 0;
}
