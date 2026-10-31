# Al-Khaser immediate-crash lane — final report

Branch: `lane-alkhaser-int2d`. Two fix commits on top of the current integ tip
(`692875bd8`, the dead-start fix, which the lane was rebased onto mid-flight —
see "Proof environment" below):

- `26e10926a` — cpu64: deliver IDT error code for INT imm8 #GP faults
- `066fd18ec` — cpu64: deliver INT3 as SIGTRAP #BP instead of yielding

Final build under test: dist wasm `6190dc6d04df11c7…`, BUILD_INFO git_sha
`066fd18ec`, git_dirty_files 0, dependency archive `3f1268a546e0` (identical to
the shipped site). Export verified by the build script itself
("Browser export verified: 34 .mjs syntax files; runtime version substituted;
dependency archive 3f1268a546e0 verified").

## The original crash (all-demos sweep, live build 5741034f3)

Al-Khaser printed its Debugger Detection results and died right after
"Software Breakpoints [ GOOD ]":

    wine: Unhandled privileged instruction at address 00000001400175D7
    exit_group status=3221225622 (0xC0000096 STATUS_PRIVILEGED_INSTRUCTION)

The next check is its `int 0x2d` probe (`mov rax,0; int 0x2d; nop; ret` at the
crash address in the binary). The checker's source registers a vectored handler
expecting EXCEPTION_BREAKPOINT: on Windows, `int 0x2d` surfaces as a breakpoint
exception with RIP already advanced.

## Root cause #1 — INT imm8 raised #GP with error code 0 (shared machinery)

Wine 8.0's x86-64 signal handler recognizes a software interrupt only from the
saved #GP error code: it requires `(err & 7) == 2` and dispatches on `err >> 3`
(0x2d → EXCEPTION_BREAKPOINT with Rip += 3, 0x29 → __fastfail, 0x2c → assertion
failure). With err == 0 it falls back to opcode classification and reports
EXCEPTION_PRIV_INSTRUCTION — the observed death, for any program, any vector.

CPU64's instruction-fault path hard-coded error code 0
(`raiseSyncFault(sig, trapNo, 0, ipStart)`), even though the parameter existed
and the page-fault paths already used it correctly.

Fix (`26e10926a`): the INT pre-dispatch in `CPU64::step` now computes the
architecturally correct IDT error code `(imm << 3) | 2` and passes it through
`faultAtInstruction` into the delivered frame's REG_ERR (REG_TRAPNO stays 13).
All other instruction faults keep error code 0. `tools/cpu-diff/harness.cpp`'s
stub gained the fifth parameter so that harness still links.

## Root cause #2 — INT3 never delivered at all (same class, found by the proof)

With #1 fixed, the website proof ran the checker past Interupt 0x2d [ GOOD ]
and then parked forever: opcode 0xCC logged "INT3 at RIP=… — yielding (no
SIGTRAP delivery yet)" and yielded unconditionally — a leftover from before
`raiseSyncFault` existed. The checker's next check ("Interupt 1") executes a
breakpoint instruction and waits for its exception; any guest thread executing
a deliberate breakpoint (debugger probes, assert paths) parked the same way,
handler or not.

Fix (`066fd18ec`): INT3 is #BP — Linux delivers SIGTRAP, trapno 3, si_code
TRAP_BRKPT, saved RIP already past the breakpoint byte; Wine's trap handler
converts exactly that frame into EXCEPTION_BREAKPOINT, backing the exception
address up over the CC itself. dsp_50 now routes through `faultAtInstruction`
with RIP pre-advanced (the helper gained an siCode parameter; other callers
keep si_code 0). With no handler installed, the fallback still yields, one
byte past the CC, exactly as before.

The class fixed, in shared machinery: **architected breakpoint/interrupt
exceptions from guest code are now delivered to the guest's registered
handlers with hardware-accurate contexts** — int 0x29/0x2c/0x2d via #GP+IDT
error code, and int3 via SIGTRAP. Nothing is Al-Khaser-specific; no Wine-side
or program-side change.

32-bit guests: the claim is scoped to the 64-bit CPU. The 32-bit path was not
observed to share this code; unproven either way.

## Tests

- `testOpcodeFault64Delivery` (native process suite) now covers, with a guest
  handler installed: INT 0x29/0x2c/0x2d → SIGSEGV frame with REG_ERR =
  (vector << 3) | 2, REG_TRAPNO = 13, REG_RIP and si_addr = fault address;
  and INT3 → SIGTRAP frame with saved RIP = fault address + 1, REG_ERR = 0,
  REG_TRAPNO = 3.
