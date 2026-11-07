# FD-189: the shipped 64-bit prefix has no PlugPlay or bus-driver registration — 2026-11-06

Lane: `lane-fd189` (worktree `/home/ubuntu/winbox-project/worktrees/wt-fd189`),
based on integration tip `faf95d26c`. Evidence: `test-results/fd189/` in
the lane worktree.

## What was filed

FD-189, filed by the gamepad lane: in a 64-bit guest session the PlugPlay,
WineBus and WineUsb services do not exist (a guest probe's OpenService
answers error 1060 for all three) and the root device enumeration is empty.
The shipped `prefix64.zip`'s `system.reg` contains no PlugPlay or WineBus
entries at all; the 32-bit `prefix32.zip` carries them (its guest probe shows
PlugPlay running and both bus services registered, stopped). Closing terms:
the 64-bit prefix (or its in-session initialisation) registers the PlugPlay
service and the ROOT\WINE bus devices the way the 32-bit one does, proven by
the same guest probe reporting the services present.

## Characterization (measured, in order)

1. The shipped `prefix64.zip` (19,133,895 B, `b1bfa64e…`) carries exactly
   ONE service registration in `system.reg` — MountMgr. The 32-bit
   `prefix32.zip` carries 19, including PlugPlay (Start=2, Type=0x20,
   `C:\windows\system32\plugplay.exe`), winebus and wineusb (Start=3,
   Type=1, Group=WinePlugPlay, `C:\windows\system32\drivers\wine*.sys`),
   plus `Enum\ROOT\WINE\WINEBUS` and `WINEUSB` with their Device
   Parameters subkeys. The 64-bit prefix has none of these and no
   `WinePlugPlay`/`WINEBUS`/`WINEUSB`/`PlugPlay` mention anywhere.
2. Why the 32-bit prefix has them and the 64-bit one does not: the 32-bit
   prefix is produced by actually running native 32-bit Wine
   (`tools/build-prefix32.sh`: `wineboot --init` twice), so the real
   wineboot registers the services. The 64-bit prefix is assembled
   ad-hoc (`tools/rootfs64/build-prefix64.sh` starts from the previous
   `prefix64.zip` and merges `prefix64-reg.txt` fragments) and never had
   a real wineboot run — the base zip was staged ad-hoc.
3. The values are authoritative for the 64-bit Wine too: the 64-bit
   wine's own `wine.inf` (Debian Wine 8.0, in `wine64.zip`) defines
   `[PlugPlayService]` with ServiceType 32, StartType 2 and
   `%11%\plugplay.exe`, matching the 32-bit prefix's PlugPlay section
   exactly; the bus drivers point at `winebus.sys`/`wineusb.sys`, which
   the 64-bit wine zip ships (`x86_64-windows/`, plus the `winebus.so`/
   `wineusb.so` Unix modules the service-runtime check pins).
4. The gap was invisible to the project's own guard: `prepare_prefixes`
   `_validate_prefix` only required a missing service registration
   when `require_full_init=True` (the native-boot packaging path), and
   only required the `Enum\ROOT\WINE` keys for win32 or full-init. The
   shipped `check`/`check-one` path therefore passed a 64-bit prefix
   with no PnP stack at all — the guard was weakened to match the
   shipped artifact instead of the artifact being fixed. A unit test
   (`test_win64_runtime_defaults_are_allowed_when_prefix_has_no_service_overrides`)
   encoded that leniency by name.

## The fix

- `tools/rootfs64/prefix64-reg.txt`: the seven sections a real wineboot
  writes for the PnP stack, verbatim from the 32-bit prefix's
  `system.reg` — `Services\PlugPlay`, `Services\winebus`,
  `Services\wineusb`, `Enum\ROOT\WINE\WINEBUS` (+ Device Parameters),
  `Enum\ROOT\WINE\WINEUSB` (+ Device Parameters). `build-prefix64.sh`
  already merges every section the fragment carries that `system.reg`
  lacks, idempotently; no new staging script was needed.
- `tools/rootfs64/prepare_prefixes.py`: the guard now requires the
  PlugPlay/winebus/wineusb registrations and all four controller/HID
  keys (including both `Enum\ROOT\WINE` keys) on win64 in the shipped
  `check` path too. RpcSs keeps the old allowance: the shipped 64-bit
  prefix has never registered it and changing that is outside FD-189's
  scope. The `Driver` values in the Enum sections reference
  `Control\Class\{4D36E97D…}` keys that exist in neither prefix — the
  same dangling reference the 32-bit side ships, where the probe still
  reports the devices.
