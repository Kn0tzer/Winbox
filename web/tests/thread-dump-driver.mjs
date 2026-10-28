// Shared interpretation of bw64_dump_guest_threads() results for the probe
// rigs (demo-matrix, framefloor-probe, gl-game-probe).
//
// The C++ dump never blocks on an emulator lock (see the deadlock contract
// in source/kernel/syscall64.cpp): it returns 1 when a dump was taken
// (possibly with labeled unavailable sections), 2 when the dump was NOT
// taken because a guest thread holds the log lock across a
// browser-proxied call, and 0 when diagnostics are not enabled. Before
// that contract existed the dump returned void and a contended call
// deadlocked the whole session; rigs must therefore treat rc=2 as a
// first-class, VISIBLE outcome -- never as a successful invocation and
// never silently.

export const THREAD_DUMP_RC = { DISABLED: 0, DUMPED: 1, UNAVAILABLE_LOCK_HELD: 2 };

export function threadDumpStatus(rc) {
  if (rc === THREAD_DUMP_RC.UNAVAILABLE_LOCK_HELD) return 'unavailable-lock-held';
  if (rc === THREAD_DUMP_RC.DUMPED) return 'dumped';
  if (rc === THREAD_DUMP_RC.DISABLED) return 'disabled';
  return 'unknown';
}

// Invoke the dump on a module-like object (a real Emscripten Module in a
// Node context, or a stand-in in tests). Returns { rc, via, status }.
export function callThreadDump(moduleLike) {
  if (!moduleLike) return { rc: null, via: null, status: 'no-module' };
  let rc;
  let via;
  if (typeof moduleLike._bw64_dump_guest_threads === 'function') {
    rc = moduleLike._bw64_dump_guest_threads();
    via = 'export';
  } else if (typeof moduleLike.ccall === 'function') {
    rc = moduleLike.ccall('bw64_dump_guest_threads', 'number', [], []);
    via = 'ccall';
  } else {
    return { rc: null, via: null, status: 'no-export' };
  }
  return { rc, via, status: threadDumpStatus(rc) };
}

// The one line a rig records for an invocation attempt. An unavailable
// dump is spelled out as such so a missing dump in the evidence is never
// mistaken for a taken one.
export function threadDumpDriverLine(rc, via, invocationNumber) {
  const suffix = invocationNumber == null ? '' : ` (#${invocationNumber})`;
  if (rc === THREAD_DUMP_RC.UNAVAILABLE_LOCK_HELD) {
    return `BW_THREAD_DUMP_DRIVER unavailable: dump not taken -- a guest thread holds the log lock across a browser-proxied call; skipped instead of deadlocking the session${suffix}`;
  }
  return `BW_THREAD_DUMP_DRIVER invoked via ${via}${suffix}`;
}
