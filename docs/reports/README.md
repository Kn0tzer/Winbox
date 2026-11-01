# Reports — the dated record

Everything here is a record of what was measured or decided on the
date in its name, against the build it names. Dates, hashes, passing
operations and failures describe the runtime each report measured, not
a fresh run of the current checkout. For what is true now, see
`docs/WINBOX-STATUS.md` and the registers in `docs/known-gaps/`.

## Campaign and deploy records

- [Phase-2 finish](winbox-finish-20261020.md): the merged Phase-2 tree
  and its verification.
- [DirectX 12 campaign](d3d12-campaign-state-20261026.md): the running
  record of the D3D12 campaign; shipped 2026-10-29.
- Deploy records: [2026-10-30](deploy-20261030.md),
  [Celeste redeploy](deploy-20261030-celeste.md),
  [dead-start fix deploy](deploy-20261030-deadstart.md) — what shipped,
  the gate results on the shipped build, and the live-site
  measurements taken afterwards.
- [Broad conformance analysis](broad-conformance-analysis-20261030.md):
  what blocks whole categories of programs, and the census tools under
  `tools/conformance/`.
- [Loader gaps](loader-gaps-20261030.md): a class of missing Windows
  entry points found by census and fixed as a class.

## Cleanup campaign and merge wave (2026-10-31)

- [Merge wave record](merge-wave-20261031.md): all sixteen cleanup /
  demo-class branches composed into the integration branch — every
  composition decision, the packaging re-pin, and the gate results on
  the merged tree.
- The pattern standard: `docs/failure-logging-and-recovery.md`
  (pathfinder lane). Lane reports: kernel subsystems
  ([cleanlog-ksubsys](cleanlog-ksubsys-20261030.md)), kernel gate
  census ([cleanwave-kernelgate](cleanwave-kernelgate-20261030.md)),
  32-bit fault logging ([cleanup-fault32](cleanup-fault32-20261030.md)),
  guest memory ([cleanmem](cleanmem-guest-memory-20261030.md)),
  filesystem ([cleanup-fs](cleanup-fs-20261031.md)), CPU core
  ([cleanup-cpu-core](cleanup-cpu-core-20261030.md)), graphics bridges
  ([cleanup-bridges](cleanup-bridges-20261030.md)), tooling honesty
  ([tooling-honesty](tooling-honesty-20261030.md)), documentation
  ([docs-cleanup](docs-cleanup-20261030.md)).
- [First-visit start reliability](first-visit-start-reliability-20261031.md):
  the win32u impatience pair, its backport fix, and the before/after
  start measurements. Merged; deploys with the next release.
- Demo-class records: Al-Khaser INT 2D
  (`test-results/alkhaser-int2d/REPORT.md`), first-frame D3D12 walls
  (lane report in `wt-firstframe/test-results/firstframe-20261030/REPORT.md`;
  outcome summarized in the merge wave record),
  [Mindustry JVM stall](mindustry-jvm-stall-20261030.md) (record-only).
- Independent frontier review of the composition:
  `test-results/frontier-review-20261031/REVIEW.md`.
- Open defects found by all of the above live in one register:
  `docs/defect-register.md` (FD-01…FD-38); its header carries the
  maintenance rule.

## Preserved investigation reports

The 2026-10-13 maintenance preserved a batch of reports from
`test-results/`; the collection has grown since. Their relative
subdirectories and findings are unchanged.

- [D3D conformance matrix](d3d-conformance-MATRIX.md): recorded D3D9–12
  API/pixel buckets, consumed by the static suite D3D guard.
- [Bridge coverage](bridge-coverage/README.md): Vulkan/OpenGL matrices
  and bug notes.
- [CPU differential report](cpu-differential/REPORT.md): instruction
  forms, oracle comparisons and timing buckets.
- [Timing method](frametime-profile-pong2/METHOD.md) and
  [ranked measurements](frametime-profile-pong2/RANKED.md).
- [Wine32 report](wine32-final/report.md), standalone Baldi/GL reports,
  and the preserved D3D9 and maintenance inventories.

Raw logs, screenshots and captured chunks referenced by the oldest
reports were archived under
`/home/ubuntu/backups/boxedwine64-maintenance-20261013/`; that archive
no longer exists on this machine (checked 2026-10-30), so those
reports stand on their own text. Fresh probes write ignored
`test-results/`; summaries worth keeping are promoted here with their
date and build provenance, and probe evidence is sometimes committed
under `test-results/` directly (for example
`test-results/site-chromium-20261030/`). Regression source recovered
from probe output lives in `tools/regression/` and `testdata/`.
