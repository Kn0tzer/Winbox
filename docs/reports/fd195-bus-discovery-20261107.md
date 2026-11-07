# FD-195: the guest sees and uses the host game controller - lane record

Branch `lane-fd195-bus`, base integ tip `334c70be4`.
State: CLOSED on the lane (fixing commits listed at the end).
The FD-24 witness signature - XInput slot=0 CONNECTED + DINPUT
devices=1 - is met on BOTH architectures, and the standing
`input-gamepad` suite test passes end to end on both.

## Proof (preserved lane logs, `test-results/fd195/`)

- `gamepad-final10.out`: `PASS input-gamepad (176941ms)`, exit 0.
  The same session console (`test-results/suite-input-gamepad/console.log`)
  carries the witness verdicts for both guests: 32-bit
  `READY xinput=1 dinput=1` with `XINPUT slot=0 STATE ...
  lx=32767 ly=-32767`, and 64-bit (pid 71 wine64) `XINPUT slot=0
  STATE buttons=0x0000 lt=0 rt=0 lx=32767 ly=32767` - live sticks,
  triggers and buttons on both word sizes.
- `udev86-final.out`: `PASS fd195-udevprobe` (the permanent
  regression test, asserting form) and `PASS fd195-udevprobe64`
  (its diagnostic twin, deleted after use); `hidenum-final.out`:
  `PASS fd188-hid-enum`. Final trio exits A=0, B=0.
- Earlier milestones: x86 witness verdict first reached in
  `diag-fix4-coinstall/` (`{"arch":"x86","hidDevices":3,
  "winebusState":"4","xinputSlot0":true,"dinputDevices":1}`);
  64-bit udev surface first green in `udev64h.out` (enumerate
  count=1, devnode=/dev/input/event5, initialized=1).
- Every product build printed the build script's own verdict
  (`build20.log`: "Browser export verified"); the runtime
  completeness checker reports 0 failures, 0 skips;
  `bootstrap.py --check` verifies all 13 pinned files.

## What was wrong - eleven convicted layers of one defect

The chain from host pad to guest XInput had a break at every
stage; each layer masked the next.

1. **Uevent file type bits.** systemd's sd-device refuses a
   uevent file whose stat mode lacks S_IFREG (-EBADF).
   `FsVirtualNode::getMode()` returned bare permission bits.
   Fix: include the node type bits (`fsvirtualnode.cpp`).
2. **Uevent trailing newline.** sd-device's line parser only
   dispatches newline-terminated lines; the synthesized event5
   uevent content ended at `DEVNAME=input/event5` with no
   newline, so the devnode never landed. Fix: trailing newline
   on both synthesized uevent files (`sysinput.cpp`).
3. **Symlink type bits.** sd-device resolves /sys/class links
   only when lstat reports S_IFLNK; `FsDynamicLinkNode::getMode()`
   returned 0700. Fix: include the type bits
   (`fsdynamiclinknode.cpp`). After 1-3 the gamepad flowed
   through winebus on the 32-bit side - and wedged in PnP.
4. **PnP install wedge (setupapi CoInitialize).** The PnP
   manager installs bus children sequentially on one thread;
   `SetupInstallFromInfSectionW`'s register-server step calls
   `CoInitialize` unconditionally, and in a service process in
   this environment that call never returns (the apartment's
   hidden window creation never completes - the underlying
   defect is filed separately, FD-204 below). First device
   wedged the queue forever (setupapi trace count frozen at
   exactly 291 in every wedged run). Fix, both Wine vintages:
   only initialize COM for that step when the section actually
   lists RegisterDlls/UnregisterDlls entries
   (`wine10-fd195-setupapi-coinstall.patch`,
   `wine8-fd195-setupapi-coinstall.patch`). Installs then run
   to completion (1,743 setupapi traces; gamepad installed via
   winexinput.inf).
5. **Feed state lost at the open boundary.** The emulator feed
   synced pad state once at host connect - always before the
   guest opened the evdev node - and dropped updates while no
   instance was open. Fix: feed state kept in statics;
   `openDevInputJoystick` queues the full current state as the
   first frame (`devinput.cpp`; native test extended).
