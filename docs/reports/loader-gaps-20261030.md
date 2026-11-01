# Loader gaps — every demo's load path resolves (2026-10-30)

Lane: `lane-loadergaps` (worktree `wt-loadergaps`, off integ `421e18dce`).
Question: which of the 15 demos the live site offers fail before their own
code runs, and what closes the gaps — by class, not by instance.

## Measurement

`tools/conformance/pe_census.py` against the deployed runtime
(`/home/ubuntu/kn0tzer/winbox/runtime`, build `efa0189a1`) and the demo
archives in `/home/ubuntu/kn0tzer/games`. A program's load path counts as
resolving when every regular import of every file in its load closure —
and every delay import it can hit at startup — names an export the staged
runtime actually provides (API-set contracts resolved as Windows resolves
them; forwarders chased).

Baseline result: **13 of 15 demos fully resolve.** Two do not, plus one
runtime self-check anomaly:

| program | gap | kind |
| --- | --- | --- |
| godot-squash | `user32.dll!GetPointerPenInfo` — imported by `SquashTheCreeps.exe` itself | regular, in-closure — the loader refuses the process |
| mindustry | `kernel32.dll!CopyFile2` — imported by the bundled `steam_api64.dll` | regular, bundled file |
| mindustry | `iphlpapi.dll!GetAnycastIpAddressTable` — delay-imported by the bundled JRE's `net.dll` | delay |
| (runtime self-check) | staged vkd3d `d3d12core.dll` regular-imports 9 `gdi32.dll` D3DKMT functions the staged gdi32 does not export | no demo affected; disposition below |

Everything else the census surfaces is provably inert: files that ship in
an archive but are never loaded (Portal's SDK tools, WinMerge's msys2
helpers importing `NtQuery/SetQuotaInformationFile`), and Chromium delay
imports — Chromium's own load path resolves 517 imports with 0 missing.

## The class

The x86_64 staged set is Debian bookworm **Wine 8.0** (image
`boxedwine64/wine64-debian:bookworm`); the i386 set is WineHQ wine-stable
**10.0**. Every convicted gap is an export upstream Wine added between 8.0
and 10.0:

- wine64 user32 exports only 5 of the pointer-info family; the whole
  `GetPointer*Info`/`GetPointerFrame*Info` set is post-8.0 spec work.
- wine64 kernelbase/kernel32 have no `CopyFile2` (added upstream after
  8.0); wine32 kernelbase has it.
- wine64 iphlpapi carries `GetAnycastIpAddressTable` as a commented-out
  spec stub; wine32 iphlpapi exports the finished implementation.

So the fix is a vintage backport, applied once per missing export family —
not a per-program workaround.

## Fix

Three small patches against the pinned Wine 8.0 source (each mirrors the
upstream change), four rebuilt DLLs, staged as hash-pinned payloads
exactly the way the existing WineHQ-10 backports are staged:

| patch | change |
| --- | --- |
| `tools/buildWine/patches/wine8-user32-pointer-family.patch` | spec + implementations for 9 functions in `dlls/user32` (`GetPointerPenInfo`, `GetPointerPenInfoHistory`, `GetPointerInfoHistory`, `GetPointerFrameInfo`/`History`, `GetPointerFrameTouchInfo`/`History`, `GetPointerFramePenInfo`/`History`). Implementations are the same FIXME stubs Wine 8.0 itself uses for this family — they return FALSE, which is the correct answer on a machine with no pointer device. |
| `tools/buildWine/patches/wine8-kernelbase-copyfile2.patch` | upstream's `copy_file` refactor of `CopyFileExW`, plus a real `CopyFile2` in `dlls/kernelbase/file.c`, its spec entry, and the `kernel32.spec` `-import` forward. |
| `tools/buildWine/patches/wine8-iphlpapi-anycast-table.patch` | `GetAnycastIpAddressTable` enabled and implemented as upstream does (argument validation, allocated empty table, `NO_ERROR`), spec entry un-commented, `MIB_ANYCASTIPADDRESS_*` types added to `include/netioapi.h`. |

Deliverables:

- `tools/rootfs64/wine8-backports-x86_64/` — the four rebuilt DLLs +
  `README.md` (provenance, sha256 pins, rebuild recipe). Pins: user32.dll
  2,002,112 B `5da10eb9…`; kernelbase.dll 1,588,877 B `344b7faa…`;
  kernel32.dll 859,511 B `e2145d2a…`; iphlpapi.dll 380,852 B `2b0ed793…`.
- `tools/rootfs64/build-wine8-backports.sh` — reproducible build of the
  payloads from the pinned source + patches.
- `tools/rootfs64/stage-wine8-backports.sh` — hash-aware merge into a
  runtime tree (backup, skip-if-current, refuse-symlink, final hash
  verification), registered as a poststep in
  `tools/rootfs64/apply-runtime-poststeps.sh` after
  `stage-wine64-payloads.sh`.
- `tools/rootfs64/build-wine64-zip.sh` — preflight + staging block so
  freshly built wine64.zips carry the four DLLs (hard-fail if missing).
- `tools/rootfs64/check_runtime_completeness.py` — new pins + check
  "wine8 loader backports (source + shipped zip)", so any future staging
  regression fails the project's own completeness gate.