- `tools/rootfs64/test_prepare_prefixes.py`: the lenient test is
  rewritten to the new behavior (RpcSs still allowed missing; PlugPlay
  missing and Enum keys missing now fail), plus
  `test_win64_guard_requires_pnp_services_and_bus_keys` and
  `test_pnp_fragment_registers_plugplay_and_bus_devices` (the merge
  installs exactly the PnP sections into a PnP-less registry and the
  result validates).
- `tools/rootfs64/check_runtime_completeness.py`: new check
  `prefix64 PlugPlay + bus services (FD-189)` — the five key sections
  must be present in the shipped `system.reg` and the fragment merge
  must be a no-op (no drift); the built-artifact check also asserts
  them. Documented as item 4d in the checker's header.
- Guest proof fixture `tools/guest-fixtures/fd189-services.c` (registered
  in `web/tests/suite/lib/guest-fixtures.mjs`, `-ladvapi32 -lsetupapi`)
  plus `web/tests/suite/browser/fd189-services64.test.mjs` and
  `fd189-services32.test.mjs`: the gamepad lane's diagnostic probe in
  fixture form — OpenService for the three services, SetupDi root
  enumeration counting `ROOT\WINE` devices. RESULT 0 means all three
  services present and both bus devices enumerate. Presence is the
  criterion; whether a service is running is reported, not required
  (driver start is FD-173's separate question).
- Re-pin through the retained-archive procedure: `prefix64.zip` is the
  only changed member (19,066,808 B, sha256
  `197251244ccb367f6a46c3fc8ec7fdaaa425891f75aa02113f27f1e3d3126b62`);
  new retained archive `browser-runtime-20261106-fd189.tar`
  (491,755,520 B, sha256
  `c0066c976cf2f6de925af9981196d6d194d833d262a9a18358965a4a8975a6c9`)
  at `/home/ubuntu/winbox-project/backups/winbox-runtime-assets-20261106-fd189/`;
  `tools/browser/runtime-assets.json` re-pinned to release
  `runtime-assets-20261106-fd189`; the runtime-artifact manifest
  regenerated (one line changed: the prefix64 row; member count
  unchanged).

## Proof (RED → GREEN, all on this lane's tree)

Evidence in `test-results/fd189/`.

RED:

1. Checker against the unfixed canonical set: the new FD-189 check
   fails with `prefix64.zip system.reg:
   [System\\CurrentControlSet\\Services\\PlugPlay] missing (FD-189)`
   (2 failures, both FD-189; everything else PASS), and
   `check-one --archive prefix64.zip --arch win64` fails with
   `missing service registration PlugPlay; missing service
   registration winebus; missing service registration wineusb;
   missing controller/HID registry key …Enum\\ROOT\\WINE\\WINEBUS;
   …WINEUSB` — the filed gap, named.
2. Unit tests RED on the old checker: the two new tests fail against
   the pre-change `prepare_prefixes.py` (verified by running them
   against the HEAD version in a scratch copy) and pass with the fix.
3. Browser RED (`red64.log`, guest console
   `test-results/suite-fd189-services64/console.log`, cleaned lines in
   `red64-fd189-lines.log`): against the baseline build (wasm
   `92b36180…`, BUILD_INFO git_sha `faf95d26c`) the 64-bit fixture
   reports `PlugPlay FAIL OpenService err=1060`, `WineBus FAIL
   OpenService err=1060`, `WineUsb FAIL OpenService err=1060`,
   `root-wine-devices FAIL total=0 wine=0`, `RESULT 4` — the filed
   signature verbatim. The 32-bit control on the same build passes
   (`red32.log`, `red32-fd189-lines.log`): all three services present,
   `ROOT\WINE\WINEBUS` and `ROOT\WINE\WINEUSB` enumerate,
   `total=2 wine=2`, `RESULT 0` — proving the probe itself works.

GREEN:

4. Rebuilt prefix: `build-prefix64.sh` merges exactly the seven PnP
   sections (`build-prefix64.log`), `check-one` and the full `check`
   pass with the tightened guard, and a per-file comparison against
   the canonical zip shows the same 152 files with ONLY `system.reg`
   differing.
5. Archive procedure: rebuilding the 20261106 tar from its pinned
   files with the deterministic method (GNU tar, mode 0644, uid/gid 0,
   mtime 0, manifest order) reproduced it byte-for-byte (sha256
   `6006959c…`) before trusting the procedure; during the new
   archive's build all 12 unchanged members were asserted
   byte-identical to their pins.
6. Clean-bootstrap reproduction: a scratch root holding only
   `bootstrap.py` + the new `runtime-assets.json` restored from the
   new tar — "Restored and verified 13 runtime assets" — and the
   restored `prefix64.zip`'s `system.reg` carries all seven PnP
   sections.
7. Completeness checker on the fixed tree: 0 failures, 0 skips;
   `bootstrap.py --check`: "Runtime assets verified (13 files)".
8. Fixed build (`build-fixed.log`): BUILD_EXIT=0, "Browser export
   verified"; the exported `dist/runtime/prefix64.zip` carries the PnP
   sections and the export gate's completeness check passes (0 failures,
   0 skips). Wasm byte-identical to the baseline (`92b36180…`,
   5,722,692 B) — a payload-only change, as expected.
