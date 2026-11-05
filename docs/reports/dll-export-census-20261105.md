# DLL export census — promised functions vs provided implementations (2026-11-05)

Lane `lane-dll-census`, worktree `/home/ubuntu/wt-dllcensus`, base integ
`48d27952a` (the batch-8 register-audit commit). Nothing pushed, deployed,
or merged; master untouched.

## What was built

`tools/conformance/dll_export_census.py` — a standing census in the
`tools/conformance/` family (siblings: `pe_census.py` for the loader
surface, `syscall_census.py` for the kernel surface). Where the loader
census proves a program's imports EXIST in the staged runtime, this
census proves what the resolved export actually IS: a real
implementation, a forwarder to one, a data export — or a Wine
*unimplemented stub*: a function that loads fine and fails the moment it
is called (`Unimplemented function X.Y called`).

**Detection, and why it can be trusted.** Wine's build tool generates
every spec `stub` entry as the same tiny body: it loads the DLL's own
name and the function's own name as arguments and calls the DLL's copy
of `__wine_spec_unimplemented_stub` (x86-64: `sub rsp,0x28; lea rcx;
lea rdx; call`; i386: `sub esp,0xc; mov [esp+4],<func>; mov [esp],
<module>; call`, after alignment padding). The census pattern-matches
that body and then *validates* it against the binary: the embedded
module string must equal the DLL's own export-directory name and the
embedded function string must equal the export's name. On the entire
shipped set the validation is total: **34,990 pattern hits, 0
unvalidated** — every counted stub carries its own name as proof. Two
traps were checked explicitly and are NOT counted: win32u's working
`NtUser*`/`NtGdi*` syscall entries (a different generated shape:
`mov r10,rcx; mov eax,<num>; syscall`) and import thunks (an export
that jumps through the import table to a real implementation in
another DLL counts as provided, as it should).

One real bug was found and fixed during development, in the census
itself: the i386 argument order is the reverse of the x86-64 order
(winebuild stores the function name at `[esp+4]` first). The first
full run reported every i386 pattern hit as unvalidated (17,829 of
them); the strings were read directly off the shipped `advapi32.dll`
to settle the true order, and the synthetic fixture in the self-test
now mirrors the real emit order so the mistake cannot return silently.

**The input set is fixed by design**: `wine64.zip` + `wine32.zip` (the
shipped system libraries) plus `prefix32.zip` + `prefix64.zip`, whose
override copies win their (arch, DLL) pair in sorted order — the same
precedence `pe_census.py` and the runtime's staging use. (In the
current vintage the prefix32 overrides are the three DXVK DLLs —
d3d11/d3d9/dxgi, all fully implemented; prefix64 carries no DLLs.)
Other zips a deployment serves from the same directory (the support
archive, the glibc rootfs zips) are not the system libraries and are
ignored.

## The standing guard

`tools/conformance/dll_export_baseline.json` records the census of the
currently shipped vintage (gate archive `runtime-assets-20261104`,
which is what batch 7 shipped and what the live site serves):

- zip sha256 for both wine zips (the vintage gate),
- per-DLL rows for all 1,171 tables: slots, named exports, forwarders,
  stub slots, code, data, empty, and the full sorted stub-name list,
- totals (below).

`dll_export_census.py --check --baseline …` exits 0 on an exact match,
1 on drift (a stub appeared, a stub disappeared, or a count changed —
the change that causes drift regenerates the baseline in the same
commit, so the record shows it), and 2 when the wine zips are a
different vintage (a restage regenerates the baseline deliberately).
The prefix zips are not hash-gated — they also carry deploy-generated
registry files (the live prefix32.zip differs from the archive's in
`system.reg`/`user.reg`/`deskpin32.exe` only; every DLL byte is
identical) — their DLL tables are compared exactly by the drift diff.

Wiring: `web/tests/suite/unit/dll-export-census.test.mjs` (3 tests) —
the classifier's verdicts on synthetic x86-64 and i386 PEs
(`--self-test`), the baseline's internal consistency, and, whenever
the runtime zips are staged in the tree (the normal post-bootstrap
state), a full `--check` run against them. A tree carrying a different
runtime vintage skips the third test by the exit-2 contract rather
than failing. Verified behaviour: `--check` passes against the gate
archive's runtime dir AND against the live deployed runtime dir
(`/home/ubuntu/kn0tzer/winbox/runtime`) — the check is green on what
is actually served today. A tampered baseline (one invented stub)
fails with exit 1 naming the DLL and function; a swapped wine zip
exits 2 with the vintage message. Full-run time ≈ 11–18 s on the
shared box.

## Coverage — the honest numbers (shipped batch-7 vintage)

Effective staged view (wine zips + prefix overrides), both arches:

| | x86-64 | i386 | total |
|---|---|---|---|
| DLL tables | 565 | 606 | 1,171 |
| export slots | 87,602 | 118,500 | 206,102 |
| named exports | 80,029 | 81,647 | 161,676 |
| forwarders | 9,460 | 10,030 | 19,490 |
| **stub slots (validated)** | **17,197** | **17,793** | **34,990** |
| named stubs | 16,797* | 16,959* | **33,756** |
| real code | 51,983 | 52,657 | 104,640 |
| data exports | 2,382 | 2,389 | 4,771 |
| empty slots | — | — | 41,661 |

*per-arch named-stub split from the baseline rows (stub slots include
ordinal-only stubs). Of the 161,676 named exports, **127,920 (79.1%)
are real implementations or forwarders to one; 33,756 (20.9%) are
validated unimplemented stubs.** 686 of the 1,171 DLL tables carry at
least one stub. The 41,661 empty slots are address-table holes (an
ordinal import landing there fails; they are counted, not
name-checkable — the same rule `pe_census.py` states).

