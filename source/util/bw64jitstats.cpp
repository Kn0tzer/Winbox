// Drop-in JIT statistics helper: see include/bw64jitstats.h for the contract a
// JIT owner implements and docs/reports/winbox-metrics-contract-20261018.md for
// the schema the live panel consumes.
//
// This file owns the worker-shared atomics and the 250 ms publish throttle, so a
// JIT lane only adds counter calls at points it already owns.
#include "bw64jitstats.h"
#include "bw64metrics.h"

#include <cstring>

namespace bw64jit {

std::atomic<uint64_t> g_blocksEmitted[2];
std::atomic<uint64_t> g_blocksInstantiated[2];
std::atomic<uint64_t> g_blocksEntered[2];
std::atomic<uint64_t> g_instructions[2];
std::atomic<uint64_t> g_fallbacks[2];
std::atomic<uint64_t> g_emitUs[2];
std::atomic<uint64_t> g_instantiateUs[2];

namespace {

struct State {
    std::atomic<uint64_t> available{0};
    std::atomic<uint64_t> enabled{0};
    std::atomic<uint64_t> lastPublishUs{0};
    char engine[40] = {0};
};

State g_state[2];

inline void addUs(std::atomic<uint64_t>& a, double ms) {
    if (!(ms > 0.0)) return;
    double us = ms * 1000.0;
    if (us > 9.0e15) us = 9.0e15;
    a.fetch_add((uint64_t)(us + 0.5), std::memory_order_relaxed);
}

// Publish throttled by wall time; the caller already bumped a counter, so the
// check runs on the writer's thread with no extra synchronisation.
void maybePublish(int slot) {
    State& st = g_state[slot];
    uint64_t nowUs = (uint64_t)(bw64_metrics_now_ms() * 1000.0 + 0.5);
    uint64_t last = st.lastPublishUs.load(std::memory_order_relaxed);
    if (last && nowUs > last && nowUs - last < 250000ULL) return;
    if (!st.lastPublishUs.compare_exchange_strong(last, nowUs, std::memory_order_relaxed)) return;
    publish(slot == 0 ? ARCH32 : ARCH64);
}

} // namespace

void set_state(int arch, bool available, bool enabled, const char* engine) {
    int slot = slot_of(arch);
    State& st = g_state[slot];
    st.available.store(available ? 1 : 0, std::memory_order_relaxed);
    st.enabled.store(enabled ? 1 : 0, std::memory_order_relaxed);
    size_t o = 0;
    if (engine) {
        for (const char* p = engine; *p && o + 1 < sizeof(st.engine); p++) st.engine[o++] = *p;
    }
    st.engine[o] = 0;
    // Make the state visible immediately: "compiled in but disabled" must never
    // be mistaken for "no JIT hook in this build".
    publish(arch);
}

void publish(int arch) {
    int slot = slot_of(arch);
    State& st = g_state[slot];
    Bw64JitStats s;
    memset(&s, 0, sizeof(s));
    s.structSize = sizeof(s);
    s.engine = st.engine[0] ? st.engine : nullptr;
    s.available = (uint32_t)st.available.load(std::memory_order_relaxed);
    s.enabled = (uint32_t)st.enabled.load(std::memory_order_relaxed);
    s.blocksEmitted = g_blocksEmitted[slot].load(std::memory_order_relaxed);
    s.blocksInstantiated = g_blocksInstantiated[slot].load(std::memory_order_relaxed);
    s.blocksEntered = g_blocksEntered[slot].load(std::memory_order_relaxed);
    s.instructionsRetired = g_instructions[slot].load(std::memory_order_relaxed);
    s.fallbacks = g_fallbacks[slot].load(std::memory_order_relaxed);
    s.emitMicroseconds = g_emitUs[slot].load(std::memory_order_relaxed);
    s.instantiateMicroseconds = g_instantiateUs[slot].load(std::memory_order_relaxed);
    bw64_jit_stats_publish(arch, &s);
}

void add_blocks_emitted(int arch, uint64_t count, double emitMs) {
    int slot = slot_of(arch);
    g_blocksEmitted[slot].fetch_add(count, std::memory_order_relaxed);
    addUs(g_emitUs[slot], emitMs);
    maybePublish(slot);
}

void add_blocks_instantiated(int arch, uint64_t count, double instantiateMs) {
    int slot = slot_of(arch);
    g_blocksInstantiated[slot].fetch_add(count, std::memory_order_relaxed);
    addUs(g_instantiateUs[slot], instantiateMs);
    maybePublish(slot);
}

void add_fallback(int arch, uint64_t count) {
    int slot = slot_of(arch);
    g_fallbacks[slot].fetch_add(count, std::memory_order_relaxed);
    maybePublish(slot);
}

void flush(int arch) { publish(arch); }

void snapshot(int arch, Snapshot* out) {
    if (!out) return;
    int slot = slot_of(arch);
    State& st = g_state[slot];
    out->available = st.available.load(std::memory_order_relaxed);
    out->enabled = st.enabled.load(std::memory_order_relaxed);
    out->blocksEmitted = g_blocksEmitted[slot].load(std::memory_order_relaxed);
    out->blocksInstantiated = g_blocksInstantiated[slot].load(std::memory_order_relaxed);
    out->blocksEntered = g_blocksEntered[slot].load(std::memory_order_relaxed);
    out->instructionsRetired = g_instructions[slot].load(std::memory_order_relaxed);
    out->fallbacks = g_fallbacks[slot].load(std::memory_order_relaxed);
    out->emitUs = g_emitUs[slot].load(std::memory_order_relaxed);
    out->instantiateUs = g_instantiateUs[slot].load(std::memory_order_relaxed);
    out->engine = st.engine[0] ? st.engine : nullptr;
}

} // namespace bw64jit