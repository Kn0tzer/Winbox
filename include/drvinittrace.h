/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __DRVINITTRACE_H__
#define __DRVINITTRACE_H__

// BW64_DRVINIT=1 — opt-in driver-init step trace (campaign section 4.30).
//
// The section 4.18 Godot DXGI failure was an intermittent guest
// driver-init flake: init_vulkan printed "Failed to load Wine graphics
// driver supporting Vulkan", the app never connected to X and never
// issued a bridge trap, and the ordinary logs could not say WHICH init
// step diverged (graphics-driver registration, the libvulkan dlopen,
// the X connect, desktop readiness, first bridge trap), because the
// steps were logged by different subsystems with no common clock —
// and two of them (process starts, the libvulkan.so dlopen itself)
// were not logged at all.
//
// With BW64_DRVINIT set (the browser launcher seeds it from ?drvinit=1,
// see project/emscripten/wine64-launcher.js), each init step emits ONE
// structured line on the klog channel:
//
//   drvinit: t=<ms> step=<name> <detail>
//
// t= is milliseconds since the first drvinit timestamp in this process
// (steady_clock; the first call is the first process start, i.e. boot),
// so process, X-server and bridge steps share one relative clock and a
// failing run can be diffed step-by-step against a healthy one: the
// first step present in the healthy trace but missing or late in the
// failing trace is the divergence.
//
// When the variable is unset, every helper below costs one cached-flag
// check: no lines, no behavior change. Header-only on purpose — the
// functions are inline with function-local statics, so no new
// translation unit or link symbol appears in any build (the gate
// script's hand-maintained source list included). Include AFTER
// boxedwine.h (log.h needs the tree's base types).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "log.h"

// Launcher-forced enable (bw64_drvinit_enable, next to
// bw64_vktrace_enable in vk64bridge.cpp): getenv() is not reachable
// from every pthread worker, so the page can force the trace through
// this shared flag instead, exactly like BW64_VKTRACE does.
inline std::atomic<bool>& bw64DrvInitForcedFlag() {
    static std::atomic<bool> forced{false};
    return forced;
}

inline bool bw64DrvInitEnabled() {
    static const bool envOn = std::getenv("BW64_DRVINIT") != nullptr;
    return envOn || bw64DrvInitForcedFlag().load(std::memory_order_relaxed);
}

inline unsigned int bw64DrvInitMs() {
    static const std::chrono::steady_clock::time_point t0 =
        std::chrono::steady_clock::now();
    return (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

template <class... Args>
inline void bw64DrvInitStep(const char* step, const char* format, Args&&... args) {
    if (!bw64DrvInitEnabled()) return;
    char detail[512];
    std::snprintf(detail, sizeof(detail), format, std::forward<Args>(args)...);
    klog_fmt("drvinit: t=%ums step=%s %s", bw64DrvInitMs(), step, detail);
}

inline void bw64DrvInitStep(const char* step) {
    if (!bw64DrvInitEnabled()) return;
    klog_fmt("drvinit: t=%ums step=%s", bw64DrvInitMs(), step);
}

// First-occurrence milestone: returns true exactly once per flag, and
// only when the trace is enabled (a disabled run must not consume the
// milestone if the trace is forced on later in the same boot).
inline bool bw64DrvInitOnce(std::atomic<bool>& flag) {
    if (!bw64DrvInitEnabled()) return false;
    return !flag.exchange(true, std::memory_order_relaxed);
}

// True when a guest path names one of the driver-init modules whose
// load order decides the section 4.18 flake: the X driver (PE + unix
// sides), the Vulkan loader names a guest can resolve (the bw64 PE
// shim is staged under all three), the unix libvulkan the X driver
// dlopens (LD_LIBRARY_PATH decides WHICH file wins — the shim
// selection itself), and the D3D12/DXGI front ends. Prefix match, so
// libvulkan.so.1 qualifies. Checks the enable flag first, so the
// per-openat cost when off is one cached load.
inline bool bw64DrvInitIsDriverPath(const char* path) {
    if (!bw64DrvInitEnabled() || !path) return false;
    const char* base = std::strrchr(path, '/');
    base = base ? base + 1 : path;
    static const char* const names[] = {
        "winex11.drv", "winex11.so", "libvulkan.so",
        "winevulkan.dll", "vulkan-1.dll", "bw64vulkan.dll",
        "dxgi.dll", "d3d12.dll", "d3d12core.dll",
    };
    for (const char* n : names)
        if (std::strncmp(base, n, std::strlen(n)) == 0) return true;
    return false;
}

#endif
