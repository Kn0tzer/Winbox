# FD-188: kernel-mode Wine drivers initialise — lane record

Branch `lane-fd188`, base integ tip `faf95d26c`, fix commit `7b0c2a50b`.
Record: `docs/reports/fd188-drivers-initialise-20261107.md`.

## What the defect was

Every attempt to load a kernel-mode driver in a guest session failed the
same way: ZwLoadDriver answered c0000142 (STATUS_DLL_INIT_FAILED) for
MountMgr, nsiproxy, NDIS and wineusb, and a guest-side StartService for
the WineBus driver returned error 1114. The failures appear in preserved
session logs from unmodified builds, so the condition predated any one
lane's changes. Consequence: no PlugPlay device class could appear in a
guest — HID devices (game controllers, FD-24) included — because the bus
driver that would create them cannot start.

## Diagnosis (each step read from the run's own preserved log)

1. Wine 8.0/10.0 `load_driver` opens the driver by the absolute path
   `C:\windows\system32\drivers\<name>.sys`. The loader's `module`
   channel (not `loaddll`) shows the failure: `Failed to load module
   L"C:\windows\system32\drivers\mountmgr.sys"; status=c0000135`
   (STATUS_DLL_NOT_FOUND) — identically for nsiproxy, ndis, wineusb.
   DriverEntry was never called; the PE load itself failed.
