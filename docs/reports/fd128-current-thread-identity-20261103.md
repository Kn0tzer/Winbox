# FD-128 — Godot worker unaligned-access trap after the window maps: the current-thread identity is destroyed under the worker

Lane: `lane-fd128` (worktree `wt-fd128`), based on `lane-fd127fix` tip
`6ba55a560`. Fix commit: `849a55412`. Date: 2026-11-03.
Evidence: `test-results/fd128/` (captures `diag-prof`, `diag-diag1` …
`diag-diag6`, verification `diag-fix1`; build and suite logs alongside).

## The filed shape

On the FD-127-fixed build, the Godot capture proceeds through device
setup (first `vkCreateShaderModule` → 32 pipeline creations → 6 queue
submits → first window mapped) and, within ~2 s of the window mapping,
a WASM worker dies: `Uncaught RuntimeError: operation does not support
unaligned accesses`. Guest thread dumps stop; no present is ever
observed. Reproduced in the filer's diag5 (window 226.0 s, trap
227.4 s) and diag6 (window 299.3 s, trap 301.5 s).

## Identification

This lane's driver (`web/tests/fd128-godot.mjs`, committed with the
fix) is the filer's capture with one addition: it captures CDP
`Runtime.exceptionThrown` events from every target, so a worker trap
arrives with its WebAssembly stack. The identification capture ran a
profiling build (`BW64_PROFILE=1`, function names retained) of the
unmodified branch tip.

**`diag-prof`** (base build, profiling): banner 115.6 s, window mapped
170.1 s, trap **171.1 s**. The captured stack (in
`diag-prof/exceptions.jsonl`):

```
RuntimeError: operation does not support unaligned accesses
    at BoxedWineCondition::wait(std::unique_lock<std::mutex>&)  (wasm-function[711]:0x1fc0a)
    at KUnixSocketObject::readNative(unsigned char*, unsigned int)
    at sys_read64(CPU64*, ...)  at ksyscall64(CPU64*)
    at CPU64::step(bool)  at platformThread(CPU*)
```

Disassembly of the profiling wasm at `0x1fc0a`: **`i32.atomic.rmw.add`
with alignment 2, offset 4** — the reference-count decrement of a
`std::shared_ptr` control block. The surrounding code is the inlined
assignment `thread->waitingCond = shared_from_this()` at the top of
`BoxedWineCondition::wait`: it stores the new pair at
`KThread+1060/+1064` and atomically decrements the *old* control-block
word at `old+4`. The trap means the previously stored control-block
value was not 4-aligned — impossible for a genuine control block.

Gated instrumentation rounds (all diagnostic code reverted before the
fix build; the rounds are in `diag-diag1` … `diag-diag6`) established
what the stored value actually was, step by step:

- Transition logging over every `waitingCond` set/clear showed the
  "old pair" on the affected worker holding **text bytes**
  (`ptr=0x63697672` = "rvic", `ptr=0x72657669` = "iver",
  `cntrl=0x80e27365` / `0xa680e273` — slices of UTF-8 text with an
  ellipsis).
