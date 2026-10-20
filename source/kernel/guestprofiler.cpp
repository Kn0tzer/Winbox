/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "boxedwine.h"

#ifdef BOXEDWINE_GUEST_X64
#include "cpu64.h"
#endif

#include "guestprofiler.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace {

// One wall-clock sample per guest thread per millisecond.
constexpr uint64_t SAMPLE_INTERVAL_US = 1000ULL;

struct ModuleRange {
    uint64_t lo = 0;
    uint64_t hi = 0;
    std::string path;
};

struct ProcessSamples {
    std::string exe;
    std::map<std::string, uint64_t> files;
};

std::mutex g_mutex;
std::map<int, std::vector<ModuleRange>> g_modules;   // 64-bit guest mappings
std::map<int, ProcessSamples> g_samples;
std::string g_jsonText;

// Per-host-thread (== per guest thread in the MT build) sample clock.
thread_local uint64_t t_lastSampleUs = 0;

bool resolve64Locked(int pid, uint64_t rip, std::string& out) {
    auto it = g_modules.find(pid);
    if (it == g_modules.end()) return false;
    // Newest mapping wins: MAP_FIXED sub-segments cover earlier whole-span
    // reservations, exactly like the RIP sampler's resolver.
    for (auto r = it->second.rbegin(); r != it->second.rend(); ++r) {
        if (rip >= r->lo && rip < r->hi) {
            out = r->path;
            return true;
        }
    }
    return false;
}

void recordSample(int pid, const std::string& exe, uint64_t rip, KProcess* process) {
    std::string file;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        resolve64Locked(pid, rip, file);
    }
    if (file.empty() && rip <= 0xFFFFFFFFULL && process) {
        // 32-bit guest: the mapped-file table is the bookkeeping that backs
        // /proc/<pid>/maps (KProcess::mappedFiles / getModuleName).
        BString name = process->getModuleName((U32)rip);
        if (!name.isEmpty() && std::strcmp(name.c_str(), "Unknown") != 0) {
            file = name.c_str();
        }
    }
    if (file.empty()) file = "(unmapped)";

    std::lock_guard<std::mutex> lock(g_mutex);
    ProcessSamples& s = g_samples[pid];
    if (s.exe != exe) s.exe = exe;
    s.files[file] += 1;
}

inline bool dueForSample() {
    uint64_t now = KSystem::getSystemTimeAsMicroSeconds();
    if (t_lastSampleUs != 0 && now - t_lastSampleUs < SAMPLE_INTERVAL_US) return false;
    t_lastSampleUs = now;
    return true;
}

} // namespace

bool bwGuestProfileEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("BW_GUEST_PROFILE");
        if (!value || !value[0]) return false;
        return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
    }();
    return enabled;
}

void bwGuestProfileNoteModule(int pid, uint64_t lo, uint64_t len, const std::string& path) {
    if (!bwGuestProfileEnabled() || len == 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_modules[pid].push_back(ModuleRange{lo, lo + len, path});
}

void bwGuestProfileClearPid(int pid) {
    if (!bwGuestProfileEnabled()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_modules.erase(pid);
    g_samples.erase(pid);
}

void bwGuestProfileTick64(CPU64* cpu) {
#ifdef BOXEDWINE_GUEST_X64
    if (!cpu || !cpu->thread || !cpu->thread->process) return;
    if (!dueForSample()) return;
    KProcess* process = cpu->thread->process.get();
    const std::string exe = process->name.c_str();
    recordSample((int)process->id, exe, cpu->rip, process);
#else
    (void)cpu;
#endif
}

void bwGuestProfileTick32(CPU* cpu) {
    if (!cpu || !cpu->thread || !cpu->thread->process) return;
    if (!dueForSample()) return;
    KProcess* process = cpu->thread->process.get();
    const std::string exe = process->name.c_str();
    recordSample((int)process->id, exe, (uint64_t)cpu->getEipAddress(), process);
}

void bwGuestProfileReset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_samples.clear();
    g_jsonText.clear();
}

const char* bwGuestProfileJson() {
    std::vector<BwGuestProfileEntry> entries;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& process : g_samples) {
            for (const auto& file : process.second.files) {
                entries.push_back(BwGuestProfileEntry{process.first, process.second.exe, file.first, file.second});
            }
        }
    }
    g_jsonText = bwGuestProfileAggregateJson(entries);
    return g_jsonText.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* bw64_guest_profile_json() {
    return bwGuestProfileJson();
}

extern "C" EMSCRIPTEN_KEEPALIVE void bw64_guest_profile_reset() {
    bwGuestProfileReset();
}
