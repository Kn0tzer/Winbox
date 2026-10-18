/*
 * Boxedwine64 live frame-stage metrics registry.
 *
 * Accumulates the stages that are genuinely observable on the host and emits a
 * single JSON line every ~500 ms while frames or bridge traffic are active:
 *
 *     bw64: METRICS {"v":1,...}
 *
 * The line is forwarded by the existing print hook (project/emscripten/
 * wine64-launcher.js -> window.bwRuntimeLog) and parsed by web/app.mjs. It is
 * drained from the visible log pane on purpose.
 *
 * Deliberate limits (see docs/reports/winbox-metrics-contract-20261018.md):
 *   - guestMs is a WALL SPAN inside a Vulkan present-to-present window, not OS
 *     process CPU accounting;
 *   - Vulkan buf/img times are subsets of submit, and inclusive stages overlap,
 *     so nothing here is summed into a "total";
 *   - Wine/DXVK/VKD3D run as guest code and are reported unavailable, never
 *     inferred from the host-visible bridge times;
 *   - GPU execution time is page-tier only (WebGPU timestamp queries). Submit
 *     time is never substituted for it.
 *
 * All hooks are cheap when disabled: one relaxed atomic load.
 */
#include "bw64metrics.h"

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
#ifdef BOXEDWINE_GUEST_X64
void jit64PublishMetrics();
#endif
#ifdef BOXEDWINE_WASM_JIT
void wasmJit32PublishMetrics();
#endif
static void refreshJitMetrics() {
#ifdef BOXEDWINE_GUEST_X64
    jit64PublishMetrics();
#endif
#ifdef BOXEDWINE_WASM_JIT
    wasmJit32PublishMetrics();
#endif
}
#else
static void refreshJitMetrics() {}
#endif

// include/log.h declares this; keeping the TU light avoids pulling BString and
// the whole kernel into a file called from hot paths.
void klog(const char* msg);

#if defined(BOXEDWINE_GUEST_X64)
#define BW64_METRICS_ARCH_TEXT "x86-64"
#ifdef BOXEDWINE_WASM_JIT
#define BW64_METRICS_CPU32_SUPPORTED 1
#else
#define BW64_METRICS_CPU32_SUPPORTED 0
#endif
#define BW64_METRICS_CPU64_SUPPORTED 1
#else
#define BW64_METRICS_ARCH_TEXT "x86"
#define BW64_METRICS_CPU32_SUPPORTED 1
#define BW64_METRICS_CPU64_SUPPORTED 0
#endif

#ifdef BOXEDWINE_VULKAN64
#define BW64_METRICS_VK_SUPPORTED 1
#else
#define BW64_METRICS_VK_SUPPORTED 0
#endif

#ifdef BOXEDWINE_OPENGL
#define BW64_METRICS_GL_SUPPORTED 1
#else
#define BW64_METRICS_GL_SUPPORTED 0
#endif

#ifdef BOXEDWINE_GUEST_X64
#define BW64_METRICS_SYSCALL_SUPPORTED 1
#else
#define BW64_METRICS_SYSCALL_SUPPORTED 0
#endif

