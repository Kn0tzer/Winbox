# First-visit start reliability — 2026-10-31

Lane `lane-firstvisit`, worktree `wt-firstvisit`, base = integration tip
`538f03db5` (the tree whose build `692875bd8` is live). Not pushed, not
deployed; deployment is a separate coordinated step.

## The problem

A first-time visitor to the live site — fresh browser, nothing cached —
clicks a demo and the session often never starts, worst on the very
first visit. The build now live already contains the convicted
dead-start fix (guest write protection), and that defect's fingerprint
appears in none of the failed starts. The same live build starts 6/6
through a local page path but, measured by the deploy lane with the
launcher's automatic-restart workaround disabled, started only 6/18
through the public website (fresh-profile control: 0/4). Failed starts
stall partway through startup without crashing: exactly 2 of the usual
5 X11 connections are accepted, the desktop pin prints "The explorer
process failed to start.", and the session never recovers from inside.

## The cause

Both halves live in the staged Wine 8.0's desktop initialization
(`win32u`), and both are impatience bugs that real hardware never
notices because a native desktop process wins these races easily:

1. **A 10-second fuse on the desktop process.** When a process asks
   for the desktop window and the server has none, the client starts
   `explorer.exe /desktop` and waits for it to go input-idle with
   `NtUserWaitForInputIdle(process, 10000, FALSE)`
   (`dlls/win32u/winstation.c`, `get_desktop_window`). If that wait
   times out, the client re-asks with `force=1`, and the wineserver
   **fabricates a detached desktop window**. Fabrication is poison:
   when the real explorer later creates its desktop window, the
   server parents it under the fabricated one, and
   `desktop_window_proc`'s WM_NCCREATE refuses a desktop window with
   a non-zero parent — so explorer can never set the
   `__wine_display_device_guid` property that every other process
   needs to find the graphics driver. Under emulation, explorer's
   startup (loader + winex11 load + X init) routinely takes far
   longer than 10 s of wall time while the browser is simultaneously
   downloading, compiling and streaming ~210 MB of runtime assets —
   which is why cold first visits through the public route fail most,
   and warm/local starts usually win the race.

2. **A single-probe latch in every other process.** The first time a
   process needs the graphics driver, `load_desktop_driver`
   (`dlls/win32u/driver.c`) sends WM_NULL to the desktop window,
   reads the guid property, looks up the `GraphicsDriver` (or
   `DriverError`) registry value explorer publishes at the end of
   `load_graphics_driver`, and loads that driver. It probes **once**:
   any miss — no desktop window yet, no property yet, no registry
   value yet — returns FALSE and `load_driver()` installs the **null
   driver permanently for that process**. The desktop pin
   (`deskpin.c`) already retries its own window creation for 10
   minutes, but retries are futile once its process has latched the
   null driver, which is exactly the failure the launcher's restart
   workaround (§4.68) papers over by rebooting the whole session.

The signature line itself is win32u's default `driver_load_error`
string, printed by the null driver's `nodrv_CreateWindow` — it marks
a process that latched the null driver, in every observed failed boot
the pin's process (pid 14).

## The change

One backport patch against the pinned Wine 8.0 source, shipped the
same way as the existing Wine 8 backports (same-source rebuild, no
version skew; export surface verified identical to the staged Debian
build: 1321 exports, no additions, no removals; its only imports are
four ntdll functions, all present in the staged ntdll):

- `tools/buildWine/patches/wine8-win32u-desktop-wait.patch`
  - `winstation.c`: the input-idle wait for the just-started desktop
    process goes from 10 s to 300 s. The wait still returns
    immediately if the process exits, so a genuinely dead explorer
    fails as fast as before; only a slow-but-alive one is waited for.
    Staying on the un-forced path also means the server never
    fabricates the detached desktop window under a healthy desktop.
  - `driver.c`: `load_desktop_driver` retries its publication probe
    every 500 ms for up to ~300 s. A named driver or a published
    `DriverError` remains a definitive, immediate answer (returned on
    the first probe that sees it); only a *missing* publication —
    desktop still coming up — is retried. When the desktop is
    already up, the first probe succeeds exactly as before, at zero
    added cost.
- `tools/rootfs64/build-wine8-backports.sh`,
  `tools/rootfs64/stage-wine8-backports.sh`: `win32u.dll` joins the
  backport set (now user32, kernelbase, kernel32, iphlpapi, win32u).
