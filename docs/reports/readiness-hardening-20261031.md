# Readiness certification hardened + four-arm start measurement — 2026-10-31

Lane: `lane-readygate`, off integ `0dc6c2961`.
Register: FD-43 closed (CL-10); FD-47 opened and fixed on
this branch (CL-11); FD-01 updated, stays open.

## What was wrong

The launcher's session observer
(`project/emscripten/wine64-launcher.js`,
`observeSessionOutput`) certified a session as started
from the desktop pin's `deskpin: READY hwnd=0x...` line
alone, accepting any hex value. On a build whose win32u
syscalls are never dispatched (FD-40: the staged
win32u.dll lacked the "Wine builtin DLL" signature, so
wineserver never registered its syscall table), the pin's
window creation "returns" the NTSTATUS error `0xC000000D`
(STATUS_INVALID_PARAMETER). The pin checks only for NULL
and prints it as a handle; the launcher certified the
session started. No desktop exists on such a build: no
explorer, zero X11 connections, and the failure signature
that would name the problem is never printed because the
code that prints it never runs (attribution: FD-43).

Every consumer of `bwRuntime.ready()` inherited the false
verdict: the suite's `bootGuest`,
`web/tests/boot-rate.mjs`'s "started" class, and
`web/tests/boot-diag.mjs`. This is the defect that voided
the first-visit start measurements (see the ledger
below).

## The fix (launcher, one block)

Certification now requires the whole startup handshake,
in any order, all three:

1. **A plausible window handle** from the pin's line:
   nonzero with the top bit clear. NTSTATUS-shaped values
   (warning/error classes `0x8.......` / `0xC.......`,
   including `0xC000000D`) are rejected and logged loudly,
   once per distinct value:
   `BW64-FAIL component=launcher.session
   reason=deskpin-hwnd-not-a-handle hwnd=0x...`.
2. **At least five accepted X11 client connections**
   (`XWireServer: accepted X11 client connection`). A
   healthy boot makes exactly five on both arches
   (calibration below); dead boots stall at two.
3. **Explorer actually opened** (the wineserver trace's
   `OPENED '...explorer.exe'` line).

The §4.68 restart watcher (`observeSessionHealth`) is
untouched. The pin fixture itself is untouched: hardening
the consumer rather than the canary is the fix —
rebuilding the committed pin blobs to change their
printout would court exactly the source/binary
divergence FD-40 was.

Unit contract (`web/tests/suite/unit/runtime-session-ready.test.mjs`)
rewritten to the hardened rule: bad handle alone → not
ready; valid handle alone → not ready; handle + 4 X11 →
not ready; handle + 5 X11 without explorer → not ready;
full handshake → ready; order-independent; `0x0` and
`0x80000003` rejected. Red/green verified: the new
scenarios fail against the old launcher, all pass against
the new. Full JS unit suite on this branch: **340 passed
/ 0 failed** (matches the integ baseline).

## Consumer audit

- Suite tests via `bootGuest` / `bwRuntime.ready()`:
  inherit the fix automatically (they consume the shipped
  launcher).
- `web/tests/boot-rate.mjs`: its "started" class waits on
  `ready()` (inherits). Its own pin-line signal is
  negative-only — it can withhold a "race-failed" label,
  never certify a start.
- `web/tests/boot-diag.mjs`: waits on `ready()` (inherits).
- `boot64-smoke`: never used the flag — it asserts
  wineserver-stream evidence directly. Unaffected.
- `deskpin-diag-probe.mjs` and the provenance test: make
  no readiness verdicts.

## Calibration (before the measurement)

- **CAL64** — live dist, OLD launcher in page, broken
  dispatch: pin printed `hwnd=0xc000000d` at 34.6 s and
  the in-page `ready()` returned true. The false positive
  reproduced end-to-end under the measurement driver:
  old-cert true / new-cert false / page true.
- **CAL86** — live dist, 32-bit, hardened rule: certified
  a genuine start — handle `0x1004c`, 5 X11 connections,
  explorer opened, winstation reached; pin at 109.9 s,
  evidence complete 111.5 s. The five-connection
  threshold is correct for both arches; no arch
  conditioning needed.

