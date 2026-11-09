# FD-206 — ten Vulkan handlers answer -3 to the coverage harness

## The defect as filed

The 2026-11-07 vkcov coverage runs (two independent end-to-end runs
with identical return codes) recorded VK_ERROR_INITIALIZATION_FAILED
(-3) for ten implemented Vulkan handlers: vkSetEvent, vkResetEvent,
vkCmdSetEvent, vkCmdResetEvent, vkGetEventStatus (the whole event
family), vkGetQueryPoolResults, vkFreeDescriptorSets,
vkGetDeviceImageMemoryRequirements, vkGetPhysicalDeviceProperties2KHR
and vkGetPhysicalDeviceImageFormatProperties2KHR. The filing stated
the harness's arguments were "valid-ish by design (created objects,
a begun command buffer, established state)" and asked the fixing
lane to triage, per name, handler defect vs harness artifact.

## Triage: all ten are the harness's own artifact

Each of the ten names has exactly one call site in
`tools/vkcov/vkcov.c`, and it is not a real call. All ten sit in
`t_stubs()`, the "stub-only exports" section, whose probes invoke
the function pointer through a cast to a no-argument function type:

    ((void*(*)())pvkSetEvent)()

That probe shape is sound only for an entry point whose thunk
answers without touching its arguments — the by-design loud stubs
that return VK_ERROR_FEATURE_NOT_PRESENT (-8). These ten are not
stubs. They are implemented handlers (added for DXVK's D3D9 query
path and the 1.3/KHR getter surface) that validate their arguments
first: an event, query-pool or descriptor-pool handle must resolve
in the bridge's object table, an output pointer must be non-null, a
create-info pointer must be readable. A no-argument call delivers
whatever the argument registers happen to hold at that point in the
run — leftovers of the harness's own reporting calls — so every one
of the ten stopped at its argument validation and returned -3. The
event family's uniform -3, which the filing read as a functional
failure on its face, is the same shape five times: all five handlers
validate the event handle the same way, and none of them was ever
handed the event the setup chain created.

Per name, the -3 was the correct answer to the call the harness
actually made:

- vkSetEvent / vkResetEvent / vkGetEventStatus — the event handle
  delivered was not a bridge object; the handlers answer -3 for an
  unresolvable event (source/vulkan/vk64bridge.cpp, the
  VK64_fn_vkSetEvent family of cases).
- vkCmdSetEvent / vkCmdResetEvent — same event-handle validation.
- vkGetQueryPoolResults — the pool handle delivered was not a
  bridge object; the handler answers -3 before any result logic.
- vkFreeDescriptorSets — the pool handle delivered was not a bridge
  object; -3.
- vkGetDeviceImageMemoryRequirements — the info/output pointers
  delivered were null or carried no readable create-info pointer;
  -3 at the handler's pointer gates.
- vkGetPhysicalDeviceProperties2KHR — reaches the core
  vkGetPhysicalDeviceProperties2 handler (the guest shim maps the
  KHR name to the core trap id, as in real Vulkan); the physical
  device handle delivered was not a bridge object; -3.
- vkGetPhysicalDeviceImageFormatProperties2KHR — likewise mapped
  to the core handler; its output pointer was null as delivered;
  -3 at that gate.

No bridge code is defective for these names, and no bridge code is
changed by this lane. The defect was in the coverage harness: ten
implemented handlers were filed in the section reserved for
argument-free stub probes, so the coverage matrix recorded answers
to calls that were never made.

## The fix (harness only)

`tools/vkcov/vkcov.c` (and the rebuilt `tools/vkcov/vkcov.exe`,
mingw, byte size 359,527, sha256
abdd88e42e515e8d2829c72eb7a7c385163a1de0236af75066b2695fc90dab13):

- The ten no-argument rows are removed from `t_stubs()`.
- A new section, `t_fd206_calls()`, runs after `t_stubs()` and
  before teardown and calls all ten with the setup chain's real
  objects: the created event (host set/reset with a status read
  after each, then the command forms inside a begun command
  buffer), the created query pool (a never-executed query, then
  query 0 recorded, submitted and read back), a descriptor set
  allocated from a local pool created with the free-sets flag and
  then freed, a device-image memory-requirements query describing
  a 64x64 R8G8B8A8_UNORM image, and the two KHR getters with the
  same arguments as their core twins.