An export-table diff of each rebuilt DLL against the deployed original
shows **zero exports removed** and exactly the intended additions — no
existing binding can change.

Rejected alternative: swapping in WineHQ-10 DLLs. A scratch-runtime census
shows the swap cannot stand on this tree: WineHQ-10 kernelbase
regular-imports `ntdll!RtlFreeActivationContextStack`, which Wine 8.0's
ntdll does not export (the swapped tree would fail to load kernelbase at
all), and WineHQ-10 gdi32's D3DKMT entries forward to `win32u.NtGdiDdDDI*`
names that 8.0's win32u only partly has (`NtGdiDdDDIOpenResource2`
missing). Same-vintage backports carry none of that skew.

## Proof

1. **Census, before/after, identical closures** (JSON kept in
   `test-results/pe-census/` and `test-results/pe-census-fixed/`):
   - godot-squash: 693 named in-closure regular imports — 615 OK +
     58 forwarded + 19 API-set + **1 missing** → **616 OK + 58 + 19 +
     0 missing**.
   - mindustry: **2 missing** (1 regular, 1 delay) → **0 missing**
     (regular OK 1345→1346, delay OK 23→24; all other counts identical).
   - Per-program missing-set diff across **all 15 demos**: exactly the
     three convicted items removed, **zero new misses anywhere**; the
     runtime self-check is byte-identical before and after (see
     dispositions). **All 15 demo load paths now fully resolve.**
2. **Completeness checker** (`check_runtime_completeness.py`, the same
   checker the JS `runtime-completeness` test shells out to), run as an
   A/B on identical trees: with the pre-fix wine64.zip it FAILS the new
   wine8 check ("user32.dll in wine64.zip != pinned bytes" — the teeth);
   with the fixed zip that check PASSes. Two further failures (gstreamer
   staged tree empty; artifact-manifest drift) appear identically with
   both zips: they are artifacts of dropping a deployed-runtime copy into
   a source tree by hand, not of this change — in the pipeline state the
   poststeps stage the support pack and regenerate the manifest, which
   is what satisfies both.
3. **Suites.** No emulator or JavaScript source is touched by this lane,
   so the suites cannot regress from it beyond the checker gate above
   (green, with teeth). The JS unit suite was attempted against a
   hand-assembled copy of the deployed runtime as `dist/`; it reports
   263 passed / 70 failed, and the failures are all in areas a runtime-zip
   change cannot reach (WebGPU/shader/spirvfix unit tests that require a
   real built `dist/`). The one JS test that exercises
   this lane's change is `runtime-completeness`, covered by the checker
   A/B in item 2. The coordinator's pre-deploy gate builds the real dist
   and runs the full suite against it.
4. **Guest boot (native harness).** 4. **Guest boot (native harness, `tools/run_wine64.sh`, fixed zips).**
   - **Notepad: full boot.** Wine came up, windows were created (WM_CREATE
     traces on the +win/+message channels) and the guest settled into the
     message loop, with **zero `err:module` lines** in the log. The
     rebuilt user32 is the DLL servicing those window calls — the patched
     DLL works in a live guest, not just on paper. (The cold prefix init
     parked twice at near-zero CPU before a run got through — the known
     session-start stall another lane owns; the same stall is on record
     for the unmodified build, and an export-table diff proves the
     rebuilt DLLs remove no existing binding, so the stall cannot be a
     regression from this change.)
   - **Godot (`SquashTheCreeps.exe`) on fixed zips:** the process loaded
     and user32 initialized inside it (`User32InitializeImmEntryTable`
     trace), a child process exited with status 0, and no loader error
     appeared. The guest then went quiet at the same early-startup stage
     where the baseline control also went quiet — downstream of the
     loader, in the startup-stall territory another lane owns.
   - **Control (baseline wine64.zip, same prefix):** within the same
     window the guest neither printed a loader error nor progressed past
     early startup (log frozen, guest parked) — tonight this harness
     cannot produce the baseline loader refusal on demand, because the
     startup stall swallows the run before the import is even bound. The
     discriminative load-path evidence is therefore the census (item 1),
     which is exact and repeatable; the boot evidence shows the fixed
     runtime booting a real program cleanly with the patched DLLs in
     service. The post-deploy site sweep remains the product-level boot
     gate, as with every runtime change.

## Dispositions (recorded in `docs/known-gaps/runtime.md`)

- **gdi32 D3DKMT nine** — self-check only; no demo load path touches it
  and the D3D12 guest probes pass on the shipped build. A gdi32 refresh
  is blocked on 8.0's win32u lacking `NtGdiDdDDIOpenResource2` (measured,
  above). Revisit with a win32u backport or a whole-set vintage bump.
- **kernel32 `GetDurationFormat`** — absent from every staged vintage
  including WineHQ 10 (specs carry only `GetDurationFormatEx`); a delay
  import of a prefix-bundled tool (HxD), not a demo. NOT-A-GAP for demos.
- **i386 side** — wine32 user32 also lacks `GetPointerPenInfo`, but no
  i386 demo imports it (census). The same backport applies if one ever
  does.

## Deploy note

Nothing here is deployed. The fixes take effect when a runtime is next
staged/built through the normal pipeline (`apply-runtime-poststeps.sh`
runs the new poststep; `build-wine64-zip.sh` stages the DLLs into fresh
zips; the checker pins both ends).