Where the stubs live (disjoint partition of the 33,756 named stubs):

- **win32u.dll — 2,037** (926 x64 + 1,111 x86): 1,572 `NtUser*`/`NtGdi*`
  names plus 465 composition/display/Dxgk-family names Wine leaves
  unimplemented. → FD-168
- **Microsoft CRT family — 10,698** across msvcp60–140, msvcr70–120
  (+`_app`), ucrtbase, concrt140, msvcm80/90, api-ms-crt: the C/C++
  runtime surface MSVC-built programs link against. → FD-169
- **Whole-DLL absences — 6,734 across 90 tables** that are ≥90% stub
  (of 96 such tables before the win32u/CRT split): entire subsystems a
  program can load but never use — esent.dll 336/336 named in BOTH
  arches (the Jet database engine simply does not exist), msasn1
  259/266, adsldpc 175/175, wdscore 159/159, bluetoothapis 95/96,
  winusb 21/22, dx8vb, mprapi, fwpuclnt, clusapi, regapi, samlib, …
  → FD-170
- **The long tail — 14,287** spread over the remaining
  mostly-implemented DLLs (setupapi alone: 263 x64 + 265 x86;
  rpcrt4: 214 + 214; combase: 175 + 166; sechost: 120 + 120;
  ntdll: 113 + 111; kernel32: 89 + 97; shell32: 69 + 69;
  gdi32: 67 + 68). → FD-171

## What the demo programs actually touch

The census re-resolves every demo program's imports (the
`pe_census.py` demo set) with stub awareness — an import counts as
STUB when its resolution, chasing forwarders, lands on a validated
stub. Full detail in the evidence JSON.

- **No demo imports a stub on its static load path** (in-closure,
  regular imports): 0 across all 15 demos. Programs start; the stub
  surface is a feature-path hazard, not a load-path one.
- **Chromium** (chrome.dll, delay imports, dynamically loaded):
  35 distinct stub functions — Bluetooth GATT (bluetoothapis ×8,
  bthprops.cpl ×3), the ESENT Jet engine (×12), Media Foundation
  (mf `MFCreateDeviceSource`, mfplat `MFCreateMediaBufferWrapper`),
  wevtapi `EvtRender`, WinUSB (×8), propsys `VariantCompare`. These
  fail only when the corresponding feature path runs (Web Bluetooth,
  WebUSB, media capture, event logging).
- **Gang Beasts** (Xbox/Party plugin DLLs, regular imports of
  dynamically loaded libraries): 4 stubs, all MSVC error-path helpers
  in msvcp140 (`std::_Throw_C_error`, `std::_Throw_Cpp_error`,
  `std::_XGetLastError`, `std::_Xbad_function_call`) — called when
  something else has already gone wrong; the stub then raises
  "unimplemented" instead of throwing.
- **Mindustry** (steam_api64.dll, regular import): setupapi
  `SetupDiGetDeviceInterfaceAlias` — a device-enumeration path.

These 40 pairs are filed as FD-167 with the census JSON as evidence.

## Limits, stated plainly

- The census proves *presence of an implementation*, not correctness:
  a function implemented in C as a semi-stub (a FIXME plus a plausible
  return) is invisible to static analysis. That is a behaviour
  question for guest probes, exactly as the loader census's README
  states for its own necessary-condition result.
- API-set imports are resolved by Wine's api-set database at runtime;
  api-set DLLs themselves are almost entirely forwarders and are
  counted as such.
- The census covers the staged system-library zips. DLLs a program
  bundles itself are classified for the cross-reference when they are
  Wine-built, but they are the program's own surface, not the
  runtime's promise.

## Filings (all provisional numbers, settling at the merge audit)

- **FD-167** — the 40 demo-reachable stub pairs above.
- **FD-168** — the win32u stub family (2,037).
- **FD-169** — the Microsoft CRT stub mass (10,698).
- **FD-170** — whole-DLL absences (6,734 across 90 tables ≥90% stub).
- **FD-171** — the remaining long tail (14,287) and the standing
  draw-down rule: the census check fails on any new stub; a resolved
  stub is recorded by regenerating the baseline in the fixing change.

(FD-166 is expected to go to the in-flight pre-handshake stall
renumber at the same audit; these entries start at FD-167 to leave it
free.)

## Proof

- `--self-test`: classifier verdicts correct on synthetic x86-64 and
  i386 PEs (stub / code / forwarder / data all distinguished).
- `--check` vs the gate-archive runtime dir: exact match (exit 0).
- `--check` vs the live deployed runtime dir: exact match (exit 0).
- Tampered baseline → exit 1, drift lines naming DLL + function.
  Swapped wine64.zip → exit 2, vintage message.
- JS unit suite: `node tools/run-tests.mjs` — **ALL TESTS PASSED**
  (legacy 115 pass / 0 fail / 1 skipped; unit suite exit 0 on the
  loaded shared box). The new test file also passes standalone:
  3/3 in ~12 s. (One earlier combined run, under the batch-8 gate's
  heavier load, reported a legacy failure in its tail; the legacy
  set re-run standalone and the full re-run both pass, and this lane
  changes no code the legacy tests exercise.)
- Census report + cross-reference: `test-results/dll-export-census/
  dll-export-census.json` (also copied to the consolidated evidence
  store). Detector validation on the shipped set: 34,990 pattern
  hits, 0 unvalidated.

## Files

- `tools/conformance/dll_export_census.py` (new)
- `tools/conformance/dll_export_baseline.json` (new, generated)
- `web/tests/suite/unit/dll-export-census.test.mjs` (new)
- `tools/conformance/README.md` (census section added)
- `docs/defect-register.md` (FD-167..171 + Section B row)
- This record.