- `tools/rootfs64/wine8-backports-x86_64/win32u.dll`: the rebuilt DLL,
  sha256 `a8ca5fbb6b0003397221cfe6140fc042a01abc444e8dfaf15bcf78abd7115f58`
  (354,218 bytes), pinned in
  `tools/rootfs64/check_runtime_completeness.py`
  (`WINE8_BACKPORT_PINS`) and documented in the backports README.
- `tools/browser/runtime-assets.json`: `wine64.zip` re-pinned to the
  staged canonical zip, sha256
  `44581bbf3f11f15dd1a993d3a4f2f07c7b47c5f72fdf10b4a82d17b108ee983a`,
  209,677,994 bytes.
- `check_runtime_completeness.py` on this tree: **0 failures** (5
  standing environmental skips).

## The measurement

Method (identical for before and after): a byte-identical copy of the
live tree (`692875bd8` / wasm `b1f0c1da`) served locally by the
project's own test server; per boot a **fresh Chromium process**
(fresh profile, cold cache) opens the session URL
(`cmd /c echo vkboot`, `session=1&persist=0&fresh=1&arch=x64`) with
**`bwretry=2`** — the launcher's restart budget already exhausted, so
the automatic-restart workaround cannot rescue a failed first attempt
and every counted start is a genuine first-attempt start. A boot
counts as started when `bwRuntime.ready()` turns true within 360 s.
The "after" dist is the same copy with exactly one zip member
replaced — `win32u.dll` inside `runtime/wine64.zip`, swapped with the
stage script's own merge algorithm; the swap tool verified the new
member's hash equals the pinned backport hash and every other
member's data hash is unchanged. Harness:
`test-results/measure-firstvisit.mjs`; raw per-boot console logs and
`summary.json` under `test-results/firstvisit/{before,after}/`.

- Before: **0 / 6** started. All 6 boots printed the explorer-failed
  signature, none reached deskpin-ready, and all stalled at exactly 2
  accepted X11 connections — the known failure fingerprint, reproduced
  six times out of six under this (coldest) method.
- After: **6 / 6** started, first attempt every time. Ready times:
  61.3 s, 37.5 s, 34.7 s, 35.2 s, 35.9 s, 33.6 s (median ~35.9 s).
  Zero boots printed the signature; deskpin reported ready in all 6.

Load note, stated for honesty: the before boots ran at 1-minute load
averages of roughly 6–15 and the after boots at roughly 4–8 (the box
was shared throughout). The first before boot started at load 6.3 —
the same range as the after boots — and still failed with the full
fingerprint, while every after boot at those loads started in about
half a minute; and the fingerprint itself flipped categorically
(signature 6/6 → 0/6, deskpin-ready 0/6 → 6/6), which is the
mechanism's signature, not a load effect. Per-boot rows and load
averages are in the two `summary.json` files.

## Suites

All on this tree (base `538f03db5` + this change), run serially:

- Emulator build: `tools/bw64-build.sh` — literal "Browser export
  verified" (dependency archive `3f1268a546e0` verified). The dist's
  wasm is `b1f0c1da`, byte-identical to the live build's, as expected:
  this change touches no emulator code. The dist's `wine64.zip`
  hashes to the re-pinned manifest value `44581bbf…` and its win32u
  member to the pinned backport hash.
- `check_runtime_completeness.py` against the built tree: **0
  failures** (1 standing environmental skip; the regenerated
  `docs/known-gaps/runtime-artifact-manifest.md` differs by exactly
  the one `wine64.zip` row).
- JS unit suite (`BW_TEST_DIST=<this tree's dist>`,
  `node web/tests/suite/runner.mjs --unit`): **333 passed, 0
  failed**.
- Emulator native suite (serial, `boxedwine 0 0 1`): **648 tests, 0
  FAILED**, exit 0.
- Confirmation boots on this tree's own canonical dist, same
  harness and method: **2/2 started** (31.7 s, 33.6 s), no signature.

The two defects documented at the base (project/linux Release app
link failure; x64-test full-run segfault after 33 passes) are
unchanged and untouched by this lane — no emulator code changed.

## Residuals, stated plainly

- The 32-bit staged set is WineHQ 10, whose `win32u` carries the same
  10 s constant and the same single-probe latch (verified in the
  Wine 10.0 source). This project's backport pipeline builds only
  Wine 8.0 x86_64, so the 32-bit side is **not** covered by this fix;
  it needs its own route (a Wine 10 backport build or a binary-level
  approach) and is a follow-up, not part of this lane.
- Nothing here is deployed. The live site still serves `692875bd8`
  without the win32u backport; deployment is the coordinator's
  separate step, after which the live first-visit rate should be
  re-measured through the public route.
