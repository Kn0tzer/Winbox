# FD-170 — whole-DLL placeholder libraries: per-DLL decisions and the x64 draw-down

Lane: `lane-fd170`, based on the DLL-export census branch tip
`1e0a37f99` (itself the batch-8 integration tip `48d27952a` plus the
census). Date: 2026-11-05/06.

FD-170's set, reproduced exactly from the census baseline with the
rule stated in the register entry (named exports >= 20, named-stub
fraction >= 90%, excluding the win32u family — FD-168 — and the CRT
family — FD-169): **90 tables, 50 distinct DLLs, 6,734 named stubs.**

## What this lane did

1. Measured every FD-170 DLL's stub state in all three Wine vintages
   the project can draw on — the shipped Wine 8.0 (x64) and Wine 10.0
   (x86) sources, and current upstream master — from the spec files.
   Result: for 49 of the 50 DLLs the stub state is essentially
   identical in all three vintages (upstream has never implemented
   them anywhere). The single exception is `bluetoothapis`, where
   Wine 10.0 carries real radio/SDP code Wine 8.0 lacks.
2. Implemented, on the x64 side, the three DLLs where an
   authoritative implementation exists to mirror:
   - **bluetoothapis.dll** — Wine 10.0's own `main.c`/`sdp.c`
     backported into the Wine 8.0 build (Wine 8.0 ships this DLL as
     a spec-only module with no C sources at all), plus the
     no-radio / no-device contract bodies the FD-167 lane proved on
     the i386 side, and the headers they need (`bluetoothapis.h` at
     its 10.0 state, new `bthdef.h`, upstream
     `bluetoothleapis.h`/`bthledef.h`). Named stubs 95 -> 71.
   - **winusb.dll** — the FD-167 no-device contract bodies (the same
     eight functions, byte-identical patch content; the 8.0 and 10.0
     winusb sources were byte-identical before patching, and the
     patch also carries FD-167's `include/winusb.h` update the
     contract signatures need). Named stubs 21 -> 13.
   - **netutils.dll** — Wine 10.0's `main.c` (the `NetApiBuffer*`
     process-heap buffer functions) backported; Wine 8.0 ships this
     DLL spec-only too. Named stubs 22 -> 17.
   Delivered as `tools/buildWine/patches/wine8-fd170-contracts.patch`
   in the established Wine 8.0 backport pipeline (built by
   `tools/rootfs64/build-wine8-backports.sh`, staged by
   `tools/rootfs64/stage-wine8-backports.sh` +
   `tools/rootfs64/build-wine64-zip.sh`, hash-pinned fail-closed in
   `tools/rootfs64/check_runtime_completeness.py`). Pins:
   bluetoothapis.dll `306a2b6fea13…`, winusb.dll `cb65e30f375e…`,
   netutils.dll `47652b97794e…`. The five older backport DLLs and
   win32u.so came out of the same build byte-different (the build is
   not byte-deterministic for them, as FD-167 also recorded) and
   were restored to their pinned bytes from git; only the three new
   DLLs are kept. One integration fix rides along: the backport
   build's source reset now also removes files a previous patch run
   added (`git clean` over the two patched areas) — without it, any
   re-run of the build dies in `git apply` with "already exists" on
   this patch's new headers.
3. Recorded a per-DLL decision for all 50 DLLs (register entry
   FD-170, discharged as provisional CL-125): the three above as
   implemented (x64), the other 47 as documented absences, each with
   the concrete substrate it would need (table below). The x86 sides
   of bluetoothapis/winusb were already drawn down by the FD-167
   lane (separate branch; the composition sees both). Runtime
   assets re-cut as `runtime-assets-20261105-fd170`
   (tar sha256 `7e818c7b4530…`, 514,447,360 bytes; wine64.zip
   sha256 `7ad41bcf237c…`).

## Proof

