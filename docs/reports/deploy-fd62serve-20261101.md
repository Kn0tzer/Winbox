# Deploy 2026-11-01 — FD-62 serving batch (texel-pointer atomics served, CL-25)

Deployed by the deploy lane. Live site serves git_sha
1d8213fb0 / wasm a2e1059dfc1a (verified over public
HTTPS from off the box: BUILD_INFO stamp matches the
shipped build, shell 200 on /winbox/ and
/winbox/runtime/index.html, range 206 on the wasm,
the support archive, wine64.zip and wine32.zip).
Rollback: kn0tzer/winbox-prev-20261101-1225 (the
103032872 build); the older rollback
(winbox-prev-20261101-0928, the 2c7290977 build) was
pruned per the newest-only rule. Never pushed;
master untouched; the personal game archive
untouched; the warmed demo archive is served, not
edited.

## What shipped

The composed batch at integ tip 1d8213fb0: FD-62
texel-pointer serving (fix 24becdb53, composed as
068ad3f67) — modules doing atomics through image
texel pointers in descriptor heaps are rewritten to
storage-buffer form in spirvfix, mirrored in the
bridge audit, and the page tier binds the resulting
buffers to the actual image data (previously such a
bind silently fell back to a dummy zero buffer; it
now binds the real bytes or drops loudly) — plus
the citation re-pins, the texel format-table
if-chains, the legacy corpus re-baseline, and the
register composition (CL-25 closes FD-62; FD-72 and
FD-73 filed; FD-55 annotated), with the CL-25
placement settlement 8803de521 and the merge record
1d8213fb0. No deploy composition commits were
needed: the only commit after the merge gate's
build (8803de521) is the merge record (documents),
and the rebuilt wasm below is byte-identical to
the gate's.

Restage: the merge lane had stripped the staged
runtime set, so it was restaged per the established
split before building: retained-archive sha256
verified against the manifest (9978166f...); four
members extracted from it and pin-verified per
file; wine64.zip (pin 53d77f33) and wine32.zip
(pin 7f2209cf) copied from the live site's exact
copies and pin-verified (FD-73 records why the
tar's inner wine zips are not used). bootstrap.py
--check reports "Runtime assets verified (13
files)".

## Gate on the shipped build (1d8213fb0)

- Build: tools/bw64-build.sh, literal "Browser
  export verified" + BUILD COMPLETE; exactly one
  support archive in the dist:
  support-c4dbe35986a5 (517,564,189 B), the
  deterministic FD-53 pack, byte-identical to the
  previous live build's.
