# FD-48 — restart-path session certification — 2026-10-31

Lane: `lane-fd48-restart-cert`, off integ `51d0aff98`.
Fix commit: `509c26752`. Register: FD-48 fixed on this branch;
FD-53 opened (see below).

## What was actually wrong

FD-48 was found at the batch merge gate: after the FD-01 race
killed a first boot attempt and the §4.68 stopgap restarted the
session, the restarted 32-bit session could reach a real desktop
— plausible deskpin handle (`0x10054`), five X11 connections,
explorer running in the server traces — and still never be
certified by the FD-47 hardened readiness test.

The gate could not decide from the persisted artifacts whether
the observer's explorer-open line was emitted before the
restarted observer consumed it, or never emitted in that shape.
Re-examination of every preserved 32-bit session stream (the
gate's two failed vkboot runs, the readiness lane's passing
single-session run, celeste's session) settles it: **the line is
never emitted on the 32-bit path at all, in any shape.**

The line the observer matches — `OPENED '<path>' -> fd <n>` —
is the 64-bit kernel's process-spawn signal, printed by
`sys_openat64` in `source/kernel/syscall64.cpp` on every
successful .exe open, by design ("Process-spawn signal:
successful .exe opens are rare (one per process) and worth one
line by default (probes detect EXE SPAWN from this…)"). The
32-bit open paths (`syscall_open` / `syscall_openat` in
`source/kernel/syscall.cpp`) never emitted an equivalent line.
So in a 32-bit session the handshake's explorer-launch element
had no counterpart in the guest stream — single-session or
restarted. Explorer's launch was visible the whole time in the
wineserver `+server` trace (`create_file` / `new_process` for
explorer.exe, present in both the dead and the genuine
attempts) — but that trace exists only when a harness enables
`WINEDEBUG=+server`, so teaching the observer to read it would
have left every other 32-bit shape (including plain
`bootGuest` consumers that pass no `winedbg`) just as blind.

A corollary worth recording plainly: the readiness lane's
report that its single-session `boot32-vkboot` pass went
"through the hardened predicate" is not supported by its own
preserved console, which contains the full boot (handle
`0x10052`, five X11 accepts) and zero `OPENED` lines. How that
run's `ready()` returned true cannot be reconstructed from the
preserved artifacts; what is certain is that the stream
evidence for the explorer element does not exist on the 32-bit
path before this fix. The batch gate's FD-48 reproductions are
the first times the shape was watched with the observer's state
in question, which is why the gap surfaced there and not
earlier.

## The fix (one file, `source/kernel/syscall.cpp`)

Both 32-bit open entry points now emit the same process-spawn
signal the 64-bit path has always emitted, through a shared
helper (`logExeSpawnSignal`): on a successful open (result is
not a negative errno) whose path names an `.exe`, log
`sys_open: OPENED '<path>' -> fd <n>` / `sys_openat: OPENED
'<path>' -> fd <n>` via `klog_fmt`. Failed opens never produce
the line. The format after the syscall name is byte-identical
to the 64-bit signal, so the launcher observer and the
measurement classifier match it unchanged.

**Nothing about the certification was weakened, or touched at
all.** The launcher, the observer, the three-element handshake
(plausible handle + ≥5 X11 accepts + explorer launched), the
restart watcher, and every readiness rule are byte-identical
to the integrated tree. The fix is entirely on the evidence
side: an event that demonstrably happens in every genuine
32-bit boot now leaves the same trace it always left on
64-bit. A dead 32-bit session still shows explorer opening and
dying (the four-arm x64 anatomy) — and still fails the other
two elements, as the proof below shows twice.

## Proof (all on a dist built from this branch; "Browser
export verified", dependency archive `de592964f772`)

### The restart shape itself (suite `boot32-vkboot`, 18 runs)

Per-segment classification of the preserved consoles (split at
each stopgap restart; a segment certifies iff plausible handle
+ ≥5 X11 + explorer `OPENED` all present in that segment):

- **Run 10 — restart pass.** Segment 1: signature printed, X11
  stalls at 2, no pin → not certified. Segment 2: handle
  `0x10054`, 5 X11, explorer `OPENED` lines present →
  certified; suite PASS. (This is the gate's exact FD-48
  anatomy — same handle — with the opposite outcome.)
- **Run 18 — restart pass, full budget.** Segment 1 dead (2
  X11), segment 2 dead (2 X11), segment 3: handle `0x10054`,
  5 X11, explorer `OPENED` → certified; suite PASS.
- **Runs 4 and 16 — genuinely dead, certified zero times.**
  Both attempts killed by the FD-01 race (run 16's second
  attempt reached 4 of 5 X11 before dying; run 4's the same).
  The explorer signal fired on every attempt and certification
  was still withheld for the full 600 s `bootGuest` budget —
  the conjunction working as designed, fail-closed and loud.
- **14 single-attempt passes** (runs 1–3, 5–9, 11–15, 17),
  every one certified on the first attempt.

18 runs: 16 certified, 2 not — and both uncertified runs are
ones where no desktop ever existed. False certifications: 0.

### Sabotage pair, both directions, on this branch's build

- **Broken-dispatch control** (this branch's dist with the
  wine64.zip win32u member swapped back to the unsigned
  `a8ca5fbb…` build; measurement driver, x64, 420 s window):
  the pin printed `hwnd=0xc000000d`; the OLD certification
  fires (`oldCert: true`, driver outcome "false-ready"); the
  hardened certification refused in-page for the whole window
  (`newCert: false`, `pageCert: false`, 0 X11 accepts, no
  explorer). A first control boot at load ~17–25 never reached
  the pin (the recorded crawl family) and likewise certified
  zero times.
- **Genuinely dead sessions**: runs 4 and 16 above.
- **Genuine starts**: the 16 certifications above, plus the
  x64 direction is unchanged code (see below).

### The 64-bit shape

This branch changes no 64-bit code path and no launcher byte;
the container contents are member-identical to integ's pin
(see composition note). On this branch's dist: two x64 boots
through the measurement driver died to the FD-01 race / crawl
family at sustained box load ~20 (explorer opened, desktop
never completed, correctly uncertified), one `dbwin-echo` run
starved in early wineboot (290 console lines in 360 s — the
same anatomy the batch gate recorded for that test on the
pre-FD-48 tree), and a third measurement attempt waited ~20
minutes without receiving a probe slot (another lane's build
was pending) and was stood down. A certified x64 start on
this exact dist was therefore not captured; the positive x64
direction rests on the batch gate's certified x64 start
(D3D12-64 SESSION READY) on the merged tree this branch
forks, with every x64-relevant byte identical, and on UC-2's
negative-direction proof above, which did run on this dist.

### Suites

- Dist build: "Browser export verified" (34 .mjs files;
  dependency archive `de592964f772` verified).
- Runtime completeness: 0 failures, 1 skip (the standing
  prefix64 skip).
- JS unit suite: **342 passed, 0 failed** (matches the batch
  gate; includes the FD-47 readiness contract tests, which
  pass unchanged — the observer they test is untouched).
- Native suite, main flavour: **659 test lines OK, 0 FAILED recorded; the run then stalled in a late test (threads spinning in userspace) as box load rose to ~28 and was terminated at ~30 minutes, so the final summary was not recorded. The identical suite completed 661/661 serially at the batch-3 gate on the parent tree; full serial completion is a merge-gate re-proof item for this branch, as it has been for other lanes.**
- Native suite, x64-test flavour: **not run on this branch — the main flavour's stall under load consumed the window. The flavour is rebuilt and run as standard at the merge gate (61/61 at batch-3).**

## Composition note (commit `75b10fcac`)

The worktree was restaged from the retained runtime archive
plus the live base `78dde7c2`, following the previous merges'
sequence: the pinned wine64.zip (`8ea33c43`) exists nowhere on
the box, so it was rebuilt with `stage-wine8-backports.sh` and
member-audited — 1257 members, name set identical, exactly one
content difference vs base (win32u.dll `a8ca5fbb` →
`49b87438`, the FD-40 fix) — i.e. contents identical to the
pinned container; only zip metadata differs (new sha
`d8284ce9…`, same 209,678,123 bytes). Pin updated and the
runtime artifact manifest regenerated (w64 row only).

## Found on the way (registered, not fixed)

**FD-53** — the 64-bit spawn signal changes dialect under a
diagnostic flag: with `BW64_DLLTRACE` set, `sys_openat64`
prints `DLLTRACE … OPEN '<path>'` for .exe opens *instead of*
the default `OPENED` line (the default line is explicitly
suppressed when the variable is set). In that configuration a
64-bit session's explorer launch produces no line the
readiness observer matches, so certification can never
complete — the same withheld-true-verdict shape as FD-48,
behind an environment variable no session harness currently
sets. Registered open; the fix direction (print the spawn
signal in the same format regardless of the trace flag, or
have the trace line keep the `OPENED` token) is for its own
lane, since it touches the 64-bit path this lane deliberately
left alone.