9. Browser GREEN (`green64.log`, `green32.log`; guest consoles in
   `test-results/suite-fd189-services64/console.log` and
   `...-services32/console.log`, cleaned lines in
   `green64-fd189-lines.log`): fd189-services64 PASS (177,537 ms) —
   `PlugPlay ok present state=1`, `WineBus ok present state=1`,
   `WineUsb ok present state=1`, devices `ROOT\WINE\WINEBUS` and
   `ROOT\WINE\WINEUSB` enumerate, `total=2 wine=2`, `RESULT 0`;
   fd189-services32 PASS (36,713 ms) — unregressed, `RESULT 0`. Both
   consoles show 0 BW64-FAIL lines. Stated plainly: the services are
   registered and present, and services.exe now attempts to start
   them — but they do not reach running. The green64 console records
   `Auto-start service L"PlugPlay" failed to start: 1053`
   (plugplay.exe ran and exited 0 without reporting SERVICE_RUNNING)
   and, for the bus drivers, `ZwLoadDriver … winebus: c0000142` /
   `Auto-start service L"winebus" failed to start: 1114` (same for
   wineusb) — the FD-188 signature, now also visible on the 64-bit
   side. The PlugPlay start failure is filed as FD-193 (provisional);
   FD-188's entry is annotated with the 64-bit evidence. Starting the
   services is FD-188/FD-193's question, not FD-189's: FD-189's
   closing terms are registration + presence, both proven here.
10. JS checks (`node tools/run-tests.mjs`, `js-checks.log`): ALL TESTS
    PASSED — legacy exit 0, unit suite 354 passed, 0 failed.
11. Citation audit: 352 rows, 0 drift (232 MATCH / 14 MATCH-INHERITED /
    99 UNCHECKABLE / 7 EXEMPT) — the standing distribution.
12. Native suites serial, 1 worker (`native-main.log`,
    `native-x64.log`): main 692 tests, 0 FAILED (253 s); x64 116
    tests, 0 FAILED (25 s). (Totals are up from the standing 689/113
    because batch 9 added tests; the suites' own headers are
    authoritative.)
13. run-native (`run-native.log`): 71 PASS sections, 0 FAIL, exit 0.

## Deliberately not done

- No RpcSs registration added: the shipped 64-bit prefix has never
  carried it, 64-bit sessions demonstrably work without it, and
  registering it could change COM/RPC startup behavior in ways this
  lane did not measure. FD-189's closing terms do not ask for it; the
  guard still allows it missing (documented in the code).
- No other service registrations added (BITS, Eventlog, NDIS,
  nsiproxy, …): the 64-bit prefix carries only MountMgr besides the
  new PnP sections, and the closing terms name only PlugPlay and the
  ROOT\WINE bus devices. Registering auto-start services wholesale
  would change boot behavior; that is a separate decision.
- No INF staging: neither shipped prefix carries
  `drive_c/windows/inf/*.inf` (the 32-bit one does not either), and
  the closing terms do not require them. The `require_full_init` INF
  requirement stays behind the native-boot packaging path.
- No change to the shipped gamepad witness test: it stays red behind
  FD-173 (drivers fail to initialise) — FD-189 only registers the
  services; starting WineBus is FD-173's question.
- Nothing pushed, deployed, or merged. The merge that composes this
  lane must take the `runtime-assets-20261106-fd189` manifest and
  archive superseding 20261106.