- BUILD_INFO: git_sha
  1d8213fb011d3c2a0fe749f3ad34c412947003cd,
  wasm_sha256
  a2e1059dfc1a55fb405d998adcb35348c3d86ce5cf0cafd9
  fb9dc91889d6b91a — the merge gate's wasm,
  byte-identical (and therefore identical to the
  serving lane's own in-vivo build wasm).
  git_dirty_files 1 (the pre-existing untracked
  vkd3d probe DLL, as at the merge gate).
- check_runtime_completeness.py (dist present):
  0 failures, 1 skip (the standing prefix64
  built-artifact skip), with FD-53's
  member-uniqueness and Vulkan shim-pin checks
  active against the freshly built pack and
  passing.
- JS unit suite (BW_TEST_DIST = the shipped
  dist): 347 passed, 0 failed — identical to the
  merge gate.
- Staging diff vs the previous live tree: file
  sets identical (the support archive name is
  unchanged); vendor/ byte-identical; content
  differs in exactly these files:
  runtime/boxedwine64.wasm (the FD-62 serving
  bridge changes), spirvfix.mjs (the texel
  rewrite), vkwebgpu.mjs (the page-tier binding
  change), runtime/boxedwine64.js (the emscripten
  glue regenerated against the new wasm),
  runtime/BUILD_INFO.json, and app.mjs +
  runtime/index.html (release-id substitution
  only). dependencies.json, demos.mjs, wine64.zip
  and wine32.zip are byte-identical to the
  previous live build.

## First-visit starts — measured through the public route

Protocol identical to the FD-01/FD-02 and FD-50+/
FD-53+ deploy measurements: brand-new browser
profile per boot, the deskpin-anchored harness
session (session=1) at the public URL, the
launcher's restart budget pre-exhausted (bwretry=2)
so no start is a rescue, 900 s window, full console
captured and classified OFFLINE under the hardened
FD-47 rule (page's own bwRuntime.ready() AND a
plausible deskpin handle AND the explorer OPENED
spawn signal). Evidence:
test-results/deploy-fd62serve-20261101/
(live-driver.mjs preserved there).

Candidate — this build (1d8213fb0), after the
swap:

- x86 n=5: certified 5/5, handle 0x10052 on every
  boot, certified-ready 20.6–21.5 s (median
  21.5 s), zero restarts, zero false-ready, zero
  rescues. Box 1-minute load ranged 3.0–3.8
  across the arm.
- x64 n=3: certified 3/3, handle 0x10048 on every
  boot, certified-ready 80.6–92.7 s (median
  90.5 s), with the same zero counters. Box
  1-minute load climbed 3.8 -> 9.0 across this arm
  (the concurrently running park lane's Chromium
  diagnostic sequence was active); the medians are
  reported as measured, with no cause claimed for
  the difference from the previous deploy's
  61.1 s.
- Console census across all 8 boots (2,821 lines):
  24 explorer OPENED spawn signals, 0 BW64-FAIL
  lines, 0 memory faults, 0 explorer-failure
  signatures, 0 session restarts.

## Consumer check — Chromium through the public route

The standard demo path against the live site
(demo-matrix, DEMO=chromium, BW_TEST_BASE_URL =
https://kn0tzer.work.gd/winbox/, fresh profile).
One attempt; it completed. Evidence:
test-results/deploy-fd62serve-20261101/
chromium-consumer/ + chromium-consumer.log.

- Window mapped 45.1 s after the click; overlay
  cleared; frames presented: 8; the mapped sample
  is a real rendered window (99.2% non-dark
  pixels, 2,265 distinct colors — the same pixel
  signature as the previous deploys' successful
  runs); the run's input script (type + Enter)
  completed against the live session with zero
  page errors. The run's own summary stamps the
  build under test as git_sha 1d8213fb0 / wasm
  a2e1059dfc1a.
- The run waited on the probe-slot interlock
  behind the park lane's diagnostic sequence
  before browsing — the interlock working as
  designed.

## Bearing on the register

No closures ride this deploy: the batch's closure
(CL-25, FD-62) was written at composition on its
own proof. FD-55 stays open on its stated terms
(FD-70 + FD-72 and the downstream submit/swapchain
behavior); FD-70, FD-71, FD-72 and FD-73 stay open
as filed. The section 4.68 restart stopgap remains
in the shipped launcher untouched, by the same
separate decision as FD-01/FD-02.

## Incidents

- SSH connections from the lane's shell to the
  box dropped repeatedly, and two monitoring
  polls returned with their output lost. Every
  long run (build, both measurement arms, the
  consumer run) was launched detached (setsid) on
  the box and survived its channel; all results
  were read from the runs' own files on the box.
  No run was repeated or lost because of a drop.
- The first console census used a wrong path
  glob (one directory level too deep) and read
  all-zero counts over zero matched lines; the
  error was caught because the explorer OPENED
  count contradicted the driver's own per-boot
  certification (which requires that element),
  and the census was re-run over the correct
  paths (2,821 lines) with the results above.
  The wrong-glob numbers stand for nothing and
  are recorded here only for auditability.
- The measurement and consumer runs waited on
  the probe-slot interlock behind the concurrently
  running park lane's diagnostic sequences. The
  other lane's processes were observed read-only
  and never disturbed; this lane's runners were
  managed by exact process id only.