namespace {

constexpr uint64_t kEmitIntervalUs = 500000;   // ~2 snapshots/s
constexpr uint64_t kStaleWindowUs = 2000000;   // drop a window after a long stall

inline void addRelaxed(std::atomic<uint64_t>& a, uint64_t v) {
    if (!v) return;
    a.fetch_add(v, std::memory_order_relaxed);
}

inline uint64_t takeRelaxed(std::atomic<uint64_t>& a) {
    return a.exchange(0, std::memory_order_relaxed);
}

struct StageAcc {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> us{0};
    std::atomic<uint64_t> bytes{0};
};

StageAcc g_stage[BW64_STAGE_COUNT];

std::atomic<uint64_t> g_presentVk{0};
std::atomic<uint64_t> g_presentVk32{0};
std::atomic<uint64_t> g_presentSdl{0};

// 32-bit guest Vulkan path (vk32Bridge): only the present boundary and the
// present-to-present wall are measurable there, because that path has no
// per-trap timers. Everything else stays explicitly unavailable rather than 0.
std::atomic<uint64_t> g_vk32Frames{0};
std::atomic<uint64_t> g_vk32WallUs{0};

// Vulkan frame-window breakdown (fed from the existing BW64_VKPROF hooks).
std::atomic<uint64_t> g_vkFrames{0};
std::atomic<uint64_t> g_vkWallUs{0};
std::atomic<uint64_t> g_vkSubmitUs{0};
std::atomic<uint64_t> g_vkTrapOtherUs{0};
std::atomic<uint64_t> g_vkPresentUs{0};
std::atomic<uint64_t> g_vkHopUs{0};
std::atomic<uint64_t> g_vkBufUs{0};
std::atomic<uint64_t> g_vkImgUs{0};
std::atomic<uint64_t> g_vkGuestUs{0};
std::atomic<uint64_t> g_vkTrapCalls{0};
std::atomic<uint64_t> g_vkHopChunks{0};
std::atomic<uint64_t> g_vkHopBytes{0};

std::atomic<uint64_t> g_lastEmitUs{0};
std::atomic<int> g_emitLock{0};
std::atomic<int> g_enabled{-1};

// JIT owners publish monotonic counters; the panel distinguishes "no hook in
// this build" (available=0) from "compiled in but disabled" (available=1,
// enabled=0).
struct JitSlot {
    std::atomic<uint64_t> available{0};
    std::atomic<uint64_t> enabled{0};
    std::atomic<uint64_t> blocksEmitted{0};
    std::atomic<uint64_t> blocksInstantiated{0};
    std::atomic<uint64_t> blocksEntered{0};
    std::atomic<uint64_t> instructionsRetired{0};
    std::atomic<uint64_t> fallbacks{0};
    std::atomic<uint64_t> emitUs{0};
    std::atomic<uint64_t> instantiateUs{0};
    char engine[40] = {0};
};
JitSlot g_jit[2];
std::atomic<int> g_jitLock{0};

struct Spin {
    std::atomic<int>& lock;
    explicit Spin(std::atomic<int>& l) : lock(l) {
        while (lock.exchange(1, std::memory_order_acquire)) {
            // Publishers are rare; the emit path is a few hundred microseconds.
        }
    }
    ~Spin() { lock.store(0, std::memory_order_release); }
};

inline double usToMs(uint64_t us) { return (double)us / 1000.0; }

inline uint64_t millisToUs(double ms) {
    if (!(ms > 0.0)) return 0;
    double us = ms * 1000.0;
    if (us > 9.0e15) us = 9.0e15;
    return (uint64_t)(us + 0.5);
}

void sanitizeEngine(const char* in, char* out, size_t cap) {
    if (!out || cap == 0) return;
    size_t o = 0;
    if (in) {
        for (const char* p = in; *p && o + 1 < cap; p++) {
            unsigned char c = (unsigned char)*p;
            out[o++] = (std::isalnum(c) || c == ' ' || c == '_' || c == '-' ||
                        c == '.' || c == ':' || c == '/') ? (char)c : '_';
        }
    }
    out[o] = 0;
}

void resetWindow() {
    for (int i = 0; i < BW64_STAGE_COUNT; i++) {
        takeRelaxed(g_stage[i].calls);
        takeRelaxed(g_stage[i].us);
        takeRelaxed(g_stage[i].bytes);
    }
    takeRelaxed(g_presentVk);
    takeRelaxed(g_presentVk32);
    takeRelaxed(g_presentSdl);
    takeRelaxed(g_vk32Frames);
    takeRelaxed(g_vk32WallUs);
    takeRelaxed(g_vkFrames);
    takeRelaxed(g_vkWallUs);
    takeRelaxed(g_vkSubmitUs);
    takeRelaxed(g_vkTrapOtherUs);
    takeRelaxed(g_vkPresentUs);
    takeRelaxed(g_vkHopUs);
    takeRelaxed(g_vkBufUs);
    takeRelaxed(g_vkImgUs);
    takeRelaxed(g_vkGuestUs);
    takeRelaxed(g_vkTrapCalls);
    takeRelaxed(g_vkHopChunks);
    takeRelaxed(g_vkHopBytes);
}

// JSON writer with fixed capacity; truncates rather than overflowing.
struct Jw {
    char* out;
    size_t cap;
    size_t len = 0;
    Jw(char* o, size_t c) : out(o), cap(c) { if (cap) out[0] = 0; }
    void raw(const char* s) {
        while (s && *s && len + 1 < cap) out[len++] = *s++;
        if (cap) out[len] = 0;
    }
    void fmt(const char* f, ...) {
        if (len + 1 >= cap) return;
        va_list ap;
        va_start(ap, f);
        int n = vsnprintf(out + len, cap - len, f, ap);
        va_end(ap);
        if (n > 0) len += (size_t)n < (cap - len) ? (size_t)n : (cap - len - 1);
    }
};

// Small wrapper so every number is finite and JSON-legal.
inline double finiteOr0(double v) { return (v > 0.0 && v < 1.0e12) ? v : 0.0; }

struct WindowSnapshot {
    uint64_t winUs = 0;
    uint64_t presentVk = 0, presentVk32 = 0, presentSdl = 0;
    uint64_t vk32Frames = 0, vk32WallUs = 0;
    uint64_t stageCalls[BW64_STAGE_COUNT] = {};
    uint64_t stageUs[BW64_STAGE_COUNT] = {};
    uint64_t stageBytes[BW64_STAGE_COUNT] = {};
    uint64_t vkFrames = 0, vkWallUs = 0, vkSubmitUs = 0, vkTrapOtherUs = 0;
    uint64_t vkPresentUs = 0, vkHopUs = 0, vkBufUs = 0, vkImgUs = 0, vkGuestUs = 0;
    uint64_t vkTrapCalls = 0, vkHopChunks = 0, vkHopBytes = 0;
};

// Caller holds g_emitLock. Consumes the window.
void takeWindow(uint64_t nowUs, WindowSnapshot& w) {
    uint64_t last = g_lastEmitUs.exchange(nowUs, std::memory_order_relaxed);
    w.winUs = (last && nowUs > last) ? nowUs - last : 0;
    w.presentVk = takeRelaxed(g_presentVk);
    w.presentVk32 = takeRelaxed(g_presentVk32);
    w.presentSdl = takeRelaxed(g_presentSdl);
    w.vk32Frames = takeRelaxed(g_vk32Frames);
    w.vk32WallUs = takeRelaxed(g_vk32WallUs);
    for (int i = 0; i < BW64_STAGE_COUNT; i++) {
        w.stageCalls[i] = takeRelaxed(g_stage[i].calls);
        w.stageUs[i] = takeRelaxed(g_stage[i].us);
        w.stageBytes[i] = takeRelaxed(g_stage[i].bytes);
    }
    w.vkFrames = takeRelaxed(g_vkFrames);
    w.vkWallUs = takeRelaxed(g_vkWallUs);
    w.vkSubmitUs = takeRelaxed(g_vkSubmitUs);
    w.vkTrapOtherUs = takeRelaxed(g_vkTrapOtherUs);
    w.vkPresentUs = takeRelaxed(g_vkPresentUs);
    w.vkHopUs = takeRelaxed(g_vkHopUs);
    w.vkBufUs = takeRelaxed(g_vkBufUs);
    w.vkImgUs = takeRelaxed(g_vkImgUs);
    w.vkGuestUs = takeRelaxed(g_vkGuestUs);
    w.vkTrapCalls = takeRelaxed(g_vkTrapCalls);
    w.vkHopChunks = takeRelaxed(g_vkHopChunks);
    w.vkHopBytes = takeRelaxed(g_vkHopBytes);
}

void writeJit(Jw& j, const char* key, JitSlot& s, bool supported) {
    char engine[sizeof(s.engine)];
    {
        Spin lock(g_jitLock);
        memcpy(engine, s.engine, sizeof(engine));
    }
    uint64_t available = s.available.load(std::memory_order_relaxed);
    supported = supported || available; // an explicit publisher can provide a backend
    j.fmt("\"%s\":{\"supported\":%s,\"available\":%s,\"engine\":", key,
          supported ? "true" : "false", available ? "true" : "false");
    if (engine[0]) { j.raw("\""); j.raw(engine); j.raw("\""); }
    else j.raw("null");
    j.fmt(",\"enabled\":%s,\"blocksEmitted\":%llu,\"blocksInstantiated\":%llu,"
          "\"blocksEntered\":%llu,\"instructionsRetired\":%llu,\"fallbacks\":%llu,"
          "\"emitUs\":%llu,\"instantiateUs\":%llu}",
          s.enabled.load(std::memory_order_relaxed) ? "true" : "false",
          (unsigned long long)s.blocksEmitted.load(std::memory_order_relaxed),
          (unsigned long long)s.blocksInstantiated.load(std::memory_order_relaxed),
          (unsigned long long)s.blocksEntered.load(std::memory_order_relaxed),
          (unsigned long long)s.instructionsRetired.load(std::memory_order_relaxed),
          (unsigned long long)s.fallbacks.load(std::memory_order_relaxed),
          (unsigned long long)s.emitUs.load(std::memory_order_relaxed),
          (unsigned long long)s.instantiateUs.load(std::memory_order_relaxed));
}

void writeWindowJson(Jw& j, const WindowSnapshot& w, uint64_t nowUs) {
    double winMs = usToMs(w.winUs);
    // One path owns the FPS number for this window; the others are still shown
    // separately. Preference: 64-bit Vulkan, then 32-bit Vulkan, then the SDL
    // (OpenGL / X11-wire) present sink.
    uint64_t presentCount = w.presentVk ? w.presentVk : (w.presentVk32 ? w.presentVk32 : w.presentSdl);
    const char* path = w.presentVk ? "vk" : (w.presentVk32 ? "vk32" : (w.presentSdl ? "sdl" : "none"));
    // A rate is only meaningful over a real window. Under kMinRateWindowUs
    // (frame 1 of a session, or a snapshot taken by hand) the counters are
    // reported and fps/frameMs are null — a rate computed from a 0.03 ms window
    // would be a fabricated number, not a measurement.
    const bool rateValid = presentCount > 0 && winMs >= 100.0;
    double fps = rateValid ? (double)presentCount * 1000.0 / winMs : 0.0;
    double frameMs = rateValid ? winMs / (double)presentCount : 0.0;

    j.fmt("{\"v\":%d,\"uptimeMs\":%.0f,\"winMs\":%.1f,\"arch\":\"%s\",\"enabled\":true,",
          BW64_METRICS_VERSION, usToMs(nowUs), winMs, BW64_METRICS_ARCH_TEXT);
    j.raw("\"present\":{");
    j.fmt("\"vk\":%llu,\"vk32\":%llu,\"sdl\":%llu,\"count\":%llu,\"fps\":",
          (unsigned long long)w.presentVk, (unsigned long long)w.presentVk32,
          (unsigned long long)w.presentSdl, (unsigned long long)presentCount);
    if (rateValid) j.fmt("%.1f", finiteOr0(fps)); else j.raw("null");
    j.raw(",\"frameMs\":");
    if (rateValid) j.fmt("%.2f", finiteOr0(frameMs)); else j.raw("null");
    j.fmt(",\"path\":\"%s\"},", path);

    j.fmt("\"vk\":{\"supported\":%s,\"frames\":%llu,\"wallMs\":%.2f,"
          "\"submitMs\":%.2f,\"trapOtherMs\":%.2f,\"presentMs\":%.2f,\"hopMs\":%.2f,"
          "\"bufMs\":%.2f,\"imgMs\":%.2f,\"guestMs\":%.2f,\"trapCalls\":%llu,"
          "\"hopChunks\":%llu,\"hopBytes\":%llu},",
          BW64_METRICS_VK_SUPPORTED ? "true" : "false",
          (unsigned long long)w.vkFrames, finiteOr0(usToMs(w.vkWallUs)),
          finiteOr0(usToMs(w.vkSubmitUs)), finiteOr0(usToMs(w.vkTrapOtherUs)),
          finiteOr0(usToMs(w.vkPresentUs)), finiteOr0(usToMs(w.vkHopUs)),
          finiteOr0(usToMs(w.vkBufUs)), finiteOr0(usToMs(w.vkImgUs)),
          finiteOr0(usToMs(w.vkGuestUs)),
          (unsigned long long)w.vkTrapCalls, (unsigned long long)w.vkHopChunks,
          (unsigned long long)w.vkHopBytes);

    // 32-bit guest path: measured present count + wall span only.
    j.fmt("\"vk32\":{\"supported\":%s,\"frames\":%llu,\"wallMs\":%.2f,"
          "\"breakdown\":false,"
          "\"reason\":\"32-bit guest Vulkan presents have no per-trap timers; submit/trap/hop and the guest span are unavailable\"},", 
          BW64_METRICS_VK_SUPPORTED ? "true" : "false",
          (unsigned long long)w.vk32Frames, finiteOr0(usToMs(w.vk32WallUs)));

    const bool supported[BW64_STAGE_COUNT] = {
        BW64_METRICS_SYSCALL_SUPPORTED != 0,
        BW64_METRICS_GL_SUPPORTED != 0,
        true   // audio bridge is part of every browser build
    };
    const char* names[BW64_STAGE_COUNT] = { "syscall", "gl", "audio" };
    j.raw("\"stages\":{");
    for (int i = 0; i < BW64_STAGE_COUNT; i++) {
        if (i) j.raw(",");
        j.fmt("\"%s\":{\"supported\":%s,\"calls\":%llu,\"ms\":%.2f",
              names[i], supported[i] ? "true" : "false",
              (unsigned long long)w.stageCalls[i], finiteOr0(usToMs(w.stageUs[i])));
        if (i == BW64_STAGE_AUDIO) j.fmt(",\"bytes\":%llu", (unsigned long long)w.stageBytes[i]);
        j.raw("}");
    }
    j.raw("},");

    j.raw("\"jit\":{");
    writeJit(j, "cpu32", g_jit[0], BW64_METRICS_CPU32_SUPPORTED != 0);
    j.raw(",");
    writeJit(j, "cpu64", g_jit[1], BW64_METRICS_CPU64_SUPPORTED != 0);
    j.raw("},");

    j.raw("\"unavailable\":["
          "{\"id\":\"wine\",\"reason\":\"guest Win32/ELF code runs inside the emulated CPU; no host boundary\"},"
          "{\"id\":\"dxvk\",\"reason\":\"guest-side D3D translation; only its Vulkan calls are host-visible\"},"
          "{\"id\":\"vkd3d\",\"reason\":\"guest-side D3D12 translation; no host-visible stage boundary\"}"
          "]}");
}

// Caller holds g_emitLock.
int formatJsonLocked(char* out, size_t capacity, uint64_t nowUs) {
    if (!out || capacity < 64) return 0;
    WindowSnapshot w;
    takeWindow(nowUs, w);
    Jw j(out, capacity);
    writeWindowJson(j, w, nowUs);
    return (int)j.len;
}

} // namespace