- The "thread pointer" itself was **`0x25`, later `0x9`** — small
  integers, not pointers. The thread id read through it was 0. A
  memory dump at `thread+992` showed the wasm **static string table**
  (the launcher's progress strings: "Initializing Wine prefix….
  Loading device drivers…. Starting Windows services…. Detecting
  devices…."): at base 0x25/0x9, every `KThread` field access lands in
  low static memory.
- A tripwire on `setCurrentThread` (the only writer of the slot)
  **never fired** with a bogus value. A 1 kHz monitor polling the
  slot's absolute address saw its value **change at Vulkan-call
  frequency during the first-frames burst** (values 0x40, 0x8, 0x48,
  …), and saw the same class of writes on other workers' slots from
  early boot. The slot **address is constant** per worker
  (e.g. diag-diag5: pthread `0x22c0f38`, slot `0x23c78d8`, holding the
  genuine `KThread* 0x2231008` from 9.1 s; at 270.8 s the same slot
  read 0x25, at 272.3 s 0x9).
- Repeat captures (`diag-diag2` … `diag-diag5`) died at a **second
  site with the same cause**: `KFile::preadNative`
  (wasm-function[1403]:`0x634f1`) — the `FaultStageScope`
  constructor's `i32.atomic.load` at `currentThread()+1068`, on the
  demand path paging in the first 8 MB `vkAllocateMemory`. A
  fault-stage tripwire confirmed `currentThread() == 0x9` on that
  worker (pthread `0x22c0f28` family) at the fatal read.

The affected worker is the demo's guest thread (emulator tid `0xB`)
whose platform thread services its socket/file path. It runs
correctly for 200+ s; its identity slot is first observed destroyed
~1.0–1.8 s after the window maps, in the middle of the first
`vkAllocateMemory` (8 MB) / `vkCreateImage` / `vkBindImageMemory2`
burst — the filing's "first frames' image work". Between observations
the wait machinery, running on the fake base, kept publishing and
consuming `waitingCond` inside the static string table — working by
accident, because nothing else writes those particular static bytes —
until a consumer performed an alignment-checked atomic on the
"thread's" fields and trapped.

**Conclusion.** The trapping operations are (1) the shared_ptr
release in `BoxedWineCondition::wait` and (2) the fault-stage atomic
loads via `KFile::preadNative`; the data both consume is
`KThread::currentThread()`, and on the dying worker that value has
been overwritten in its TLS slot with a small integer by a writer
that does not pass through `setCurrentThread`. The writer itself is
**not** identified by this lane; it is filed separately (FD-129 on
this branch's register base) with the full monitor evidence.

## The fix (`849a55412`)

The correction is at the identity layer — the same layer FD-127
fixed for descriptor ownership: consumers must not take the current
thread from a single writable slot uncritically.

- `KThread::currentThread()` (`include/kthread.h`) validates the
  slot: a genuine `KThread` is heap-allocated, 8-aligned and above
  the static region (`>= 0x100000`). A value failing the test is
  replaced by the identity registered for the calling host thread,
  and the slot is healed with it.
- The registry (`source/kernel/kthread.cpp`) maps
  `pthread_self()` → `KThread*` behind a leaf mutex. It is written
  only by platform threads.
- `platformThread`
  (`source/emulation/cpu/normal/normalPlatformMultiThreaded.cpp`)
  registers its guest thread through an RAII guard
  (`KThread::PlatformThreadIdentityRegistration`) that unregisters
  on every exit path.

`ChangeThread` scopes, host threads without a guest thread (slot
null), and the single-threaded build are unaffected: the fallback
engages only for a non-null slot value that cannot be a `KThread`,
and the registry is empty for unregistered host threads.

## Proof

Standard (non-profiling) build of the committed tree:
`BUILD_INFO` dirty **0**, wasm sha256
`511e3151cdb9db888b2070ad8e551c4304b0670875f8812fe34060bc269b00fa`
— byte-identical to the build the verification capture ran against
(the capture build was made from the same tree state immediately
before the commit landed; `build-fix2.log` rebuilt from the
committed tree and reproduced the identical wasm hash).

**Verification capture `diag-fix1`** (same recipe as the filer's
diag6): banner 154.4 s → window mapped **227.4 s** → **no worker
trap** (no `exceptions.jsonl`; zero "unaligned" in the full console)
→ guest alive and working at the 1,200 s deadline (outcome
`deadline`, elapsed 1,212.3 s). The filed death point (window + 1.0 s
diag-prof, + 1.4 s diag5, + 2.2 s diag6) was passed by ~983 s of
continued guest work.

Gates on the final tree: browser build "Browser export verified"
(`build-fix2.log`); native suites **serial** — main **675/675, 0
FAILED** in 258 s (`native-main-fix-serial.log`), x64 **95/95, 0
FAILED** in 24 s (`native-x64-fix.log`); JS unit suite **352 passed,
0 failed** (`js-unit-fix.log`).

One honesty note: a first attempt at the main suite with 4 parallel
workers (the harness default off-Emscripten) showed two clone-test
failures and exited without a tally under box contention
(`native-main-fix.log`); the campaign-standard serial re-run above
is fully green, matching how the filer's gates were taken.

## In-vivo pipeline account (harvested toward FD-89 / CL-43)

From `diag-fix1`'s console tallies — the first capture on this code
line that survives past the pipeline phase, so the run reached its
measurement point:

- `vkCreateShaderModule`: **93** creations, **zero refusals**,
  including hash `0b3b4d8cc939485a` (the FD-62 module the register
  tracks) and the filer's first module hash lineage.
- Pipeline creations: `vkCreateGraphicsPipelines` **6**,
  `vkCreateComputePipelines` **68** (74 total; the filer's captures
  died after 32), `vkCreatePipelineLayout` **187**, zero `E_FAIL`
  lines in the whole run.
- Submissions: `vkQueueSubmit` **6**, `vkQueueSubmit2` **6**.
- Memory/image work: `vkAllocateMemory` **5**, `vkCreateImage`
  **29**, `vkCreateBuffer` **8**.
- Bridge drops: **3** `DROPPED guest command` lines total (a
  semaphore destroy, a pipeline-cache destroy, one more) — against
  8,386 dropped submissions in the FD-62-era baseline recorded
  under FD-55.
- At 337.6 s the run met the pipeline clause's front blocker in
  vivo: `UNSUPPORTED spirv-builtin-helper-invocation` — the FD-72
  module, refused by name under the FD-75 audit, exactly as FD-55's
  entry predicts.
- `vkCreateSwapchainKHR` **0**, `vkQueuePresentKHR` **0**: after the
  pipeline phase the guest settles into a ~17 s cycle re-dumping one
  shader blob (`11035b3a55999521.rs`, vkd3d-proton) without crossing
  the bridge — the downstream submit/swapchain behaviour FD-55's
  entry holds open on FD-70 + FD-72. **The demo's first present
  remains gated on FD-72** (no WGSL helper-invocation query exists;
  the refusal is kept loud because the one related spelling gives
  silently wrong values) — so FD-128's closure records survival past
  the trap point with the present clause bounded by FD-72/FD-70's
  existing entries, not by anything in this lane's power to serve.

## Residual

FD-129 (this branch's register): the unidentified writer of the
runningThread TLS slots. FD-128's fix makes the known fatal
consumers immune; the write itself continues and should be found
and stopped at its source.
