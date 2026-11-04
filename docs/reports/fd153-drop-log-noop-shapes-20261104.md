# FD-153 — the Vulkan bridge's drop log reported legal no-op shapes as dropped guest commands

Lane `lane-fd153`, based on the integration tip `7a2308cd6`
(batch-5 deploy record). Date: 2026-11-04.

## The defect, by substance

The Vulkan bridge (`source/vulkan/vk64bridge.cpp`) reports work it
refuses through one channel: the `VK64_DROP_IF` macro logs a DROPPED
line naming the failed condition and increments the counter behind
`vk64BridgeDropCount()`. Those lines are the bridge's honesty signal —
the FD-147 lane's diagnosis was slowed because consoles full of them
read as render-pass and pipeline loss while the operative signal was a
single unrelated warning. A survey of all 257 guarded sites (the census
pinned in `web/tests/suite/unit/vk-drop-macro-guard.test.mjs`) found
five places where the guarded condition fires on a shape that is legal
and loses nothing — a helper invoked with a legal null/empty input, or
bookkeeping that is not a guest command at all:

1. **`vkCmdBeginRendering`'s attachment helper (both ABIs).** The
   helper runs unconditionally for the depth and stencil slots; its
   guard included `!address`, so a colour-only pass (the filed
   measurement: colors=1, pDepth=0x0, pStencil=0x0) logged two drops
   for a pass that loses nothing. A null attachment pointer is the
   API's "no such attachment" shape.