extern "C" {

int bw64_metrics_enabled(void) {
    int v = g_enabled.load(std::memory_order_relaxed);
    if (v < 0) {
        const char* env = getenv("BW64_METRICS");
        v = (!env || strcmp(env, "0") != 0) ? 1 : 0;
        g_enabled.store(v, std::memory_order_relaxed);
    }
    return v;
}

double bw64_metrics_now_ms(void) {
#ifdef __EMSCRIPTEN__
    return emscripten_get_now();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

void bw64_jit_stats_publish(int arch, const Bw64JitStats* stats) {
    int slot = arch == BW64_METRICS_ARCH64 ? 1 : (arch == BW64_METRICS_ARCH32 ? 0 : -1);
    if (slot < 0) return;
    JitSlot& s = g_jit[slot];
    if (!stats || stats->structSize != sizeof(Bw64JitStats)) {
        s.available.store(0, std::memory_order_relaxed);
        s.enabled.store(0, std::memory_order_relaxed);
        Spin lock(g_jitLock);
        sanitizeEngine(nullptr, s.engine, sizeof(s.engine));
        return;
    }
    s.available.store(stats->available ? 1 : 0, std::memory_order_relaxed);
    s.enabled.store(stats->enabled ? 1 : 0, std::memory_order_relaxed);
    s.blocksEmitted.store(stats->blocksEmitted, std::memory_order_relaxed);
    s.blocksInstantiated.store(stats->blocksInstantiated, std::memory_order_relaxed);
    s.blocksEntered.store(stats->blocksEntered, std::memory_order_relaxed);
    s.instructionsRetired.store(stats->instructionsRetired, std::memory_order_relaxed);
    s.fallbacks.store(stats->fallbacks, std::memory_order_relaxed);
    s.emitUs.store(stats->emitMicroseconds, std::memory_order_relaxed);
    s.instantiateUs.store(stats->instantiateMicroseconds, std::memory_order_relaxed);
    Spin lock(g_jitLock);
    sanitizeEngine(stats->engine, s.engine, sizeof(s.engine));
}

void bw64_metrics_add_stage(int stage, double millis, uint64_t bytes) {
    if (stage < 0 || stage >= BW64_STAGE_COUNT) return;
    if (!bw64_metrics_enabled()) return;
    // Flush BEFORE accounting: a stale/idle window reset must never discard the
    // event that triggered this call. The event lands in the next window.
    bw64_metrics_maybe_emit();
    StageAcc& a = g_stage[stage];
    addRelaxed(a.calls, 1);
    addRelaxed(a.us, millisToUs(millis));
    addRelaxed(a.bytes, bytes);
}

void bw64_metrics_vk_frame(const Bw64VkFrame* f) {
    if (!f || !bw64_metrics_enabled()) return;
    bw64_metrics_maybe_emit();
    if (f->arch == 1) {
        addRelaxed(g_presentVk32, 1);
        addRelaxed(g_vk32Frames, 1);
        addRelaxed(g_vk32WallUs, millisToUs(f->wallMs));
        return;
    }
    addRelaxed(g_presentVk, 1);
    addRelaxed(g_vkFrames, 1);
    addRelaxed(g_vkWallUs, millisToUs(f->wallMs));
    addRelaxed(g_vkSubmitUs, millisToUs(f->submitMs));
    addRelaxed(g_vkTrapOtherUs, millisToUs(f->trapOtherMs));
    addRelaxed(g_vkPresentUs, millisToUs(f->presentMs));
    addRelaxed(g_vkHopUs, millisToUs(f->hopMs));
    addRelaxed(g_vkBufUs, millisToUs(f->bufMs));
    addRelaxed(g_vkImgUs, millisToUs(f->imgMs));
    addRelaxed(g_vkGuestUs, millisToUs(f->guestMs));
    addRelaxed(g_vkTrapCalls, f->trapCalls);
    addRelaxed(g_vkHopChunks, f->hopChunks);
    addRelaxed(g_vkHopBytes, f->hopBytes);
}

// Launcher escape hatch (?metrics=0): the metrics timing hooks run on guest
// worker threads where window.location is not readable, so the main thread
// turns them off through this export.
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void bw64_metrics_set_enabled(int enabled) {
    g_enabled.store(enabled ? 1 : 0, std::memory_order_relaxed);
}

void bw64_metrics_present_sdl(void) {
    if (!bw64_metrics_enabled()) return;
    bw64_metrics_maybe_emit();
    addRelaxed(g_presentSdl, 1);
}

void bw64_metrics_maybe_emit(void) {
    if (!bw64_metrics_enabled()) return;
    uint64_t nowUs = millisToUs(bw64_metrics_now_ms());
    uint64_t last = g_lastEmitUs.load(std::memory_order_relaxed);
    if (last && nowUs > last && nowUs - last < kEmitIntervalUs) return;
    if (g_emitLock.exchange(1, std::memory_order_acquire)) return;

    uint64_t early = 0;   // snapshot for idle/stale gates
    bool emit = false;
    {
        // Re-check under the lock: another thread may have just emitted.
        last = g_lastEmitUs.load(std::memory_order_relaxed);
        uint64_t since = (last && nowUs > last) ? nowUs - last : 0;
        if (since >= kEmitIntervalUs || !last) {
            uint64_t vkPresents = g_presentVk.load(std::memory_order_relaxed);
            uint64_t vk32Presents = g_presentVk32.load(std::memory_order_relaxed);
            uint64_t sdlPresents = g_presentSdl.load(std::memory_order_relaxed);
            uint64_t vkFrames = g_vkFrames.load(std::memory_order_relaxed);
            uint64_t glCalls = g_stage[BW64_STAGE_GL].calls.load(std::memory_order_relaxed);
            uint64_t audioCalls = g_stage[BW64_STAGE_AUDIO].calls.load(std::memory_order_relaxed);
            bool frameActivity = vkPresents || vk32Presents || sdlPresents || vkFrames;
            bool bridgeActivity = glCalls || audioCalls;
            if (!last) {
                // First call: start the window, nothing to report yet.
                g_lastEmitUs.store(nowUs, std::memory_order_relaxed);
            } else if (since > kStaleWindowUs) {
                // Long idle gap (paused app, background tab): drop the stale
                // window. Reporting it would divide recent presents by the
                // entire stall; the next window is accurate again.
                resetWindow();
                g_lastEmitUs.store(nowUs, std::memory_order_relaxed);
            } else if (frameActivity || bridgeActivity) {
                emit = true;
                early = nowUs;
            } else {
                resetWindow();
                g_lastEmitUs.store(nowUs, std::memory_order_relaxed);
            }
        }
    }

    if (emit) {
        // One line, identifiable by the panel: klog() goes to stdout, which the
        // launcher forwards through window.bwRuntimeLog (print hook).
        static const char kPrefix[] = "bw64: METRICS ";
        char buf[6208];
        memcpy(buf, kPrefix, sizeof(kPrefix) - 1);
        refreshJitMetrics();
        int n = formatJsonLocked(buf + sizeof(kPrefix) - 1,
                                 sizeof(buf) - (sizeof(kPrefix) - 1), early);
        if (n > 0) {
            klog(buf);
        }
    }
    g_emitLock.store(0, std::memory_order_release);
}

int bw64_metrics_format_json(char* out, size_t capacity) {
    if (!out || capacity < 64) return 0;
    if (!bw64_metrics_enabled()) return 0;
    uint64_t nowUs = millisToUs(bw64_metrics_now_ms());
    if (g_emitLock.exchange(1, std::memory_order_acquire)) return 0;
    refreshJitMetrics();
    int n = formatJsonLocked(out, capacity, nowUs);
    g_emitLock.store(0, std::memory_order_release);
    return n;
}

} // extern "C"
