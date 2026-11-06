# FD-168 draw-down 1 — win32u stubs: 14 implemented, remainder dispositioned

Lane `lane-fd168` (worktree wt-fd168), base: lane-dll-census tip
(integ/winbox-20261022b + the census change). FD-168 stays open; this is
the first family draw-down under its closing terms.

## What shipped in this change

Fourteen win32u syscall placeholders in the shipped 64-bit Wine 8 build
are now real implementations, via a new standing backport patch
`tools/buildWine/patches/wine8-win32u-fd168-backports.patch` (15 files)
wired into `tools/rootfs64/build-wine8-backports.sh`: each function moves
from a spec `@ stub` line to a `-syscall` entry (syscall table regenerated
from the spec sequence and verified equal) with a unix-side body written
against this vintage's own server requests, handle tables and shared
state — most adapted from the later upstream implementations where those
exist, the rest from this tree's own user32/gdi32 counterparts:

NtUserAlterWindowStyle, NtUserBuildHimcList, NtUserBuildNameList,
NtUserBuildPropList, NtUserGetClipCursor, NtUserGetCurrentInputMessageSource,
NtUserGetPointerType, NtUserGetThreadState, NtUserQueryDisplayConfig,
NtUserQueryWindow, NtUserRegisterWindowMessage, NtGdiGetMiterLimit,
NtGdiSetMiterLimit, NtGdiIcmBrushInfo.

The backports were rebuilt end to end (PE + unix halves), the runtime set
restaged (`apply-runtime-poststeps.sh`: complete and verified), the six
backport hashes re-pinned (`check_runtime_completeness.py` +
`wine8-backports-x86_64/README.md`), `tools/browser/runtime-assets.json`
re-pinned and the retained archive rebuilt
(`/home/ubuntu/backups/winbox-runtime-assets-20261105/`), the census
baseline regenerated in the same change, and `dist/` rebuilt last
(`bw64-build.sh`, "Browser export verified") so the shipped export
matches the restaged runtime.

## Counts (census, shipped set)

- win32u.dll x86-64 validated stubs: 926 -> 912 (exactly the 14 names;
  the removed-name list is the implementation list above).
- win32u.dll i386: 1,111, unchanged.
- FD-168 total: 2,037 -> 2,023.
- Shipped-set named stubs: 33,756 -> 33,742; across all 1,171 census
  tables only x64/win32u.dll changed. `dll_export_census.py --check`
  exits 0 on the regenerated baseline.

## Proof

- Completeness checker + `bootstrap.py --check`: 0 failures on the
  restaged set (including "retained runtime-assets archive agrees with
  the file pins").
- Guest test `win32u-fd168` (browser suite; fixture
  `tools/guest-fixtures/win32u-fd168.c`): 17 checks, each calling a
  previously stubbed export under a stub-exception guard and
  cross-checking the answer against the user32/gdi32/imm32 counterpart
  or documented contract (RegisterWindowMessage must return the same
  atom as user32's; the miter limit set through win32u must read back
  through gdi32 and vice versa; QueryDisplayConfig must report a source
  mode matching the screen size; GetCurrentInputMessageSource inside a
  sent-message window proc must report the unavailable source).
  Result: PASS (suite: 1 passed / 0 failed). All 17 checks report
  ok=1 and the fixture ends RESULT 0, read from the run's preserved
  console log. The first full run scored 16/17 and caught a real bug
  in this change: the miter-limit pair was first written with the
  FLOAT parameter form Wine 8's (never-exercised) header declares,
  but the syscall frame delivers the value as integer bits, so the
  setter stored 0.0. Wine 10's header declares both functions with
  DWORD bit-pattern parameters; the pair was rewritten to that form
  and the rerun passes both directions (7.5 set through win32u reads
  back 7.5 through gdi32; 3.25 set through gdi32 reads back through
  win32u).
- JS unit suite: full run on the final tree (after the restage and
  the dist rebuild) 354 passed / 0 failed, the standing size, with the
  dll-export-census test green in-suite. An intermediate run showed
  352/2 where the two failures (packaging-prefix,
  provenance-artifacts) read the then-stale dist export; both were
  addressed by rebuilding dist after the restage, and the final run
  confirms them green. Four further one-off failures in a run at load
  average 27-28 (boot-progress-snapshot, x11-freefont, x11-gc-setters,
  x11-get-window-attributes) each pass standalone at ordinary load and
  are unrelated to this change's surface.
- Legacy suite: 115 pass / 0 fail.

## Why the other 2,023 stay, per family

Recorded in full in the FD-168 register entry. Summary: 31 NtGdiDdDDI*
names exist upstream only inside Wine 10's Vulkan-backed D3DKMT layer,
which has no counterpart in this tree (and the emulator's DXVK/vkd3d
path does not use D3DKMT); a handful need wineserver protocol additions
or driver-interface/state this vintage does not have (SwitchDesktop,
NotifyIMEStatus, SetWindowFNID, ModifyUserStartupInfoFlags,
GetPointerDeviceRects, MakeFontDir); eight have only FIXME semi-stub
bodies upstream that fake answers, so implementing them would move the
count without giving a guest a working call; one name is a misspelling
in Wine 8's own spec that no caller can reference; 866 x86-64 names are
stubs in every upstream vintage including current master (215 of them
compositor-internal, whose counterparty processes do not exist in
Wine's architecture); the i386 side ships the vendor Wine 10 build
untouched — the 35 names Wine 10 implements are already live there —
and no i386 PE-substitution pipeline exists. Import-graph evidence for
all of it: no shipped DLL on either architecture imports any of the
2,037 names, so the only possible callers are a guest program's own
direct win32u calls.

## Build notes

- The unix (x86_64-linux-gnu) half of the backports build needs 64-bit
  FreeType/Fontconfig development files; on this aarch64 host they were
  supplied from a scratch amd64 sysroot through the FREETYPE_*/
  FONTCONFIG_* variables the build script documents.
- Ordering matters: restage the runtime first, build `dist/` last —
  poststeps' zip rewrite drops the wineserver symlink bit in the dist
  copies, which the packaging/provenance unit tests check; the dist
  build restores it.
- One trap recorded for the next draw-down: the build script reports
  success through its launcher even when the unix make fails mid-script
  (the error lands in `x64linux-build/make-unix.log` and the previous
  win32u.so stays in the output). Verify the output hashes actually
  changed before re-pinning.
