/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include <atomic>

static std::atomic<bool> g_cpu64ControlTrace{false};

void ripSamplerSetControlTrace(bool enabled) {
    g_cpu64ControlTrace.store(enabled, std::memory_order_relaxed);
}

bool ripSamplerControlTraceEnabled() {
    return g_cpu64ControlTrace.load(std::memory_order_relaxed);
}

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif
extern "C" EMSCRIPTEN_KEEPALIVE void bw64_set_control_trace(int enabled) {
    ripSamplerSetControlTrace(enabled != 0);
}

// The sampler only exists in the multi-threaded x86-64 guest build — it relies
// on each guest thread driving its own CPU64 on its own host thread (so a
// background thread can sample their per-thread {rip, instructionCount}). In any
// other build configuration every entry point is an inert stub so callers don't
// need their own #ifdefs.
#if defined(BOXEDWINE_MULTI_THREADED) && defined(BOXEDWINE_GUEST_X64)

#include "ripSampler.h"
#include "cpu64.h"
#include "ksystem.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Tunables (overridable via env so a run can be retargeted without a rebuild).
// ---------------------------------------------------------------------------
static const int  SAMPLE_INTERVAL_MS = 30;   // sampler wake cadence

// A thread is "busy" in a tick if it advanced more than this many instructions
// since the previous sample. ~2M insn / 30ms is a tiny fraction of the interp's
// throughput but far above the handful of instructions a genuinely-idle (but
// not-yet-blocked) thread retires between samples.
static U64 g_spinInsnThreshold = 2000000ULL;

// The accumulated rip window over consecutive busy ticks must stay narrower than
// this for the thread to count as a tight spin (one loop body, not normal code
// making forward progress through many functions).
static U64 g_spinWindow = 4096ULL;

// Consecutive busy+narrow ticks required before we report (debounce against a
// transient hot loop that's actually making progress). 4 * 30ms ~= 120ms.
static const U32 SPIN_TICKS_REQUIRED = 4;

// ---------------------------------------------------------------------------
// Env gate — read once.
// ---------------------------------------------------------------------------
bool ripSamplerProfileEnabled() {
    static const bool on = std::getenv("BW64_RIPSAMPLE_PROFILE") != nullptr;
    return on;
}

bool ripSamplerEnabled() {
    static const bool on = []() {
        bool e = std::getenv("BW64_RIPSAMPLE") != nullptr || ripSamplerProfileEnabled();
        if (e) {
            if (const char* t = std::getenv("BW64_RIPSAMPLE_THRESH"))
                g_spinInsnThreshold = std::strtoull(t, nullptr, 0);
            if (const char* w = std::getenv("BW64_RIPSAMPLE_WINDOW"))
                g_spinWindow = std::strtoull(w, nullptr, 0);
        }
        return e;
    }();
    return on;
}

bool ripSamplerModuleTrackingEnabled() {
    return ripSamplerEnabled() || ripSamplerControlTraceEnabled();
}

// ---------------------------------------------------------------------------
// Live-CPU64 registry. One slot per guest host-thread. The sampler reads
// slot.cpu (an atomic pointer, so it never tears) and then reads cpu->rip /
// cpu->instructionCount RACILY — those are plain U64s written only by the
// owning thread. A stale/torn sample over a 30ms window is harmless for a
// statistical profiler; we only ever ask "did rip stay in a narrow window while
// instructionCount climbed", which a one-sample-old value answers just as well.
// ---------------------------------------------------------------------------
#define RIP_MAX_SLOTS 256
struct RipSlot {
    std::atomic<CPU64*> cpu{nullptr};
    std::atomic<CPU*> cpu32{nullptr};
    int pid = -1;
    int tid = -1;
};
static RipSlot     g_slots[RIP_MAX_SLOTS];
static std::mutex  g_slotMutex;       // guards acquire/release scans only

// Sampler-thread-private per-slot state (never touched by guest threads).
struct LastSample {
    U64  rip = 0;
    U64  icount = 0;
    U64  ripLo = 0;
    U64  ripHi = 0;
    U32  spinTicks = 0;
    bool reported = false;
};
static LastSample g_last[RIP_MAX_SLOTS];

