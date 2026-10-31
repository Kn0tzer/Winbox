# Deploy 2026-10-30 (third) — dead-start root-cause fix (guest write protection)

Deployed by the deadstart-deploy lane. Live site serves git_sha 692875bd8 /
wasm b1f0c1da (verified over public HTTPS from the box and off-box:
BUILD_INFO stamp, shell 200, range 206 on the wasm and the support
archive). Rollback: kn0tzer/winbox-prev-20261030-2202 (the 5741034f3
build); the older rollback was pruned per the newest-only rule. Never
pushed.

## What merged
- lane-deadstart-fix 692875bd8 (single commit, fast-forwarded onto the
  integ tip 8ebb588c5): k64CheckGuestAccess denies a write to a mapped
  page only when the guest itself imposed the page's current
  permissions via mprotect (new K64_PAGE_ENFORCED bit); protections a
  page is born with at mmap stay advisory for writes, and the
  emulator's own ELF loader opts out for its PT_GNU_RELRO pass. This
  undoes the dead-start regression convicted to a835b2a65 ("enforce x64
  guest page permissions"), whose check faulted loader/section-image
  writes that Wine startup legitimately makes — 3 such writes, all in
  early startup, per the fix lane's fault census of a parking boot —
  while keeping a835b2a65's mprotect-imposed fault behavior (and its
  self-test / K-M3 pins) intact. Full-diff accounting: exactly the
  lane's 3 files (include/kmemory64.h, source/kernel/kmemory64.cpp,
  source/emulation/cpu/cpu64RunElf.cpp), +24/-4, nothing else.

## Gate on the shipped build (692875bd8)
- bw64-build.sh: literal "Browser export verified" (dependency archive
  3f1268a546e0 verified). The gate dist's wasm (b1f0c1da) is
  byte-identical to the fix lane's own proof build.
- check_runtime_completeness.py: 0 failures (1 standing environmental
  skip). bootstrap.py --check: 13 files verified.
- JS unit suite: 333 passed / 0 failed.
- Emulator native suite: 648 tests / 0 FAILED, exit 0 (canonical
  single-threaded invocation `boxedwine 0 0 1`, 569 s).
- Guest probes on the gate dist (system-placement variants, the form
  the previous deploys gated on): tri12 RESULT 0, passed; tri12-32
  RESULT 0, passed. The staged variant also produced tri12 RESULT 0
  with a clean guest exit; its stricter pixel-capture flag read false
  with zero frames captured — the same signature several historical
  gate runs shipped with; noted, not hidden.
- Pristine --export-only re-export was the shipped artifact: prefix64
  back to the canonical 152 members (gate probe executables staged for
  the probes, then removed by the re-export), exactly one support
  archive, dist wine64.zip byte-identical to the canonical copy.

## Deploy mechanics
Staged dist -> kn0tzer/winbox-next; full-tree diff vs live showed only
the intended changes (runtime/boxedwine64.wasm, runtime/BUILD_INFO.json,
and the release-id stamps in app.mjs and runtime/index.html); everything
else identical. Rename-swap; live verified as above.

## Session starts after the deploy — measured, with a residue
Measured through the LIVE website with the launcher's restart budget
pre-exhausted (bwretry=2), so the §4.68 self-healing restart could not
fire: a start counted only if the session readied on its own first
attempt (evidence: test-results/deadstart-deploy-20261030/live-boots*):
- Run 1 (box load 7.7-13.9): 2/6 started (ready 93 s, 148 s).
- Run 2 (load 4.5-15.5): 1/6 started (105 s).
- Run 3 (load 3.7-11.5): 3/6 started (83 s, 106 s, 134 s).
- Control on the same shipped dist over the local page path
  (web/tests/boot-rate.mjs, same bwretry=2, loads 2.5-7.3): 6/6
  started, 0 rescued, 0 race-failed, median ready 84 s — reproducing
  the fix lane's proof.
- Fault census across all 18 live boots: ZERO guest memory-fault /
  SEGV lines in any console, started or failed. The convicted
  regression's fingerprint is gone from the live site.
- Every failed live boot shows the older §4.27 shape instead: exactly
  2 of the 5 X11 handshakes healthy boots make, the explorer-failure
  line, no faults. A fresh-browser-profile control boot at low load
  (2.8) failed the same way, so the residue is not the measurement
  profile and not purely load either: it is a probabilistic stall in
  the desktop-init parade that load makes more likely. The §4.68
  restart workaround remains live for real users and covers part of
  it; with the workaround disabled, first-attempt reliability through
  the public path tonight was 6/18. Stated plainly: the regression is
  fixed and shipped; the pre-existing start race is not yet extinct
  and is the named follow-up.
