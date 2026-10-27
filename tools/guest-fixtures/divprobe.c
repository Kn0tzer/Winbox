// divprobe: DIV/IDIV correctness battery + DIV-dense timing loop.
// The checksum must be identical under the interpreter and under the JIT
// (old JIT falls back on every DIV block; new JIT executes them). The
// timing loop's block is DIV-dense, so its rate exposes whether DIV
// blocks run as emitted code or fall back to the interpreter.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

int main(void) {
    uint64_t h = 0x123456789abcdef0ULL;
    for (uint64_t i = 1; i < 200000; i++) {
        uint64_t a = (i * 2654435761u) % 1000003;
        uint64_t d = (i % 977) + 1;
        h = mix(h, a / d); h = mix(h, a % d);
        uint32_t a32 = (uint32_t)(a * 31 + i);
        uint32_t d32 = (uint32_t)((i % 65521) + 1);
        h = mix(h, a32 / d32); h = mix(h, a32 % d32);
        int64_t sa = (int64_t)a - 500000;
        int64_t sd = (int64_t)(i % 89) - 44; if (!sd) sd = 7;
        h = mix(h, (uint64_t)(sa / sd)); h = mix(h, (uint64_t)(sa % sd));
        int32_t s32 = (int32_t)a32 - 100000;
        int32_t d32s = (int32_t)(i % 251) - 125; if (!d32s) d32s = 3;
        h = mix(h, (uint64_t)(int64_t)(s32 / d32s));
        h = mix(h, (uint64_t)(int64_t)(s32 % d32s));
        uint16_t a16 = (uint16_t)(a + i);
        uint16_t d16 = (uint16_t)((i % 251) + 1);
        h = mix(h, a16 / d16); h = mix(h, a16 % d16);
        int16_t s16 = (int16_t)(a16 - 30000);
        int16_t d16s = (int16_t)((i % 97) - 48); if (!d16s) d16s = 5;
        h = mix(h, (uint64_t)(int64_t)(s16 / d16s));
        h = mix(h, (uint64_t)(int64_t)(s16 % d16s));
        uint8_t a8 = (uint8_t)(a >> 3);
        uint8_t d8 = (uint8_t)((i % 13) + 1);
        h = mix(h, a8 / d8); h = mix(h, a8 % d8);
        int8_t s8 = (int8_t)(a8 - 100);
        int8_t d8s = (int8_t)((i % 11) - 5); if (!d8s) d8s = 2;
        h = mix(h, (uint64_t)(int64_t)(s8 / d8s));
        h = mix(h, (uint64_t)(int64_t)(s8 % d8s));
    }
    printf("DIVPROBE checksum %llu\n", (unsigned long long)h);
    fflush(stdout);

    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    uint64_t x = 0x9E3779B97F4A7C15ULL, s = 0;
    for (uint64_t k = 0; k < 3000000; k++) {
        uint64_t d = (x >> 40) % 1000 + 1;
        s += x / d;
        s += x % (d + 7);
        x = x * 2862933555777941757ULL + 3037000493ULL;
    }
    QueryPerformanceCounter(&t1);
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    printf("DIVPROBE timing %.1f ms sink %llu\n", ms, (unsigned long long)s);
    fflush(stdout);
    return 0;
}
