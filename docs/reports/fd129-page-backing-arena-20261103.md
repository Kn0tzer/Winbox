# FD-129 lane record — the runningThread-slot writer: guest-page backing allocation aliasing live host-thread TLS

Branch: `lane-fd129` (base: `lane-fd128` tip `f21582c5e`). Identification
captures ran on branch `lane-fd129pre` (base `6ba55a560`, the pre-FD-128-fix
tree), because the corruption only manifests on unfixed builds.

## The defect, by substance

During a guest session's graphics work, emulator host threads found the
thread-local slot holding their current guest-thread identity
(`KThread::runningThread`) overwritten with small integers and Vulkan
struct-field-shaped values. The previous lane (FD-128) made the known
fatal consumers immune by validating the slot and falling back to a
per-thread registry, and filed the writer itself as unidentified.

The writer is the emulator's own guest-memory system. When the guest
maps fresh memory, `K64Page::commit()` allocates the page's 4 KB backing
buffer from the general heap. In the browser build, during the
allocation burst that accompanies the first heavy graphics work, those
heap allocations land inside the stack/TLS spans of other, still-live
host threads. The commit then zeroes its "new" buffer — zeroing the
victim's identity slot along with it — and from then on the guest's
ordinary stores to that page write guest data over the slot. The
victim's identity is destroyed until its interpreter loop happens to
re-publish it, which is why the corruption appeared transient, why it
tracked Vulkan-call frequency, and why the values seen in the slot were
guest data values rather than anything a host writer would compute.

The allocator-internal reason the browser runtime recycles live thread
spans into the heap could not be proven from inside the process, and
does not need to be: the latecomer allocation is always the emulator's
page buffer, the aliasing materializes at the emulator's own commit()
boundary, and it is stopped there.

## Identification evidence (pre-fix tree)

Diagnostic (temporary commits on `lane-fd129pre`, evidence in
`wt-fd129pre/test-results/fd129pre/`): a registry of every live thread's
identity slot, a 1 kHz monitor, hooks on every guest-write funnel
reporting when a write's host destination lands in a registered slot, a
page scan proving which guest page's backing buffer contains a slot,
and (round 2) a lock-free lifecycle ring logging page allocations,
decommits, registrations and heap growth in order.

Two fully attributed hits, one in each of two independent captures
(diag1, diag3), same signature:

- Guest process pid 10, thread tid 11 stores 8 bytes of
  0xffffffffffffffff (guest RIP 0x1cadcec60) to guest VA 0x82f3170/… —
  the guest page's backing buffer contains another live thread's slot,
  at buffer offset 0x170 — during vkAllocateDescriptorSets /
  vkUpdateDescriptorSets (diag1; repeated in diag3 at guest VA
  0x8437170).
- The same guest thread stores 4 bytes of 0x242424 to guest VA
  0x73b3d6530/… — again a page whose buffer contains a live slot at
  offset 0x170 — during vkGetBufferDeviceAddress (diag3), where the
  victim slot had held its genuine pointer quietly for 15.4 s after
  registering. The 0x242424 pairing matches the earlier lane's
  observation.

The lifecycle ring gives the causal order in both cases: slot registers
first; a burst of page commits marches across the heap at a uniform
chunk stride; one fresh page buffer lands exactly over the live slot
(buffer base = slot − 0x170 both times); the commit's zeroing produces
the observed genuine → 0 transition; guest stores then write the data
values the monitors had been seeing.

Ruled out along the way: any host-side writer through the Vulkan bridge
(bracket surveillance at bridge entry/exit across the whole burst:
clean, on both pre- and post-fix builds); the slot's one legitimate
writer (tripwire, per the earlier lane); dead-thread monitor artefacts
(reproduced as a class and excluded — the two attributed hits are on
slots proven live before and after).

## The fix

Commit `f94ceb4c5` ("page backing buffers come from a dedicated arena,
never the general heap"), `include/kmemory64.h` +
`source/kernel/kmemory64.cpp` +
`web/tests/suite/unit/memory64-backing-lifetime.test.mjs`:

- A dedicated backing arena: 16 MB chunks obtained once and never
  freed, sub-allocated in page-size slots behind a mutex-guarded free
  list. `commit()` takes arena slots; it falls back to the heap only
  when the arena is disabled (`BW64_PAGE_ARENA=0`) or cannot grow.
- Because an arena chunk is never free, no part of it can later be
  handed to a thread as stack/TLS while a guest page uses it, and no
  arena slot can be carved out of a live thread's span — the aliasing
  class is closed in both directions, not just for the observed victim.
- Provenance is tracked per buffer (`dataArena`; retired buffers carry
  their provenance with them), so every buffer returns to its own
  allocator when its page dies. Shared (borrowed) buffers are
  untouched, as is FD-128's slot validation — it remains, now as
  defence in depth.

The one test adaptation: `memory64-backing-lifetime` compiles a slice
of `kmemory64.h` standalone and asserts the retain/fallback semantics
under AddressSanitizer. The arena declarations were placed inside the
sliced header region, and the test stubs the arena allocator to its
heap fallback, so the slice keeps exercising the real retain/free code
with its original assertions.

## Proof

- Browser build of the fix: verified ("Browser export verified",
  dependency archive c4dbe35986a5; fixed wasm sha256
  4453757f0699b527).
- Native suites, run serially on the fixed tree: main 675/675 and
  x64 95/95, 0 failed — run twice (before and after the
  header-consistency amend), same result. (A 4-thread invocation of
  the main suite fails spuriously — cwd-sharing between parallel
  tests — and is not a product signal; the suites' serial form is
  `boxedwine 0 0 1`.)
- JS unit suite: 352 passed, 0 failed.
- The closing-terms capture: the same Godot session that corrupted
  slots pre-fix, run with the full slot-surveillance diagnostic
  applied on top of the fix (diagnostic committed temporarily and
  reverted after the run; branch tip is the fix commit):
  `test-results/fd129/proof1`. Banner 100.5 s → window mapped 152.6 s →
  guest alive and working at the 1,200 s deadline (elapsed 1,205.4 s).
  36 slots registered and watched; across the whole correlated burst
  (339 vkGetPhysicalDeviceFormatProperties2, 167
  vkAllocateMemory/vkCreateImage-class calls): CORRUPT 0, ZERO 0,
  STOMP 0, WATCH 0, BRACKET 0, SCAN 0. No registered slot ever held a
  non-KThread value.

## New defects filed by this lane

None. The anomalies met during the work were artefacts of the lane's
own diagnostics (a monitor polling freed threads' recycled TLS blocks)
and a pre-existing infrastructure pin rotation in a payload-preparation
script; neither is a product defect.
