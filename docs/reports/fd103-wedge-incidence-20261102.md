# FD-103 — the wedged first-visit start: filing and measured incidence

Lane `lane-wedge-incidence`, based on integration tip
`7f2bf5f6d`. This record files the single wedged
first-visit 32-bit start observed during the
FD-66 + FD-65 + FD-85 + FD-77-stall-surface deploy
checks and measures its incidence on the shipped
build. No product code was changed.

## The observed instance (from the deploy's preserved evidence)

Source: `docs/reports/deploy-fd66-65-85-fd77surface-20261102.md`
and `test-results/deploy-fd66-65-85-fd77surface-20261102/`
on the integration tree; every figure below was
re-read from those files by this lane.

The shipped build is git_sha `8ac504abf` / wasm
`9a7d101c`. The deploy measured first-visit starts
through the public route
(https://kn0tzer.work.gd/winbox): brand-new browser
profile per boot, the deskpin-anchored harness
session (session=1), the launcher's restart budget
pre-exhausted (bwretry=2) so no start is a rescue, a
900 s window, the full console captured and
classified offline under the hardened FD-47 rule
(pageReady + plausible handle + explorer OPENED).

- x86 n=5: certified 4/5. Boot 1 is the failure:
  `results.jsonl` reads class "other", pageReady
  false, readyMs null, wallMs 901393, hwnd null,
  restarts 0, note "budget exhausted".
- Boot 1's console (684 lines,
  `live-x86/boot-1/console.log`): the session starts
  normally — wineserver32 and the deskpin spawned,
  the fontconfig link set served (5 BW64-INFO
  "hard-link … outcome=served by shared backing
  path" lines) — and the session had opened
  `winedevice.exe` -> fd 63. Then a guest thread of
  the winedevice host process (pid=71, tid=76)
  enters a delivered-memfault loop:
  - first line: `BW64-FAIL guest-memfault32 pid=71
    tid=76 addr=0x10584384 eip=0x10584384 err=0
    delivered=1 count=1`
  - then `addr=0xfffffd08 eip=0x10584384 err=2
    delivered=1` repeated; the preserved lines carry
    counts 1–64 consecutively and a final preserved
    line at count=1024 — 65 BW64-FAIL lines in the
    console, and the thread took at least 1,024
    delivered faults inside the window.
  - the closing BW_THREAD_DUMP lines for pid=71
    tid=76 read state=running, rip=0x10584384,
    noprogress_ms=720019 and finally 780018.
- The desktop handshake never completed (no explorer
  OPENED line, no deskpin READY line in the
  console), zero session restarts occurred, and the
  batch's new FD-77 stall surfacing did not fire —
  by its stated design it reports only stalls after
  the handshake. Within the window the start
  presented as an endless wait.
- The deploy's other starts were clean: x86 boots
  2–5 certified (hwnd 0x10052; readyMs 39019, 40795,
  47131, 33603 — range 33.6–47.1 s, median 40.8 s
  per the preserved summary.json), x64 3/3 certified
  (hwnd 0x10048), and six uninstrumented consumer
  Chromium sessions mapped 6/6 with clean consoles.
  Across the deploy's 8-boot census (5,141 lines)
  the 65 BW64-FAIL lines of boot 1 are the only
  failure lines.
- No mechanism is claimed for the fault — not by the
  deploy record, and not here.

## The incidence series (this lane)

Protocol identical to the deploy's: the deploy's own
`live-driver.mjs`, copied byte-for-byte into this
lane's evidence (md5 `fc1a4cadc8ceb764ac7c9b8aa592f6f5`
on both copies), run as
`--base=https://kn0tzer.work.gd/winbox --arch=x86
--n=10 --budget-ms=900000`, one boot per heavy probe
slot via `web/tests/probe-slots.mjs`, fresh profile
per boot, bwretry=2. The live BUILD_INFO was re-read
during the series and is unchanged: git_sha
`8ac504abf`, wasm_sha256 `9a7d101c…44a02c` (preserved
as `live-build-info.json`). Box 1-minute load across
the series ran ~2.4–5.6 (the driver's own per-boot
readings), with other lanes' probes interleaving
through the slot queue between boots.

Result: 10/10 certified, 0 wedged, 0 false-ready,
0 race-failed, 0 rescues. Every boot: hwnd 0x10052,
restarts 0.

| boot | class     | ready (s) | console lines | BW64-FAIL | memfault32 | served-link | explorer OPENED |
|------|-----------|-----------|---------------|-----------|------------|-------------|-----------------|
| 1    | certified | 23.0      | 317           | 0         | 0          | 5           | 3               |
| 2    | certified | 24.9      | 317           | 0         | 0          | 5           | 3               |
| 3    | certified | 23.1      | 317           | 0         | 0          | 5           | 3               |
| 4    | certified | 24.3      | 317           | 0         | 0          | 5           | 3               |
| 5    | certified | 23.1      | 317           | 0         | 0          | 5           | 3               |
| 6    | certified | 23.9      | 317           | 0         | 0          | 5           | 3               |
| 7    | certified | 24.2      | 317           | 0         | 0          | 5           | 3               |
| 8    | certified | 24.5      | 317           | 0         | 0          | 5           | 3               |
| 9    | certified | 24.2      | 317           | 0         | 0          | 5           | 3               |
| 10   | certified | 22.6      | 317           | 0         | 0          | 5           | 3               |

Median certified-ready 24.2 s (summary.json:
medianCertifiedReadyMs 24190). Census totals over
the ten preserved consoles: 3,170 lines, 0 BW64-FAIL
lines of any kind, 0 guest-memfault32 lines, 50
BW64-INFO served-link lines (exactly 5 per boot, the
standing FD-81 shape), 30 explorer OPENED lines.

The wedge did not reproduce, so no cleaner signature
was obtained; the deploy's boot 1 remains the only
observed instance, and its signature stands as filed
in FD-103.

## Combined count and what it does and does not show

On the shipped build `8ac504abf`, measured
first-visit 32-bit starts now total 15 (the deploy's
5 + this series' 10): 1 wedged, 14 certified. That
bounds the observed rate at one-in-fifteen so far.
It does not prove the rate — a rarer fault would
need a far larger series to bound, and this lane's
brief was to bound, not to chase — and it does not
close the defect: the one observed start failed
silently, which is the failure shape the project's
no-silent-failure rule exists to eliminate. FD-103
records its closing terms accordingly (mechanism
found and fixed with a clean series at least this
size, or pre-handshake stalls of this shape surfaced
as reported failures).

## Evidence

On this lane tree, `test-results/wedge-incidence-20261102/`:
`live-driver.mjs` (the byte-identical driver copy),
`driver.log`, `series/results.jsonl`,
`series/summary.json`, `series/boot-1..10/console.log`
(every console preserved), `series-census.txt`,
`live-build-info.json`. The deploy instance's
evidence stays where the deploy left it:
`test-results/deploy-fd66-65-85-fd77surface-20261102/`
on the integration tree.
