/*
 * Boxedwine64 unified logging infrastructure.
 *
 * Levels: ERROR (always on), WARN (default on), INFO (opt-in),
 *         DEBUG (opt-in, verbose), TRACE (per-subsystem, very verbose).
 *
 * Configuration via BW64_LOG environment variable:
 *   BW64_LOG=debug                    - set global level to DEBUG
 *   BW64_LOG=vulkan:trace            - TRACE for vulkan only
 *   BW64_LOG=warn,wineserver:debug   - WARN globally, DEBUG for wineserver
 *   BW64_LOG=off                     - only ERROR
 *
 * URL parameters (set by launcher): ?loglevel=debug&logfilter=vulkan:trace
 *
 * Usage:
 *   BLOG_ERROR(SUBSYS_VULKAN, "failed: %s", msg);
 *   BLOG_WARN(SUBSYS_WINESERVER, "unexpected reply %d", code);
 *   BLOG_INFO(SUBSYS_KERNEL, "spawned pid %d", pid);
 *   BLOG_DEBUG(SUBSYS_VULKAN, "vkCreateImage: %dx%d fmt=%d", w, h, fmt);
 *   BLOG_TRACE(SUBSYS_SYSCALL, "syscall %d args=%x,%x", nr, a, b);
 *
 * Rate-limited variant for hot paths (logs at most once per N calls):
 *   BLOG_DEBUG_RL(SUBSYS_VULKAN, 1000, "frequent event %d", x);
 *
 * The enabled check is a single array lookup + integer compare when disabled.
 * Zero overhead when the level is off (modulo the branch).
 */

#ifndef __BWLOG_H__
#define __BWLOG_H__

#include <cstdio>

// Log levels (higher = more verbose)
#define BW64_LOG_ERROR 0
#define BW64_LOG_WARN  1
#define BW64_LOG_INFO  2
#define BW64_LOG_DEBUG 3
#define BW64_LOG_TRACE 4
#define BW64_LOG_OFF   -1

// Subsystem IDs - keep in sync with bwlog_subsys_names[] in log.cpp
#define SUBSYS_VULKAN     0   // vk64bridge, Vulkan calls, V2 chunks
#define SUBSYS_PAGE       1   // page-side (JS) - for C++ code that feeds the page
#define SUBSYS_KERNEL     2   // kernel: threads, memory, processes
#define SUBSYS_SYSCALL    3   // syscall entry/exit
#define SUBSYS_WINESERVER 4   // wineserver protocol requests/replies
#define SUBSYS_LAUNCHER   5   // session/launcher (C++ side)
#define SUBSYS_SHADER     6   // SPIR-V / shader conversion
#define SUBSYS_CPU        7   // CPU emulator
#define SUBSYS_MEMORY     8   // memory mapping/management
#define SUBSYS_AUDIO      9   // audio/DSP
#define SUBSYS_INPUT      10  // input handling
#define SUBSYS_GL         11  // OpenGL bridge
#define SUBSYS_FS         12  // filesystem / VFS
#define SUBSYS_X11        13  // X11 / windowing
#define BW64_SUBSYS_COUNT 14

#ifdef __cplusplus
extern "C" {
#endif

// Initialized once at startup from BW64_LOG env var. Direct array access
// is the fast path for the enabled check.
extern int bw64_log_levels[BW64_SUBSYS_COUNT];
extern int bw64_log_default_level;

// Called once during startup to parse BW64_LOG
void bw64_log_init(void);

// Slow path: format and emit. Called only when enabled.
void bw64_log_emit(int level, int subsys, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

// Fast enabled check - single array read + compare
static inline int bw64_log_enabled(int level, int subsys) {
    int sl = bw64_log_levels[subsys];
    if (sl < 0) sl = bw64_log_default_level;
    return level <= sl;
}

// Main macros
#define BLOG_ERROR(sub, ...) do { \
    if (bw64_log_enabled(BW64_LOG_ERROR, (sub))) \
        bw64_log_emit(BW64_LOG_ERROR, (sub), __VA_ARGS__); \
} while(0)

#define BLOG_WARN(sub, ...) do { \
    if (bw64_log_enabled(BW64_LOG_WARN, (sub))) \
        bw64_log_emit(BW64_LOG_WARN, (sub), __VA_ARGS__); \
} while(0)

#define BLOG_INFO(sub, ...) do { \
    if (bw64_log_enabled(BW64_LOG_INFO, (sub))) \
        bw64_log_emit(BW64_LOG_INFO, (sub), __VA_ARGS__); \
} while(0)

#define BLOG_DEBUG(sub, ...) do { \
    if (bw64_log_enabled(BW64_LOG_DEBUG, (sub))) \
        bw64_log_emit(BW64_LOG_DEBUG, (sub), __VA_ARGS__); \
} while(0)

#define BLOG_TRACE(sub, ...) do { \
    if (bw64_log_enabled(BW64_LOG_TRACE, (sub))) \
        bw64_log_emit(BW64_LOG_TRACE, (sub), __VA_ARGS__); \
} while(0)

// Rate-limited variant: logs at most once per N invocations.
// Uses a per-call-site counter. Thread-safe enough for logging.
#define BLOG_DEBUG_RL(sub, n, ...) do { \
    static unsigned long _rl_count = 0; \
    if (bw64_log_enabled(BW64_LOG_DEBUG, (sub)) && (++_rl_count % (n) == 1)) \
        bw64_log_emit(BW64_LOG_DEBUG, (sub), __VA_ARGS__); \
} while(0)

#define BLOG_TRACE_RL(sub, n, ...) do { \
    static unsigned long _rl_count = 0; \
    if (bw64_log_enabled(BW64_LOG_TRACE, (sub)) && (++_rl_count % (n) == 1)) \
        bw64_log_emit(BW64_LOG_TRACE, (sub), __VA_ARGS__); \
} while(0)

#endif // __BWLOG_H__
