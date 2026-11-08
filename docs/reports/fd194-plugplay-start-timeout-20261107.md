# FD-194: the registered 64-bit PlugPlay service never starts — error 1053 — 2026-11-07

Lane: `lane-fd194` (worktree `/home/ubuntu/winbox-project/worktrees/wt-fd194`),
based on integration tip `faf95d26c` plus the FD-189 registration commit
cherry-picked (`88fefb838`). Evidence: `test-results/fd194/` in the lane
worktree.

## What was filed

FD-194 (filed by the FD-189 lane on its branch as its lane-local FD-193;
the settled number follows the batch-10 composition plan, which assigns
FD-193 to the block-statistics filing): with FD-189's registration in
place, a 64-bit guest session finds the PlugPlay service present but it
never reaches the running state — the project's service probe reports
`fd189-64: PlugPlay … present state=1`, services.exe logs that sending
the start message failed, and the start returns error 1053. The 32-bit
session starts the same service fine (state=4). Closing terms: the
64-bit PlugPlay service reaches the running state in a guest session,
proven by the same probe reporting it present and running.

## Characterization (measured, in order)

1. RED on the starting build (dist of `88fefb838`, BUILD_INFO-verified,
   wasm byte-identical to the live batch-9 build): fd189-services64
   reports PlugPlay present state=1 on every run; the 32-bit control
   fd189-services32 reports state=4 (25,824 ms). With `+service`
   tracing, services.exe logs `err:service:process_send_start_message
   service L"PlugPlay" failed to start`; the plugplay child produces no
   service traces at all and exits with status 0 a few log lines AFTER
   the error — the signature of a process killed mid-start, not of one
   that failed a handshake step.
2. The handshake, decoded from the shipped binaries (services.exe,
   sechost.dll, plugplay.exe from wine64.zip): services.exe creates
   `\\.\pipe\net\NtControlPipe%u` (the id is the default DWORD value of
   `HKLM\SYSTEM\CurrentControlSet\Control\ServiceCurrent`), creates the
   process, then sends a start message (magic "WINE") with an
   overlapped write; on ERROR_IO_PENDING it waits
   `service_pipe_timeout` for the exchange to complete and fails the
   start (1053) when the wait expires. The child's sechost reads the
   pipe id, opens the pipe, and runs the dispatcher; plugplay.exe's
   whole main is one `StartServiceCtrlDispatcherW` call.
3. The dispatcher path is healthy standalone: a mingw fixture calling
   `StartServiceCtrlDispatcherW` in a plain session returns FALSE with
   error 1063 (no service pipe) after tracing the call, and standalone
   plugplay.exe itself makes the same call with its exact static table
   and exits through the same "failed to create control pipe" warning.
   Nothing in the pipe protocol is broken.
4. In-guest service fixtures (proper ServiceMain: register handler,
   report RUNNING, wait for stop), installed demand-start through an
   in-guest CreateServiceW fixture and started by services.exe itself,
   split exactly on DLL weight: advapi32-only starts (both CRT
   families), +gdi32 starts, and +imm32, +win32u (linked directly) and
   +user32 all fail with 1053 (both CRT families). The PE subsystem is
   not a discriminator either. A GUI-stack service child does not
   finish process initialisation inside the manager's budget.
5. With the `+server` channel on the failing session: the child's main
   thread made ~1,598 wineserver requests inside the 10 s window at
   ~6.3 ms per request with no idle gap, and was killed mid
   font-enumeration (a `get_key_value` returning "vGaFix.fon",
   `create_file` on the fonts directory, directory-cache misses). The
   send→failure elapsed measured 10,072 ms from the console's own
   uptime stamps. The constant's initial value in services.exe's data
   is 10000 ms. The failure is a timeout against a busy child, not a
   broken pipe: the child is working the whole time, just too slowly.
6. services.exe's `main` reads its startup knobs from
   `HKLM\System\CurrentControlSet\Control`: `ServicesPipeTimeout`,
   `WaitToKillServiceTimeout`, `AutoStartDelay`. For
   ServicesPipeTimeout it accepts ONLY the REG_SZ (string) form — it
   wcstol-parses the string and stores it if nonzero; a REG_DWORD
   value is silently ignored. Neither shipped prefix carried the
   value, so the 64-bit side ran with the 10 s built-in default that
   the 32-bit stack happens to fit.

