# FD-65: Wine Gecko's "unable to find a usable font" — the 64-bit GDI font engine was never built — 2026-11-02

Lane: `lane-fd65`, off integ `1d932714c`.
Register: FD-65 CLOSED — see CL-41 (provisional; the
composing merge verifies by audit). FD-85 and FD-86 filed
(found along the way, not fixed here).

## What was filed

In the common-dependencies browser test the guest boots
cleanly, `mshtml-document.exe` spawns, and Wine Gecko 2.47.3
aborts at startup — `###!!! ABORT: unable to find a usable
font (serif)` — so the fixture never prints `MSHTML: RESULT
0`. The register's open question: is the guest font set
genuinely absent on this path, or does enumeration race
first-boot font-cache generation?

## Characterization (measured, in order)

1. **The font faces were absent — but that was not the
   blocker.** The 64-bit rootfs (`glibc-rootfs64.zip`)
   carried no font files at all: no `.ttf`/`.otf`, no
   `usr/share/fonts`, no `etc/fonts`. Its 32-bit sibling
   carries DejaVu faces plus a minimal fonts.conf, staged by
   steps (`tools/rootfs32/stage-fonts.py`,
   `tools/rootfs64/stage-fonts32.sh`) that were never
   mirrored for 64-bit. `prefix64.zip` ships the fontconfig
   machinery (Debian fonts.conf, libfontconfig, libfreetype
   and their full dependency chain) but no faces;
   `wine64.zip` ships Wine's own fonts (Tahoma and the
   raster set — no serif text face). Staging the faces
   (below) changed nothing for Gecko.

2. **The decisive defect: the shipped 64-bit `win32u.so`
   has no font engine.** A step-by-step guest probe
   (lane-local `fontsteps` fixture: GetDC, EnumFontFamiliesEx
   with a counting callback, CreateFontIndirect + SelectObject
   + GetTextMetrics) on the baseline build:
   - x64: DC valid; **EnumFontFamiliesEx count = 0**;
     GetTextMetrics **fails** (tmHeight 0) for Tahoma.
   - x86 control, identical fixture: **count = 83**,
     GetTextMetrics succeeds (tmHeight 17).
   AddFontResourceEx on explicit paths (zip-backed rootfs,
   wine fonts dir, writable home) returned 1 per file and
   GetTextFace then resolved the added name — but
   enumeration stayed 0 and metrics still failed. Font
   *files* were never the limiting factor.
   DirectWrite's own collection in the same processes saw
   families all along (5 before the font staging, 10 after)
   — its enumeration is independent of win32u, which is why
   Chromium-class text rendered while every GDI consumer
   starved.
   Static confirmation: the shipped `win32u.so`
   (`bffd9036…`, the FD-01 backport build) contains no
   freetype or fontconfig strings at all and needs only
   `ntdll.so`/`libm`/`libc`. Root cause, in the project's
   own build script: `tools/rootfs64/build-wine8-backports.sh`
   configured all three of its Wine 8.0 trees — including
   tree 3, the `--host=x86_64-linux-gnu` tree that emits the
   unix half of win32u — with `--without-freetype
   --without-fontconfig`. The font driver and the fontconfig
   enumerator live in that unix half, so the shipped 64-bit
   stack could not enumerate or realize any font, whatever
   files were present. Not a race: the failure is identical
   on every run, at any wait length.

## The fix

- `tools/rootfs64/build-wine8-backports.sh` — tree 3 is no
  longer configured `--without-freetype
  --without-fontconfig` (trees 1–2 keep theirs: build tools
  and the PE half do not render). A post-build guard greps
  the product for both SONAMEs and fails the build loudly
  if either is absent, so a fontless win32u cannot ship
  silently again. Rebuilt `win32u.so`: 1,479,712 B, sha256
  `d382cefb32fbe71d7e52349da06c478a22c03bde2b38d8e6bfb4eaff42ef2608`
  (was 1,428,136 B / `bffd9036…`); DT_NEEDED unchanged
  (both libraries are dlopen'ed by SONAME, and the guest
  already carries them plus their dependency chain in the
  prefix64 overlay). Re-pinned in
  `tools/rootfs64/check_runtime_completeness.py`
  (WINE8_BACKPORT_PINS) and the backports README, and
  staged into `wine64.zip` by the existing
  `stage-wine8-backports.sh` (wine64.zip now 209,637,447 B,
  `be4d4956…`). Build environment note: the build host
  needs the x86_64 freetype/fontconfig development files;
  this lane built against a local sysroot extracted from
  the distribution's amd64 packages (freetype 2.13.2,
  fontconfig 2.15.0) named via FREETYPE_CFLAGS/
  FREETYPE_LIBS and FONTCONFIG_CFLAGS/FONTCONFIG_LIBS —
  configure's own error ("FreeType 64-bit development
  files not found. Fonts will not be built.") is what the
  old `--without` flags were silencing.
