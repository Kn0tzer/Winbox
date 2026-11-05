# FD-155 — the 32-bit specialization capture discarded refused guest input silently

Lane `lane-fd155`, based on the integration tip `3ea2fd97d`
(batch-6 deploy record). Date: 2026-11-04.

## The defect, by substance

The Vulkan bridge captures a guest's VkSpecializationInfo per
pipeline stage so the page tier can apply the guest's shader-constant
values instead of the SPIR-V defaults. The capture has two entry
points in `source/vulkan/vk64bridge.cpp`: `captureStageSpec`
(64-bit guests) and `captureStageSpec32` (32-bit guests).

The bridge's rule is that refused guest work is reported, never
dropped quietly: every refusal goes through VK64_DROP_IF, which logs
a DROPPED line naming the failed condition and increments the
counter behind `vk64BridgeDropCount()`. The 64-bit capture honours
it — its header reads the struct through a drop guard, separates
the empty shape (a legal no-op: an empty VkSpecializationInfo is
the per-stage default DXVK attaches) from the refusals, and drops
loudly on an over-cap mapEntryCount and on claimed entries whose
map-entry array or data is absent.

The 32-bit capture's header was a single lumped plain return:

    if (!readStruct32(va, si) || !si.mapEntryCount || si.mapEntryCount > 16 ||
        !si.pMapEntries || !si.pData || !si.dataSize || si.dataSize > 256) return;

Four unlike shapes shared that one silent exit: the empty no-op
(correctly silent) and three genuine refusals — an unreadable
VkSpecializationInfo, a mapEntryCount over the capture cap of 16,
and claimed entries whose map-entry array or data is absent (null
array, null data, zero data size, or a data size over 256) — all
discarded with no log line and no counter movement. The 32-bit
function's own per-entry guards already reported; only its header
was mute. It was the one capture path that could refuse guest work
silently — the FD-153 survey's opposite-sign find, filed as FD-155.

The served behaviour was always correct (nothing is captured for a
refused spec; the pipeline keeps the shader's defaults), so the
harm is the silence itself: a guest whose specialization data was
refused on the 32-bit path left no trace in the bridge's honesty
signal — the failure mode the no-silent-drops rule exists to
prevent.

## The fix

`captureStageSpec32`'s header is restructured to mirror
`captureStageSpec` (fix `5bd07e141`):

- A null spec pointer returns first, silently — the legal no-spec
  shape (the 64-bit twin's `if (!specVa) return;`; on the old 32-bit
  code this shape was silent only by accident, via the struct
  reader's null check).
- A struct that cannot be read drops loudly:
  `VK64_DROP_IF(return, !readStruct32(va, si))`.
- mapEntryCount 0 returns silently — the legal no-op, per FD-153.
- An over-cap count drops loudly:
  `VK64_DROP_IF(return, si.mapEntryCount > 16)`.
- Claimed entries with an absent array or data drop loudly:
  `VK64_DROP_IF(return, !si.pMapEntries || !si.pData ||
  !si.dataSize || si.dataSize > 256)` — the same condition the
  64-bit header guards.

Nothing else changes: the per-entry guards, the capture cap (16),
the data cap (256) and the captured values are untouched, and the
served shapes behave exactly as before. The VK64_DROP_IF census
grows 255 -> 258 (the three new return-action sites); the pin in
`web/tests/suite/unit/vk-drop-macro-guard.test.mjs` is updated to
match ('return' 28 -> 31; the flow-control site map is unchanged).
A mirror check found no second capture of this kind: the only
other VkSpecializationInfo handling in the tree is the native host
marshalling path (`vk_host_marshal.cpp`), which forwards the data
to a real driver and discards nothing.

## Proof

New native FD-155 section in `tools/vkd3d/tests/vk64_native_test.cpp`
(tests `64dbe21e5`, two PASS lines):

- RED on the unmodified bridge (`test-results/fd155/red-native.log`):
  the section's served control passes (a valid entry captured
  verbatim: constantID 7, size 4, value 0x0D15EA5E, drop count
  unmoved), then the run aborts at the over-cap pin (line 2459,
  `vk64BridgeDropCount() == d0 + 1`) — on the base the over-cap spec
  is discarded with the counter unmoved.
- GREEN on the fix (`test-results/fd155/green-native.log`):
  run-native 69 PASS (67 standing + 2 new), 0 failures. Through
  vkCreateComputePipelines, the over-cap count (17), claimed
  entries with absent data, an absent entry array, and a zero data
  size each drop exactly +1 with nothing captured, and the valid
  entry is captured verbatim with no drop. Direct pins on the
  capture entry keep the null spec pointer silent and make an
  unreadable struct (no guest memory bound, so the struct read
  fails) drop +1.

Suites: native suites serial — main 682 OK lines, 0 FAILED (286 s;
`test-results/fd155/native-main.log`) and x64 108 OK lines, 0 FAILED (28 s;
`test-results/fd155/native-x64.log`) — both at the
standing baselines. JS unit 353 passed / 0 failed and legacy 115
passed / 0 failed (`test-results/fd155/js-suites.log`); the unit
total runs against the updated census pin, and the silent-failure
scanner measures vk64bridge.cpp at its standing allowance
(emptyCatch 0, unloggedDefault 10, unloggedErrno 0), unmoved.
Browser build 'Browser export verified' (`test-results/fd155/build.log`:
bootstrap restored 13 assets from the retained
`runtime-assets-20261104` archive only, `--check` exit 0 before the
build at 0 failures / 5 standing skips; the build's own bootstrap
verified the 13 assets in place and its completeness runs read
0 failures / 1 standing skip; BUILD_INFO git_sha `0dc8a0c7f`,
dirty 0, wasm sha256
`e0b6f72157a9edd85a10e0dc1d1a3935a68e53285f66982a8190ed2a3dec0eb2`,
5,684,554 B; staged support archive `c4dbe35986a5` as pinned).
Citation audit: 352 rows, 0 drift — the header rewrite (+11 lines)
shifted two cited bridge lines, re-pinned in `0dc8a0c7f`: reg:65's
vk32 logAskedFor call 12136 -> 12147 (exactly the +11 shift), and
reg:403's 32-bit vkCmdCopyImageToBuffer case label 13869 -> 13876,
the construct's true line (it sat 4 lines stale, inside the audit's
tolerance, at the base; the shift pushed it out).

No new defects filed.

Evidence: `test-results/fd155/` in the lane's worktree
(red-native.log, green-native.log, js-suites.log, build.log,
native-main.log, native-x64.log and the native build logs).