## The fix

Commit `f05e260f8` on `lane-fd194`:

- `tools/rootfs64/prefix64-reg.txt`: a new fragment section
  `[System\\CurrentControlSet\\Control]` carrying
  `"ServicesPipeTimeout"="120000"` (120 s, string form) — the only
  shape services.exe reads. `build-prefix64.sh` merges it into the
  shipped prefix's system.reg idempotently; the rebuilt prefix64.zip's
  system.reg is byte-identical to the hand-patched archive proven in
  step "Proof" below except one blank separator line the builder adds.
- `tools/rootfs64/prepare_prefixes.py`: the shipped-prefix guard now
  requires the string-form ServicesPipeTimeout, above the built-in
  default, on win64 — previously nothing required any value, which is
  how the defect shipped green.
- `tools/rootfs64/test_prepare_prefixes.py` +
  `check_runtime_completeness.py`: unit tests (guard fails when the
  value is missing; guard fails when it is a REG_DWORD; the fragment
  merge installs it) and a completeness assertion on the shipped zip.
- Runtime assets re-pinned to `runtime-assets-20261107-fd194`: only
  prefix64.zip changed (19,066,828 B, sha256 `e87bfc99…`); retained
  archive `backups/winbox-runtime-assets-20261107-fd194/
  browser-runtime-20261107-fd194.tar` (491,755,520 B, sha256
  `7a609130…`); artifact manifest regenerated.

A byte-level caution from the first proof attempt: the fragment was
initially appended to a test copy of the archive with SINGLE
backslashes in the section header (a shell heredoc collapsed the
doubled form). Wine's system.reg format doubles the separators, so
the value landed under a bogus key and an in-guest registry fixture
read it back absent (query returned error 2). With the header in the
file's doubled form the same fixture reads `ServicesPipeTimeout`
back as REG_SZ "120000" (type 1, 14 bytes). The committed fragment
uses the doubled form throughout, like every other section in the
file, and the unit tests pin it.

## Proof

- Patched-dist run (the fd189 archive plus exactly the committed
  fragment appended): fd189-services64 reports
  `fd189-64: PlugPlay ok present state=4`, RESULT 0 — preserved
  console `test-results/fd194/console-run14-fixed-state4.log`.
- Final run on the fixing commit's own browser build (BUILD_INFO
  git_sha `f05e260f8`; build log carries 'Browser export verified'
  exactly once, preserved as `test-results/fd194/build-run4.log`;
  the dist archive's 152 members are content-identical to the pinned
  archive): fd189-services64 PASS, console reports
  `fd189-64: PlugPlay ok present state=4`, RESULT 0
  (`console-run15-final-state4.log`); the 32-bit control
  fd189-services32 PASS, `fd189-32: PlugPlay ok present state=4`,
  RESULT 0 (`console-run16-final32-control.log`).
- Unit: `test_prepare_prefixes` 13/13, including the two new FD-194
  tests; the prefix builder's own `check` (both prefixes) passes on
  the rebuilt archive.

## What this does not change

WineBus, WineUsb and MountMgr still fail to start with error 1114 on
both architectures — that is the separate driver-initialisation layer
(FD-188's remainder, settled as FD-195), untouched here. One related
observation, recorded for that work: a fixture ServiceMain whose first
call was `ImmCreateContext` was seen parked in a win32u
`NtUserCallNoParam` wait inside the budget window; PlugPlay's own
ServiceMain (create event, register handler, report status) reaches
RUNNING under the raised budget, so the observation does not block
this service, but a future GUI-calling service may meet it.

## Checking queue

Pipeline 42 (submitted from branch tip `9b40fae2a`; the pipeline's
own verify-source step confirms the mounted commit was
`9b40fae2a80f33d68ddf71071e868525c2bbfeb4` and the mounted tree
pristine): citations exit 0; js-legacy exit 0; native suites 692
tests 0 FAILED (main) and 116 tests 0 FAILED (x64); run-native
exit 0; js-unit 352 passed / 2 failed — provenance-artifacts and
runtime-completeness, the two known bare-checkout failures (the
queue job has no build output and no runtime store mounted; the
previous accepted pipeline on this base, 41, shows the identical
pair). Run locally in this prepared tree,
`check_runtime_completeness.py` passes 0 failures / 0 skips,
including the FD-194 assertions.