- **Census against the built binaries (the lane's hard proof).**
  Running `dll_export_census.py` over a runtime dir carrying the
  staged wine64.zip differs from the base baseline in **exactly
  three tables**: x64 bluetoothapis named stubs 95 -> 71 (code
  exports 1 -> 25; the resolved names are the 13 Wine 10.0 radio/SDP
  functions plus the 11 FD-167 contract functions), x64 winusb
  21 -> 13 (code 1 -> 9, the eight FD-167 contracts), x64 netutils
  22 -> 17 (code 0 -> 5). Totals 33,756 -> **33,719** named stubs —
  exactly the 37 resolved, not one more. The baseline was
  regenerated in the same change and `--check` exits 0.
  `tools/browser/bootstrap.py --check` passes on the re-cut
  archive: "Runtime assets verified (13 files)", completeness
  0 failures.
- **JS unit suite: 352 passed / 2 failed.** Both failures are
  artifacts of the dist this lane tested against (below), not of
  the change: `provenance-artifacts` (fails identically on unbuilt
  trees at base, per the census lane's record) and
  `binary-patch-registry` (compares the dist's BUILD_INFO wasm
  hash against web/runtime's; they diverge because the dist was
  borrowed). The census guard test itself passes.
- **Guest fixture — verdict owed to the batch-9 gate.**
  `tools/guest-fixtures/fd170-contracts64.c` +
  `web/tests/suite/browser/fd170-contracts64.test.mjs` assert the
  radio contracts (no-radio answers from the real Wine 10.0 code),
  the GATT validation contracts, the WinUSB no-device contracts,
  and real NetApiBuffer allocate/size/reallocate/free behaviour
  including content preservation across reallocation. On
  2026-11-05/06 the shared box never gave the fixture a completed
  run: four attempts died in the probe-slot queue without starting
  (at the 300 s, 900 s and 1800 s budgets), and the one attempt
  that acquired a slot (900 s budget) was still in its guest boot
  sequence — the console shows it reaching the `vkboot` readiness
  stage with syscall counts still climbing — when the 960 s
  hung-probe cap killed it, at load 13–19 on the 4-core box shared
  with five other lanes and a CI pipeline's back-to-back builds.
  The lane's own dist build met the same contention (four attempts
  died waiting for the shared build lock), so the runs used the
  batch-8 gate's dist copied from the integration tree — its
  wine64.zip is byte-identical to the shipped pin
  (`be4d4956b9cc…`), and the staging script swapped this lane's
  staged zip into it for the post-change configuration, the same
  mechanism the merge process uses. This is the same handling
  FD-167's x64 side received (verdict owed to the gate); the RED
  side needs no faith either — the census proves the shipped
  bluetoothapis.dll carries winebuild stub bodies for every
  function the fixture calls first, and a stub body raises
  0x80000100, so against the pre-change runtime the fixture
  process dies before printing any RESULT line. Preserved logs:
  `test-results/fd170/` in the lane worktree (red-run*.log,
  green-loop*.log, the timed-out attempt's console in
  `test-results/suite-fd170-contracts64/`).

## Process note (evidence discipline)

Two background-result deliveries during this lane were corrupt:
one reported a completed dist build that had in fact died waiting
for the shared build lock, and one reported a complete guest-run
console narrative for a run whose preserved log shows it timed out
waiting for a probe slot and never started. Both were caught by
re-reading the preserved logs on disk; every claim in this record
is taken from files re-read after the fact, not from deliveries.
A third tooling lesson is recorded in the build script change
above: content-bearing shell heredocs over the SSH channel can
mangle (one retry-loop script arrived corrupted); the lane's
scripts were written as files and copied instead.

## Per-DLL decisions

Implemented (x64, this lane):

| DLL | stubs before -> after (x64 named) | basis |
| --- | --- | --- |
| bluetoothapis | 95 -> 71 | Wine 10.0 radio/SDP code + FD-167 contracts |
| winusb | 21 -> 13 | FD-167 no-device contracts |
| netutils | 22 -> 17 | Wine 10.0 NetApiBuffer* |

The remainders of these three stay stubs for the same reasons as
their families below: bluetoothapis's remaining surface is the
device/SDP-object layer upstream still stubs even on master (72 of
97) pending its winebth driver work; winusb's remaining surface is
the descriptor/policy layer whose upstream bodies are libusb-backed
device paths and there is no USB device backend in the sandbox;
netutils's remaining 17 are server-service utility entry points
upstream stubs in every vintage.

Documented absences (47 DLLs). "All vintages" below means the
measured spec state is stub-identical in Wine 8.0, Wine 10.0 and
upstream master — no authoritative implementation exists anywhere
to mirror.

| DLL | named stubs (both arches) | decision basis |
| --- | --- | --- |
| esent | 672 | The Jet embedded database engine. Upstream has never implemented any of it (336/336 stubs per arch even on master); an embedded DB engine is a subsystem of its own, not a contract surface. FD-167 separately showed the one demo-side consumer's Jet calls sit in an unreachable legacy-import path. |
| msasn1 | 518 | ASN.1 BER/DER encoding runtime. Upstream implements 7 of 266 in every vintage; the other 259 functions are a whole encoding runtime whose stream semantics have no open authoritative implementation to mirror — left stub under the same standard that left `std::_Throw_Cpp_error` stub (FD-172) rather than guessed. |
| adsldpc | 350 | Active Directory LDAP provider; its calls terminate in a domain controller's directory service. All vintages stub-identical. |
| wdscore | 318 | Windows setup/deployment scoring internals; undocumented. All vintages stub-identical (master's export list shrank to 93, still all stub). |
| apphelp | 312 | Application-compatibility shim engine. Its answers come from the system shim database, a Windows data artifact this runtime does not ship, plus in-process patching machinery. Upstream implements only a 13-function subset even on master; the 10.0 vintage adds 3 database-query entry points that are meaningless without the database. |
| fwpuclnt | 274 | Windows Filtering Platform client; terminates in the kernel filtering engine and the Base Filtering Engine service. No packet-filtering substrate exists in a browser sandbox. All vintages stub-identical. |
| dx8vb | 270 | DirectX 8 Visual Basic automation layer — a retired COM surface. All vintages stub-identical. |
| mprapi | 258 | Routing and Remote Access management; terminates in the RRAS service, which does not exist here. All vintages stub-identical. |
| clusapi | 216 | Failover-cluster API; terminates in the cluster service. No cluster exists or can exist inside one browser tab. All vintages stub-identical. |
| winsta | 174 | Terminal-services station API; terminates in the session-manager service. The runtime has exactly one session by construction. All vintages stub-identical. |
| msdrm | 170 | Rights Management client; license acquisition terminates in RMS servers and protected-content infrastructure an open runtime cannot provide. All vintages stub-identical. |
| msls31 | 158 | Uniscribe line-layout services — a full text-layout engine upstream has never implemented in any vintage. |
| vssapi | 152 | Volume Shadow Copy; terminates in the VSS service and volume snapshot drivers. The sandbox has no block-device layer. All vintages stub-identical. |
| atmlib | 148 | ATM (asynchronous transfer mode) networking for retired hardware. All vintages stub-identical. |
| resutils | 142 | Cluster resource utilities; same cluster-service substrate as clusapi. All vintages stub-identical. |
| regapi | 138 | NT-era registry API, superseded by advapi32's registry surface (which is provided). All vintages stub-identical. |
| samlib | 130 | Security Accounts Manager client; terminates in the SAM/LSA service. The runtime runs no account-database service. All vintages stub-identical. |
| sppc | 126 | Software-protection (activation) client; terminates in the licensing service and Microsoft's activation infrastructure. All vintages stub-identical. |
| localspl | 122 | Local print-spooler provider. The runtime has no spooler and no printers. All vintages stub-identical. |
| npptools | 120 | NetWare-era network-provider tools; retired subsystem. All vintages stub-identical. |
| twinapi.appcore | 120 (x86 table) | WinRT/UWP app-model core; terminates in the UWP activation and deployment infrastructure the runtime does not carry. All vintages stub-identical. |
| d3d8thk | 112 | Direct3D 8 driver-thunk layer; terminates in display-driver kernel thunks. This runtime's graphics path is the WebGPU/WebGL bridges, not display drivers. All vintages stub-identical. |
| rtutils | 108 | RRAS tracing/routing utilities; same RRAS service substrate as mprapi. All vintages stub-identical. |
| wiaservc | 108 | Windows Image Acquisition service client; terminates in the WIA service and scanner/camera devices. No imaging devices exist in the sandbox. All vintages stub-identical. |
| feclient | 100 | EFS client; terminates in the Encrypting File System service and its key infrastructure. All vintages stub-identical. |
| w32skrnl | 91 (x86 table) | 16-bit WOW kernel thunk layer. The sandbox has no 16-bit subsystem — the CPU core is long-mode only (project-established) — so this layer can never have anything beneath it. |
| dhcpcsvc | 84 | DHCP client service API. Guest networking is provided by the emulator's relay; there is no DHCP client service. Upstream stubs the whole DLL in every vintage, including the trivial initialize pair — implementing the pair alone would report success for a service that cannot answer, a fake surface, so it is refused as a set. |
| ktmw32 | 82 | Kernel transaction manager client; terminates in the NT kernel's transaction manager. No transactional NTFS exists here. All vintages stub-identical. |
| appxdeploymentclient | 77 (x86 table) | AppX deployment client; terminates in the AppX deployment service (UWP app model). All vintages stub-identical. |
| utildll | 72 | LAN Manager utility library; retired. All vintages stub-identical. |
| hal | 71 (x64 table) | Hardware abstraction layer; by definition it talks to real buses and hardware. A browser sandbox has neither. All vintages stub-identical. |
| bcp47langs | 71 (x86 table) | WinRT globalization language data tables for a globalization stack the runtime does not carry; upstream implements 2 of 71 even on master. |
| rasdlg | 70 | Remote Access dial-up dialogs; RAS service substrate and modem UI for hardware that cannot exist here. All vintages stub-identical. |
| wmasf | 70 | Windows Media Format (ASF) runtime, superseded by Media Foundation — mf/mfplat are provided and partially implemented. Upstream never implemented wmasf in any vintage. |
| ntprint | 64 | Print-spooler support; same spooler substrate as localspl. All vintages stub-identical. |
| tdh | 64 | ETW event-information API; reads provider metadata registered with the OS by installed components. The runtime keeps no ETW provider registry; upstream implements 3 of 34. |
| drmclien | 56 | Windows Media DRM client; same protected-content substrate as msdrm. All vintages stub-identical. |
| odbcbcp | 56 | SQL Server bulk-copy library; requires the SQL Server ODBC driver and a server endpoint. The ODBC driver manager (odbc32) is a separate, provided surface. All vintages stub-identical. |
| slbcsp | 50 | Smart-card base CSP; smart-card hardware substrate (with sccbase). No card readers exist in the sandbox. All vintages stub-identical. |
| sccbase | 48 | Smart-card base library; same hardware substrate as slbcsp. All vintages stub-identical. |
| dhcpcsvc6 | 44 | DHCPv6 client service; same basis as dhcpcsvc. All vintages stub-identical. |
| printui | 42 | Printer user interface; spooler substrate. All vintages stub-identical. |
| svrapi | 40 | LAN Manager server API; retired file-server management surface. All vintages stub-identical. |
| traffic | 38 | QoS traffic-control API; terminates in the kernel packet scheduler, a layer the sandbox does not have. All vintages stub-identical. |
| coremessaging | 29 (x86 table) | WinRT core messaging / dispatcher infrastructure (UWP app model). All vintages stub-identical. |
| magnification | 21 (x86 table) | Screen-magnifier runtime; part of the desktop accessibility/compositor stack. Upstream implements 2 on master. |
| tbs | 19 (x64 table) | TPM base services. No TPM device exists in the sandbox; upstream implements 2 of 21 on master. |
