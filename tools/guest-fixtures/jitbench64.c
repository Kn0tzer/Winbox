/* jitbench64.c — deterministic 64-bit integer / branch / memory workload with
 * a printed checksum, for the CPU64 interpreter-vs-JIT comparison.
 *
 *   "jitbench64: checksum=<hex> work=<decimal>"
 *
 * Same source builds three ways and must print the SAME checksum for the same
 * pass count in all of them:
 *
 *   x86_64-linux-gnu-gcc -DJITBENCH_FREESTANDING  -> static ELF64, direct
 *       syscalls (write/exit_group), no libc. Runs natively on the host, under
 *       node boxedwine64-runelf.js --x64-run-elf, and under --x64-selftest-like
 *       harnesses. argv[1] may override the pass count.
 *   x86_64-w64-mingw32-gcc                      -> Win64 PE64 (mingw CRT
 *       printf). Runs under wine64 in the browser lane; the pass count comes
 *       from the JITBENCH_PASSES define or the default below.
 *
 * Why this shape: every operation is defined (unsigned wrap-around, explicit
 * narrowing) so the checksum cannot depend on compiler UB assumptions, and the
 * body is dominated by 64-bit register ALU (add/adc/sub/sbb/imul/shl/shr/xor),
 * 64-bit loads/stores, all six arithmetic condition-code families, a
 * call/ret + push/pop pair, and 8/32-bit widening loads (movzx/movsx) — the
 * exact surface the CPU64 JIT claims to handle. No division (interpreter-only
 * in CPU64: DIV/IDIV keep their trap path), no floating point, no SIMD, so the
 * measurement is not dominated by unimplemented paths.
 *
 * The checksum folds in the pass count, so a JIT/interpreter mismatch in the
 * loop trip count cannot hide behind a matching tail value.
 */
#include <stdint.h>
#ifdef JITBENCH_FREESTANDING
#define JB_NULL ((void*)0)
#else
#define JB_NULL NULL
#endif

#ifndef JITBENCH_PASSES
/* ~10-15M guest instructions: comfortably inside the run-elf harness's 64M
 * instruction cap while still long enough for warm JIT behaviour to dominate
 * the compile phase. */
#define JITBENCH_PASSES 4096u
#endif

#define TABLE_ENTRIES 64u
#define TABLE_MASK (TABLE_ENTRIES - 1u)

static uint64_t table[TABLE_ENTRIES];

/* --- platform shims ------------------------------------------------------- */

#ifdef JITBENCH_FREESTANDING
/* Direct Linux syscalls; no libc, no errno, no TLS. */
static long jb_sys3(long nr, long a, long b, long c) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}
static void jb_write(const char* text, unsigned long len) {
    jb_sys3(1 /*write*/, 1 /*stdout*/, (long)text, (long)len);
}
static void jb_exit(int code) {
    jb_sys3(60 /*exit_group*/, code, 0, 0);
    for (;;) {} /* not reached */
}
__asm__(".text\n.global _start\n.type _start,@function\n_start:\n"
        "  movq %rsp, %rdi\n"          /* initial stack: argc/argv/envp/auxv */
        "  andq $-16, %rsp\n"
        "  call jitbench_main\n"
        "  movl %eax, %edi\n"
        "  movl $60, %eax\n"            /* exit_group */
        "  syscall\n");
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static void jb_write(const char* text, unsigned long len) {
    fwrite(text, 1, (size_t)len, stdout);
    fflush(stdout);
}
#endif

/* --- helpers ------------------------------------------------------------- */

/* Out-of-line and not inlined away: this is the 64-bit call/ret + push/pop
 * pair under test (and it must not be constant-folded into the caller). */
__attribute__((noinline, noclone))
static uint64_t jb_mix(uint64_t hash, uint64_t value, uint32_t round) {
    uint64_t rotated = (hash << 13) | (hash >> 51);
    uint64_t product;
    /* 64-bit imul (the JIT's J64_IMUL_R_RM path). */
    product = (value | 1u) * 0x9E3779B97F4A7C15ull;
    hash ^= rotated;
    hash += product;
    /* 64-bit add-with-immediate + xor-immediate. */
    hash ^= (uint64_t)round * 0xC2B2AE3D27D4EB4Full;
    hash += 0x165667B19E3779F9ull;
    return hash;
}

