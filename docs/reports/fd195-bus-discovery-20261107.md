# FD-195: guest enumerates no HID device - lane record (INTERIM)

Branch `lane-fd195-bus`, base integ tip `334c70be4`.
State: DIAGNOSIS ADVANCED, FIX NOT YET ATTEMPTED. The lane stopped
on its disk guardrail (box fell to 5.5G free under the wave's build
relay; the lane stripped its own generated state, ~1G) and on
probe-slot starvation (see "Blocked" below). FD-195 stays OPEN.

## End state sought

The FD-24 witness sees the controller: XInput slot=0 CONNECTED +
DINPUT devices=1 (FD-195's closing terms on the register).

## Verified from preserved logs (batch-10 gate, same product
contents as this lane's base; read directly, not from summaries)

Evidence: `wt-merge-batch10/test-results/suite-fd188-hid-enum/`
(console.log, hid-enum-lines.txt, hid-enum-fixture-lines.txt) and
`wt-merge-batch10/test-results/suite-input-gamepad/console.log`.

- Host feed live: `Gamepad feed: controller 'Standard Gamepad'
  opened (instance 0)` (input-gamepad console line 97).
- WineBus service state=4; C:\windows\inf carries input.inf,
  winehid.inf, wineusb.inf, winexinput.inf (fixture lines).
- The guest's SetupDi HID enumeration finds **0 HID devices**
  (fixture: `fd188-hid: HID devices=0`); the witness reports
  `DINPUT devices=0` and XInput slot 0 never connects.
- Exactly ONE WINEBUS child device is created in the whole
  session: `WINEBUS\VID_845E&PID_0001\0&0000&0&0&0` - version 0,
  default serial "0000", uid 0, index 0, is_gamepad=0, no &MI_
  suffix. It is NOT the gamepad. SetupAPI enumerates compatible
  drivers for it from winehid.inf, yet no HID interface device
  results.
- The gamepad (VID_045E&PID_028E) never appears as a device ID
  anywhere in the session console.
- Exactly one BW64-FAIL line per session:
  `netlink-recvfrom flags=0x22 outcome=returned-EINVAL`
  (input-gamepad console line 733).

## Source-level facts established

- The kernel evdev gamepad (/dev/input/event5, 045e:028e) has a
  complete ioctl surface for winebus's needs (EVIOCGID,
  EVIOCGBIT(EV_KEY/EV_ABS) with the full button/axis sets,
  EVIOCGABS per axis, EVIOCGNAME) - source/kernel/devs/
  devinput.cpp, DevInputJoystick::joystickIoctl; pinned by the
  native testDevInputJoystick.
- /sys/class/input/event5 + the parent input5 id/capability
  files and a synthesised udev database entry
  (/run/udev/data/c13:69, ID_INPUT_JOYSTICK=1) are published by
  source/kernel/sys/sysinput.cpp (FD-24 lane).
- The shipped winebus.so (both zips) carries BOTH the udev and
  SDL backends and links libudev.so.1 (strings/readelf on the
  extracted binaries, lane scratch dir).
- Wine 10.0 winebus source (fetched, tag wine-10.0):
  udev_bus_init creates the netlink monitor FIRST; if
  create_monitor fails, the whole udev backend returns
  STATUS_UNSUCCESSFUL **before** build_initial_deviceset_udevd
  runs - no udev device can ever appear. In udev_add_device, a
  device whose udev devnode is NULL is dropped silently, and
  for subsystem "input" the desc vid/pid/version come from
  EVIOCGID on the opened node.
- The emulator's netlink recvfrom
  (source/kernel/knetlink.cpp, KNetLinkObject::recvfrom)
  returns -EINVAL for any flags beyond MSG_DONTWAIT|
  MSG_NOSIGNAL - the source of the session's one BW64-FAIL
  line. 0x22 = MSG_PEEK|MSG_TRUNC, the sizing-peek pattern
  libudev's monitor receive path uses.
- No unix-side winebus traces appear in these session consoles
  at all (PE-side plugplay/setupapi/ntoskrnl channels print;
  unix-side channels do not), so the monitor-abort step is a
  source-supported hypothesis, NOT yet proven from a log. The
  one created device's backend (udev vs SDL) is likewise not
  identified from the traces; its desc matches neither the
  gamepad's EVIOCGID identity nor a udev PRODUCT parse of the
  published sysfs uevent (3/45e/28e/110).

## Lane artifacts

- Verified build of the base: "Browser export verified",
  BUILD_INFO git_sha 334c70be4, dirty 0, wasm 92b36180
  (byte-identical to the live build, as expected - batch 10
  changed no product code). Log: test-results/fd195/build1.log.
  The dist was stripped again under the disk guardrail;
  rebuild before probing.
- Diagnostic probe `web/tests/suite/browser/fd195-diag.test.mjs`
  (commit 5065ef2ed): boots x86 with the simulated pad and
  +ntoskrnl,+plugplay,+setupapi,+winebus,+hid channels, runs
  the fd188-hid-enum fixture and the gamepad-witness in one
  session, and writes verdict.json instead of throwing on the
  broken state. Its slot budget is 3600s because probe-base
  passes the test's own timeoutMs to acquireProbeSlot - the
  standing probes' 900s/1500s budgets are what starve them
  under a build relay.
- Register: FD-195 progress note appended (same commit).

## Blocked (why no fix yet)

- Probe-slot starvation: attempt 1 (standing fd188-hid-enum,
  900s budget) and attempt 2 (fd195-diag, 3600s budget) both
  died waiting for a light slot behind the wave's continuous
  build relay (build-pending held the whole time); neither
  booted - both console.logs are 0 bytes. This is the
  probe-parallel lane's filed starvation shape, not a new
  defect.
- Disk: the box fell to 5.5G free (98%) during the wave; the
  lane stripped its own dist/Build/web-runtime (~1G, tree now
  424M) per the disk discipline. Box read 7.4G after the strip.

## Resume steps

1. Rebuild (tools/bw64-build.sh; expect the verified line and
   the same wasm), re-run winbox-wt-prep.sh first if web/runtime
   links are needed.
2. Run fd195-diag when a probe window exists (build relay
   drained); read its console.log + verdict.json.
3. Convict the exact break (monitor creation vs enumeration
   vs devnode vs PnP install) from those traces; if the unix
   traces are still absent, instrument at the emulator layer
   (the guest-side consumers are libudev calls on knetlink /
   sysfs / devnodes - all emulator-owned surfaces).
4. Fix at the convicted layer, re-prove with fd195-diag, then
   close on the standing witness (input-gamepad, both archs)
   and the queue's light checks.
