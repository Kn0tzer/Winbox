/*
 * Boxedwine64 live frame-stage metrics: public C ABI.
 *
 * This header is the only seam between the emulator/JIT owners and the live
 * metrics panel. See docs/reports/winbox-metrics-contract-20261018.md.
 *
 * Threading: all functions are safe to call from any guest worker thread.
 * Counters are monotonic; the registry keeps its own ~500 ms windows.
 */
#ifndef __BW64METRICS_H__
#define __BW64METRICS_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BW64_METRICS_VERSION 1
#define BW64_METRICS_ARCH32 32
#define BW64_METRICS_ARCH64 64

/* Bridge stages that are timed by the metrics owner. */
enum {
    BW64_STAGE_SYSCALL = 0,   /* 64-bit syscall dispatch, excluding GL/VK traps */
    BW64_STAGE_GL = 1,        /* gl64/gl32 guest->host trap wall */
    BW64_STAGE_AUDIO = 2,     /* DevDsp::writeNative copy/convert/push */
    BW64_STAGE_COUNT = 3
};

typedef struct Bw64JitStats {
    uint32_t structSize;              /* sizeof(Bw64JitStats) */
    const char* engine;               /* static lifetime, JSON-safe text */
    uint32_t available;               /* 1 = an engine published statistics */
    uint32_t enabled;                 /* 1 = dispatching into emitted code */
    uint64_t blocksEmitted;           /* emitted, not cache lookups */
    uint64_t blocksInstantiated;      /* module/instance ready to enter */
    uint64_t blocksEntered;           /* entries into emitted code */
    uint64_t instructionsRetired;     /* incremented from emitted code */
    uint64_t fallbacks;               /* unsupported-op / invalidation fallbacks */
    uint64_t emitMicroseconds;
    uint64_t instantiateMicroseconds;
} Bw64JitStats;

typedef struct Bw64VkFrame {
    uint64_t frameNo;
    double wallMs;          /* present-exit to present-exit */
    double submitMs;        /* vkQueueSubmit/2 handler wall */
    double trapOtherMs;     /* all other vk64 trap handlers */
    double presentMs;       /* vkQueuePresentKHR handler body */
    double hopMs;           /* worker->page chunk hops inside the frame */
    double bufMs;           /* subset of submit: buffer uploads */
    double imgMs;           /* subset of submit: image uploads */
    double guestMs;         /* wall - submit - trapOther - present */
    uint64_t trapCalls;
    uint64_t hopChunks;
    uint64_t hopBytes;
    uint32_t breakdown;     /* 1 = the per-stage fields above are measured */
    uint32_t arch;          /* 0 = 64-bit guest path, 1 = 32-bit guest path */
} Bw64VkFrame;

/* --- JIT owners implement only this call ------------------------------- */
void bw64_jit_stats_publish(int arch, const Bw64JitStats* stats);

/* --- instrumentation hooks (metrics owner) ----------------------------- */
int bw64_metrics_enabled(void);
double bw64_metrics_now_ms(void);
void bw64_metrics_add_stage(int stage, double millis, uint64_t bytes);
void bw64_metrics_vk_frame(const Bw64VkFrame* frame);
void bw64_metrics_set_enabled(int enabled);
void bw64_metrics_present_sdl(void);
void bw64_metrics_maybe_emit(void);

/* Test/diagnostic surface: serialise the current window without logging it. */
int bw64_metrics_format_json(char* out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* __BW64METRICS_H__ */
