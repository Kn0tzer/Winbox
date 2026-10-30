# Mindustry (bundled-JVM) stall — diagnosis state, 2026-10-30 (lane-mindustry)

## Task
Mindustry on the live site: the bundled Java runtime starts, prints one
warning, and goes silent forever. Find and fix the cause in the shared
machinery. This lane was stopped at a natural point by a coordination
call (a full-codebase cleanup wave is being staged); no fix landed.
Everything below is measured on the current shipped code.

## What was run
- Worktree /home/ubuntu/wt-mindustry, branch lane-mindustry, base integ
  tip 8ebb588c5. Baseline build via tools/bw64-build.sh printed the
  trustworthy "Browser export verified"; dist stamp 8ebb588c5, wasm
  e297364a — byte-identical to the live build (5741034f3).
- One diagnostic run through the real site path (locally served shell,
  game archive fetched from the live catalog, card click):
  DEMO=mindustry node web/tests/demo-matrix.mjs with BW_THREAD_DUMP=1
  (45 s interval), BW_TEST_URL_PARAMS=guestdiag=45 (in-page thread dumps
  + guest RIP sampler + memstats), BW_CAPTURE_PLAYER_LOG=1,
  MAPPED_TIMEOUT_S=900.
- Evidence: test-results/mindustry-lane-20261030/base1/ (summary.json,
  log.txt, log-tail.txt committed; the full 1.7 MB console capture with
  all 38 thread dumps and 19 profile intervals is console.txt in the
  same directory on disk).
- Sweep reference: integ test-results/demo-sweep-20261030/mindustry/.

## Proven
1. Reproduction on the current build: stage "not-mapped", 0 frames in
   900 s. This run had heavier box contention than the sweep; the
   sweep run (same shipped build) reached an SDL window and a first
   present at ~394 s but never a steady frame. Both runs: the JVM
   starts and no game ever appears.
2. The "one warning" is OpenJDK: "Attempt to get process group
   affinity failed: Call not implemented". It also appears in the
   sweep run that DID map a window — treat as a red herring unless
   proven otherwise.
3. Where the time goes (guest RIP sampler, 19 intervals of 45 s):
   - Boot phase: wineserver takes up to ~94% of all guest execution
     samples in the first interval — the same wineserver-burn
     signature the dead-start investigation measured. This phase is
     shared with the session-start problem.
   - From ~360 s to the end of the run (~900 s): the app process
     dominates, and roughly two-thirds to three-quarters of ALL guest
     execution is inside jre/bin/server/jvm.dll, sustained. Last
     interval (~860 s): jvm.dll 30033 samples, zip.dll 1071 — the
     game jar was still being read near the end of the run.
4. Thread state during the JVM phase (thread dumps at 498.9 s and
   858.9 s, pid 10, 13 threads): the main thread is RUNNING with a
   stack entirely inside jvm.dll; ~9 helper threads are parked in
   ordinary futex waits; 4 threads are parked reading Wine sockets;
   wineserver late-phase syscalls are a regular
   gettimeofday/clock_gettime/epoll_wait poll (~48/s each), not a
   spin.

## Ruled out (for the JVM phase)
- A lost-wakeup deadlock among the guest threads: the working
  threads are running and the parked ones sit in textbook idle
  waits while execution (per the sampler) never stops.
- Wineserver request churn as the JVM-phase bottleneck: wineserver
  goes near-idle once the JVM phase begins.
- The loader/import class fixed earlier (CopyFile2,
  GetAnycastIpAddressTable): the JVM loads and runs; this is not a
  load failure.

## Not proven / open
- Loop vs crawl: whether the jvm.dll execution is a loop (same code
  repeating — an emulator-contract bug) or very slow linear startup
  (class loading + JIT of a large game on a slowed emulator).
  zip.dll still active at ~860 s hints at continued class loading,
  but 8+ minutes of JVM execution with no window is broken either
  way; the distinction decides the fix and is the first thing to
  settle on resume.
- Whether the early wineserver burn materially delays the JVM phase.
- No code was changed by this lane; no fix is claimed.

## Resume steps (in order)
1. Bisect the JVM workload with tiny jars through the same site
   path (the bundled javaw with -version, then a hello-world that
   opens a window): separates JVM-start cost from game-start cost.
2. RIP-level sampling inside jvm.dll across intervals (are the hot
   offsets stable?) plus stack walks of ALL app-process threads in
   several dumps: stable hot offsets = loop; moving offsets = crawl.
3. If loop: symbolicate the hot jvm.dll offsets (release build;
   offsets plus the JDK version in the jre release file narrow it)
   and check known emulator-sensitive spins (safepoint polling,
   clock sources, perf-counter shared memory).
4. If crawl: the fix belongs to the shared execution-rate problem
   (dead-start lane ground), not to Java-specific work.

## Housekeeping
- Suites not run: this lane changed no code, so there was nothing
  to gate. Live site untouched; nothing pushed; nothing deployed.
- Generated build state stripped from wt-mindustry after the run
  (dist, project/linux + emscripten Build dirs, web/runtime copies,
  copied dxvk/vkd3d/rootfs payload dirs, node_modules symlink).