// ---------------------------------------------------------------------------
// Module-range table: per-pid [lo,hi) -> path, so a sampled rip resolves to
// "<basename>+0x<off>". Populated at module-load time, queried off the hot path.
// ---------------------------------------------------------------------------
struct ModRange { U64 lo; U64 hi; std::string path; };
static std::map<int, std::vector<ModRange>> g_modRanges;
static std::mutex g_modMutex;

void ripSamplerNoteModule(int pid, U64 lo, U64 len, const std::string& path) {
    if (!ripSamplerModuleTrackingEnabled() || len == 0) return;
    std::lock_guard<std::mutex> lk(g_modMutex);
    g_modRanges[pid].push_back({lo, lo + len, path});
}

void ripSamplerClearPid(int pid) {
    if (!ripSamplerModuleTrackingEnabled()) return;
    std::lock_guard<std::mutex> lk(g_modMutex);
    g_modRanges.erase(pid);
}

// ---------------------------------------------------------------------------
// BW64_RIPSAMPLE_PROFILE statistical histogram: pid -> (module -> insns).
// Written from CPU64::run() (one host thread per guest thread, 64k-insn
// cadence, so the mutex cost is negligible); read + reset from the vk64
// present trap for a per-frame module split. Resolution reuses the
// g_modRanges table above (ripSamplerResolve is newest-wins on overlap).
// ---------------------------------------------------------------------------
static bool ripSamplerResolve(int pid, U64 rip, std::string& outBase, U64& outOff);

static std::map<int, std::map<std::string, U64>> g_profMods;
static std::map<int, U64> g_profTotal;
static std::mutex g_profMutex;

void ripSamplerProfAdd(int pid, U64 rip, U64 insns) {
    if (!ripSamplerProfileEnabled() || insns == 0) return;
    std::string base; U64 off = 0;
    std::string key = ripSamplerResolve(pid, rip, base, off) ? base : "(unmapped)";
    std::lock_guard<std::mutex> lk(g_profMutex);
    g_profMods[pid][key] += insns;
    g_profTotal[pid] += insns;
}

void ripSamplerProfDumpReset(int pid) {
    if (!ripSamplerProfileEnabled()) return;
    std::map<std::string, U64> mods;
    U64 total = 0;
    {
        std::lock_guard<std::mutex> lk(g_profMutex);
        auto it = g_profMods.find(pid);
        if (it != g_profMods.end()) { mods = it->second; g_profMods.erase(it); }
        auto jt = g_profTotal.find(pid);
        if (jt != g_profTotal.end()) { total = jt->second; g_profTotal.erase(jt); }
    }
    klog_fmt("RIPPROF pid=%d totalInsns=%llu", pid, (unsigned long long)total);
    for (auto& kv : mods) {
        double pct = total ? (100.0 * (double)kv.second / (double)total) : 0.0;
        klog_fmt("RIPPROF pid=%d mod=%s insns=%llu pct=%.2f", pid, kv.first.c_str(),
                 (unsigned long long)kv.second, pct);
    }
}

static bool ripSamplerResolve(int pid, U64 rip, std::string& outBase, U64& outOff) {
    std::lock_guard<std::mutex> lk(g_modMutex);
    auto it = g_modRanges.find(pid);
    if (it == g_modRanges.end()) return false;
    // Most-recently-mapped wins on overlap (later MAP_FIXED sub-segments of a DSO
    // re-cover earlier whole-span reservations) — scan newest-first.
    for (auto r = it->second.rbegin(); r != it->second.rend(); ++r) {
        if (rip >= r->lo && rip < r->hi) {
            size_t slash = r->path.find_last_of('/');
            outBase = (slash == std::string::npos) ? r->path : r->path.substr(slash + 1);
            outOff = rip - r->lo;
            return true;
        }
    }
    return false;
}

bool ripSamplerResolveModule(int pid, U64 rip, std::string& module, U64& offset) {
    return ripSamplerResolve(pid, rip, module, offset);
}

