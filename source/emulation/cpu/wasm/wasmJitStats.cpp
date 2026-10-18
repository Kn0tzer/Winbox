/*
 *  Copyright (C) 2012-2026  The Boxedwine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef BOXEDWINE_WASM_JIT

#include "wasmJitStats.h"
#include "wasmJitRuntime.h"
#include "../common/cpu.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

// Counters live in the (pthreads-shared) linear-memory data segment, so every
// worker updates the same storage; atomics make those updates and the reads in
// wasmJit32StatsSnapshot safe.
static std::atomic<U64> g_jit32BlocksEmitted{0};
static std::atomic<U64> g_jit32BlocksEntered{0};
static std::atomic<U64> g_jit32BlocksInstantiated{0};
static std::atomic<U64> g_jit32EmitNs{0};
static std::atomic<U64> g_jit32InstantiateNs{0};
static std::atomic<U64> g_jit32Fallbacks[(U32)WasmJit32Fallback::Count];

// Emitted code increments CPU::wasmJitRetiredInstructions; the CPU registry
// lets a snapshot on any thread sum every guest thread's counter without
// walking the scheduler (a worker is not addressable from the main thread).
static std::mutex g_jit32CpuMutex;
static std::vector<CPU*> g_jit32Cpus;

void wasmJit32StatsBlockEmitted() {
    g_jit32BlocksEmitted.fetch_add(1, std::memory_order_relaxed);
}

void wasmJit32StatsBlockEntered() {
    g_jit32BlocksEntered.fetch_add(1, std::memory_order_relaxed);
}

void wasmJit32StatsBlockInstantiated(U64 ns) {
    g_jit32BlocksInstantiated.fetch_add(1, std::memory_order_relaxed);
    if (ns) {
        g_jit32InstantiateNs.fetch_add(ns, std::memory_order_relaxed);
    }
}

void wasmJit32StatsAddEmitNs(U64 ns) {
    if (ns) {
        g_jit32EmitNs.fetch_add(ns, std::memory_order_relaxed);
    }
}

void wasmJit32StatsFallback(WasmJit32Fallback kind) {
    U32 index = (U32)kind;
    if (index < (U32)WasmJit32Fallback::Count) {
        g_jit32Fallbacks[index].fetch_add(1, std::memory_order_relaxed);
    }
}

void wasmJit32StatsRegisterCpu(CPU* cpu) {
    if (!cpu) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_jit32CpuMutex);
    for (CPU* existing : g_jit32Cpus) {
        if (existing == cpu) {
            return;
        }
    }
    g_jit32Cpus.push_back(cpu);
}

void wasmJit32StatsUnregisterCpu(CPU* cpu) {
    std::lock_guard<std::mutex> lock(g_jit32CpuMutex);
    for (size_t i = 0; i < g_jit32Cpus.size(); ++i) {
        if (g_jit32Cpus[i] == cpu) {
            g_jit32Cpus.erase(g_jit32Cpus.begin() + (long)i);
            return;
        }
    }
}

void wasmJit32StatsSnapshot(WasmJit32StatsSnapshot* out) {
    if (!out) {
        return;
    }
    out->blocksEmitted = g_jit32BlocksEmitted.load(std::memory_order_relaxed);
    out->blocksEntered = g_jit32BlocksEntered.load(std::memory_order_relaxed);
    out->blocksInstantiated = g_jit32BlocksInstantiated.load(std::memory_order_relaxed);
    out->emitNs = g_jit32EmitNs.load(std::memory_order_relaxed);
    out->instantiateNs = g_jit32InstantiateNs.load(std::memory_order_relaxed);
    for (U32 i = 0; i < (U32)WasmJit32Fallback::Count; ++i) {
        out->fallbacks[i] = g_jit32Fallbacks[i].load(std::memory_order_relaxed);
    }
    U64 retired = 0;
    {
        std::lock_guard<std::mutex> lock(g_jit32CpuMutex);
        for (CPU* cpu : g_jit32Cpus) {
            if (cpu) {
                retired += __atomic_load_n(&cpu->wasmJitRetiredInstructions, __ATOMIC_RELAXED);
            }
        }
    }
    out->instructionsRetired = retired;
    out->sampledAtMs = KSystem::getMilliesSinceStart();
    out->available = true;
    out->enabled = wasmJit32Enabled();
    out->safeMemory = wasmJit32SafeMemoryEnabled();
    out->retiredCounting = wasmJit32CollectRetiredInstructions();
}

namespace {
    struct JsonWriter {
        char* out;
        int capacity;
        int written;

        JsonWriter(char* buffer, int size) : out(buffer), capacity(size), written(0) {
            if (out && capacity > 0) {
                out[0] = 0;
            }
        }

        bool put(const char* text) {
            int len = (int)std::strlen(text);
            if (!out || written + len + 1 > capacity) {
                return false;
            }
            std::memcpy(out + written, text, (size_t)len);
            written += len;
            out[written] = 0;
            return true;
        }

        bool putU64(U64 value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
            return put(buffer);
        }
    };
}

int bw32_jit_stats_json(char* out, int outSize) {
    if (!out || outSize <= 1) {
        return -1;
    }
    WasmJit32StatsSnapshot snapshot;
    wasmJit32StatsSnapshot(&snapshot);

    JsonWriter writer(out, outSize);
    bool ok = writer.put("{\"engine\":\"wasm-jit\"")
        && writer.put(snapshot.available ? ",\"available\":true" : ",\"available\":false")
        && writer.put(snapshot.enabled ? ",\"enabled\":true" : ",\"enabled\":false")
        && writer.put(snapshot.safeMemory ? ",\"safeMemory\":true" : ",\"safeMemory\":false")
        && writer.put(snapshot.retiredCounting ? ",\"retiredCounting\":true" : ",\"retiredCounting\":false")
        && writer.put(",\"blocksEmitted\":") && writer.putU64(snapshot.blocksEmitted)
        && writer.put(",\"blocksEntered\":") && writer.putU64(snapshot.blocksEntered)
        && writer.put(",\"blocksInstantiated\":") && writer.putU64(snapshot.blocksInstantiated)
        && writer.put(",\"instructionsRetired\":") && writer.putU64(snapshot.instructionsRetired)
        && writer.put(",\"emitMs\":") && writer.putU64(snapshot.emitNs / 1000000ull)
        && writer.put(",\"instantiateMs\":") && writer.putU64(snapshot.instantiateNs / 1000000ull)
        && writer.put(",\"fallbacks\":{\"emulateSingleOp\":")
        && writer.putU64(snapshot.fallbacks[(U32)WasmJit32Fallback::EmulateSingleOp])
        && writer.put(",\"slotMiss\":") && writer.putU64(snapshot.fallbacks[(U32)WasmJit32Fallback::SlotMiss])
        && writer.put(",\"installFailure\":") && writer.putU64(snapshot.fallbacks[(U32)WasmJit32Fallback::InstallFailure])
        && writer.put(",\"disabled\":") && writer.putU64(snapshot.fallbacks[(U32)WasmJit32Fallback::Disabled])
        && writer.put("}")
        && writer.put(",\"sampledAtMs\":") && writer.putU64(snapshot.sampledAtMs)
        && writer.put("}");
    if (!ok) {
        if (out && outSize > 0) {
            out[0] = 0;
        }
        return -1;
    }
    return writer.written;
}

// Parent contract entry point. The cpu64 section is owned by the cpu64 JIT
// backend, which has no provider in this lane, so it is reported as explicitly
// unavailable rather than zero-filled. The integrator replaces this placeholder
// with lane 2's cpu64 serializer when merging the two backends (the CPU64 owner
// exports its own stats JSON; this function is the single composition point).
#define BW32_JIT_STATS_CPU64_PLACEHOLDER "{\"available\":false,\"reason\":\"cpu64 jit stats are provided by the cpu64 backend owner; compose them here during integration\"}"

int bw64_jit_stats_json(char* out, int outSize) {
    if (!out || outSize <= 1) {
        return -1;
    }
    char cpu32Section[1024];
    if (bw32_jit_stats_json(cpu32Section, (int)sizeof(cpu32Section)) < 0) {
        return -1;
    }
    JsonWriter writer(out, outSize);
    if (!writer.put("{\"cpu32\":") || !writer.put(cpu32Section)
            || !writer.put(",\"cpu64\":") || !writer.put(BW32_JIT_STATS_CPU64_PLACEHOLDER)
            || !writer.put("}")) {
        out[0] = 0;
        return -1;
    }
    return writer.written;
}

const char* bw64_jit_stats_json_text() {
    // Shared, non-thread-safe scratch buffer: the metrics panel calls this from
    // the main thread and copies the string immediately.
    static char text[1024];
    if (bw64_jit_stats_json(text, (int)sizeof(text)) < 0) {
        text[0] = 0;
    }
    return text;
}

#endif // BOXEDWINE_WASM_JIT