## The four-arm measurement

Driver: `tools/browser/startup-measurement.mjs`
(alternation: `tools/browser/run-arms.sh`). One boot per
invocation through the project's probe slot system, fresh
profile each, parameters identical to boot64-smoke
(`cmd /c echo vkboot`, session=1, persist=0, fresh=1,
arch=x64, bwretry=2, winedbg='+loaddll,+server'), 360 s
window, arms alternating A B C D, machine load logged per
boot. Verdicts are classified **offline after teardown**
from the complete console stream: on this loaded box,
live console delivery to a driver can lag the guest by
minutes, so a live poll cannot be the classifier (the
per-boot `console.log` the probe harness writes is
complete). Data: `test-results/readygate-summary.json`
and `test-results/suite-readygate-<arm>-<i>/boot.json`.

Arms are symlink forests of the live dist
(`/home/ubuntu/kn0tzer/winbox`, build `7e98ccced`) with
the hardened launcher dropped into all three of A/B/C
(the dist's launcher is byte-identical to its source, so
the drop-in is the faithful build product), plus one
member swap in the wine64.zip where the arm requires it:

| arm | build | n | old cert | new cert | true first-attempt rate | mean load |
|---|---|---|---|---|---|---|
| A stock | live dist, wine64.zip member = stock win32u.dll `643b7623…` | 10 | 0 | 0 | **0/10** | 7.8 |
| B stamped | live dist, member = stamped win32u.dll `49b87438…` | 10 | 0 | 0 | **0/10** | 10.6 |
| C control | live dist, untouched (unsigned member `a8ca5fbb…`) | 10 | **10** | **0** | 0 (no desktop exists) | 9.3 |
| D baseline | dist built from `692875bd8` (stock member, verified line: "Browser export verified") | 10 | 0 | 0 | **0/10** | 8.1 |

DLL provenance: stock win32u.dll extracted from the
20261026 runtime-assets archive on the box (archive sha
`9978166f…`, its wine64.zip `b7476541…`); win32u.so is
byte-identical in the archive and live zips
(`5dac5821…`, stock timeout intact). Arm D's dist was
built by the project's own build from `692875bd8` with
assets restored from that same on-box archive via the
project's bootstrap (the manifest's download URL was
never used); its BUILD_INFO records git_sha `692875bd8…`,
clean tree.

## What the measurement says

- **The control proves the sabotage pair.** On the
  broken-dispatch build the OLD certification says
  "started" 10 of 10 times — total false-ready. The NEW
  certification says "started" 0 of 10 on the same boots,
  offline and in-page alike (arm C's own dist carries the
  hardened launcher and never certified; under the old
  launcher the same build returned in-page "ready" in
  calibration). Every arm-C boot: handle `0xc000000d`,
  X11 connections 0, no explorer.
- **Genuine starts still pass.** The hardened
  certification certifies every independently witnessed
  genuine start this lane produced: the suite's
  boot32-vkboot on the stamped arm's dist passes through
  the hardened predicate (handle `0x10052`, five X11
  connections, winstation reached); celeste-boot on the
  same dist passes with a pixel-verified frame;
  boot64-smoke on the same dist passes; CAL86 above. No
  genuine start was refused. The D3D12 readback oracles
  were not re-run by this lane; FD-43's scoping of their
  status stands untouched (their readiness evidence void,
  their guest readback results a separate claim this lane
  neither confirms nor voids).