// ---------------------------------------------------------------------------
// The background sampler thread.
// ---------------------------------------------------------------------------
static void ripSamplerLoop() {
    // Heartbeat/summary mode (BW64_RIPSAMPLE_HB): once per ~2s, dump EVERY live
    // slot's per-tick instructionCount delta + current rip+module, regardless of
    // the spin latch. This is the ground-truth probe: if a thread burns CPU but
    // its instructionCount is NOT climbing, the spin is NOT in the guest
    // interpreter loop (it's host-side — our syscall/epoll/futex handling — or
    // the thread is blocked), which changes the whole diagnosis. Also confirms
    // the sampler thread is alive and slots are populated.
    static const bool hb = std::getenv("BW64_RIPSAMPLE_HB") != nullptr;
    U64 hbAccumMs = 0;
    klog_fmt("RIPSAMPLE: sampler thread started (threshold=%llu window=%llu hb=%d)",
             (unsigned long long)g_spinInsnThreshold, (unsigned long long)g_spinWindow, (int)hb);
    while (!KSystem::shutingDown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(SAMPLE_INTERVAL_MS));
        hbAccumMs += SAMPLE_INTERVAL_MS;
        bool emitHb = hb && hbAccumMs >= 2000;
        if (emitHb) hbAccumMs = 0;
        for (int i = 0; i < RIP_MAX_SLOTS; i++) {
            CPU64* c = g_slots[i].cpu.load(std::memory_order_relaxed);
            CPU* c32 = g_slots[i].cpu32.load(std::memory_order_relaxed);
            if (c32) {
                if (emitHb) {
                    U32 eip = c32->getEipAddress();
                    auto process = c32->thread->process;
                    klog_fmt("RIPSAMPLE32 HB pid=%d tid=%d eip=0x%x %s+0x%x eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x esp=0x%x",
                        g_slots[i].pid, g_slots[i].tid, eip,
                        process->getModuleName(eip).c_str(), process->getModuleEip(eip),
                        c32->reg[0].u32, c32->reg[3].u32, c32->reg[1].u32,
                        c32->reg[2].u32, c32->reg[4].u32);
                    const char* stackPid = std::getenv("BW64_RIPSAMPLE_STACK_PID");
                    const char* stackTid = std::getenv("BW64_RIPSAMPLE_STACK_TID");
                    if (std::getenv("BW64_RIPSAMPLE_STACK") &&
                        (!stackPid || std::atoi(stackPid) == g_slots[i].pid) &&
                        (!stackTid || std::atoi(stackTid) == g_slots[i].tid)) {
                        U32 sp = c32->reg[4].u32;
                        if (c32->memory->canRead(sp, 4)) {
                            int emitted = 0;
                            // Wine's window/message calls can put the game caller
                            // beyond 2KiB. Stop at the mapped stack boundary.
                            for (U32 off = 0; off < 16384 && emitted < 64; off += 4) {
                                if (!c32->memory->canRead(sp + off, 4)) break;
                                U32 addr = c32->memory->readd(sp + off);
                                BString name = process->getModuleName(addr);
                                if (!name.isEmpty() && (name.contains(B(".dll")) || name.contains(B(".so")))) {
                                    klog_fmt("RIPSAMPLE32 STACK pid=%d tid=%d sp+0x%x=0x%x %s+0x%x",
                                        g_slots[i].pid, g_slots[i].tid, off, addr,
                                        name.c_str(), process->getModuleEip(addr));
                                    emitted++;
                                }
                            }
                        }
                    }
                }
                continue;
            }
            if (!c) { g_last[i].spinTicks = 0; g_last[i].reported = false; continue; }

            U64 rip = c->rip;                 // racy sample — see registry note
            U64 ic  = c->instructionCount;
            LastSample& L = g_last[i];
            U64 dIns = ic - L.icount;

            if (emitHb) {
                std::string hbBase; U64 hbOff = 0;
                bool res = ripSamplerResolve(g_slots[i].pid, rip, hbBase, hbOff);
                klog_fmt("RIPSAMPLE HB slot=%d pid=%d tid=%d dIns=%llu rip=0x%llx %s%s0x%llx",
                         i, g_slots[i].pid, g_slots[i].tid, (unsigned long long)dIns,
                         (unsigned long long)rip,
                         res ? hbBase.c_str() : "(unmapped)", res ? "+" : " @",
                         (unsigned long long)(res ? hbOff : rip));
            }

            L.rip = rip;
            L.icount = ic;

            bool busy = dIns > g_spinInsnThreshold;
            if (!busy) {
                L.spinTicks = 0;
                L.reported = false;
                continue;
            }

            // Accumulate the rip window across consecutive busy ticks.
            if (L.spinTicks == 0) { L.ripLo = rip; L.ripHi = rip; }
            else { if (rip < L.ripLo) L.ripLo = rip; if (rip > L.ripHi) L.ripHi = rip; }
            L.spinTicks++;

            U64 span = L.ripHi - L.ripLo;
            if (span >= g_spinWindow) {
                // Wandered out of a tight loop — a real hot path making progress.
                // Re-seed the window on the current rip and clear the latch.
                L.ripLo = rip; L.ripHi = rip; L.spinTicks = 1; L.reported = false;
                continue;
            }

            if (L.spinTicks >= SPIN_TICKS_REQUIRED && !L.reported) {
                L.reported = true;
                int pid = g_slots[i].pid;
                int tid = g_slots[i].tid;
                std::string base;
                U64 off = 0;
                if (ripSamplerResolve(pid, L.ripLo, base, off)) {
                    klog_fmt("RIPSAMPLE SPIN pid=%d tid=%d insn/tick=%llu "
                             "window=[0x%llx,0x%llx] span=%llu file=%s+0x%llx rip=0x%llx",
                             pid, tid, (unsigned long long)dIns,
                             (unsigned long long)L.ripLo, (unsigned long long)L.ripHi,
                             (unsigned long long)span, base.c_str(),
                             (unsigned long long)off, (unsigned long long)rip);
                } else {
                    klog_fmt("RIPSAMPLE SPIN pid=%d tid=%d insn/tick=%llu "
                             "window=[0x%llx,0x%llx] span=%llu (unmapped) rip=0x%llx",
                             pid, tid, (unsigned long long)dIns,
                             (unsigned long long)L.ripLo, (unsigned long long)L.ripHi,
                             (unsigned long long)span, (unsigned long long)rip);
                }
            }
        }
    }
}

