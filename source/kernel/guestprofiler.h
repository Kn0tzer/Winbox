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

/*
 * BW_GUEST_PROFILE=1 — a lightweight guest-PC sampler.
 *
 * Wine, DXVK and vkd3d all execute as guest x86/x86-64 code, so the host CPU
 * profiler cannot tell them apart: every guest instruction looks like the same
 * interpreter or JIT function. This sampler records, once per wall-clock
 * millisecond of guest execution, the current guest instruction pointer of each
 * running guest CPU thread and attributes it to the mapped file that backs it.
 *
 * It is driven from the CPU loops themselves (the existing per-thread
 * instruction/branch tick), so it needs no sampler thread and adds exactly one
 * predictable branch when BW_GUEST_PROFILE is not set. The aggregation and JSON
 * helpers are header-only and have no boxedwine dependencies so the native unit
 * test (tools/profile/guestprofiler_test.cpp) can compile them standalone.
 *
 * Exported to JS (EMSCRIPTEN_KEEPALIVE in guestprofiler.cpp):
 *   bw64_guest_profile_json()  -> const char* (see below)
 *   bw64_guest_profile_reset() -> void
 */

#ifndef __GUEST_PROFILER_H__
#define __GUEST_PROFILER_H__

#include <algorithm>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

class CPU;
class CPU64;

// One aggregated (process, mapped file) bucket.
struct BwGuestProfileEntry {
    int pid = 0;
    std::string exe;
    std::string file;
    uint64_t samples = 0;
};

// True iff BW_GUEST_PROFILE is set to a non-zero value. Cached after first call.
bool bwGuestProfileEnabled();

// Record that [lo, lo+len) of process `pid` is backed by `path`. Called from the
// same mapping sites that feed the RIP sampler (64-bit ELF loader / PE reads /
// file mmaps).
void bwGuestProfileNoteModule(int pid, uint64_t lo, uint64_t len, const std::string& path);
void bwGuestProfileClearPid(int pid);

// Sample hooks called from the CPU loops. Each thread samples its own current
// guest instruction pointer at most once per millisecond.
void bwGuestProfileTick64(CPU64* cpu);
void bwGuestProfileTick32(CPU* cpu);

void bwGuestProfileReset();
const char* bwGuestProfileJson();

inline std::string bwGuestProfileJsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            } else {
                out += c;
            }
        }
    }
    return out;
}

// Aggregate per-sample buckets into the JSON contract described in the header
// above. Deterministic: processes ascending by pid, files descending by sample
// count then path. Sample counts are summed across duplicate (pid, file) rows.
inline std::string bwGuestProfileAggregateJson(const std::vector<BwGuestProfileEntry>& entries) {
    struct Proc {
        std::string exe;
        std::map<std::string, uint64_t> files;
    };
    std::map<int, Proc> processes;
    uint64_t total = 0;
    for (const BwGuestProfileEntry& entry : entries) {
        Proc& proc = processes[entry.pid];
        if (!entry.exe.empty()) proc.exe = entry.exe;
        proc.files[entry.file] += entry.samples;
        total += entry.samples;
    }
    std::ostringstream out;
    out << "{\"totalSamples\":" << total << ",\"processes\":[";
    bool firstProc = true;
    for (auto& processPair : processes) {
        if (!firstProc) out << ',';
        firstProc = false;
        out << "{\"pid\":" << processPair.first
            << ",\"exe\":\"" << bwGuestProfileJsonEscape(processPair.second.exe) << "\",\"files\":[";
        std::vector<std::pair<std::string, uint64_t>> files(processPair.second.files.begin(),
                                                           processPair.second.files.end());
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        bool firstFile = true;
        for (const auto& file : files) {
            if (!firstFile) out << ',';
            firstFile = false;
            out << "{\"path\":\"" << bwGuestProfileJsonEscape(file.first)
                << "\",\"samples\":" << file.second << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

#endif // __GUEST_PROFILER_H__
