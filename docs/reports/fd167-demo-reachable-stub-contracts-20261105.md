# FD-167 — demo-reachable Wine spec stubs given real behaviour — 2026-11-05

Lane: `lane-fd167`, off `lane-dll-census` tip `1e0a37f99`.
Scope: the 40 demo-reachable stub pairs filed as FD-167 by the DLL export
census (`docs/reports/dll-export-census-20261105.md`).

## Disposition of the 40 pairs

- **27 implemented with real behaviour**, proven where a guest can reach them:
  - bluetoothapis.dll — `BluetoothGATTGetCharacteristicValue`,
    `BluetoothGATTGetCharacteristics`, `BluetoothGATTGetDescriptors`,
    `BluetoothGATTGetIncludedServices`, `BluetoothGATTGetServices`,
    `BluetoothGATTRegisterEvent`, `BluetoothGATTSetCharacteristicValue`,
    `BluetoothGATTUnregisterEvent`: the documented no-radio contract from
    Wine 10.0 (real upstream code, not a guess): ERROR_INVALID_HANDLE for a
    null service handle, ERROR_BAD_COMMAND for a null/invalid device, and
    `SetLastError(ERROR_INVALID_HANDLE)` for a null radio handle in
    `BluetoothGATTUnregisterEvent`.
  - bthprops.cpl — `BluetoothFindDeviceClose` (FALSE + ERROR_INVALID_HANDLE),
    `BluetoothFindFirstDevice` (NULL + ERROR_INVALID_PARAMETER for a null
    params pointer, else ERROR_NO_MORE_ITEMS),
    `BluetoothFindFirstRadio` (NULL + ERROR_INVALID_HANDLE): Wine 10.0 bodies.
  - winusb.dll — `WinUsb_ControlTransfer`, `WinUsb_GetOverlappedResult`,
    `WinUsb_Initialize`, `WinUsb_QueryDeviceInformation`,
    `WinUsb_QueryInterfaceSettings`, `WinUsb_ReadPipe`, `WinUsb_ResetPipe`,
    `WinUsb_WritePipe`: the no-device contract (FALSE + ERROR_INVALID_HANDLE);
    `WinUsb_Initialize` validates its out-parameter first
    (ERROR_INVALID_PARAMETER, handle set to NULL). Wine 10.0 bodies, whose
    validation order mirrors upstream master.
  - mf.dll — `MFCreateDeviceSource`: upstream body verbatim (NULL source →
    E_INVALIDARG, `*source = NULL`, returns E_NOTIMPL).
  - mfplat.dll — `MFCreateMediaBufferWrapper`: a genuine implementation —
    an IMFMediaBuffer window over another IMFMediaBuffer at an offset and
    length (delegates locking/querying to the inner buffer with clamped
    ranges), not an error stub.
  - propsys.dll — `VariantCompare`: the real comparison — delegates to
    PropVariantCompareEx and adds its own VT_BOOL / VT_DATE / VT_FILETIME
    handling. The one demo-reachable pair with a live guest caller (Chromium's
    shortcut handling).
  - wevtapi.dll — `EvtRender`: upstream body verbatim (FALSE +
    ERROR_CALL_NOT_IMPLEMENTED).
  - msvcp140.dll (x86-64 / Wine 8.0 pipeline) — `_Throw_C_error`,
    `_XGetLastError`, `_Xbad_function_call`: upstream master bodies backported
    (`_Throw_C_error` throws a `std::system_error` carrying the mapped errno;
    `_XGetLastError` throws `std::system_error` with GetLastError();
    `_Xbad_function_call` throws `std::bad_function_call`). Verified at the
    shipped ordinals 623 / 642 / 647.
  - setupapi.dll (x86-64 / Wine 8.0 pipeline) —
    `SetupDiGetDeviceInterfaceAlias`: a genuine implementation for the 8.0
    tree (the 8.0 spec entry was a stub; 10.0/master already implement it),
    delegating to the existing device-interface lookup with the alias class
    GUID. Verified at ordinal 345.
- **12 proven unreachable in practice, recorded as such** — esent.dll
  `JetAddColumnA`, `JetAttachDatabaseA/W`, `JetBeginSessionA`,
  `JetBeginTransaction`, `JetCreateDatabaseA`, `JetCreateIndexA`,
  `JetCreateTableA`, `JetEndSession`, `JetGetTableColumnInfoA`,
  `JetOpenDatabaseA/W`: the census lane's reachability evidence was
  re-examined — chrome.dll's Jet call sites sit in the `EdgeDataImporter`
  string neighbourhood (the legacy-Edge profile importer). Reaching them
  requires a legacy EdgeHTML ESENT WebCache profile on disk and an explicit
  import action; the shipped demo has neither, so no guest path in the demo
  can call them. Upstream Wine has never implemented esent (336/336 exports
  are stubs even on master), so there is no authoritative behaviour to copy
  either. They stay stubs, recorded here and in the register.