- Four more rows of the same no-argument class are given real
  calls in the same section: vkCreateQueryPool and the void KHR
  aliases vkGetPhysicalDeviceFeatures2KHR,
  vkGetPhysicalDeviceMemoryProperties2KHR and
  vkGetPhysicalDeviceQueueFamilyProperties2KHR. They are not part
  of FD-206's ten (vkCreateQueryPool is already called with real
  arguments by the setup chain), but this lane's first fixed run
  showed them flipping from 0 to -3 purely because recompiling the
  harness changed the leftover register contents the no-argument
  probes deliver — the affected rows are whichever implemented
  handlers the garbage happens to fail. With real calls, no
  implemented handler's matrix row depends on register garbage.

## Proof

Coverage run on the fixed harness against this tree's browser
build (build script, "Browser export verified"; BUILD_INFO
git_sha 5aa04b1b8 — the integration tip this lane branched from —
wasm sha256 62f6123e…, byte-identical to the shipped batch-13
wasm, as no product code changes): probe
`web/tests/vkcov-probe.mjs`, TAG=fd206-fix3, preserved at
`test-results/bridge-coverage-fd206-fix3/` (console.log,
vkcov-results.json, summary.json, matrix-vk64.md). Verdict:
vkcov RESULT 0, 309 handlers tested, no page errors. The ten
names, from the run's own results file:

- vkSetEvent 0,0 — VK_SUCCESS
- vkResetEvent 0,0 — VK_SUCCESS
- vkCmdSetEvent 0,0 — void, recorded into a begun buffer
- vkCmdResetEvent 0,0 — void, recorded into a begun buffer
- vkGetEventStatus 3,3 — VK_EVENT_SET, the spec answer for an
  event just set; the same console records 4,4 (VK_EVENT_RESET)
  for the read after the reset, so both states are proven
- vkGetQueryPoolResults 0,0 — VK_SUCCESS for the executed query,
  with the read-back value 1 (the bridge's occlusion answer, one
  sample passed); the same console records 1,1 (VK_NOT_READY) for
  a never-executed query without the wait flag — the spec answer
  for that call
- vkFreeDescriptorSets 0,0 — VK_SUCCESS
- vkGetDeviceImageMemoryRequirements 0,0 — void; the console's
  note records the written size, 16384 bytes, exactly 64x64x4 for
  the described image
- vkGetPhysicalDeviceProperties2KHR 0,0 — void, properties filled
- vkGetPhysicalDeviceImageFormatProperties2KHR 0,0 — VK_SUCCESS

Run-wide: no row in the 309 answers -3 any more. Against the
original 2026-11-07 results file, exactly 17 rows changed return
codes: the ten above; the vkCmdBlitImage trio (-2 crashed to 0 —
FD-204's fix, shipped in batch 12, after the original run); and
the four same-class rows named above (0 to -3 on garbage in the
intermediate run, 0 on real calls in the final run). Buckets move
221 working / 3 slow-or-flaky / 85 broken-or-stub to 233 / 3 / 73.
The analyzer counts vkGetEventStatus's 3 as "broken"; that is its
known mis-bucket of non-error VkResult answers, the same class as
the three standing ones (vkGetBufferDeviceAddress's address value
and the two VK_TRUE presentation answers, all still present). The
remaining broken-or-stub rows are the 62 by-design -8 stubs and
the 7 harness-skipped crash rows, unchanged.

Two attempts precede the proof run and are preserved:
`test-results/bridge-coverage-fd206-fix/` — the session's worker
died with a wasm "memory access out of bounds" trap during the
initial boot, before the harness was staged (the rare start-fault
shape; the identical build's next attempt booted and ran clean);
`test-results/bridge-coverage-fd206-fix2/` — a complete pass with
the ten names already correct, kept because it is the run that
exposed the four same-class rows flipping on recompiled garbage.

## Scope notes

- The 32-bit bridge dispatch carries mirror handlers for all ten
  names (the KHR pairs share the core cases there, as the 64-bit
  shim mapping does here); vkcov is a 64-bit guest, so the proof
  covers the 64-bit path, which is the path FD-206's evidence
  measured.
- The no-argument probe section remains in the harness for the
  genuine stub exports, whose -8 answers do not depend on
  arguments. Any future implemented handler must be given a real
  call, not a stub-section row; the new section's header comment
  in vkcov.c says so.
