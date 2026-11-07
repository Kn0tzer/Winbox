# D22 — Mindustry JVM ergonomics measurement (2026-11-07)

Branch: `lane-d22-mindustry`. Base: integration tip `334c70be4`
merged in as `4913603a9`; the measurement ran against that build.
This discharges the D22 item carried since 2026-11-06: the probe
was committed (`d86d229d7`) but never run, because the disk
emergency stopped the lane first.

## What was measured

Whether the emulator's advertised machine definition (4 GiB RAM,
2 vCPUs — the FD-03 floor-(c) raise) actually drives a real
program's memory behaviour. The test subject is the JVM bundled
in the Mindustry game archive (its `jre` subtree, extracted
byte-for-byte into the probe zip: 33,076,287 bytes, 97 members,
plus a `HeapProbe` class), launched inside the guest with the
same arguments `Mindustry.json` gives the game, in three runs:

- **flags** — `-XX:+PrintFlagsFinal -version`: the JVM's own
  ergonomics table.
- **settings** — `-XshowSettings:all -version`: the
  human-readable cross-check.
- **runtime** — `HeapProbe`: `Runtime.maxMemory()`,
  `Runtime.availableProcessors()`, and a live allocation of
  maxMemory/4 that must be held.

Probe: `web/tests/d22-jvm-ergonomics.mjs`. Evidence:
`test-results/d22-mindustry-20261106/` (`build.log`,
`probe-run.log`, `console-flags.log`, `console-settings.log`,
`console-runtime.log`, `summary.json`).

## Build identity

Fresh build of the merged tip: `build.log` carries the literal
line "Browser export verified". `dist/runtime/BUILD_INFO.json`
reads git_sha `4913603a9`, git_dirty_files 0, wasm sha256
`92b36180e9b3469b07014b91cf0054e4f50e1b119b2881e6ca1a85819d27bfdf`
— byte-identical to the live batch-10 build, as expected: this
branch changes no product code (a test script, this record, and
register text only).

## Results (read from the runs' own preserved logs)

| Run | Outcome | Time |
| --- | --- | --- |
| flags | both markers hit | 247 s |
| settings | deadline at 903 s — see adjudication | 903 s |
| runtime | both markers hit, `HEAPPROBE DONE` | 433 s |

The guest JVM is OpenJDK 25.0.1 (2025-10-21). Its ergonomics
follow the raised machine definition exactly, in all three
independent views:

- Flags table (`console-flags.log`): `MaxHeapSize = 1073741824`
  (the 25% default — `MaxRAMPercentage = 25` — of the 4 GiB the
  JVM sees), `InitialHeapSize = 67108864`,
  `CICompilerCount = 2`, `ParallelGCThreads = 2`,
  `ConcGCThreads = 1`, `UseG1GC = true`,
  `UseCompressedOops = true`.
- Settings print (`console-settings.log` line 5956):
  `Max. Heap Size (Estimated): 1.00G`.
- Java API (`console-runtime.log`): `maxMemory=1073741824`,
  `totalMemory=67108864`, `availableProcessors=2`; the live
  allocation held `heldBytes=268435456` and the heap grew to
  `totalMemoryAfter=757071872` with no error, `maxMemory`
  unchanged after.

On the pre-raise definition (1 GiB / 1 CPU) the same defaults
arithmetic gives this JVM a 256 MiB maximum heap and 1
processor. The game's heap ceiling is set by the machine
definition it can see, and it now sees the raised one. The
FD-03 entry carries a progress note with these numbers.

## Adjudication — the settings run's timeout is a probe defect

The settings run recorded markerHit=false / needHit=false at
its 900 s deadline, but its own preserved log shows the run
captured what it was sent for: the Locale, Property, Security
and VM settings sections all printed, and the heap line above
was in the stream at guest time 390.0 s (log line 5956). The
probe waited for two strings this JDK never produces in that
position — the literal `Maximum heap size`, and the version
line, which `-XshowSettings` prints only after every section;
the trailing sections (Operating System Metrics among them)
had still not streamed through the guest console when the
deadline expired. Filed as **FD-201** and fixed on this branch
by `13d57b2a2`: the matcher now keys on the VM settings
section header and the label the JDK actually prints, and both
corrected patterns match the preserved settings stream exactly
once each. The settings run was not re-executed: its evidence
was already complete in the preserved log.

## Run conditions (operational notes)

- The probe waited 14 min 56 s for its heavy probe slot
  (launched 14:51 UTC, slot acquired 15:06:27 UTC per the slot
  system's own info file, queue position 1/1 throughout): the
  batch-11 wave's build relay held the build-pending flag
  continuously, which blocks all probe starts. This is the
  starvation shape already on file with the probe-parallelism
  work; no new filing.
- Box free disk dipped to 5.5G during the same relay (sibling
  build outputs) and recovered as builds completed; this lane
  started no new disk-heavy step while it was under ~6G.
- Runs executed while sibling lanes' light probes were active;
  guest timings above are under that contention and are
  reported as measured, not as performance claims.

## Checks

No checking-queue submission was made for this branch: the
queue's shared-source race (FD-197) is open and a serialized
series of owed verdicts was in flight; a further concurrent
submission risks spoiling a sibling's run. The branch's delta
is a test script, this record, and register text; its checks
run with the batch-11 composition gate.

## Closeout

Commits on `lane-d22-mindustry`, in order: `d86d229d7` (probe,
from the stopped attempt), `4913603a9` (merge of integration
tip `334c70be4`), `13d57b2a2` (FD-201 matcher fix), `25115bb32`
(register: FD-201 + FD-03 note), and this record. Generated
state (dist, web/runtime, build tree) is stripped from the
working copy; the evidence directory is preserved.
