# wine10-fd195-i386 — FD-195 device-install + controller-state fixes (i386 PE)

Two PE binaries rebuilt from the pinned Wine 10.0 source tarball
(sha256 c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601)
plus `tools/buildWine/patches/wine10-fd171-contracts.patch`,
`tools/buildWine/patches/wine10-fd195-setupapi-coinstall.patch` and
`tools/buildWine/patches/wine10-fd195-hidclass-last-report.patch`, by
`tools/rootfs32/build-wine10-fd195.sh`. It replaces the FD-171
build's setupapi.dll, carrying both lanes' changes (disjoint source
files of the same DLL — the FD-171 config-manager family and this
lane's install.c change), so `tools/rootfs32/build-wine32-base.sh`
substitutes it last for setupapi.dll, the same discipline as the
FD-167+FD-171 combined build for propsys/mf (the FD-171 pin set no
longer lists setupapi.dll).

FD-195: `SetupInstallFromInfSectionW` called `CoInitialize`
unconditionally before iterating the RegisterDlls/UnregisterDlls
section fields, as part of the SPINST_ALL processing every PnP
device install performs. In the PnP manager's install thread — a
service process (`winedevice.exe`) — that initialization never
returns in this environment: creating the thread's COM apartment
window needs the process's window layer fully usable, which is not
the case when a bus driver reports devices before the session
desktop can service window creation. The first bus device's
install therefore stalled forever (the setupapi trace count froze
mid-install; a 420 s settle changed nothing), the manager never
enumerated the remaining devices, and the game controller the
winebus SDL/udev backends had already created never appeared to
guest programs. The patch only pays the COM initialization when
the section actually registers DLLs; sections without
RegisterDlls/UnregisterDlls entries (all the bus driver INFs:
winehid, wineusb, winexinput) skip it, and the install completes.

With the patched DLL the standing diagnostic session reports the
FD-24 witness signature: HID devices enumerated 3, XInput slot 0
connected, DirectInput devices 1 (preserved run:
test-results/fd195/diag-fix4-coinstall/).

FD-195 (hidclass.sys): hidclass delivers each input report only
to the per-open queues that exist when the report arrives, so
the controller state report winebus produces during device
start was lost to every program that opened the device later —
XInput and DirectInput state readers only ever saw reports
from live changes, and a resting controller never produced a
state reading at all. The patch makes hidclass remember the
most recent report of a controller collection (game pad /
joystick / multi-axis) and seed it into a newly opened queue,
so a state reader opening the device after the last report
still observes the current controller state.

## Packaging integrity

sha256 of this exact file, verified by
`tools/rootfs64/check_runtime_completeness.py` (source hash + the
shipped `wine32.zip` member hash) and pinned in
`tools/rootfs32/build-wine32-base.sh` (`FD195_SHA32`):

| file | bytes | sha256 |
| --- | --- | --- |
| setupapi.dll | 766368 | 14724d59d4d2ea77cb4833c2661df053a37e2bc94585c7c83fd054ea8b00e54e |
| hidclass.sys | 95334 | 608e9c23a0f19f9780516a710ca29a6919e659d760494d4b29ee050c4dd94454 |

## Rebuild

    tools/rootfs32/build-wine10-fd195.sh
