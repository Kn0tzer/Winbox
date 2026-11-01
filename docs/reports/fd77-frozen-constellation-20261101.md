# FD-77 follow-up: the frozen constellation, resolved — and the dump taught to see it

Lane `b957f35c` (branch `lane-fd77`, worktree `wt-fd77`), 2026-11-01.
Base: integ tip `39926f47c` (the FD-12 batch). Nothing deployed; the
live site was not touched. Evidence: `test-results/fd77/` in this tree
(driver logs + consoles for three consumer runs), plus the preserved
FD-12/deploy captures re-read firsthand
(`test-results/fd12/` in `wt-fd12`;
`test-results/deploy-fd12-20261101/` on integ).

## Mandate

The FD-77 residue: a session boots fully, then silently stops — no
window, no error — while a group of the main process's threads sits in
state `running` with no wait condition, byte-identical across repeated
dumps. Trace that configuration to its mechanism and fix it there, or
prove it originates outside the project's code and harden the project
side so the stall is visible and loudly reported instead of silent.

## What the frozen threads actually are (new, firsthand)

**The constellation is deterministic.** Three independent parked
captures — hunt-4 (pre-amplifier-fix build), the armed public-route
capture b1 (shipped FD-12 build), and this lane's `run-loaddll-1`
(same shipped build, loader tracing on) — freeze the same guest
threads at the same guest addresses, with the same argument patterns:
RIPs `0x7069a7c6`, `0x724f0f47`, `0x724f131c`, `0x71ef18ad` and a
thread at the Wine syscall stub `0xb001abb0` appear in all three;
several more addresses repeat in two of three. Module bases are
identical across runs (a `+loaddll` run puts chrome.dll at
`0x6EA80000`), so the addresses resolve absolutely:

- The chrome.dll threads sit at **basic-block entries inside
  chrome.dll's `.text`** (base `0x6EA80000`): RVAs `0x1C1A7C6`,
  `0x3A70F47`, `0x3A7131C` (a jump target of its neighbour),
  `0x34718AD`, `0x3460C27` (two threads), `0x28FBD40` and `0x5FB2F0`
  (both exact function prologues). The instructions there are
  ordinary code — no wait, no syscall, no spin loop.
- The main thread (tid 11) sits in **user32.dll** (`0x7A4A0000 +
  0xE6E4` in b1); one thread sits in **shell32.dll**.
- One thread sits at the Wine syscall-dispatcher stub with an
  NTSTATUS `0xC000000D` in its frame — the same shape healthy
  captures show transiently during startup.
- In b1, threads were seen joining the frozen set between dumps
  (blocked on wineserver reads at dump 1, frozen by dump 2): the
  freeze takes each thread at the next block it enters after its
  current wait completes. No thread was created after dump 1.

**Frozen snapshots alone do not convict — a correction this lane
adds to the record.** The healthy control run (`run-plain-2`, mapped
at 41.8 s, input completed) holds **24** threads in state `running`
with byte-identical snapshots across 11 dumps / 300 s, including 16
threads parked at the same syscall stub — a normal guest thread-pool
wait state this emulator's dump simply cannot see into. The old
dump's `running` label conflates three different states: executing
guest code, waiting inside a host-side path (a browser-proxied call,
a wait inside a syscall implementation), and genuinely stuck. Any
future reading of a parked capture has to separate them by executed
work, not by register stillness.

**Ruled out on evidence, not argument:**

- Not a guest wait: `waitingCond` is null for the whole set (the
  dump reads it directly), and every condition wait in the MT build
  sets it (`source/util/synchronization.cpp`).
- Not the fetch layer's timed paths: across 900 s parked captures,
  none of its loud lines ever appeared (60 s JS stall watchdog,
  `STALLED`/`FAILED` retry logs, the 240 s in-flight waiter
  deadline). A thread inside those paths would have produced one.
- Not a dead browser main thread: the guestdiag dumps (main-thread
  `ccall`s) ran on schedule for the whole freeze in every capture.
- Not the FD-12 amplifier's pile-up shape: the constellation
  persisted unchanged after that fix (b1), per FD-77's own specified
  question.
