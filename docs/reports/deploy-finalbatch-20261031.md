# Deploy 2026-10-31 (3) — final batch: FD-16 + FD-44 + FD-48 + FD-01 remedy + FD-49

Deployed by the deploy lane. Live site serves git_sha fe4cfc265 /
wasm d0aff354 (verified over public HTTPS from off the box:
BUILD_INFO stamp matches the shipped build, shell 200 on /winbox/
and /winbox/runtime/index.html, range 206 on the wasm, the
support archive, and wine64.zip). Rollback:
kn0tzer/winbox-prev-20261031-2024 (the 7e98ccced build); the
older rollback was pruned per the newest-only rule. Never
pushed; master untouched; the personal game archive untouched.

## What shipped

The final-batch merge (integ tip e9e67be23: FD-16 c90cae341,
FD-44 eca093d60, FD-48 ab1333602, FD-01 remedy ca4166040,
FD-49 c34488cf7) plus two deploy composition commits:
1c0a1b534 (wine64.zip container re-pin for this restage) and
fe4cfc265 (runtime-artifact-manifest regeneration for that
pin). The shipped wasm d0aff354 is byte-identical to the
merge gate's wasm. The one git-dirty file at build time was a
pre-existing untracked probe DLL left by another lane.

Restage: the integ tree's staged runtime set had been stripped
after the merge gate. Five of the six zips were restored from
canonical copies after byte-verification against the pins in
tools/browser/runtime-assets.json (all five matched exactly).
wine64.zip was restaged from the then-live base (78dde7c2)
with the merged tree's tools/rootfs64/stage-wine8-backports.sh;
member audit against the live base: 1,257 members, name set
identical, exactly two content differences — win32u.dll
a8ca5fbb -> 49b87438 (the FD-40 stamp) and win32u.so
5dac5821 -> bffd9036 (FD-01's patched unix half), both
byte-identical to the committed backports. New pin 53d77f33;
it differs from the merge lane's 7d9e89f9 in zip metadata only
(the staging script is not metadata-deterministic; contents
are the audited ones). bootstrap --check: "Runtime assets
verified (13 files)".

## Gate on the shipped build (fe4cfc265)

- First build attempt failed its in-build completeness step
  on exactly one line — runtime-artifact-manifest.md still
  carried the merge pin. Regenerated with
  gen_runtime_manifest.py (single-line diff, committed as
  fe4cfc265) and rebuilt clean: literal "Browser export
  verified"; dependency archive de592964f772; exactly one
  support archive in the dist.
- check_runtime_completeness.py (dist present): 0 failures,
  1 skip (the standing prefix64 built-artifact skip).
- JS unit suite (BW_TEST_DIST = the shipped dist): 344
  passed, 0 failed, RC=0 — identical to the merge gate.
- Staging diff vs the previous live tree: file sets
  identical (nothing in the old site missing from the new
  one, nothing extra), vendor/ byte-identical, exactly 12
  files differ: app.mjs, gl-webgpu.mjs, spirvfix.mjs,
  vkwebgpu.mjs, wgsl-utils.mjs (the batch's page tier),
  runtime/boxedwine64.wasm and runtime/boxedwine64.js
  (FD-44/FD-48 emulator), runtime/wine64-launcher.js (the
  hardened FD-47 observer), runtime/wine64.zip (FD-40 +
  FD-01), runtime/BUILD_INFO.json, runtime/index.html
  (version substitution), runtime/wine32-font-provenance.json
  (provenance regeneration).

## First-visit starts — measured through the public route

Protocol (identical for baseline and candidate): brand-new
browser profile per boot, the deskpin-anchored harness session
(session=1) at the public URL, the launcher's restart budget
pre-exhausted (bwretry=2) so no start is a rescue, 900 s
window, full console captured and classified OFFLINE under
the hardened FD-47 rule (page's own bwRuntime.ready() AND a
plausible deskpin handle AND the explorer OPENED spawn
signal). Evidence: test-results/deploy-finalbatch-20261031/
(live-driver.mjs preserved there). Scope note: this is the
harness session shape through the public route; the previous
build's full-Wine demo shape was separately known-good
(Celeste pixel-verified on that build family).

Baseline — the previous live build (7e98ccced), measured
before the swap, x64 n=6: the page's own (pre-hardening) rule
reported ready 6/6; hardened certification 0/6; false-ready
6/6 — every boot's deskpin value was the NTSTATUS 0xC000000D,
explorer never opened, ready stamps 68–115 s, zero restarts,
zero memory faults. In this shape the old build's sessions
never produced a desktop at all (the FD-43 anatomy), and its
launcher called all six started.

Candidate — this build (fe4cfc265), after the swap:

- x64 n=6: certified 6/6, handle 0x10048 on every boot,
  certified-ready 82–135 s (median 102.9 s), zero restarts,
  zero explorer-failure signatures, zero memory faults, zero
  BW64-FAIL readiness rejections. Box 1-minute load ranged
  3.8–14.2 across the run (other lanes' probes and builds
  interleaved through the slot interlock).
- x86 n=3: certified 3/3, handle 0x10052 on every boot,
  certified-ready ~31–37 s (median 31.2 s), zero restarts.
  This is the first live proof that a 32-bit session can be
  certified at all (FD-48). It runs on the stock Wine-10
  payload — FD-02's payload fix ships in the next batch, and
  FD-02's own live re-measurement remains its closing term.

## Bearing on FD-01

FD-01's remaining terms were deploy and live re-measurement.
Both are now discharged for the 64-bit harness shape through
the public route: 0/6 certified (6/6 false-ready) before,
6/6 certified after, same protocol, same window. The closure
decision itself is the coordinator's. The §4.68 restart
stopgap remains in the shipped launcher; nothing about this
measurement retires it.

## Incidents

- The first baseline attempt was lost to a probe-slot
  acquire timeout (other lanes' builds held the interlock's
  build-pending flag; the driver's 600 s acquire expired and
  its throw escaped, killing the run before any boot —
  crashed log preserved as live-baseline-x64.crashed.log).
  The driver was hardened (per-boot catch, 2400 s acquire,
  observation budget starts at page goto) and the baseline
  re-run clean. No other lane's process or build was
  disturbed at any point.
