# FD-54 — the 64-bit exe spawn signal under BW64_DLLTRACE (2026-11-01)

Lane `lane-fd54`, off integration tip `1d932714c`. Evidence:
`test-results/fd54/` (RED/GREEN single-test logs, both suite logs,
run-native log, browser build log).

## The defect as filed

With `BW64_DLLTRACE` set, the 64-bit process-spawn signal changed
dialect and 64-bit session certification could never complete: in
`sys_openat64` (`source/kernel/syscall64.cpp`) the default
spawn-signal line (`sys_openat64: OPENED '<path>' -> fd <n>` on a
successful .exe open) was explicitly suppressed when the flag was
set, replaced by a `DLLTRACE pid=<n> OPEN  '<path>' -> fd <n>` line.

## Mechanism — verified at both ends

- Emitter: the signal's condition was
  `if (!getenv("BW64_DLLTRACE") && strstr(path, ".exe"))`. A tree-wide
  search confirms this was the only place a diagnostic flag
  suppressed a default line: the only other `BW64_DLLTRACE` uses in
  `syscall64.cpp` gate the DLLTRACE lines themselves.
- Observer: the launcher's readiness observer
  (`observeSessionOutput` in `project/emscripten/wine64-launcher.js`)
  sets its explorer-launched element on
  `/OPENED '[^']*explorer\.exe'/i`. The DLLTRACE dialect
  (`OPEN  '<path>'`, two spaces, no `ED`) never matches, so with the
  flag set the FD-47 handshake's explorer element could never become
  true and no 64-bit session could be certified — the same
  withheld-true-verdict shape as FD-48, behind a flag.
- The 32-bit open paths never had the defect: their helper
  (`logExeSpawnSignal` in `source/kernel/syscall.cpp`, FD-48) emits
  the signal unconditionally.

## Fix (`e3660ab57`)

The `!getenv("BW64_DLLTRACE")` condition is removed: the signal is
emitted on every successful .exe open, in the default format,
regardless of the trace flag. The DLLTRACE line is untouched, so a
traced session now logs both lines — the richer diagnostic and the
contract signal. The comment at the emission now states the contract
explicitly (a readiness-handshake signal, not a debug aid; diagnostic
flags must not suppress it), matching the FD-48 comment's terms on
the 32-bit side. Nothing else in the open path changed; failed opens
still never produce the signal, in either configuration.

## Proof (`bc2cfd046`)

New test `testExeSpawnSignal64` (x64 suite), which drives the real
dispatcher: it installs a fixture filesystem root holding one real
`.exe`, invokes `openat64` through `ksyscall64`, and captures klog
via `KSystem::logFile` (every `internal_log` line is mirrored there
when a log file is open). It pins four things:

1. Control, flag unset: the signal is emitted (passes before and
   after the fix — the default path was never broken).
2. Flag set: the DLLTRACE line appears (the trace path really was
   active) **and** the spawn signal is still emitted in its default
   format.
3. Failure discipline, flag set: a failed .exe open emits no signal.

RED on the unfixed tree (single test, serial;
`test-results/fd54/red-single.log`):

```
sys_openat64: OPENED '/spawnprobe.exe' -> fd 0          (control)
DLLTRACE pid=10 OPEN  '/spawnprobe.exe' -> fd 0          (traced: no OPENED line)
DLLTRACE pid=10 FAIL open('/missing.exe') rc=-2 cwd=''
Test x64 exe spawn signal under DLLTRACE (FD-54) ... FAILED
  FD-54: x64 spawn signal suppressed under BW64_DLLTRACE=1
```

GREEN after the fix (`test-results/fd54/green-single.log`): the
traced open logs both lines and the test passes; the failed open
still logs no signal line. The RED run is the sabotage check: the
same test code fails on the unfixed code for exactly the filed
reason, and its control and trace-presence assertions guard against
"fixing" the defect by breaking the default path or the trace.

## Suites and build (fixed tree, all read from preserved logs)

- x64 native suite, serial: **67/67**, 0 FAILED (66 baseline + the
  new test), 21 s.
- Main native suite, serial: **667/667**, 0 FAILED — the standing
  baseline, unchanged (the fix and test live in the x64 build).
- run-native: exit 0, **56 PASS** — the standing baseline.
- Browser build (`tools/bw64-build.sh`, staging via the retained
  archive bootstrap — "Restored and verified 13 runtime assets",
  `--check` PASS — plus the node_modules link and the generated
  payloads copied from the integration tree): **"Browser export
  verified" / BUILD COMPLETE**; wasm `35302eba…` (new — the fix is
  in shared code); support archive `c4dbe35986a5…`, the standing
  deterministic pack, unchanged. No browser probe was run: the
  change is a kernel log-line condition, its behaviour is pinned by
  the native test, and the composition gate owns the boot checks.

## Register

FD-54 is marked CLOSED with a resolution block naming the fixing
commit and this proof; closure **CL-35** is filed provisionally
(next free number on the integration register at filing; the
composing merge verifies by audit and renumbers if a sibling lane
took it first). Audit on this tree: 78 FD entry bullets, zero
duplicates; closures CL-01…CL-35 in document order.

No other defect was found on this lane: the tree-wide search for
sibling suppressions (any other default line conditioned on
`BW64_DLLTRACE`) came back empty, which the resolution block
records.