- Not "the box was busy": the constellation is identical on the
  public route (the user's own browser) and locally.

**Settled by this lane's instrumented run (below):** the frozen
threads are *not* executing guest code and *not* inside the lazy-fetch
proxy call. What remains is a host-side wait entered from the
demand/fault path family — after the fetch machinery itself, which
the same run cleared.

## What this lane shipped (the hardening half of the mandate)

The mechanism did not yield to conviction from outside the page, so
the project side is hardened exactly where the silence was: the
guest thread dump — the instrument every parked capture already
uses — can now see the states it previously conflated.

1. **Per-thread no-progress, measured in executed ops**
   (`source/kernel/syscall64.cpp`). Each running thread's line gains
   `noprogress_ms`, derived from the CPU's executed-op counter
   (`blockInstructionCount`, 32-bit core; `instructionCount`, 64-bit
   core) remembered across dumps: it advances only while the
   thread's own host thread executes guest ops. Executing threads
   (including genuine guest spin loops) read ~0; host-waiting and
   stuck threads accumulate. A per-process summary line
   (`BW_THREAD_DUMP proc pid=… running=… min_noprogress_ms=…`)
   makes the park machine-readable: *a process whose every running
   thread shows a large `min_noprogress_ms` while its window never
   appears is the FD-77 park, by a definition a future capture can
   check mechanically.*
2. **Fetch-proxy occupancy** (`source/io/fszipurl.cpp`,
   declaration in `fszipurl.h`). The synchronous proxy call that
   services lazy fetches now counts its in-flight callers (atomic
   slots keyed by guest pid/tid, no locks), and each dump footer
   prints `BW_THREAD_DUMP proxyfetch inflight=… oldest_tid=…
   oldest_ms=…`. The fetch layer's design comment assumes every
   sync fetch "settles within the JS stall window (60 s)" — true
   only once the main thread runs the task, which is precisely the
   step no instrument watched.
3. **The x86 dump no longer masks EAX for running threads**
   (`eax=0x…` field): for a thread stopped at the syscall stub, EAX
   is the syscall number it is inside, which names the wait family
   the old line hid.

The dump's never-block contract is preserved: the new bookkeeping
uses dump-private statics under a mutex only the dump takes, and
atomics on the fetch side.

## Verification

- Build: full `tools/bw64-build.sh` on the patched tree — BUILD
  COMPLETE (wasm `bbd4cd84…`); the unpatched tree at the same base
  first reproduced the shipped wasm byte-identically (`0d6a7ee9…`),
  so the lane's starting point was the shipped code, not a
  neighbour of it.
- Citation audit: no pinned citations reference the two touched
  source files (checked against the audit's own target list); the
  line shifts cannot drift a pin.
- Instrumented consumer run (`run-instr-3`, the patched build):
  **parked** — and the new instruments answered the questions they
  were built for, in the same capture:
  - `proxyfetch inflight=0` in every dump from 64.7 s to 904.9 s
    (one transient inflight=1, 6 ms old, during boot). **No thread
    was inside the lazy-fetch proxy call at any point in the
    freeze.** Combined with zero `STALL` lines and the fetch
    milestone stopping at the usual 192 MB (48.7 s), the fetch
    layer — proxy, watchdog, waiter deadline and all — is cleared
    by measurement, not by argument.
  - All 9 running threads of the main process show
    `noprogress_ms` growing dump over dump to **810–870 s**:
    **zero guest ops executed for the entire freeze.** The frozen
    threads are host-asleep in a wait — not spinning, not
    livelocked in guest code.
  - The frozen set is the same constellation (block-entry addresses
    in chrome.dll, incl. `0x7137bd40` and two threads at
    `0x724f131c`; one thread at the syscall stub, `eax=0x90`),
    entered between ~35 s and ~95 s: each thread trapped out of
    the interpreter at the first block of code it was entering
    and never returned. With every fetch-path wait excluded by
    the instruments above, the wait sits in the demand/fault path
    around the fetch: the per-process memory mutex (a plain
    recursive mutex — held across one of the fault path's
    remaining slow sections, or piled on by the frozen set behind
    one holder), the RAM-page allocator's mutex, or a proxied
    filesystem call on a non-archive backing file are the
    remaining named candidates; the fault path does not currently
    record which stage a thread is in.
- One honest caveat about this lane's own process: its single
  OS-level CPU sample was taken, in error, on the healthy control
  run's renderer after that session had mapped and gone idle. It
  is evidence of nothing and is not used above; the op-counter
  data supersedes it.
- Native suite / JS unit suite: not re-run by this lane (the
  changes are diagnostics-only on the dump path plus atomics
  around an existing fetch call; no behaviour changes); the merge
  gate runs the full set as standard.

## What remains open (FD-77 stays open, narrowed again)

- The wait itself, now localized to the demand/fault path minus
  its fetch machinery. The specified next instrument is small:
  a per-thread fault-stage marker (set on entry to each stage of
  the demand path — memory-mutex acquire, RAM-page alloc, backing
  read, install) that the dump prints for running threads, plus
  an owner field on the memory mutex so a pile-up names its
  holder. One parked capture with that dump separates the three
  remaining candidates.
- Surfacing in the default (unarmed) session: the launcher should
  treat "booted, no window, no guest output" as a reported failure
  rather than an endless quiet page, using the dump's proc line as
  the signal. That is a launcher change with its own review; it is
  specified here, not smuggled into this lane.

Evidence index: `test-results/fd77/` — `run-loaddll-1/` (parked,
loader tracing; the base-resolution source), `run-plain-2/`
(healthy control), `run-instr-3/` (instrumented build, parked,
30 dumps), `build1..5.log` (bootstrap + builds; builds 1–3 document
the fresh-worktree staging traps: `node_modules` symlink and the
dxvk/vkd3d/gstreamer generated payloads).