/* 64-bit signed compare helper: exercises signed imul + signed compares +
 * conditional branches without any signed overflow. */
__attribute__((noinline, noclone))
static uint64_t jb_signed_step(int64_t value, int64_t bound) {
    uint64_t out = 0;
    int64_t scaled = value * 3;
    if (scaled > bound) out |= 1ull;          /* signed > */
    else if (scaled < -bound) out |= 2ull;    /* signed < */
    if (scaled == 0) out |= 4ull;             /* equal */
    /* unsigned comparisons on the same value, aliased through a cast */
    uint64_t asUnsigned = (uint64_t)scaled;
    if (asUnsigned > (uint64_t)bound) out |= 8ull;
    if ((asUnsigned & 0xFFFF000000000000ull) != 0) out |= 16ull;
    return out;
}

static void jb_itoa_hex(uint64_t value, char* out) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[15 - i] = digits[(value >> (4 * i)) & 0xF];
    }
    out[16] = '\0';
}

/* Out-params are plain globals: returning a struct by value can make GCC emit
 * a memcpy call, which -nostdlib freestanding builds do not have. */
static uint64_t g_checksum;
static uint64_t g_work;

__attribute__((noinline))
static void jb_run(uint32_t passes) {
    uint64_t hash = 0x243F6A8885A308D3ull; /* pi digits; anything fixed */
    uint64_t work = 0;

    /* Seed the table with a deterministic pattern (64-bit stores). */
    for (uint32_t i = 0; i < TABLE_ENTRIES; i++) {
        table[i] = 0x0123456789ABCDEFull ^ (uint64_t)i << 33;
        table[i] += (uint64_t)(i * i) << 7;
    }

    for (uint32_t pass = 0; pass < passes; pass++) {
        uint32_t index = pass & TABLE_MASK;
        /* An intentionally data-dependent chain: the next operand address and
           the branch direction both depend on the previous result, so nothing
           can be hoisted or vectorised out of the loop. */
        for (uint32_t step = 0; step < TABLE_ENTRIES; step++) {
            uint64_t current = table[index];
            uint64_t next = table[(index + 1u) & TABLE_MASK];
            uint64_t mixed = jb_mix(hash, current, step);
            uint64_t delta = current - next;             /* 64-bit sub (CF/OF live) */
            uint64_t sum = mixed + delta;                /* 64-bit add (CF/OF live) */
            uint64_t signedBits = jb_signed_step((int64_t)current, (int64_t)next);
            uint32_t widened = (uint32_t)(uint8_t)current;    /* movzx */
            int32_t narrowSigned = (int8_t)(current & 0xFFu); /* movsx */
            uint32_t highBits = (uint32_t)(current >> 40);
            uint32_t combined = (widened + 1u) ^ (uint32_t)narrowSigned;
            /* Data-dependent branches reaching every arithmetic cc family:
               unsigned above/below, equal, signed less -- plus their
               negations. */
            if (delta > current) {
                sum += 0x100000001ull;
            } else if (delta == 0) {
                sum ^= 0x200000001ull;
            } else if ((int64_t)delta < 0) {
                sum += 0x400000001ull;
            } else {
                sum -= 0x800000001ull;
            }
            if ((signedBits & 4ull) != 0) sum ^= 0x1000000001ull;
            if ((signedBits & 16ull) != 0) sum += 0x2000000001ull;
            /* 64-bit rotate by a runtime-variable amount (shift + or), which
               also exercises the SHL/SHR-by-CL paths. */
            uint64_t shift = (current & 63u);
            sum = (sum << shift) | (sum >> ((64u - shift) & 63u));
            sum ^= (uint64_t)combined << 16;
            sum += (uint64_t)highBits * 3ull;

            table[index] = sum ^ next;
            hash = jb_mix(hash, table[index], pass & 0xFFFFu);
            index = (index + 7u) & TABLE_MASK;
            work += 6ull; /* instructions retired, counted by the guest */
        }
        hash += (uint64_t)pass << 1;
    }

    /* Fold every table slot into the checksum through a different path so a
       single wrong store cannot cancel out. */
    for (uint32_t i = 0; i < TABLE_ENTRIES; i += 2) {
        uint64_t v = table[i];
        hash ^= v >> 17;
        hash += table[i + 1u];
        hash = jb_mix(hash, v, i);
    }
    hash ^= (uint64_t)passes * 0x100000001B3ull;
    hash += work;
    g_checksum = hash;
    g_work = work;
}