- **The race is alive, and the merge wave did not change
  it.** True first-attempt rates: stock 0/10, stamped
  0/10, pre-rebuild baseline 0/10. Every dead healthy-arm
  boot shows the same fingerprint: explorer opens and
  dies, X11 accepts stall at two (one boot at one), the
  pin never prints, the stopgap's restart budget is
  spent. Two healthy-arm boots (A#9, B#9) show no failure
  signature at all — logs end mid-wineboot, still
  crawling in registry work at the 360 s window —
  non-starts under any certification, and not a
  certification question. The question the FD-43 review
  left undecidable is answered: the baseline arm matches
  the merged arms. The FD-40 stamp is likewise irrelevant
  to the race (A ≈ B). What remains is what FD-43 named:
  the stock unix-side win32u.so's input-idle fuse and
  single-probe driver latch, whose patch has shipped in
  no binary. **The remedy is the unix-side win32u.so
  backport build that ships both halves.** FD-01 stays
  open until it lands and is re-measured live.

Scope qualification, carried from FD-43: this is the
pin-anchored harness shape with the stopgap's restart
budget exhausted (`bwretry=2`). It is not a claim that
the live site's demos never start — the full-Wine session
shape is a different shape, and real visitors get the
stopgap armed.

Arm C's median pin arrival was 128.5 s live-observed;
console delivery lags the guest under load, so read it as
an upper bound (calibration measured the same event at
34.6 s). The signature needs no classifier: the pin error
arrives, the desktop never does.

## Suite sanity on the arm-B dist

Via `WINE_DIST_DIR=/home/ubuntu/readygate-arms/B`:

- **boot32-vkboot: PASS** (first attempt) — see above.
- **celeste-boot: FAIL, then PASS on re-run (271 s).**
  The first attempt's artifacts show the game displayed
  and rendered — `GAME DISPLAYED` present, 5 X11
  connections, frame pixels 589,824 lit / 145,323 varied
  against thresholds of 10,000 / 1,000, no error pattern
  in the console — but the runner recorded a failure
  whose text was not captured. The captured re-run
  passes cleanly. Reported as observed; the first failure
  is not explained by the readiness predicate
  (celeste-boot does not consume it) and is consistent
  with a runner-level flake under the day's peak load.
- **boot64-smoke: FAIL, then PASS on re-run.** The first
  attempt's console ends mid-wineboot-registry-trace at
  47k lines with none of the asserted evidence present —
  the A#9/B#9 crawl family — and the test never consumes
  the readiness flag. The re-run, at load ~3, passes with
  the winstation evidence present. Reported as observed:
  a boot-completion failure under load, not a
  certification event.

## The ledger — what this voids and what stands

**VOID** (all resting on the old certification):

- FD-01's closure and its first-visit "after" measurement
  (6/6) in `docs/reports/first-visit-start-reliability-20261031.md`.
- The live "6/6" verifications of both deploys that
  carried the unsigned win32u.dll (`14a82a302` and the
  FD-15/FD-06 deploy `7e98ccced`).
- The readiness component of every merge/deploy gate
  since `14a82a302`, including the D3D12 oracles'
  readiness component (their guest readback verdicts are
  a separate claim — see FD-43).
- CL-06 (FD-01's closure entry) — already annotated VOID
  by the FD-40 merge composition.

**STANDS** (never rested on the certification):

- The pre-rebuild stock-DLL baselines: 0/4 fresh-profile,
  6/18 overall, local 6/6 — genuine handles
  (`0x10048`/`0x1004a`), genuine non-starts.
- FD-40's winstation-stream proof (wineserver request
  stream: `image_flags=0x44`, `create_winstation`
  reached, real window handles) and the `dbwin-echo`
  fixture.
- The FD-15 celeste-boot lineage (5 X11 connections,
  pixel-verified frame, full-Wine session shape).
- boot64-smoke's evidence-based assertions, then and now.
- FD-15's and FD-06's code-level proofs (unit/native
  tests, sabotage checks) — neither fix's correctness
  ever depended on a session-start verdict.

## Register actions taken

- FD-43: RESOLUTION appended; heading marked CLOSED
  2026-10-31 (CL-10) — closed by its own named conditions
  (hardened certification + four-arm measurement), not by
  a fix to the race.
- FD-47: new entry — the certification defect itself,
  fixed on this branch (CL-11). (Numbered FD-44 on this
  branch; renumbered FD-47 at integration — FD-44 on the
  integration branch is the FD-41/FD-42 lane's
  thread-lifetime entry.)
- FD-01: UPDATE appended — prerequisites met, true rates
  recorded, entry stays OPEN pending the unix-side fix.
- Section B row for `lane-readygate`; CL-10 and CL-11
  added to section C.

Nothing deployed. Nothing pushed. Master and the live
site untouched.