- **Sabotage checks (both fixes)**: INT fix reverted → test fails on all three
  vectors with `err=0x0`. INT3 hunk reverted → test fails ("INT3 did not
  deliver SIGTRAP to the guest handler", with the old yield line in the log).
  Restored → passes.
- Native suite (X64Test): every entry passes in slices on the final code,
  including the modified test and all signal/memory-fault neighbours
  (0–9, 29, 33–39, 40–46, 47–52: "0 tests FAILED"). A single-process full run
  still dies at exit 139 after ~33 passes — pre-existing at the base commits,
  independently A/B-proven on pristine code by the dead-start lane; crash
  neighbourhood is the clone/fork tests, unrelated to this change.
- JS unit suite against the final dist: **333 passed, 0 failed** (exit 0).
  (Also green, 333/0, on the intermediate INT-only build.)

## Website-path proof

Method: the lane's own dist served locally by the project's own server
(`tools/browser/serve.mjs` — byte ranges + cross-origin-isolation headers;
a plain static server cannot load the runtime, verified the hard way),
the demo archive fetched from the live catalog exactly as the real site
fetches it, the Al-Khaser card clicked in headless Chromium through the
probe-slot system. Driver + full runtime logs in this directory
(`site-proof.mjs`, `site-proof-*/runtime-log*.json`).

Proof environment note: attempts 1–2 ran on the pre-rebase base (shipped
integ tip) and parked before any window — the convicted dead-start defect of
that base (a separate lane's fix, `692875bd8`, has since landed in integ).
The lane was rebased onto that tip so the proof measures this lane's change,
not a separately-fixed boot defect; the INT patch is byte-identical across
the rebase. Attempt 3 (rebased, INT-only build) booted and reached the
checker; attempt 4 (same build) produced the first GOOD evidence and exposed
root cause #2; attempt 5 (final build, both fixes) is the verdict run:

**Attempt 5 verdict — the startup crash is gone; the program runs its
checks.** From `site-proof-5/runtime-log.json`:

- `[Initialisation]`, `[TLS Callbacks]`, then `[Debugger Detection]` — every
  check in the section completes, including:
  `[*] Checking Interupt 0x2d  [ GOOD ]` (was: fatal crash here)
  `[*] Checking Interupt 1     [ GOOD ]` (was: permanent park here)
  plus trap flag [ BAD ] and three write-watch [ BAD ]s — checker *verdicts*
  (it is an anti-analysis tool running in an emulator; BAD means "detected
  something", not a malfunction), Memory Breakpoints [ GOOD ], page exception
  breakpoints [ GOOD ], API hooks [ GOOD ], and the rest GOOD.
- Then `[DLL Injection Detection]`: EnumProcessModulesEx 32/64/ALL [ GOOD ],
  ToolHelp32 [ GOOD ], LdrEnumerateLoadedModules [ BAD ] (verdict).

## What remains — the next layer down (explicitly not this lane's fix)

The run then ends inside "Enumerating the process LDR directly": the checker
prints its own `[!] Error reading entry.`, then faults reading NULL at its
own instruction 0x14000D0F0 (`wine: Unhandled page fault on read access to
0000000000000000`), WineDbg cannot attach, and the process exits
3221225477 (0xC0000005). This defect was unreachable before — the program
never survived long enough to expose it.

Unproven: whether the bad loader-list entry originates in the emulator's
Wine loader data, in Wine 8.0's own list maintenance, or in the checker's
error path after the failed entry read. It is a data-correctness question in
a different neighbourhood from exception delivery and needs its own
investigation; it is recorded here with the full log (lines 313–359 of
`site-proof-5/runtime-log.json`) as the starting evidence.

Also still open, unrelated: several checker verdicts read [ BAD ] (trap flag,
write-watch buffer/IsDebuggerPresent, LdrEnumerate) — expected to some degree
for an anti-VM checker under emulation; whether each matches what the same
checker reports under desktop Wine on Linux is unmeasured.

## Guardrail compliance

No deploy, no push, live site untouched (it still serves 5741034f3 unless
another lane has since deployed). All work in this worktree on
`lane-alkhaser-int2d`; both fixes committed locally. Probes ran one at a
time through the probe-slot system; builds through the build lock/queue
(one export was run directly in this tree only after its queued build was
stopped, when the step provably touched nothing outside this worktree —
the full rebuilds before and after went through the normal queue).
Generated state stripped from the worktree at wrap-up.
