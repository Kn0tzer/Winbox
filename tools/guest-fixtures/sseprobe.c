// sseprobe: SSE move/PXOR/MOVD correctness battery + SSE-dense timing loop.
// Companion to divprobe.c. The checksum must be identical under the
// interpreter and under the JIT (old JIT falls back on every block whose
// first non-fast op is an SSE form; the SSE fast paths execute them). The
// timing loop's blocks are SSE-dense (movdqa/movdqu loads, pxor, movdqa
// stores), so its rate exposes whether those blocks run as emitted code
// or fall back to the interpreter.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <emmintrin.h>

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t fold128(uint64_t h, __m128i v) {
    h = mix(h, (uint64_t)_mm_cvtsi128_si64(v));
    h = mix(h, (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(v, 8)));
    return h;
}

static uint64_t srcbuf[256];
static uint64_t dstbuf[256];
static uint64_t outbuf[256];

int main(void) {
    uint64_t x = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 256; i++) {
        x = x * 2862933555777941757ULL + 3037000493ULL;
        srcbuf[i] = x ^ (x >> 29);
    }

    uint64_t h = 0x123456789abcdef0ULL;
    const __m128i key = _mm_set_epi64x((long long)0x0F0E0D0C0B0A0908ULL,
                                       (long long)0x0706050403020100ULL);
    // 16-byte move + PXOR battery: aligned loads (movdqa), unaligned
    // stores (movdqu), unaligned reloads (movups via loadu), aligned
    // stores (movdqa), float-side moves (movaps/movups on __m128).
    for (int i = 0; i < 120; i++) {
        const __m128i *sp = (const __m128i *)(srcbuf + (i % 100) * 2);
        __m128i a = _mm_load_si128(sp);                       // movdqa load
        a = _mm_xor_si128(a, key);                            // pxor reg
        _mm_storeu_si128((__m128i *)(dstbuf + (i % 90) * 2 + 1), a); // movdqu store (odd qword offset)
        __m128i b = _mm_loadu_si128((const __m128i *)(dstbuf + (i % 90) * 2 + 1)); // movdqu load
        h = fold128(h, b);
        __m128 f = _mm_load_ps((const float *)(srcbuf + (i % 100) * 2));  // movaps load
        _mm_storeu_ps((float *)(outbuf + (i % 90) * 2 + 1), f);           // movups store
        __m128 g = _mm_loadu_ps((const float *)(outbuf + (i % 90) * 2 + 1)); // movups load
        h = fold128(h, _mm_castps_si128(g));
        _mm_store_si128((__m128i *)(outbuf + (i % 100) * 2), b);          // movdqa store
    }
    // MOVD/MOVQ battery: GPR <-> XMM transfers at both widths, memory forms.
    for (int i = 0; i < 2000; i++) {
        uint64_t v = srcbuf[i & 255];
        __m128i a = _mm_cvtsi64_si128((long long)v);          // movq load
        __m128i b = _mm_cvtsi32_si128((int)(v >> 17));        // movd load
        a = _mm_xor_si128(a, b);
        h = mix(h, (uint64_t)_mm_cvtsi128_si64(a));           // movq store
        h = mix(h, (uint32_t)_mm_cvtsi128_si32(a));           // movd store
        _mm_storeu_si128((__m128i *)(dstbuf + 200), a);
        h = mix(h, dstbuf[200]); h = mix(h, dstbuf[201]);
    }
    // Explicit pxor-with-memory-operand form (66 0F EF /r with a memory
    // source): the single-instruction shape compilers emit for xor-with-
    // constant-table loops.
    for (int i = 0; i < 2000; i++) {
        __m128i a = _mm_load_si128((const __m128i *)(srcbuf + (i & 127) * 2));
        __asm__ volatile("pxor %1, %0" : "+x"(a) : "m"(*(const __m128i *)(srcbuf + 128)));
        h = fold128(h, a);
    }
    printf("SSEPROBE checksum %llu\n", (unsigned long long)h);
    fflush(stdout);

    LARGE_INTEGER fq, t0, t1;
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&t0);
    __m128i acc = _mm_setzero_si128();
    for (uint64_t k = 0; k < 4000000; k++) {
        __m128i v = _mm_load_si128((const __m128i *)(srcbuf + (k & 127) * 2)); // movdqa load
        v = _mm_xor_si128(v, acc);                                             // pxor
        acc = _mm_xor_si128(acc, v);                                          // pxor
        _mm_store_si128((__m128i *)(dstbuf + (k & 127) * 2), v);               // movdqa store
        __m128i w = _mm_loadu_si128((const __m128i *)((const char *)srcbuf + 3 + (k & 63))); // movdqu load
        acc = _mm_xor_si128(acc, w);                                          // pxor
    }
    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart;
    uint64_t sink = (uint64_t)_mm_cvtsi128_si64(acc) +
                    (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(acc, 8));
    printf("SSEPROBE timing %.1f ms sink %llu\n", ms, (unsigned long long)sink);
    fflush(stdout);
    return 0;
}
