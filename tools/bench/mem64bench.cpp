// Native KMemory64 microbenchmark. From the repo root:
//   make -C project/linux x64 -j2
//   mkdir -p /tmp/winbox-finish/r2/work-memperf
//   objcopy --redefine-sym main=boxedwine_launcher_main "$(find project/linux/Build/X64Headless -type f -name knativesystem.cpp.o -print -quit)" /tmp/winbox-finish/r2/work-memperf/knativesystem-mem64bench.o
//   g++ -std=c++20 -O2 -DASMJIT_STATIC -DBOXEDWINE_64 -DBOXEDWINE_GUEST_X64=1 -DBOXEDWINE_MULTI_THREADED -DBOXEDWINE_DISABLE_UI -DBOXEDWINE_RECORDER -DBOXEDWINE_ZLIB -DBOXEDWINE_HAS_SETJMP -DSDL2=1 -DBOXEDWINE_POSIX -DSIMDE_SSE2_NO_NATIVE -Iinclude -Ilib/asmjit -Ilib/glew/include -Ilib/imgui -Ilib/simde -Ilib/pugixml/src tools/bench/mem64bench.cpp $(find project/linux/Build/X64Headless -type f -name '*.o' ! -path '*/platform/sdl/knativesystem.cpp.o') /tmp/winbox-finish/r2/work-memperf/knativesystem-mem64bench.o -o /tmp/mem64bench -Lproject/linux/linux_build/lib -lcurl -lssl -lcrypto -lpthread -lm -lz $(sdl2-config --libs)
//   /tmp/mem64bench
// This links the same KMemory64 object and build flags as the native x64 self-test.

#include "boxedwine.h"
#include "kmemory64.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {
constexpr U64 kBase = 0x700000000ULL;
constexpr U64 kBytes = 1ULL << 20;
constexpr U64 kQwords = kBytes / sizeof(U64);
constexpr U64 kPages = kBytes / K64_PAGE_SIZE;
constexpr U64 kIterations = 50'000'000;
constexpr U64 kRandomSamples = 1ULL << 16;
volatile U64 g_sink = 0;

U64 nextRandom(U64& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

template<typename Fn>
void timePattern(const char* name, Fn&& fn) {
    const auto begin = std::chrono::steady_clock::now();
    const U64 checksum = fn();
    const auto end = std::chrono::steady_clock::now();
    g_sink = g_sink ^ checksum;
    const double ns = std::chrono::duration<double, std::nano>(end - begin).count();
    std::printf("%-22s %.3f ns/op (%llu ops)\n", name, ns / kIterations,
                static_cast<unsigned long long>(kIterations));
}
} // namespace

int main() {
    KMemory64 memory(nullptr);
    if (memory.mmapAnonymousFixed(kBase, kBytes, 3) != kBase) {
        std::fprintf(stderr, "failed to reserve benchmark guest memory\n");
        return 1;
    }

    // Precommit all pages and warm the diagnostic gates outside timed loops.
    for (U64 i = 0; i < kQwords; ++i) memory.writeq(kBase + i * sizeof(U64), i + 1);

    std::array<U64, kRandomSamples> offsets{};
    U64 randomState = 0x4d595df4d0f33173ULL;
    for (U64& offset : offsets) offset = nextRandom(randomState) & (kQwords - 1);

    timePattern("sequential read", [&] {
        U64 sum = 0;
        for (U64 i = 0; i < kIterations; ++i)
            sum += memory.readq(kBase + (i & (kQwords - 1)) * sizeof(U64));
        return sum;
    });
    timePattern("sequential write", [&] {
        for (U64 i = 0; i < kIterations; ++i)
            memory.writeq(kBase + (i & (kQwords - 1)) * sizeof(U64), i ^ 0x9e3779b97f4a7c15ULL);
        return memory.readq(kBase);
    });
    timePattern("random read (1 MiB)", [&] {
        U64 sum = 0;
        for (U64 i = 0; i < kIterations; ++i)
            sum += memory.readq(kBase + offsets[i & (kRandomSamples - 1)] * sizeof(U64));
        return sum;
    });
    timePattern("random write (1 MiB)", [&] {
        for (U64 i = 0; i < kIterations; ++i)
            memory.writeq(kBase + offsets[i & (kRandomSamples - 1)] * sizeof(U64), i + 0x1234);
        return memory.readq(kBase);
    });
    timePattern("page-cross read", [&] {
        U64 sum = 0;
        for (U64 i = 0; i < kIterations; ++i) {
            const U64 addr = kBase + (i & (kPages - 1)) * K64_PAGE_SIZE + K64_PAGE_SIZE - 4;
            sum += memory.readq(addr);
        }
        return sum;
    });
    timePattern("page-cross write", [&] {
        for (U64 i = 0; i < kIterations; ++i) {
            const U64 addr = kBase + (i & (kPages - 1)) * K64_PAGE_SIZE + K64_PAGE_SIZE - 4;
            memory.writeq(addr, i ^ 0xd1b54a32d192ed03ULL);
        }
        return memory.readq(kBase + K64_PAGE_SIZE - 4);
    });

    std::printf("checksum: %llu\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