- `tools/rootfs64/fonts64/` + `stage-fonts64.sh` — the
  missing 64-bit mirror of the 32-bit font staging: six
  DejaVu faces (Sans, Sans-Bold, SansMono, SansMono-Bold,
  **Serif, Serif-Bold**; fonts-dejavu-core 2.37-8, hashes
  pinned in the checker) merged into
  `usr/share/fonts/truetype/dejavu/` in
  `glibc-rootfs64.zip` (now 11,469,777 B, `88fad6d6…`),
  wired into `apply-runtime-poststeps.sh`, and guarded by
  the new `fonts64 faces` check in
  `check_runtime_completeness.py` (RED before staging:
  "DejaVuSans.ttf missing from glibc-rootfs64.zip").
  With the engine alive, these are the faces fontconfig
  enumeration serves — including the serif Gecko asks for.
- Runtime-assets re-pin (FD-73 procedure):
  `tools/browser/runtime-assets.json` now pins both changed
  zips; the retained archive was regenerated deterministically
  from pin-verified files — release `runtime-assets-20261102`,
  `browser-runtime-20261102.tar`, 514,365,440 B, sha256
  `7df0cdc23cf11f81949d44dcdba79ee992c8a92fe071af52d8921f8a58f74398`,
  retained at `/home/ubuntu/backups/winbox-runtime-assets-20261102/`
  (the 20261101 generation is untouched; the GitHub upload
  remains the coordinator's standing won't-do — the box
  bootstraps from the retained copy). `bootstrap.py --check`:
  completeness 0 failures / 1 standing skip, "Runtime
  assets verified (13 files)".

## Proof (RED → GREEN, all on this lane's tree)

- RED, baseline dist (reproduced the shipped build
  byte-identically first: wasm `1d46405a…`): the real
  common-dependencies test fails with the filed signature —
  `no fonts - init: 1 fonts: 0 loader: 1 backend: gdi`,
  then the ABORT ×2, `MSHTML: RESULT 0` never printed
  (`test-results/fd65/commondep-baseline-console.log`).
  Font staging alone did not move it (same abort on the
  fonts-only build, `commondep-fixed-console.log`) — the
  two-layer characterization above is what separated the
  layers.
- GREEN, fixed dist (full `bw64-build.sh`: "Browser export
  verified", support archive unchanged): the step probe now
  reads **EnumFontFamiliesEx count = 85** and
  **tmOk = 1, tmHeight = 17** on x64 — the 32-bit stack's
  numbers (`fontsteps-fixed-console.log` vs the baseline
  `fd65-fontsteps-console.log` and the x86 control
  `fd65-fontstepsx86-console.log`). The win32-fonts fixture
  on x64 reads `GDI=1 FAMILIES=10 TAHOMA=1
  MAPPING=00000000` (its remaining ARIAL term is FD-86).
  The real common-dependencies test, twice: Gecko stage
  fully green — `MSHTML: TITLE boxedwine gecko`,
  `MSHTML: TEXT embedded html works`, `MSHTML: RESULT 0`,
  zero ABORT / no-fonts lines in either console, and the
  baseline's companion `0xC0000005` process exit gone with
  the abort (`commondep-v2-console.log`,
  `commondep-v3-console.log`). Neither the test's waits nor
  its assertions were touched.
- The test still exits red — at its final stage, the
  browser audio capture (`waitForPageFn` on
  `__audioCapture.nonZero`, line 60), identically in both
  fixed-build runs (~264 s each). That stage was
  unreachable while the test died at Gecko; it is a
  separate defect, filed as **FD-85**, not loosened and
  not fixed here.
- JS unit suite on the fixed tree: 348 passed, 0
  failed (the integ baseline; run after the lane-local
  diagnostic tests were removed — an earlier run tripped
  the harness-no-stall audit on those throwaway files and
  nothing else). Browser build compiles (above).

## Also filed

- **FD-85** — common-dependencies' audio stage: the guest
  OpenAL tone completes (`OPENAL: RESULT 0`) but no
  non-zero sample reaches the page's AudioContext capture
  within the test's 30 s window, deterministically.
  Unmasked by this fix; the break is downstream of the
  guest (emulator audio pump or page capture), not
  characterized further here.
- **FD-86** — no `Arial` family on the 64-bit stack: the
  win32-fonts fixture prints `ARIAL=0 METRICS=0` on x64
  while the 32-bit stack serves Arial through its
  registered DejaVu replacement
  (`tools/browser/stage-wine32-fonts.py`, applied to
  prefix32 at dist export). prefix64 has no
  `[Software\Wine\Fonts\Replacements]` entry. The entry
  records the mirror fix (a prefix64 staging step, checker
  coverage, and the win32-fonts suite test extended to
  x64 as the guard) for the DirectWrite/Chromium
  workstream; Gecko does not need Arial and FD-65 does
  not wait on it.

## Deliberately not done

- The shipped `win32-fonts` suite test still runs x86
  only: extending it to x64 today would assert FD-86's
  missing Arial and go red for a different defect. FD-65's
  standing guards are the backport build's SONAME check,
  the checker's fonts64 + win32u pins, and the
  common-dependencies test's Gecko stage itself.
- No native-suite run: this lane changes no emulator
  code — a Wine payload binary, rootfs staging, build
  scripts and pins. The browser build, the completeness
  gate and the guest probes are the applicable proofs.
