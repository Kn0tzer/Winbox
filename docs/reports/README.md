# Reports — the dated record

Everything here is a record of what was measured or decided on the
date in its name, against the build it names. Dates, hashes, passing
operations and failures describe the runtime each report measured, not
a fresh run of the current checkout. For what is true now, see
`docs/WINBOX-STATUS.md` and the registers in `docs/known-gaps/`;
`docs/INDEX.md` is the map of the whole documentation library.

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
  `docs/defect-register.md`; its open section is the queue and
  its header carries the maintenance rule.

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

## Complete listing

Every record in this directory, generated from the tree on 2026-11-05 and re-verified mechanically by the documentation checks described in `docs-library-20261105.md`. Curated highlights are in the sections above; this listing is the complete set.

- [baldi-a3-report.md](baldi-a3-report.md)
- [baldi-d3d11-a2-report.md](baldi-d3d11-a2-report.md)
- [baldi-d3d11-diagnosis.md](baldi-d3d11-diagnosis.md)
- [bridge-coverage/BUG-bss-host-write.md](bridge-coverage/BUG-bss-host-write.md)
- [bridge-coverage/BUG-vkCmdBlitImage.md](bridge-coverage/BUG-vkCmdBlitImage.md)
- [bridge-coverage/BUG-vkCmdCopyBuffer2.md](bridge-coverage/BUG-vkCmdCopyBuffer2.md)
- [bridge-coverage/BUG-vkCmdCopyImage.md](bridge-coverage/BUG-vkCmdCopyImage.md)
- [bridge-coverage/BUG-vkCmdCopyImageToBuffer2.md](bridge-coverage/BUG-vkCmdCopyImageToBuffer2.md)
- [bridge-coverage/BUG-vkCmdResolveImage.md](bridge-coverage/BUG-vkCmdResolveImage.md)
- [bridge-coverage/MATRIX.md](bridge-coverage/MATRIX.md)
- [bridge-coverage/SUMMARY.md](bridge-coverage/SUMMARY.md)
- [bridge-coverage/matrix-gl64.md](bridge-coverage/matrix-gl64.md)
- [bridge-coverage/matrix-vk32.md](bridge-coverage/matrix-vk32.md)
- [bridge-coverage/matrix-vk64.md](bridge-coverage/matrix-vk64.md)
- [broad-conformance-analysis-20261030.md](broad-conformance-analysis-20261030.md)
- [buildrel-fd31-32-33-20261105.md](buildrel-fd31-32-33-20261105.md)
- [chromium-20261016.md](chromium-20261016.md)
- [chromium-networking-20261017.md](chromium-networking-20261017.md)
- [cleanlog-ksubsys-20261030.md](cleanlog-ksubsys-20261030.md)
- [cleanmem-guest-memory-20261030.md](cleanmem-guest-memory-20261030.md)
- [cleanup-bridges-20261030.md](cleanup-bridges-20261030.md)
- [cleanup-cpu-core-20261030.md](cleanup-cpu-core-20261030.md)
- [cleanup-fault32-20261030.md](cleanup-fault32-20261030.md)
- [cleanup-fs-20261031.md](cleanup-fs-20261031.md)
- [cleanup-status-20261105.md](cleanup-status-20261105.md)
- [cleanwave-kernelgate-20261030.md](cleanwave-kernelgate-20261030.md)
- [cond-start-wait-time-audit-20261028.md](cond-start-wait-time-audit-20261028.md)
- [cpu-differential/REPORT.md](cpu-differential/REPORT.md)
- [d3d-conformance-MATRIX.md](d3d-conformance-MATRIX.md)
- [d3d-conformance-d3dconf2/MATRIX.md](d3d-conformance-d3dconf2/MATRIX.md)
- [d3d1011-draws-land-nothing-20261103.md](d3d1011-draws-land-nothing-20261103.md)
- [d3d12-campaign-state-20261026.md](d3d12-campaign-state-20261026.md)
- [d3d12-descriptor-addressing-research-20261027.md](d3d12-descriptor-addressing-research-20261027.md)
- [d3d9-complex/REPORT-draft.md](d3d9-complex/REPORT-draft.md)
- [d3d9-complex/REPORT.md](d3d9-complex/REPORT.md)
- [deploy-20261030-celeste.md](deploy-20261030-celeste.md)
- [deploy-20261030-deadstart.md](deploy-20261030-deadstart.md)
- [deploy-20261030.md](deploy-20261030.md)
- [deploy-20261031.md](deploy-20261031.md)
- [deploy-batch3-20261103.md](deploy-batch3-20261103.md)
- [deploy-batch4-20261103.md](deploy-batch4-20261103.md)
- [deploy-batch5-20261103.md](deploy-batch5-20261103.md)
- [deploy-batch6-20261104.md](deploy-batch6-20261104.md)
- [deploy-batch7-20261105.md](deploy-batch7-20261105.md)
- [deploy-fd02-fd38-20261101.md](deploy-fd02-fd38-20261101.md)
- [deploy-fd12-20261101.md](deploy-fd12-20261101.md)
- [deploy-fd15-fd06-20261031.md](deploy-fd15-fd06-20261031.md)
- [deploy-fd50-fd55-fd62-20261101.md](deploy-fd50-fd55-fd62-20261101.md)
- [deploy-fd53-fd55pipe-20261101.md](deploy-fd53-fd55pipe-20261101.md)
- [deploy-fd54-63-70-7980-20261102.md](deploy-fd54-63-70-7980-20261102.md)
- [deploy-fd58-60-20261101.md](deploy-fd58-60-20261101.md)
- [deploy-fd62serve-20261101.md](deploy-fd62serve-20261101.md)
- [deploy-fd66-65-85-fd77surface-20261102.md](deploy-fd66-65-85-fd77surface-20261102.md)
- [deploy-fd70-20261101.md](deploy-fd70-20261101.md)
- [deploy-fd72-20261101.md](deploy-fd72-20261101.md)
- [deploy-fd77-20261101.md](deploy-fd77-20261101.md)
- [deploy-fd77b-fd81-20261101.md](deploy-fd77b-fd81-20261101.md)
- [deploy-finalbatch-20261031.md](deploy-finalbatch-20261031.md)
- [dirty-tree-inventory-20261008/report.md](dirty-tree-inventory-20261008/report.md)
- [docs-cleanup-20261030.md](docs-cleanup-20261030.md)
- [docs-library-20261105.md](docs-library-20261105.md)
- [fd01-unix-half-fix-20261031.md](fd01-unix-half-fix-20261031.md)
- [fd02-win32u32-fix-20261031.md](fd02-win32u32-fix-20261031.md)
- [fd05-fd-table-exhaustion-20261102.md](fd05-fd-table-exhaustion-20261102.md)
- [fd06-portio-20261031.md](fd06-portio-20261031.md)
- [fd101-sendmsg-efault-20261102.md](fd101-sendmsg-efault-20261102.md)
- [fd102-fd104-message-faults-20261102.md](fd102-fd104-message-faults-20261102.md)
- [fd103-wedge-incidence-20261102.md](fd103-wedge-incidence-20261102.md)
- [fd105-fd108-message-faults-20261102.md](fd105-fd108-message-faults-20261102.md)
- [fd109-fd112-message-delivery-20261102.md](fd109-fd112-message-delivery-20261102.md)
- [fd113-fd115-socket-followups-20261103.md](fd113-fd115-socket-followups-20261103.md)
- [fd116-fd117-zerowrite-handoff-20261103.md](fd116-fd117-zerowrite-handoff-20261103.md)
- [fd118-fd119-seqpacket-peerclose-20261103.md](fd118-fd119-seqpacket-peerclose-20261103.md)
- [fd12-park-lane-20261101.md](fd12-park-lane-20261101.md)
- [fd120-fd123-socket-state-answers-20261103.md](fd120-fd123-socket-state-answers-20261103.md)
- [fd124-peerclosed-stream-sendmsg-20261103.md](fd124-peerclosed-stream-sendmsg-20261103.md)
- [fd125-fd130-test-discharge-20261103.md](fd125-fd130-test-discharge-20261103.md)
- [fd125-settlement-20261103.md](fd125-settlement-20261103.md)
- [fd126-nullpath-efault-20261103.md](fd126-nullpath-efault-20261103.md)
- [fd127-godot-device-setup-park-20261103.md](fd127-godot-device-setup-park-20261103.md)
- [fd127-ksocket-owner-fix-20261103.md](fd127-ksocket-owner-fix-20261103.md)
- [fd128-current-thread-identity-20261103.md](fd128-current-thread-identity-20261103.md)
- [fd129-page-backing-arena-20261103.md](fd129-page-backing-arena-20261103.md)
- [fd131-empty-path-20261103.md](fd131-empty-path-20261103.md)
- [fd132-null-path-efault-20261103.md](fd132-null-path-efault-20261103.md)
- [fd133-135-xattr-statfs-statx-20261103.md](fd133-135-xattr-statfs-statx-20261103.md)
- [fd136-137-memfd-null-name-20261103.md](fd136-137-memfd-null-name-20261103.md)
- [fd136-138-utimensat-null-matrix-20261103.md](fd136-138-utimensat-null-matrix-20261103.md)
- [fd139-140-152-enotdir-utimensat-flags-fstatfs-20261104.md](fd139-140-152-enotdir-utimensat-flags-fstatfs-20261104.md)
- [fd148-fd149-d3d9-20261104.md](fd148-fd149-d3d9-20261104.md)
- [fd15-honest-gl-answers-20261031.md](fd15-honest-gl-answers-20261031.md)
- [fd151-node-selftest-target-20261103.md](fd151-node-selftest-target-20261103.md)
- [fd153-drop-log-noop-shapes-20261104.md](fd153-drop-log-noop-shapes-20261104.md)
- [fd154-adjudication-20261104.md](fd154-adjudication-20261104.md)
- [fd155-capturespec32-silent-header-20261104.md](fd155-capturespec32-silent-header-20261104.md)
- [fd156-mem64-selftest-target-20261104.md](fd156-mem64-selftest-target-20261104.md)
- [fd157-161-path-verdicts-20261104.md](fd157-161-path-verdicts-20261104.md)
- [fd16-texel-heap-arrays-20261031.md](fd16-texel-heap-arrays-20261031.md)
- [fd162-164-xattr-namespaces-dotdot-20261105.md](fd162-164-xattr-namespaces-dotdot-20261105.md)
- [fd23-residue-20261102.md](fd23-residue-20261102.md)
- [fd23-syscall-census-20261031.md](fd23-syscall-census-20261031.md)
- [fd38-enablement-park-20261031.md](fd38-enablement-park-20261031.md)
- [fd38-inotify-semantics-20261031.md](fd38-inotify-semantics-20261031.md)
- [fd39-texparam-store-20261031.md](fd39-texparam-store-20261031.md)
- [fd40-boot-smoke-characterization-20261031.md](fd40-boot-smoke-characterization-20261031.md)
- [fd40-fix-20261031.md](fd40-fix-20261031.md)
- [fd41-fd42-suite-containment-20261031.md](fd41-fd42-suite-containment-20261031.md)
- [fd44-thread-lifetime-20261031.md](fd44-thread-lifetime-20261031.md)
- [fd48-restart-certification-20261031.md](fd48-restart-certification-20261031.md)
- [fd49-storage-image-formats-20261031.md](fd49-storage-image-formats-20261031.md)
- [fd50-native-harness-completion-20261101.md](fd50-native-harness-completion-20261101.md)
- [fd53-support-archive-libvulkan-20261101.md](fd53-support-archive-libvulkan-20261101.md)
- [fd54-dlltrace-spawn-signal-20261101.md](fd54-dlltrace-spawn-signal-20261101.md)
- [fd55-godot-present-path-20261101.md](fd55-godot-present-path-20261101.md)
- [fd55-pipeline-creation-20261101.md](fd55-pipeline-creation-20261101.md)
- [fd55-prewarm-packaging-20261101.md](fd55-prewarm-packaging-20261101.md)
- [fd58-fd60-kernel-semantics-20261101.md](fd58-fd60-kernel-semantics-20261101.md)
- [fd62-texel-pointer-atomics-20261101.md](fd62-texel-pointer-atomics-20261101.md)
- [fd62-texel-serving-20261101.md](fd62-texel-serving-20261101.md)
- [fd63-metrics-emission-20261101.md](fd63-metrics-emission-20261101.md)
- [fd64-legacy-descset-20261102.md](fd64-legacy-descset-20261102.md)
- [fd65-gecko-fonts-20261102.md](fd65-gecko-fonts-20261102.md)
- [fd66-readback-disposal-20261101.md](fd66-readback-disposal-20261101.md)
- [fd67-guard-attribution-20261101.md](fd67-guard-attribution-20261101.md)
- [fd67-guard-correction-20261101.md](fd67-guard-correction-20261101.md)
- [fd70-intoffset-20261101.md](fd70-intoffset-20261101.md)
- [fd70-second-wave-20261101.md](fd70-second-wave-20261101.md)
- [fd72-helper-invocation-boundary-20261101.md](fd72-helper-invocation-boundary-20261101.md)
- [fd72b-helper-invocation-recheck-20261102.md](fd72b-helper-invocation-recheck-20261102.md)
- [fd73-runtime-assets-archive-20261101.md](fd73-runtime-assets-archive-20261101.md)
- [fd77-frozen-constellation-20261101.md](fd77-frozen-constellation-20261101.md)
- [fd77-msync-abba-20261101.md](fd77-msync-abba-20261101.md)
- [fd77-stall-surfacing-20261101.md](fd77-stall-surfacing-20261101.md)
- [fd78-literal-positions-20261102.md](fd78-literal-positions-20261102.md)
- [fd79-fd80-kernel-semantics-20261101.md](fd79-fd80-kernel-semantics-20261101.md)
- [fd81-temp-link-pattern-20261101.md](fd81-temp-link-pattern-20261101.md)
- [fd82-materialized-mmap-close-deferral-20261102.md](fd82-materialized-mmap-close-deferral-20261102.md)
- [fd83-fd84-read-family-20261102.md](fd83-fd84-read-family-20261102.md)
- [fd85-audio-leg-20261102.md](fd85-audio-leg-20261102.md)
- [fd85b-audio-bisection-20261102.md](fd85b-audio-bisection-20261102.md)
- [fd85pay-audio-conviction-20261103.md](fd85pay-audio-conviction-20261103.md)
- [fd86-arial-prefix64-20261103.md](fd86-arial-prefix64-20261103.md)
- [fd87-godot-sibling-classes-20261102.md](fd87-godot-sibling-classes-20261102.md)
- [fd87-x87-stale-cache-20261103.md](fd87-x87-stale-cache-20261103.md)
- [fd88-binding-array-blocker-20261102.md](fd88-binding-array-blocker-20261102.md)
- [fd90-oss-position-20261102.md](fd90-oss-position-20261102.md)
- [fd92-failed-mmap64-teardown-20261102.md](fd92-failed-mmap64-teardown-20261102.md)
- [fd92-fd93-readv-write-family-20261102.md](fd92-fd93-readv-write-family-20261102.md)
- [fd92-spirvfix-splice-ordering-20261102.md](fd92-spirvfix-splice-ordering-20261102.md)
- [fd94-fd96-pwritev-writev-20261102.md](fd94-fd96-pwritev-writev-20261102.md)
- [fd97-single-call-write-efault-20261102.md](fd97-single-call-write-efault-20261102.md)
- [fd98-preadv32-20261102.md](fd98-preadv32-20261102.md)
- [fd99-fd100-preadv2-family-order-20261102.md](fd99-fd100-preadv2-family-order-20261102.md)
- [first-visit-start-reliability-20261031.md](first-visit-start-reliability-20261031.md)
- [frame-floor-investigation-20261027.md](frame-floor-investigation-20261027.md)
- [frametime-profile-pong2/METHOD.md](frametime-profile-pong2/METHOD.md)
- [frametime-profile-pong2/RANKED.md](frametime-profile-pong2/RANKED.md)
- [game-support-20261019.md](game-support-20261019.md)
- [gangbeasts-sonnet-20261022.md](gangbeasts-sonnet-20261022.md)
- [gl-webgpu-20261020.md](gl-webgpu-20261020.md)
- [gl-webgpu-api-list.md](gl-webgpu-api-list.md)
- [glprobe-c11-report.md](glprobe-c11-report.md)
- [guest-hangs-luna-20261022.md](guest-hangs-luna-20261022.md)
- [guest-hangs-sol-20261021.md](guest-hangs-sol-20261021.md)
- [launcher-redesign-20261017.md](launcher-redesign-20261017.md)
- [loader-gaps-20261030.md](loader-gaps-20261030.md)
- [merge-batch3-20261031.md](merge-batch3-20261031.md)
- [merge-batch3-20261102.md](merge-batch3-20261102.md)
- [merge-batch4-20261103.md](merge-batch4-20261103.md)
- [merge-batch5-20261103.md](merge-batch5-20261103.md)
- [merge-batch6-20261104.md](merge-batch6-20261104.md)
- [merge-batch7-20261104.md](merge-batch7-20261104.md)
- [merge-fd02-fd38-20261031.md](merge-fd02-fd38-20261031.md)
- [merge-fd12-20261101.md](merge-fd12-20261101.md)
- [merge-fd15-fd06-20261031.md](merge-fd15-fd06-20261031.md)
- [merge-fd40-20261031.md](merge-fd40-20261031.md)
- [merge-fd41-fd42-20261031.md](merge-fd41-fd42-20261031.md)
- [merge-fd50-fd55-fd62-20261101.md](merge-fd50-fd55-fd62-20261101.md)
- [merge-fd53-fd55pipe-20261101.md](merge-fd53-fd55pipe-20261101.md)
- [merge-fd54-63-70-7980-20261102.md](merge-fd54-63-70-7980-20261102.md)
- [merge-fd58-60-20261101.md](merge-fd58-60-20261101.md)
- [merge-fd62serve-20261101.md](merge-fd62serve-20261101.md)
- [merge-fd66-65-85-fd77surface-20261102.md](merge-fd66-65-85-fd77surface-20261102.md)
- [merge-fd67-20261101.md](merge-fd67-20261101.md)
- [merge-fd70-20261101.md](merge-fd70-20261101.md)
- [merge-fd72-20261101.md](merge-fd72-20261101.md)
- [merge-fd73-20261101.md](merge-fd73-20261101.md)
- [merge-fd77-20261101.md](merge-fd77-20261101.md)
- [merge-fd77b-20261101.md](merge-fd77b-20261101.md)
- [merge-fd81-20261101.md](merge-fd81-20261101.md)
- [merge-finalbatch-20261031.md](merge-finalbatch-20261031.md)
- [merge-wave-20261031.md](merge-wave-20261031.md)
- [mindustry-jvm-stall-20261030.md](mindustry-jvm-stall-20261030.md)
- [mindustry-luna-20261023.md](mindustry-luna-20261023.md)
- [player-features-20261017.md](player-features-20261017.md)
- [portal-boot-deepseek-20261023.md](portal-boot-deepseek-20261023.md)
- [portal-deepseek-20261022.md](portal-deepseek-20261022.md)
- [portal-perf-profile-baseline-20261026.md](portal-perf-profile-baseline-20261026.md)
- [q3-webgpu-luna-20261022.md](q3-webgpu-luna-20261022.md)
- [q3-webgpu-sol-20261021.md](q3-webgpu-sol-20261021.md)
- [readiness-hardening-20261031.md](readiness-hardening-20261031.md)
- [runtime-regressions-20261013.md](runtime-regressions-20261013.md)
- [shader-cache-luna-20261022.md](shader-cache-luna-20261022.md)
- [shared-runtime-20261017.md](shared-runtime-20261017.md)
- [sol-a-journal.md](sol-a-journal.md)
- [sol-b-journal.md](sol-b-journal.md)
- [status-snapshots-20261030-20261105.md](status-snapshots-20261030-20261105.md)
- [syscall64-jvm-wine-audit-20261023.md](syscall64-jvm-wine-audit-20261023.md)
- [sysroot32-regeneration-20261028.md](sysroot32-regeneration-20261028.md)
- [tooling-honesty-20261030.md](tooling-honesty-20261030.md)
- [vk32-feature-level-root-cause-20261026.md](vk32-feature-level-root-cause-20261026.md)
- [vk64-drop-if-macro-audit-20261025.md](vk64-drop-if-macro-audit-20261025.md)
- [winbox-finish-20261020.md](winbox-finish-20261020.md)
- [winbox-frame-metrics-20261018.md](winbox-frame-metrics-20261018.md)
- [winbox-jit-20261018.md](winbox-jit-20261018.md)
- [winbox-jit-handoff-20261018.md](winbox-jit-handoff-20261018.md)
- [winbox-jit-ui-20261019.md](winbox-jit-ui-20261019.md)
- [winbox-jit64-metrics.md](winbox-jit64-metrics.md)
- [winbox-mem-20261020.md](winbox-mem-20261020.md)
- [winbox-metrics-contract-20261018.md](winbox-metrics-contract-20261018.md)
- [winbox-performance-20261018.json](winbox-performance-20261018.json)
- [winbox-performance-20261018.md](winbox-performance-20261018.md)
- [winbox-profile-20261020.md](winbox-profile-20261020.md)
- [winbox-recover-20261021.md](winbox-recover-20261021.md)
- [wine-boot-cost-20261023.md](wine-boot-cost-20261023.md)
- [wine32-base-forensics-20261028.md](wine32-base-forensics-20261028.md)
- [wine32-final/report.md](wine32-final/report.md)
