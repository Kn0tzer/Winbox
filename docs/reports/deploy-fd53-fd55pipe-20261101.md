# Deploy 2026-11-01 — FD-53 + FD-55pipe batch (deterministic support pack, refusal logging, warm-packaging composition)

Deployed by the deploy lane. Live site serves git_sha
103032872 / wasm 36b184ee97e9 (verified over public
HTTPS from off the box: BUILD_INFO stamp matches the
shipped build, shell 200 on /winbox/ and
/winbox/runtime/index.html, range 206 on the wasm,
the support archive, wine64.zip and wine32.zip; the
previous support archive's name now returns the
gateway's 401 fall-through, i.e. it is no longer
served). Rollback: kn0tzer/winbox-prev-20261101-0928
(the 2c7290977 build); the older rollback
(winbox-prev-20261101-0752, the 02bffb14a build) was
pruned per the newest-only rule. Never pushed;
master untouched; the personal game archive
untouched; the warmed demo archive is served, not
edited.

## What shipped

The composed batch at integ tip 103032872: FD-53's
fix 1e228a7ed + closure/record 6f0034fdf
(fast-forwarded; CL-24 closes FD-53), the
pipeline-creation lane's six cherry-picks (incl.
refusal logging 72360dda5 in both bridges and the
FD-55/FD-62 register annotations with provisional
FD-70), the pre-warm lane's two commits picked
directly (8edea306b, 4932856cf: the capture/packaging
tooling, the warmed archive, the demos.mjs pointer),
the register settlement 6cb3dd4a0 (FD-70 confirmed;
the pre-warm speed entry renumbered FD-67 -> FD-71),
and the merge record 103032872. No deploy
composition commits were needed: the only commit
after the merge gate's build (6cb3dd4a0) is the
merge record (documents), and the rebuilt wasm
below is byte-identical to the gate's.

Restage: the merge lane had stripped the staged
runtime set, so it was restaged per the established
split before building: 4 members from the retained
archive /home/ubuntu/backups/
winbox-runtime-assets-20261026/
browser-runtime-20261026.tar (archive sha256
verified 9978166f... against the committed
manifest), wine64.zip (pin 53d77f33) and wine32.zip
(pin 7f2209cf) from the live site's exact copies,
every staged file's sha256 verified against
tools/browser/runtime-assets.json; bootstrap.py
--check reports "Runtime assets verified (13
files)".

## Gate on the shipped build (103032872)

- Build: tools/bw64-build.sh, literal "Browser
  export verified" + BUILD COMPLETE; support
  archive c4dbe35986a5 verified by the build,
  exactly one support archive in the dist.
- BUILD_INFO: git_sha
  1030328721f94aa97a89f5385e86796f4c34e49c,
  wasm_sha256
  36b184ee97e9d32d48c548a8f0ee43f860f3b8152ca1066a
  6ab0255f9459ae82 — the merge gate's wasm,
  byte-identical. git_dirty_files 1 (the
  pre-existing untracked vkd3d probe DLL, as at
  the merge gate).
- check_runtime_completeness.py (dist present):
  0 failures, 1 skip (the standing prefix64
  built-artifact skip), with FD-53's new
  member-uniqueness and Vulkan shim-pin checks
  active against the freshly built pack and
  passing.
- JS unit suite (BW_TEST_DIST = the shipped
  dist): 346 passed, 0 failed — identical to the
  merge gate.
- Staging diff vs the previous live tree: the
  file set differs ONLY by the support archive
  name (support-de592964f772... ->
  support-c4dbe35986a5...); vendor/
  byte-identical; content differs in exactly
  these files: runtime/boxedwine64.wasm (the
  refusal-logging change), runtime/boxedwine64.js
  (the emscripten glue regenerated against the
  new wasm; a single minified line),
  runtime/BUILD_INFO.json, runtime/
  dependencies.json (the new pack's name, byte
  count 517,564,189 and sha), demos.mjs (one
  line: the Godot demo's archive now names
  godot-squash-warmed.zip, 114,471,810 B),
  app.mjs and runtime/index.html (release-id
  substitution only). spirvfix.mjs, wine64.zip
  and wine32.zip are byte-identical to the
  previous live build. The shipped support
  archive is the deterministic FD-53 pack
  (c4dbe35986a5, 517,564,189 B) the merge gate
  reproduced byte-for-byte.

## First-visit starts — measured through the public route