- **1 pair is neither implemented nor unreachable** — msvcp140.dll
  `?_Throw_Cpp_error@std@@YAXH@Z`: demo-reachable in principle, but no
  authoritative semantics exist anywhere (Wine master: stub; ReactOS: stub;
  absent from the microsoft/STL open sources; its integer argument is an
  internal code whose enum is in no public header). A guessed exception-type
  mapping would be a silent wrong answer if wrong, so it is left a stub and
  refiled as FD-172 with closing terms: implement once an authoritative
  source for its semantics exists.

## How the fixes ship

- x86-64 (Wine 8.0 pipeline): `tools/rootfs64/wine8-backports.sh` gained the
  msvcp140 and setupapi targets; the two rebuilt DLLs are signature-stamped
  and pinned in `WINE8_BACKPORT_PINS`. New pins: msvcp140.dll
  `c8255d140b6bdbba5d7e0d61b6eea3de05f00dba7e1b8344bce08da54142d58a`,
  setupapi.dll `c7e1555da0fedb6153044bb823185a995f268903bfe55c4cf26193c80ede6d21`.
  The five older backport DLLs rebuilt in the same run came out
  byte-different (non-deterministic build outputs) and were restored to
  their pinned bytes; all six older pins still match exactly. The build
  script's final unix-tree step still exits nonzero on this host for the
  pre-existing FD-65 reason (missing x86-64 freetype/fontconfig development
  files); win32u.so is untouched and its pin is intact.
- i386 (WineHQ 10.0 pipeline): new `tools/rootfs32/build-wine10-fd167.sh`
  builds the seven fixed DLLs (bluetoothapis, bthprops.cpl, mf, mfplat,
  propsys, wevtapi, winusb) from the bottled WineHQ 10.0 sources plus
  `tools/rootfs32/wine10-fd167.patch`, output under
  `tools/rootfs32/wine10-fd167-i386/`; `build-wine32-base.sh` substitutes
  them fail-closed, `regen-wine32-zip.sh` verifies them member-by-member
  against the committed pins, and the completeness checker pins them
  (`WINE10_FD167_PINS32`) with a source-and-zip test. The build's own export
  check passed on all seven.
- Retained runtime archive: re-cut as `runtime-assets-20261105-fd167`
  (the plain same-day name collided with another lane's archive and was
  overwritten; the lane-distinct name avoids the shared-name hazard) and
  `tools/browser/runtime-assets.json` re-pinned to it; the completeness
  gate passes against it.

## Proof

- **32-bit guest fixture: PASS.** `tools/guest-fixtures/fd167-contracts32.c`
  run through `web/tests/suite/browser/fd167-contracts32.test.mjs` passed
  against the fixed runtime (preserved log: `test-results/fd167/green-run.log`).
  It exercises the i386 contract surface (bluetooth, bthprops, winusb,
  mf/mfplat, propsys VariantCompare, wevtapi) end to end in a real guest.
- **64-bit guest fixture: no verdict — stated plainly.** The x86-64 fixture
  (`fd167-contracts64`, plus the msvcp140/setupapi diagnostic splits) was run
  more than twenty times across the session. The first combined run started
  and executed the fixture but hit the runner's 360 s cap without printing a
  result. After that, the box was saturated by the batch-8 merge gate's
  browser rerun chain: a sixteen-attempt requeue loop and the earlier
  standalone runs either timed out acquiring a probe slot (300 s), or — in
  the attempts that did acquire one, thirteen guest consoles preserved as
  `test-results/fd167/console64-attempt*.log` — the x86-64 Wine session
  wedged during boot before the desktop reported ready (hundreds of
  window-metric messages, guest alive then idle, fixture executable never
  started): the known stuck-start behaviour under load, occurring before any
  FD-167 code could run, so those attempts are not evidence against the DLL
  changes. **The 64-bit guest run, and the RED run of the fixtures against
  the pre-change batch-7 runtime zips (whose only attempt likewise died in
  the probe queue, `test-results/fd167/red-run.log`), are owed to the
  batch-9 gate on a quieter box** — re-run
  `node web/tests/suite/runner.mjs --browser --filter=fd167-contracts` there.
  Meanwhile the four x86-64 pairs carry build-level proof: the Wine 8.0
  build's export check, objdump verification at the shipped ordinals
  (623 / 642 / 647 / 345), and census resolution in the x86-64 architecture.
- **Census:** named stubs 33,756 → 33,726. The arch-aware per-pair check
  resolves 27 of the 28 implemented pairs in the architecture the demo uses;
  the only pair still a stub is `_Throw_Cpp_error`, by design (FD-172).
  Baseline regenerated (`tools/conformance/dll_export_baseline.json`).
- **Runtime-completeness gate: 0 failures, 1 skip** (the usual prefix64
  built-artifact skip), standalone and inside the unit suite, against the
  re-pinned `runtime-assets-20261105-fd167` archive.
- **JS unit suite: 354 passed / 0 failed.** Browser build on the fixed tree:
  "Browser export verified" (`test-results/fd167/build-green.log`).

## What remains

- FD-172 (the `_Throw_Cpp_error` remainder) — open, Low, terms in the
  register.
- The esent twelve stay stubs by recorded proof of unreachability; if a
  future demo ships a legacy-Edge import path they become reachable and
  this disposition must be revisited.
- Batch-9 merge audit: FD-167's closure number and FD-172's number are
  provisional until the audit settles them.
