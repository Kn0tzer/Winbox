// Shared Vulkan replay logging configuration and rate limits.
// ---------------------------------------------------------------------------
// Unified leveled logging (matches C++ bwlog.h levels).
// Config via URL: ?loglevel=debug&logfilter=vulkan:trace
// Or via window.__bwLogConfig = { default: 'warn', vulkan: 'trace' }.
// Levels: error=0, warn=1, info=2, debug=3, trace=4
// ---------------------------------------------------------------------------
const BWLOG_LEVELS = { error: 0, warn: 1, info: 2, debug: 3, trace: 4 };
const BWLOG_NAMES = ['ERROR', 'WARN', 'INFO', 'DEBUG', 'TRACE'];

function bwLogLevel() {
  if (typeof window !== 'undefined' && window.__bwLogConfig) {
    return window.__bwLogConfig;
  }
  return { default: 1 }; // default WARN
}

function bwLogEnabled(level, subsys) {
  try {
    const cfg = bwLogLevel();
    let sl = cfg[subsys];
    if (sl === undefined) sl = cfg.default;
    if (typeof sl === 'string') sl = BWLOG_LEVELS[sl] ?? 1;
    return level <= sl;
  } catch { return level <= 1; }
}

export function bwLog(level, subsys, msg) {
  if (!bwLogEnabled(level, subsys)) return;
  const tag = '[' + BWLOG_NAMES[level] + '][' + subsys + ']';
  try {
    const c = globalThis.console;
    if (!c) return;
    const full = tag + ' ' + msg;
    if (level === 0 && c.error) c.error(full);
    else if (level === 1 && c.warn) c.warn(full);
    else c.log(full);
  } catch { /* logging must never throw */ }
  try { if (typeof globalThis.bwRuntimeLog === 'function') globalThis.bwRuntimeLog(tag + ' ' + msg); } catch { /* ignore */ }
}

// Rate-limited variant for hot paths (logs at most once per N calls)
const _bwLogRlCounters = {};
export function bwLogRl(level, subsys, n, msg) {
  const key = subsys + ':' + msg.slice(0, 32);
  _bwLogRlCounters[key] = (_bwLogRlCounters[key] || 0) + 1;
  if (_bwLogRlCounters[key] % n === 1) bwLog(level, subsys, msg);
}

// Parse URL params for log config (called at init)
function bwLogInitFromUrl() {
  try {
    if (typeof window === 'undefined' || !window.location) return;
    const params = new URLSearchParams(window.location.search);
    const level = params.get('loglevel');
    const filter = params.get('logfilter');
    if (!level && !filter) return;
    const cfg = { default: 1 };
    if (level && BWLOG_LEVELS[level] !== undefined) cfg.default = BWLOG_LEVELS[level];
    if (filter) {
      for (const part of filter.split(',')) {
        const [sub, lvl] = part.split(':');
        if (sub && lvl && BWLOG_LEVELS[lvl] !== undefined) cfg[sub.trim()] = BWLOG_LEVELS[lvl];
      }
    }
    window.__bwLogConfig = cfg;
  } catch { /* ignore */ }
}
// Auto-init on module load
bwLogInitFromUrl();
