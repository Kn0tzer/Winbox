/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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
#include "knativethread.h"
#include "knativesystem.h"

#include <stdio.h>

#ifdef BOXEDWINE_MSVC
#include <Windows.h>
#endif

static BOXEDWINE_MUTEX logMutex;

void kpanic(const char* msg) {
    internal_kpanic(BString::copy(msg));
}

void internal_kpanic(BString msg) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(logMutex);
    if (KSystem::logFile.isOpen()) {
        KSystem::logFile.write(msg);
    }
    fwrite(msg.c_str(), 1, msg.length(), stderr);
    fflush(stderr);
#if defined(BOXEDWINE_MSVC) && defined(_DEBUG)
    OutputDebugStringA(msg.c_str());
#endif
    if (KSystem::videoOption == VIDEO_NORMAL) {
#ifndef _TEST
        KNativeSystem::exit(msg.c_str(), 1);
#endif
    } else {
        KNativeThread::sleep(5000);
    }
    exit(1);
}

void kwarn(const char* msg) {
    internal_log(BString::copy(msg) + "\n", stdout);
}

void klog(const char* msg) {
    internal_log(BString::copy(msg) + "\n", stdout);
}

void klog_nonewline(const char* msg) {
    internal_log(BString::copy(msg), stdout);
}

void internal_log(BString msg, FILE* f) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(logMutex);
    if (KSystem::logFile.isOpen()) {
        KSystem::logFile.write(msg);
    }
    fwrite(msg.c_str(), 1, msg.length(), f);
    fflush(f);
#if defined(BOXEDWINE_MSVC) && defined(_DEBUG)
    OutputDebugStringA(msg.c_str());
#endif
}

// Boxedwine64 unified logging implementation.
// Appended to source/util/log.cpp

#include <cstring>
#include <cstdarg>
#include <cstdlib>

int bw64_log_levels[BW64_SUBSYS_COUNT];
int bw64_log_default_level = BW64_LOG_WARN; // default: ERROR + WARN

static const char* bw64_level_names[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};

static const char* bw64_subsys_names[] = {
    "vulkan", "page", "kernel", "syscall", "wineserver", "launcher",
    "shader", "cpu", "memory", "audio", "input", "gl", "fs", "x11"
};

static int bw64_parse_level(const char* s) {
    if (!s) return -2;
    if (strcmp(s, "error") == 0) return BW64_LOG_ERROR;
    if (strcmp(s, "warn") == 0) return BW64_LOG_WARN;
    if (strcmp(s, "info") == 0) return BW64_LOG_INFO;
    if (strcmp(s, "debug") == 0) return BW64_LOG_DEBUG;
    if (strcmp(s, "trace") == 0) return BW64_LOG_TRACE;
    if (strcmp(s, "off") == 0) return BW64_LOG_OFF;
    return -2; // invalid
}

static int bw64_parse_subsys(const char* s) {
    for (int i = 0; i < BW64_SUBSYS_COUNT; i++) {
        if (strcmp(s, bw64_subsys_names[i]) == 0) return i;
    }
    return -1;
}

void bw64_log_init(void) {
    // Default: all subsystems inherit the global default (WARN)
    for (int i = 0; i < BW64_SUBSYS_COUNT; i++) {
        bw64_log_levels[i] = -1; // -1 = use default
    }
    bw64_log_default_level = BW64_LOG_WARN;

    const char* cfg = getenv("BW64_LOG");
    if (!cfg || !*cfg) return;

    // Format: "level" or "sub:level,sub:level,..." or "level,sub:level,..."
    // Copy to mutable buffer for strtok
    char buf[512];
    strncpy(buf, cfg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char* saveptr = nullptr;
    for (char* tok = strtok_r(buf, ",", &saveptr); tok; tok = strtok_r(nullptr, ",", &saveptr)) {
        // Trim leading whitespace
        while (*tok == ' ' || *tok == '\t') tok++;
        char* colon = strchr(tok, ':');
        if (colon) {
            *colon = 0;
            int sub = bw64_parse_subsys(tok);
            int lvl = bw64_parse_level(colon + 1);
            if (sub >= 0 && lvl >= -1) {
                bw64_log_levels[sub] = lvl;
            }
        } else {
            int lvl = bw64_parse_level(tok);
            if (lvl >= -1) {
                bw64_log_default_level = lvl;
            }
        }
    }
}

void bw64_log_emit(int level, int subsys, const char* fmt, ...) {
    if (level < 0 || level > BW64_LOG_TRACE) return;
    if (subsys < 0 || subsys >= BW64_SUBSYS_COUNT) subsys = SUBSYS_KERNEL;

    char msgbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);

    // Format: [LEVEL][subsys] message
    // Timestamp omitted on wasm (no cheap clock); browser console adds its own
    char outbuf[1152];
    snprintf(outbuf, sizeof(outbuf), "[%s][%s] %s\n",
             bw64_level_names[level], bw64_subsys_names[subsys], msgbuf);
    internal_log(BString::copy(outbuf), stdout);
}

// Static initializer: parse BW64_LOG once at module load.
// ENV[BW64_LOG] is set by the launcher before the wasm module initializes.
struct Bw64LogAutoInit {
    Bw64LogAutoInit() { bw64_log_init(); }
};
static Bw64LogAutoInit _bw64_log_auto_init;
