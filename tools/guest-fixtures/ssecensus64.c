/* ssecensus64.c — reject-census workload for the JIT64 coverage frontier.
 *
 *   "ssecensus64: checksum=<hex>"
 *
 * Three hot loops, each dominated by one reject-census family:
 *   1. SSE-dense: movdqa/movdqu loads, pxor, movdqa stores (the SSE
 *      fast-path set). Before the SSE round every block in this loop
 *      fell back; after it, the blocks compile.
 *   2. FF /2: indirect calls through a function-pointer table.
 *   3. FF /0 and FF /4: an incl on a memory operand in a loop, and a
 *      dense switch (jump-table indirect jump) in a loop.
 * The runelf harness prints BW64JITSTATS after the run; loops 2 and 3
 * exist so the entry-weighted reject attribution (rejectEntryTop /
 * rejectSites) can convict or acquit the FF group-5 census entry with
 * per-RIP evidence instead of first-opcode-byte guesses.
 *
 * Freestanding build (static ELF64, direct syscalls, no libc):
 *   x86_64-linux-gnu-gcc -DSSECENSUS_FREESTANDING -O2 -static -nostdlib
 * The checksum is identical in every build and under interpreter vs JIT.
 */
#include <stdint.h>
#include <emmintrin.h>

#ifndef SSECENSUS_PASSES
#define SSECENSUS_PASSES 2000u
#endif

static uint64_t table[256];
static uint64_t g_checksum;

#ifdef SSECENSUS_FREESTANDING
static long sc_sys3(long nr, long a, long b, long c) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret)
                     : "a"(nr), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}
static void sc_write(const char* text, unsigned long len) {
    sc_sys3(1, 1, (long)text, (long)len);
}
__asm__(".text\n.global _start\n.type _start,@function\n_start:\n"
        "  movq %rsp, %rdi\n"
        "  andq $-16, %rsp\n"
        "  call ssecensus_main\n"
        "  movl %eax, %edi\n"
        "  movl $60, %eax\n"
        "  syscall\n");
#else
#include <stdio.h>
#include <string.h>
static void sc_write(const char* text, unsigned long len) {
    fwrite(text, 1, (size_t)len, stdout);
    fflush(stdout);
}
#endif

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

/* --- loop 1: SSE-dense -------------------------------------------------- */
__attribute__((noinline, noclone))
static uint64_t sse_loop(uint32_t passes) {
    __m128i acc = _mm_setzero_si128();
    for (uint32_t k = 0; k < passes * 512u; k++) {
        __m128i v = _mm_load_si128((const __m128i*)(table + (k & 127u) * 2));
        v = _mm_xor_si128(v, acc);
        acc = _mm_xor_si128(acc, v);
        _mm_store_si128((__m128i*)(table + (k & 127u) * 2), v);
        __m128i w = _mm_loadu_si128((const __m128i*)((const char*)table + 3 + (k & 63u)));
        acc = _mm_xor_si128(acc, w);
    }
    return (uint64_t)_mm_cvtsi128_si64(acc) +
           (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(acc, 8));
}

/* --- loop 2: FF /2 indirect calls --------------------------------------- */
typedef uint64_t (*step_fn)(uint64_t);
__attribute__((noinline, noclone)) static uint64_t step_a(uint64_t v) { return v * 2862933555777941757ull + 3037000493ull; }
__attribute__((noinline, noclone)) static uint64_t step_b(uint64_t v) { return (v ^ (v >> 29)) * 0x9E3779B97F4A7C15ull; }
__attribute__((noinline, noclone)) static uint64_t step_c(uint64_t v) { return v + 0x165667B19E3779F9ull; }
__attribute__((noinline, noclone)) static uint64_t step_d(uint64_t v) { return (v << 13) | (v >> 51); }
static step_fn const steps[4] = { step_a, step_b, step_c, step_d };

__attribute__((noinline, noclone))
static uint64_t ffcall_loop(uint32_t passes) {
    uint64_t v = 0x243F6A8885A308D3ull;
    for (uint32_t k = 0; k < passes * 256u; k++) {
        v = steps[k & 3u](v); /* FF /2 call *%rax through the table */
    }
    return v;
}

/* --- loop 3: FF /0 (incl m) and FF /4 (switch jump table) ---------------- */
static uint32_t g_counter;

__attribute__((noinline, noclone))
static uint64_t ffmisc_loop(uint32_t passes) {
    uint64_t h = 0x123456789abcdef0ull;
    for (uint32_t k = 0; k < passes * 256u; k++) {
        __asm__ volatile("incl %0" : "+m"(g_counter)); /* FF /0 */
        switch (g_counter & 7u) {                      /* FF /4 jump table */
        case 0: h = mix(h, 0x11); break;
        case 1: h = mix(h, 0x22); break;
        case 2: h = mix(h, 0x33); break;
        case 3: h = mix(h, 0x44); break;
        case 4: h = mix(h, 0x55); break;
        case 5: h = mix(h, 0x66); break;
        case 6: h = mix(h, 0x77); break;
        default: h = mix(h, 0x88); break;
        }
    }
    return h;
}

static void sc_itoa_hex(uint64_t value, char* out) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) out[15 - i] = digits[(value >> (4 * i)) & 0xF];
    out[16] = '\0';
}

static void run(uint32_t passes) {
    uint64_t x = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 256; i++) {
        x = x * 2862933555777941757ull + 3037000493ull;
        table[i] = x ^ (x >> 29);
    }
    uint64_t h = 0x243F6A8885A308D3ull;
    h = mix(h, sse_loop(passes));
    h = mix(h, ffcall_loop(passes));
    h = mix(h, ffmisc_loop(passes));
    h ^= (uint64_t)passes * 0x100000001B3ull;
    g_checksum = h;

    char line[64];
    unsigned long at = 0;
    const char* prefix = "ssecensus64: checksum=";
    for (int i = 0; prefix[i]; i++) line[at++] = prefix[i];
    char hex[17];
    sc_itoa_hex(g_checksum, hex);
    for (int i = 0; i < 16; i++) line[at++] = hex[i];
    line[at++] = '\n';
    sc_write(line, at);
}

#ifdef SSECENSUS_FREESTANDING
int ssecensus_main(long* initialStack) {
    (void)initialStack;
    run(SSECENSUS_PASSES);
    return 0;
}
#else
int main(void) {
    run(SSECENSUS_PASSES);
    return 0;
}
#endif
