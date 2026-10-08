# Boxedwine64 Unified Logging

## Quick Start

**Browser URL parameters:**
```
?loglevel=debug                    # DEBUG and below for all subsystems
?loglevel=warn&logfilter=vulkan:trace   # WARN globally, TRACE for vulkan
?loglevel=trace&logfilter=wineserver:trace,syscall:debug
```

**Levels:** `error` (0) < `warn` (1, default) < `info` (2) < `debug` (3) < `trace` (4)

**Subsystems:** `vulkan`, `page`, `kernel`, `syscall`, `wineserver`, `launcher`, `shader`, `cpu`, `memory`, `audio`, `input`, `gl`, `fs`, `x11`

## Architecture

### C++ (wasm) side
- **Header:** `include/bwlog.h`
- **Implementation:** `source/util/log.cpp` (appended)
- **Config:** `BW64_LOG` env var, parsed once at module load by `bw64_log_init()` via static initializer
- **Format:** `[LEVEL][subsys] message` → stdout → browser console

**Usage in C++:**
```cpp
#include "bwlog.h"

BLOG_ERROR(SUBSYS_VULKAN, "failed: %s", msg);
BLOG_WARN(SUBSYS_WINESERVER, "unexpected reply %d", code);
BLOG_INFO(SUBSYS_KERNEL, "spawned pid %d", pid);
BLOG_DEBUG(SUBSYS_VULKAN, "vkCreateImage: %dx%d", w, h);
BLOG_TRACE(SUBSYS_SYSCALL, "syscall %d", nr);

// Rate-limited for hot paths (logs once per N calls):
BLOG_DEBUG_RL(SUBSYS_VULKAN, 1000, "frequent event %d", x);
BLOG_TRACE_RL(SUBSYS_WINESERVER, 50, "req id=%u", id);
```

**Performance:** The enabled check is a single array lookup + integer compare. When disabled, overhead is negligible. Rate-limited variants prevent console overload in tight loops.

### Page (JavaScript) side
- **Location:** `web/vkwebgpu.mjs` — `bwLog()`, `bwLogRl()`, `bwLogEnabled()`
- **Config:** URL params `?loglevel=` and `?logfilter=`, or `window.__bwLogConfig = { default: 'warn', vulkan: 'trace' }`
- **Output:** browser console with `[LEVEL][subsys]` prefix, also forwarded to `bwRuntimeLog` if defined

**Usage in JS:**
```js
bwLog(0, 'vulkan', 'fatal: ' + msg);        // ERROR
bwLog(1, 'vulkan', 'warning: ' + msg);      // WARN
bwLog(2, 'vulkan', 'info: ' + msg);         // INFO
bwLog(3, 'vulkan', 'debug: ' + msg);        // DEBUG
bwLog(4, 'vulkan', 'trace detail');          // TRACE
bwLogRl(4, 'vulkan', 100, 'hot path msg');  // rate-limited
```

### Launcher
- **Location:** `project/emscripten/wine64-launcher.js`
- Parses `?loglevel=` and `?logfilter=` URL params
- Sets `ENV["BW64_LOG"]` for the wasm module before load
- Page-side `bwLogInitFromUrl()` in vkwebgpu.mjs reads the same params

## What Gets Logged

| Subsystem | Key events |
|-----------|-----------|
| `vulkan` | V2 chunk reception (rate-limited), READBACK queueing, pipeline creation, stub hits via existing `vk64: asked-for` |
| `wineserver` | Request ID + length + arch for each writev to Unix sockets (TRACE, rate-limited 1/50) |
| `kernel` | (add BLOG calls as needed) |
| `syscall` | (add BLOG calls as needed) |

## Debugging Recipes

**"Why is LIT=0?"**
```
?loglevel=debug&logfilter=vulkan:debug
```
Look for: V2 chunk reception, READBACK queueing, pipeline validation errors, `asked-for (unimplemented)` stub hits.

**"Why is the 32-bit boot stalled?"**
```
?loglevel=trace&logfilter=wineserver:trace
```
Look for: wineserver request IDs, which requests get replies, where the sequence stops.

**"What's the bridge doing?"**
```
?loglevel=debug&logfilter=vulkan:trace
```
Or the existing `?vktrace=1` (BW64_VKTRACE) for per-call tracing.

## Adding Logging to New Code

**C++:** Include `bwlog.h`, use `BLOG_*` macros with the appropriate subsystem. Use `_RL` variants in loops.

**JS:** Use `bwLog(level, subsys, msg)`. Levels are 0-4 matching the C++ side.

## Notes

- `BW64_VKTRACE=1` (existing) logs every Vulkan trap by name — complementary to the leveled system.
- `BW64_VKFRAME=1` dumps frame JSON — very verbose, use sparingly.
- Console overload protection: the syscall interception incident showed that unbounded logging in tight loops crashes the browser. Always use `_RL` variants for per-iteration logs.