/* Decimal parser shared by both entry paths. */
static uint32_t jb_parse_passes(const char* text) {
    if (!text) return (uint32_t)JITBENCH_PASSES;
    uint64_t value = 0;
    int digits = 0;
    for (const char* p = text; *p >= '0' && *p <= '9'; p++) {
        value = value * 10u + (uint64_t)(*p - '0');
        digits++;
        if (value > 0xFFFFFFFFull) return (uint32_t)JITBENCH_PASSES;
    }
    if (digits == 0) return (uint32_t)JITBENCH_PASSES;
    return (uint32_t)value;
}

static void jb_report(uint32_t passes) {
    char line[96];
    unsigned long at = 0;
    const char* prefix = "jitbench64: checksum=";
    for (int i = 0; prefix[i]; i++) line[at++] = prefix[i];
    char hex[17];
    jb_itoa_hex(g_checksum, hex);
    for (int i = 0; i < 16; i++) line[at++] = hex[i];

    {
        const char* mid = " passes=";
        for (int i = 0; mid[i]; i++) line[at++] = mid[i];
        char reversed[12];
        unsigned digits = 0;
        uint32_t value = passes;
        if (value == 0) reversed[digits++] = '0';
        while (value) { reversed[digits++] = (char)('0' + (value % 10u)); value /= 10u; }
        while (digits) line[at++] = reversed[--digits];
    }
    {
        const char* tail = " work=";
        for (int i = 0; tail[i]; i++) line[at++] = tail[i];
        char reversed[24];
        unsigned digits = 0;
        uint64_t value = g_work;
        if (value == 0) reversed[digits++] = '0';
        while (value) { reversed[digits++] = (char)('0' + (int)(value % 10ull)); value /= 10ull; }
        while (digits) line[at++] = reversed[--digits];
    }
    line[at++] = '\n';
    jb_write(line, at);
}

/* --- entry points -------------------------------------------------------- */

#ifndef JITBENCH_FREESTANDING

int main(int argc, char** argv) {
    uint32_t passes = jb_parse_passes(argc >= 2 && argv ? argv[1] : (const char*)JB_NULL);
#ifdef _WIN32
    LARGE_INTEGER frequency, start, end;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    for (int sample = 0; sample < 4; sample++) {
        QueryPerformanceCounter(&start);
        jb_run(passes);
        QueryPerformanceCounter(&end);
        char line[256], checksum[17];
        jb_itoa_hex(g_checksum, checksum);
        snprintf(line, sizeof(line), "BW_JITBENCH64 sample=%d ms=%.3f checksum=%s passes=%u\n",
            sample, (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)frequency.QuadPart,
            checksum, (unsigned)passes);
        OutputDebugStringA(line);
    }
#else
    jb_run(passes);
#endif
    jb_report(passes);
    return 0;
}

#else /* JITBENCH_FREESTANDING: called from the _start stub */

int jitbench_main(long* initialStack) {
    long argc = initialStack[0];
    const char* const* argv = (const char* const*)(initialStack + 1);
    uint32_t passes = jb_parse_passes(argc >= 2 ? argv[1] : (const char*)JB_NULL);
    jb_run(passes);
    jb_report(passes);
    return 0;
}

#endif /* JITBENCH_FREESTANDING */