Protocol identical to the FD-01/FD-02 and FD-50+
deploy measurements: brand-new browser profile
per boot, the deskpin-anchored harness session
(session=1) at the public URL, the launcher's
restart budget pre-exhausted (bwretry=2) so no
start is a rescue, 900 s window, full console
captured and classified OFFLINE under the
hardened FD-47 rule (page's own bwRuntime.ready()
AND a plausible deskpin handle AND the explorer
OPENED spawn signal). Evidence:
test-results/deploy-fd53-fd55pipe-20261101/
(live-driver.mjs preserved there).

Candidate — this build (103032872), after the
swap:

- x86 n=5: certified 5/5, handle 0x10052 on every
  boot, certified-ready 20.2–21.8 s (median
  20.6 s), zero restarts, zero false-ready, zero
  rescues. Box 1-minute load ranged 2.6–4.8
  across the arm.
- x64 n=3: certified 3/3, handle 0x10048 on every
  boot, certified-ready 61.0–62.0 s (median
  61.1 s), with the same zero counters, load
  1.5–6.0.
- Console census across all 8 boots: 0
  explorer-failure signatures, 0 BW64-FAIL
  lines, 0 memory faults, 0 session restarts;
  explorer OPENED spawn signals present (24
  lines).

## Consumer check — Chromium through the public route

The standard demo path against the live site
(demo-matrix, DEMO=chromium, BW_TEST_BASE_URL =
https://kn0tzer.work.gd/winbox/, fresh profile).
Two attempts; both are preserved in the evidence
(chromium-consumer/ + chromium-consumer-attempt1
.log for attempt 1, chromium-consumer2/ +
chromium-consumer.log for attempt 2).

- Attempt 1: NOT MAPPED (the 900 s mapped timeout
  expired). The session itself booted — explorer
  opened, five X11 connections accepted, the
  guest Chromium process started and fetched its
  files lazily, persistence synced — then the
  console stream went silent ~44 s into the
  run's own clock with the host browser idle,
  and nothing further was logged. No page
  errors, no HTTP errors, zero BW64-FAIL in the
  captured streams. The run had waited on the
  build interlock before browsing (the
  concurrently running FD-62 lane's build), and
  that lane's build cycle was active on the box
  during the attempt.
- Attempt 2 (fresh profile, launched after the
  concurrent build drained): window mapped
  33.5 s after the click; overlay cleared;
  frames presented: 5; the mapped sample is a
  real rendered window (99.2% non-dark pixels,
  2,265 distinct colors — the same pixel
  signature as the previous deploy's successful
  run); the run's input script (type + Enter)
  completed against the live session with zero
  page errors. The run's own summary stamps the
  build under test as git_sha 103032872 / wasm
  36b184ee97e9.

Attempt 1 is a single observed park of the shape
the register already tracks as the probabilistic
post-start park family; no causal attribution is
claimed for it. It does not block this deploy:
the harness-shape measurement above certified
8/8 on the same build in the same hour, and the
consumer path itself then completed cleanly on
attempt 2 with no change to anything.

## Bearing on the register

No closures ride this deploy: the batch's closure
(CL-24, FD-53) was written at composition on its
own proof. FD-55 stays open on its stated terms
(FD-62 + FD-70); FD-70 and FD-71 stay open as
filed. The section 4.68 restart stopgap remains
in the shipped launcher untouched, by the same
separate decision as FD-01/FD-02. The demos.mjs
change that shipped with this batch points the
Godot demo at the warmed archive: its cache phase
is now the seeded ~213 s shape, and its outcome
(zero frames) is unchanged until FD-62 serves.

## Incidents

- SSH connections from the lane's shell to the
  box dropped repeatedly (one at ~270 s). The
  x86 measurement driver, already running on the
  box, survived its connection's drop and
  completed; its parent shell did not survive to
  start the x64 arm, so the x64 arm was
  relaunched detached (setsid) and monitored by
  file reads. All later long runs (x64 arm, both
  consumer attempts) were launched detached the
  same way. No run was repeated or lost because
  of a drop; the x86 results were read from the
  driver's own results files.
- Consumer attempt 1 did not map (see above);
  its artifacts are preserved in full, and the
  successful attempt followed without any change
  to the build, the site, or the harness.
- The measurement and consumer runs waited on
  the probe-slot and build interlocks behind the
  concurrently running FD-62 serving lane's
  builds — the interlock working as designed.
  The other lane's processes were observed
  read-only and never disturbed; test runners of
  this lane were managed by exact process id
  only.