2. A guest fixture proved the directory itself is missing:
   `C:\windows\system32\drivers\` does not exist in the booted guest
   (listing FAILED err=3), while `kernel32.dll` and `ntoskrnl.exe`
   exist. For an absolute path the loader calls `open_dll_file` once on
   that exact path — there is no builtin/WINEDLLPATH fallback.
3. Why the directory is missing: the shipped prefix redirects system32
   at the filesystem level to the Wine lib dir (`system32.link`), which
   carries the `.sys` files but no `drivers/` subdirectory. On real Wine,
   wineboot creates `drivers\*.sys` placeholders; this project's shipped
   prefix deliberately disables wineboot's update
   (`.update-timestamp` = `disable`), so the placeholders never arrive.
   The prefix build's own checks never looked for any `drivers/`
   content.
4. A second, downstream gap: the shipped prefix has no
   `C:\windows\inf\` at all, and the wine zips carry no per-driver
   `.inf` files — so even after the drivers load, the PnP manager
   cannot install a function driver for an enumerated device
   (`err:plugplay:enumerate_new_device Unable to install a function
   driver`), because `SetupDiBuildDriverInfoList` finds no compatible
   driver in the empty INF store.

## The fix (commit `7b0c2a50b`)

- **Drivers mirror.** Every `.sys` file is mirrored into a `drivers/`
  subdirectory in both wine zips (wine32: 20 files; wine64: 17 files),
  byte copies of the same builtins. The loader finds the file, and
  Wine's builtin-replacement machinery loads the canonical builtin and
  associates the Unix `.so` by basename, so DriverEntry runs. Producer:
  `build-wine32-base.sh` + `build-wine64-zip.sh`; merge-script path
  `stage-wine64-drivers.sh` for already-cut zips (the container build
  cannot run on this host).
- **Driver .inf files.** The five `wine.inf` `[InfFiles]` entries
  (`input.inf`, `winebus.inf`, `winehid.inf`, `wineusb.inf`,
  `winexinput.inf`) — version-matched Wine sources, kept in
  `tools/rootfs32/wine-inf/` (8.0) and `tools/rootfs64/wine-inf/`
  (10.0) — are shipped in the wine zips' `share/wine/` and overlaid
  into prefix32's `C:\windows\inf\`. Setupapi now reads `winehid.inf`
  and enumerates compatible drivers for WINEBUS HID devices.
- **Guards (cannot silently recur).** The completeness checker has a
  `t_drivers_dir` test (mirror exact, both zips) and a `t_driver_inf`
  test (the five `.inf` files in `share/wine/` + prefix32's `inf/`,
  byte-identical to the tree sources); the prefix validation requires
  the five `.inf` files in the 32-bit prefix. Both guards fail on the
  unmirrored zips.
- **Re-pin.** wine32/wine64/prefix32 re-cut and re-pinned
  (`runtime-assets-20261107-fd188b`); the census is unchanged
  (1,171 tables, 161,677 named, 31,838 named stubs); the browser build
  is byte-identical wasm (`92b36180…`, the fix touches only the wine
  and prefix zips).
- **Store refresh.** The shared build store's wine64/wine32/prefix32
  were refreshed from the fd188b archive per the store MANIFEST's
  procedure (SHA256SUMS regenerated, 19,193 entries, integrity OK;
  pin_source updated). The fd189 lane was finished, so no active build
  was disturbed.

## Proof

- **fd188-driver-probe (regression probe, PASS, 2 runs: run 9 and run
  10 on the final build).** No c0000135/c0000142 failure lines; all
  four boot drivers show `init done`; guest fixture ground truth: the
  five driver files exist, the drivers directory lists 22 entries (the
  full 20-file mirror), the WineBus service is RUNNING (state 4), and
  MountMgr/nsiproxy/NDIS/wineusb services are all state 4.
  Evidence: `test-results/fd188/run9-evidence/` and `run10-evidence/`.
- **Readiness flakiness (not the fix).** Two of the five post-fix probe
  runs (runs 5 and 7) failed to reach the desktop within the 10-minute
  boot window; the launcher's own stall surfacing convicted run 5 as
  `prehandshake-no-progress` (one guest process spinning with zero
  progress for 420 s). Runs 6, 8, 9 and 10 reached ready in ~60 s.
  The stall matches the known no-progress shape (FD-115's lane
  measured 1 in 19 on the old build) and did not reproduce
  systematically after the fix; it is not attributed to the drivers
  mirror. Noted here so the tally is honest.
- **fd188-hid-enum (diagnostic probe).** With a simulated gamepad
  installed, the guest's SetupDi HID enumeration finds **0 HID
  devices**, even though the WineBus service runs, the root WINEBUS
  device exists, the five `.inf` files are in `C:\windows\inf\`, and
  setupapi reads `winehid.inf` and enumerates compatible drivers for
  the one enumerated WINEBUS device (VID_845E&PID_0001). The gamepad
  (VID_045E&PID_028E) is never enumerated as a device at all.
  Evidence: `test-results/suite-fd188-hid-enum/`.

## What is NOT fixed (filed as FD-193)

The FD-24 witness still sees no controller (`DINPUT devices=0`,
`XINPUT slot=0` never CONNECTED; witness runs 1 and 2). The blocker is
no longer the driver load — it is the HID device discovery layer:
winebus's Unix side does not surface the gamepad as a device (its
keyboard/mouse PDOs appear, the gamepad's does not), and the
function-driver install for the one enumerated HID PDO does not
complete to a started HID device. A single `BW64-FAIL netlink-recvfrom
flags=0x22 outcome=returned-EINVAL` appears in the witness session,
which is a lead for the udev-monitor path. FD-193 files this with the
diagnostic evidence; the FD-24 witness is its proof vehicle. The
64-bit prefix's `.inf` overlay is the FD-189 lane's (its prefix has no
PlugPlay registration at all).

## Register

- FD-188: section B row, provisional **CL-134** (fix commit
  `7b0c2a50b`). **Collision flag for the merge lane:** the fd189 lane
  also claims provisional CL-134 on its branch; first-composed wins,
  the other renumbers. The driver-load proof is complete; the witness
  proof (the closing terms' second half) is outstanding and blocked by
  FD-193, so closure to section C is the merge lane's call.
- FD-193 filed (section A): the guest enumerates no HID device at all
  with a simulated gamepad installed.

## Lane checks — checking queue, pipeline 16 (commit `7b0c2a50b`)

- verify-source OK (mounted commit matches the submitted SHA; a write
  to the read-only source mount fails as required; tree pristine).
- Citations exit 0 (352 rows, 0 drift).
- JS unit exit 1: **352 passed, 2 failed** — `provenance-artifacts`
  and `runtime-completeness`. These are the SAME two bare-checkout
  artifacts as pipeline 11's acceptance run (351 passed, 2 failed,
  identical failure lines): both fail only because the queue's bare
  checkout has no built `dist/`. No new js-unit failure from this
  change.
- JS legacy exit 0 (0 fail, 1 skip).
- Native main build exit 0, native main exit 0 (692 tests, 0 FAILED);
  native x64 build exit 0, native x64 exit 0 (116 tests, 0 FAILED).
- Run-native exit 0 (all PASS).
- The pipeline's overall status is "failure" solely because of the
  two known bare-checkout artifacts above. Full step logs:
  `checks/evidence/pipeline-16/queue-logs/`; results in MinIO
  `checks/16/results/`.