static std::once_flag g_startOnce;
static void ripSamplerEnsureStarted() {
    // The 30ms spin-detector thread only runs in spin mode (BW64_RIPSAMPLE);
    // profile mode samples inside CPU64::run() and needs no background thread.
    if (!std::getenv("BW64_RIPSAMPLE")) return;
    std::call_once(g_startOnce, []() {
        std::thread(ripSamplerLoop).detach();
    });
}

// ---------------------------------------------------------------------------
// Registry register / publish / release (called from platformThread).
// ---------------------------------------------------------------------------
int ripSamplerAcquireSlot(int pid, int tid) {
    if (!ripSamplerEnabled()) return -1;
    std::lock_guard<std::mutex> lk(g_slotMutex);
    for (int i = 0; i < RIP_MAX_SLOTS; i++) {
        if (g_slots[i].cpu.load(std::memory_order_relaxed) == nullptr && g_slots[i].pid == -1) {
            g_slots[i].pid = pid;
            g_slots[i].tid = tid;
            g_last[i] = LastSample{};
            ripSamplerEnsureStarted();
            return i;
        }
    }
    return -1;
}

void ripSamplerPublish(int slot, CPU64* cpu) {
    if (slot < 0) return;
    g_slots[slot].cpu.store(cpu, std::memory_order_relaxed);
}

void ripSamplerPublish32(int slot, CPU* cpu) {
    if (slot < 0) return;
    g_slots[slot].cpu32.store(cpu, std::memory_order_relaxed);
}

void ripSamplerReleaseSlot(int slot) {
    if (slot < 0) return;
    std::lock_guard<std::mutex> lk(g_slotMutex);
    g_slots[slot].cpu.store(nullptr, std::memory_order_relaxed);
    g_slots[slot].cpu32.store(nullptr, std::memory_order_relaxed);
    g_slots[slot].pid = -1;
    g_slots[slot].tid = -1;
    g_last[slot] = LastSample{};
}

#else // not (MULTI_THREADED && GUEST_X64) — inert stubs

#include "ripSampler.h"
class CPU64;
bool ripSamplerEnabled() { return false; }
bool ripSamplerModuleTrackingEnabled() { return ripSamplerControlTraceEnabled(); }
bool ripSamplerResolveModule(int, U64, std::string&, U64&) { return false; }
int  ripSamplerAcquireSlot(int, int) { return -1; }
void ripSamplerPublish(int, CPU64*) {}
void ripSamplerPublish32(int, CPU*) {}
void ripSamplerReleaseSlot(int) {}
void ripSamplerNoteModule(int, U64, U64, const std::string&) {}
void ripSamplerClearPid(int) {}
bool ripSamplerProfileEnabled() { return false; }
void ripSamplerProfAdd(int, U64, U64) {}
void ripSamplerProfDumpReset(int) {}

#endif
