/*
 * Drop-in JIT statistics helper for the live frame metrics panel.
 *
 * A JIT owner adds three kinds of call, and nothing else:
 *
 *   #include "bw64jitstats.h"
 *   bw64jit::set_state(bw64jit::ARCH64, true, enabledFlag, "emitted-wasm");
 *   ...
 *   bw64jit::add_blocks_emitted(bw64jit::ARCH64, 1, emitMs);      // after emit
 *   bw64jit::add_blocks_instantiated(bw64jit::ARCH64, 1, instMs); // module ready
 *   bw64jit::add_block_entered(bw64jit::ARCH64, n);               // hot path
 *   bw64jit::add_instructions(bw64jit::ARCH64, n);                // hot path
 *   bw64jit::add_fallback(bw64jit::ARCH64, 1);
 *
 * Requirements the panel depends on (see
 * docs/reports/winbox-metrics-contract-20261018.md):
 *   - blocksEntered / instructionsRetired are incremented FROM EMITTED CODE,
 *     not from decoded-block or cache-lookup counters;
 *   - counters are worker-shared (this helper owns the atomics, so a JIT owner
 *     never needs its own cross-worker locking);
 *   - set_state() is called once when the engine is created, with
 *     available=false when the engine is compiled in but not eligible to run.
 *
 * Every add_* call is one relaxed atomic add; publishing is time-throttled to
 * 250 ms inside this helper, so no caller needs its own timer.
 *
 * Full documentation: include/bw64metrics.h (ABI) and
 * docs/reports/winbox-metrics-contract-20261018.md (schema).
 */
#ifndef __BW64JITSTATS_H__
#define __BW64JITSTATS_H__

#include <atomic>
#include <stdint.h>

namespace bw64jit {

enum : int { ARCH32 = 32, ARCH64 = 64 };

// Definitions live in source/util/bw64jitstats.cpp (one per arch).
extern std::atomic<uint64_t> g_blocksEmitted[2];
extern std::atomic<uint64_t> g_blocksInstantiated[2];
extern std::atomic<uint64_t> g_blocksEntered[2];
extern std::atomic<uint64_t> g_instructions[2];
extern std::atomic<uint64_t> g_fallbacks[2];
extern std::atomic<uint64_t> g_emitUs[2];
extern std::atomic<uint64_t> g_instantiateUs[2];

inline int slot_of(int arch) { return arch == ARCH32 ? 0 : 1; }

void set_state(int arch, bool available, bool enabled, const char* engine);
void publish(int arch);

// Account one or more emitted/instantiated blocks. emitMs/instantiateMs are the
// measured wall time of that emit/instantiate, not an estimate.
void add_blocks_emitted(int arch, uint64_t count, double emitMs);
void add_blocks_instantiated(int arch, uint64_t count, double instantiateMs);
void add_fallback(int arch, uint64_t count);

// Hot path: called from emitted code. One relaxed atomic add each.
inline void add_block_entered(int arch, uint64_t count) {
    if (!count) return;
    g_blocksEntered[slot_of(arch)].fetch_add(count, std::memory_order_relaxed);
}
inline void add_instructions(int arch, uint64_t count) {
    if (!count) return;
    g_instructions[slot_of(arch)].fetch_add(count, std::memory_order_relaxed);
}

// Flush the counters into the metrics registry now (normally time-throttled).
void flush(int arch);

// Test surface: current live values for one arch.
struct Snapshot {
    uint64_t available, enabled, blocksEmitted, blocksInstantiated, blocksEntered;
    uint64_t instructionsRetired, fallbacks, emitUs, instantiateUs;
    const char* engine;
};
void snapshot(int arch, Snapshot* out);

} // namespace bw64jit

#endif /* __BW64JITSTATS_H__ */