2. **`captureStageSpec` (64-bit).** One guard lumped the empty case
   with the over-cap case: `!si.mapEntryCount || si.mapEntryCount > 16`
   log-dropped every stage whose VkSpecializationInfo is empty
   (mapEntryCount 0, null pointers — DXVK's per-stage default). There
   is nothing to capture; the pipeline is created correctly with the
   shader's defaults.
3. **`resolveFrame` (submit serializer, both submit paths).** A frame
   whose descriptor set 0 was never bound hit `VK64_DROP_IF(return,
   !s0)`: the lookup was skipped for an unbound slot and the null
   result logged as a drop. The outputs are pre-zeroed, which is the
   correct resolution for that frame; nothing is lost.
4. **The 32-bit `vkCmdCopyBufferToImage(2)` / `vkCmdCopyImageToBuffer(2)`
   guards.** `count > 1024 || !regions` log-dropped a zero-region copy
   with a null region array — an empty copy that records nothing and
   loses nothing. The 64-bit twins already tolerate exactly this shape
   (they drop only on the over-cap count; reading a zero-length array
   is a no-op).
5. **The VKPROF top-3 trap scans (both present handlers).**
   `VK64_DROP_IF(continue, w <= 0)` ran inside `if (vkProfEnabled())`
   bookkeeping that ranks trap times for a profile dump. Skipping an
   idle trap slot drops no guest command, but with profiling enabled
   each skip printed a DROPPED line and inflated the drop counter.

Everything else in the census was audited and classified genuine — it
stays loud and unchanged: every object-lookup guard (missing command
buffers, pools, views, memory, pipelines), the over-cap count guards
(the `VK64_MAX_ATTACH` clause inside the reworded attachment guard
included), range/offset/extent guards, `readStruct` failures on
structures the API requires, required-pointer guards, the per-entry
specialization guards for malformed entries, the readback guards, and
`captureStageSpec32`'s per-entry guards. Two borderline sites were
examined and deliberately left loud, with the reasoning recorded in
the survey notes below: the recorded-extent readback guard (a zero
extent there is invalid *recorded* state, not a live no-op call) and
the `vkCmdExecuteCommands` null-array guard (already unreachable for
count 0, which returns earlier).

## The fix — commit `a8720731e` (tests `f31f44d15`)

- Attachment helpers (64- and 32-bit): `if (!address) return;` ahead of
  the guard; the guard keeps the over-cap and unreadable-struct clauses.
- `captureStageSpec`: after the struct read, `if (!si.mapEntryCount)
  return;`; the over-cap clause keeps its own loud site, and the
  claimed-entries guard (absent entry array / data / size) is unchanged.
- `resolveFrame`: `if (!st->sets[0]) return;` ahead of the lookup; a
  recorded set id that no longer resolves still drops loudly.
- 32-bit copy guards: the condition is now `count > 1024 ||
  (!regions && count)` at both sites.
- VKPROF scans: the macro call is replaced by a plain `if (w <= 0)
  continue;` — the only census change: **VK64_DROP_IF sites 257 → 255,
  `continue` sites 63 → 61**, pinned in
  `vk-drop-macro-guard.test.mjs`. All other rewordings keep their site
  and action, so the rest of the census is untouched.

## Proof

New native section in `tools/vkd3d/tests/vk64_native_test.cpp`
(FD-153, five PASS lines), RED on the unmodified bridge (aborts at the
colour-only drop assertion, `test-results/fd153/red-native.log`), GREEN
on the fixed bridge (`test-results/fd153/green-native.log`):

- Colour-only 64-bit BeginRendering: drop count unchanged, framebuffer
  holds exactly the colour attachment; colour+depth served with no
  drop; a depth attachment whose view does not resolve still drops
  (+1) and is not attached.
- Specialization capture: empty info → pipeline created, drop count
  unchanged, nothing captured; one real entry → captured verbatim
  (constantID, size and value bytes), no drop; mapEntryCount 17 →
  loud (+1), nothing captured; entries claimed with null data → loud
  (+1).
- `resolveFrame`: set 0 never bound → outputs zero, no drop; stale
  recorded set id → loud (+1); a bound set resolves its binding with
  no drop.
- 32-bit: colour-only BeginRendering drops nothing (framebuffer holds
  the colour attachment); compute specialization — the empty shape
  stays silent and uncaptured, a real entry is captured;
  zero-region CopyBufferToImage2KHR / CopyImageToBuffer2KHR record no
  commands and drop nothing, while count 1 with a null array stays
  loud (+1) at both.

Suites: run-native 67 PASS (62 standing + 5 new), 0 failures.
Native suites serial: main 679/679 and x64 105/105, 0 FAILED — both
exactly the standing baselines. Browser build 'Browser export
verified' (wasm `7a0f9ba4`, 5,682,086 B; BUILD_INFO git_sha
`a8720731e`, its one dirty file being this lane's citation re-pin;
staged archive `c4dbe35986a5` verified against its pin). JS unit
352/0 and legacy 115/0, both at baseline — the unit total runs against
the updated census pin, and the silent-failure scanner's
vk64bridge.cpp allowance is unmoved by the new early returns.
Bootstrap `--check` after the build: 0 failures. Citation audit: 352
rows, 0 drift — the bridge edits shifted 46 cited lines in
`docs/known-gaps/vulkan.md`; 29 were re-pinned by the audit tool's
`--fix` and 17 by offset-preserving delta mapping, each verified
against the construct it cites. The first build attempt failed at
staging on a missing gstreamer payload in the fresh worktree (a setup
input, copied from the preserved donor tree; the WASM had compiled
and its shim libraries verified) — the re-run is the verified build
above. Evidence: `test-results/fd153/` in the lane's worktree.

## Filed from this lane

- **FD-155** — the survey's one opposite-sign find: `captureStageSpec32`'s
  header guard is a plain silent `if (...) return;` — on the 32-bit
  path an unreadable specialization struct, an over-cap entry count,
  or claimed entries with absent arrays are all discarded with no log
  line and no counter movement, against the bridge's no-silent-drops
  rule; the 64-bit twin logs all three. Not fixed here: this lane's
  scope is false positives, and conflating the two directions in one
  change would blur both proofs. The native tests pin only the 32-bit
  path's correct empty/valid behaviour, never its silence.

## Survey notes (borderline calls)

- `execCopyQueryResults`/readback guards keyed on recorded state
  (e.g. a recorded image-to-buffer extent of zero): the recorded
  command itself was malformed when written, so the loud line reports
  a real loss of guest work, not a live no-op call. Left loud.
- `vkCmdExecuteCommands`' null command-buffer array guard sits behind
  an `if (!count) return 0;`, so the legal empty shape never reaches
  it. Left as is.
- `vkCmdResetQueryPool`'s `!count` clause cannot fire for count 0 from
  its guarded loop form. Left as is.