6. **hidclass dropped pre-open reports.** Reports went only
   into per-open queues; a report delivered before the consumer
   started was consumed and lost. Fix, both vintages: hidclass
   remembers the most recent report of a controller collection
   (usage page 0x01, usages gamepad/joystick/multi-axis - exactly
   xinput1_3's accepted set) and seeds it into a newly opened
   queue (`wine10/wine8-fd195-hidclass-last-report.patch`).
7. **Inverted axes read -1 at rest.** Wine composes inverted
   axes as -scale-1; an exactly centred stick therefore read
   -1. Fix: the Y/RY feed sites pass value-1 so centre composes
   to 0, endpoints unchanged (`knativeinputSDL.cpp`).
8. **SDL delivered no axis state in the browser build.**
   Measured: a full session saw six gamepad-range SDL events
   (device-added, one button down/up pair) and zero axis-motion
   events, while `emscripten_get_gamepad_status` tracked the
   pad exactly. Fix: `processEvents` also polls the Emscripten
   gamepad status at the device index and feeds changes through
   the same entry points; in the browser build the poller owns
   the triggers (SDL emits them as digital full-scale events
   that overwrote the analog values).
9. **No libudev on the 64-bit side.** wine64.zip carried no
   libudev member at all. Staged the genuine Debian bookworm
   binary (libudev1 252.39-1~deb12u2 amd64,
   `libudev.so.1` sha256 99a5e38f8b45ec2729e5bc24d2d8e2f04d2
   60a2455431a1f9d61f500b31060dd) via the new
   `stage-libudev64.sh`, with provenance under
   `tools/rootfs64/libudev1-x86_64/` and a completeness pin.
10. **The 64-bit syscall layer dropped dirfds and dynamic
    links.** `sys_readlink64` read the raw link field (empty
    for dynamic links), and readlinkat/openat/faccessat
    resolved relative paths against the cwd, ignoring a real
    dirfd. systemd's chase_symlinks walks /sys with exactly
    those calls, so every 64-bit device lookup failed
    verification even with libudev present. Fix: readlink goes
    through the virtual `getLink()`; the three *at handlers
    resolve against the directory behind a real dirfd
    (`syscall64.cpp`).
11. **Wine 8 never started its udev backend, and the 64-bit
    prefix had no driver INFs.** winebus starts udev only when
    SDL is disabled or its init fails; the SDL thread start
    reports success while libSDL2 is absent, so the fallback
    never fired (traces: `bus_main_thread L"SDL"` only, then
    silence). Fix: `"Enable SDL"=dword:00000000` on the WineBus
    service key in the 64-bit prefix (the driver's own option,
    read from the driver key). Two packaging gaps hid behind
    it: `prepare_prefixes.merge_registry_fragment` merged only
    whole missing sections, so the value added to the existing
    WineBus section was silently dropped while the build
    reported success (the merge now inserts missing values into
    existing sections, and a validator guards the value); and
    the five driver .inf files that prepare_prefixes has
    required in the win64 prefix since FD-189 were never staged
    by any step - the new `stage-prefix64-inf.sh` stages them,
    so `enumerate_new_device` can install the gamepad (before:
    `Unable to install a function driver for device
    L"WINEBUS\\VID_045E&PID_028E..."`).

Also discharged en route: the session's one BW64-FAIL
(proto-15 netlink recvfrom EINVAL) belongs to libusb's
udev-monitor receive in the wineusb context, not winebus; the
two permanent 845e devices are wineusb's synthesized USB HID
devices, unrelated to the gamepad.

## Notes for composition

- A fresh full wine8-backports rebuild drifts all 42 outputs
  vs their pins by small metadata bytes (PE timestamps and a
  few section bytes; code-equivalent by PE section
  comparison). This lane shipped only the intended new files
  from its rebuilds and restored the rest (Option A).
- The retained runtime archive for this generation
  (`backups/winbox-runtime-assets-20261107-fd195/`) was rebuilt
  several times as members changed; tarfile's default directory
  entries for path components make members read as directories,
  so the final archive was written with explicit regular-file
  entries. Final archive: 493,557,760 bytes, sha256
  1a8a05a080d73d3ccab9fe67990866e6703793b8ee9b333dd75faef22d710535
  (the repack after the prefix64 INF staging and the last
  prefix64 re-pin; runtime-assets.json carries the per-file
  pins - prefix64.zip 19,038,895 bytes, sha256 1915531ce2bc5f4b…).
- Incident, recovered: an early regen ran with web/runtime
  still holding the prep symlinks and wrote through one into
  the shared store's wine32.zip. The store file was restored
  from the retained batch-10 archive and verified byte-identical
  to its pin (sha256 aa2bb9d63bf03dca2275d64ec117dbb280b68b8e
  ea456d54774738cefa46e070). No residue.
- build-wine8-backports.sh on this aarch64 host needs its
  tools-build pre-seeded once with `--enable-archs=i386,x86_64`
  if the cache is wiped, and its unix-link phase needs the
  amd64 freetype/fontconfig sysroot environment (see the
  script's phase-3 invocation).

## Fixing commits (branch lane-fd195-bus)

- `3e9f20c5e` udev surface shapes (layers 1-3)
- `96f017ea9` 64-bit readlink/*at dirfd handling (layer 10)
- `045cbffe3` feed state across the open boundary (layer 5)
- `2949500ed` Emscripten axis poller + centred inverted axes
  (layers 7-8)
- `5aefb0a42` Wine patches (layers 4+6), x86-64 libudev
  (layer 9), prefix SDL-off + INF staging + merge fix
  (layer 11)
- `194412a12` runtime re-pin for the FD-195 generation
- `3248805d7` permanent udev-surface regression test
- `659bba40a` input-gamepad trigger-wait hardening (FD-205)
- `40ae887dd` this record + the register update;
  `4830d58e6` the superseded diagnostic test removed

Register: FD-195 closed as CL-141 (lane-local numbering;
composition renumbers). FD-204 and FD-205 filed alongside.
