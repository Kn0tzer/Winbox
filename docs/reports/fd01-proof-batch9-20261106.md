# FD-01 closure proof on the live batch-9 build

Branch `lane-fd01-proof`, base integ `faf95d26c`.
Live build under test: git_sha `d50bcc02f519740b0af20da961d15c32c783e206`,
wasm sha256 `92b36180e9b3469b07014b91cf0054e4f50e1b119b2881e6ca1a85819d27bfdf`
(5,722,692 B), built 2026-11-06T22:32:28Z by "deploy-batch9".
BUILD_INFO re-read over public HTTPS before, during, and after the
measurement — unchanged throughout.

## Protocol (identical for every boot)

The preserved live driver
(`test-results/deploy-finalbatch-20261031/live-driver.mjs`, byte-identical
to the copy the batch-9 deploy lane ran), through the public route
`https://kn0tzer.work.gd/winbox`:

- brand-new browser profile per boot (first-time-visitor shape);
- the deskpin-anchored harness session (session=1) at
  `/runtime/index.html?p=cmd /c echo vkboot&session=1&persist=0&fresh=1&arch=<arch>&bwretry=2`;
- the launcher's restart budget pre-exhausted (`bwretry=2`), so the first
  attempt is what is measured and no start is a rescue;
- 900 s observation window, budget starting at page goto;
- full console captured per boot and classified OFFLINE under the hardened
  FD-47 rule: certified = page's own `bwRuntime.ready()` AND a plausible
  deskpin handle (nonzero, top bit clear) AND the kernel OPENED
  spawn-signal line for explorer.exe. `pageReady && !certified` is
  false-ready (the FD-43 shape); the FD-01 failure signature is
  "The explorer process failed to start".

## Results

### Batch-9 deploy lane (2026-11-06 ~22:39–22:46 UTC, re-verified from disk)

- x86-64 n=3: certified 3/3, handle `0x10048` every boot, ready
  84.4 / 116.9 / 88.1 s (median 88.1 s), restarts 0, rescues 0,
  false-ready 0, BW64-FAIL 0, memory faults 0.
- x86 n=5: certified 5/5, handle `0x10052` every boot, ready
  18.8 / 18.7 / 19.7 / 18.8 / 18.4 s (median 18.8 s), restarts 0,
  rescues 0, false-ready 0, BW64-FAIL 0, memory faults 0.
- Verified from the preserved logs, not the lane's summary: every
  boot's `results.jsonl` record carries `class: certified`,
  `plausible: true`, `explorer: true`; every boot's `console.log`
  carries exactly one `deskpin: READY` line with the real handle and
  the OPENED explorer lines, zero "restarting session boot" lines,
  zero BW64-FAIL lines.
- Evidence: `test-results/deploy-batch9-20261106/cert-x64`,
  `cert-x86` (results.jsonl, summary.json, boot-*/console.log).

### This lane (2026-11-07 ~00:29–00:36 UTC, independent replication)

- x86-64 n=3: certified 3/3, handle `0x10048` every boot, ready
  126.8 / 105.4 / 95.7 s (median 105.4 s), restarts 0, rescues 0,
  false-ready 0, BW64-FAIL 0, memory faults 0.
- Same verification from the preserved logs: every boot's
  `results.jsonl` record certified with plausible handle and
  explorer true; every `console.log` carries exactly one
  `deskpin: READY hwnd=0x10048` line, three OPENED explorer lines,
  zero restarts, zero BW64-FAIL.
- Box 1-minute load during these boots ran 3.2–8.0 (other lanes'
  work interleaved); certification was unaffected.
- Evidence: `test-results/fd01-proof-batch9-20261106/live-batch9-x64`
  (results.jsonl, summary.json, boot-*/console.log) plus
  `live-batch9-x64.log` and the driver copy used.

### Batch-9 totals

- x86-64: **6/6 certified** (84.4, 88.1, 95.7, 105.4, 116.9, 126.8 s;
  median 100.5 s), zero restarts, zero rescues, zero false-ready.
- x86: **5/5 certified** (median 18.8 s), zero restarts, zero rescues,
  zero false-ready.

This matches the 2026-10-31 closure precedent (6/6 x86-64 + 3/3 x86)
and exceeds it on x86 (5/5).

## Verdict

**PROOF PASSES.** FD-01's closure holds on the live batch-9 build:
first-attempt starts certify under the hardened rule on both widths,
with the restart safety net needed zero times in all 11 measured
boots. The §4.68 restart stopgap remains in the shipped launcher by
separate decision; nothing here retires it.

## Observations (not part of the verdict)

- Every boot's console carries Wine service-autostart lines
  ("Auto-start service L\"MountMgr\" / L\"NDIS\" / L\"nsiproxy\" /
  L\"wineusb\" failed to start: 1114"). That is the gamepad lane's
  FD-173 finding (Wine kernel-mode drivers fail to initialise in
  the emulator), not FD-01's signature, and it does not affect
  certification: explorer still opens and the session certifies.
- Incident: the SSH control connection carrying this lane's driver
  run dropped mid-run (exit 255) during boot 3. The driver process
  itself survived and completed normally (SUMMARY printed,
  results.jsonl complete); the drop affected only this lane's view,
  not the measurement. No re-run was needed.

## Files

- Record: `docs/reports/fd01-proof-batch9-20261106.md` (this file).
- Evidence, this lane:
  `test-results/fd01-proof-batch9-20261106/` (live-driver.mjs,
  live-batch9-x64.log, live-batch9-x64/results.jsonl,
  live-batch9-x64/summary.json, live-batch9-x64/boot-*/console.log).
- Evidence, deploy lane (re-verified from disk):
  `test-results/deploy-batch9-20261106/cert-x64`,
  `test-results/deploy-batch9-20261106/cert-x86`.
- Register: FD-01 entry in `docs/defect-register.md` annotated with
  this re-measurement.
