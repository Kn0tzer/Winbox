# VK64_DROP_IF macro-class audit — 2026-10-25 (vkmacro lane)

Follow-up to register row 380 (unsubmitted-command-buffer fallback). The
fallback lane proved its scan's type guard never guarded because it was
spelled `VK64_DROP_IF(continue, ...)`; this audit covers **every**
flow-control call site of the macro in the bridge and closes the class.

## The defect

`source/vulkan/vk64bridge.cpp` (old line 414):

```cpp
#define VK64_DROP_IF(action, ...) do { if (__VA_ARGS__) { vk64DropLog(#__VA_ARGS__, __LINE__); action; } } while (0)
```

`continue` and `break` bind to the innermost enclosing loop or switch.
The macro's own `do{}while(0)` is such a construct, so:

- `VK64_DROP_IF(continue, cond)` — the `continue` targets the do-while;
  its condition is `0`, so control simply leaves the macro. The caller's
  loop body **keeps executing** with the value the guard rejected.
- `VK64_DROP_IF(break, cond)` — the `break` exits the do-while, not the
  caller's loop/switch. Same fall-through.
- `return` / `goto` actions are unaffected (they bind to the function /
  label regardless of intervening constructs).

Every affected site still called `vk64DropLog`, so the drops were visible
in logs while the guarded-against work proceeded — the worst combination:
an audit trail claiming a rejection that never happened.

## Macro-semantics analysis (all action kinds, both forms)

| Property | do{}while(0) form (old) | if/else{} form (new) |
|---|---|---|
| `return` action | works | works (identical) |
| `goto` action | works | works (identical) |
| `continue` action | **binds to the macro — no-op** | binds to the caller's loop |
| `break` action | **binds to the macro — no-op** | binds to the caller's loop/switch |
| Condition evaluation | exactly once | exactly once (verified by compiled pin with a side-effecting condition) |
| Action evaluation | at most once, only when the condition holds | identical |
| Log text | `#cond` stringised, `__LINE__` of the call site | identical (same stringise/line mechanics) |
| Unbraced if-arm use `if (x) MACRO; else …` | safe | **compile error by construction** (the trailing `else{}` leaves the caller's `else` unbindable) — a loud failure, never a silent re-bind |

A macro cannot make `continue` target an outer loop while wrapping the
action in *any* loop/switch construct; removing the wrapper is the only
macro-level fix, and the `else{}` tail converts the classic dangling-else
hazard of the unwrapped form into a build error.

## Census (comment/string-aware parse of vk64bridge.cpp)

234 call sites (grep cross-check: 238 source lines mention the macro =
234 sites + the `#define` + 3 comment mentions):

- 176 return-family (`return 0` ×148, `return` ×27, `return nullptr` ×1) — always sound.
- **58 flow-control sites: 55 `continue` + 3 `break`.**
- The macro exists nowhere else: `source/vulkan/vulkancommon.cpp` (the
  32-bit bridge's shared file) and the rest of `source/` contain no
  `VK64_DROP_IF` and no equivalent action-parameter macro (tree-wide
  search for `#define …(action…)` / `do { if` patterns — one hit, this
  define). The 32-bit bridge `vk32Bridge` lives in this same file and
  uses this same macro; its sites are included below.
- Call-site context audit for the new form's one hazard: no site is the
  unbraced arm of an `if`/`else`/`for`/`while` (same-line check + a
  previous-statement check over all 234 sites; the only non-`{`/`;`/`:`-
  terminated predecessors are braced-block closers). The full WASM build
  then compiled all 234 expansions, which is the mechanical proof.

## Classification of the 58 flow-control sites

**55 LIVE** — the guarded value flows into use when the condition holds;
no downstream re-check subsumes the guard. **3 BENIGN** — a downstream
check strictly subsumes the condition (cited per site). Line numbers are
unchanged by the fix (zero net line delta).

### LIVE — spec-constant capture (64-bit `captureStageSpec`, 32-bit `captureStageSpec32`)

| Line | Condition | Failure when the guard falls through |
|---|---|---|
| 1005 (break) | `out.n >= 16` | `out.entries[out.n++]` writes past `StageSpec.entries[16]` — OOB write |
| 1006 | offset/size outside `si.dataSize`, `size > 8`, `size == 0` | `memcpy(e.value, data.data()+me.offset, me.size)`: OOB read of the data vector and OOB write past `SpecEntry.value[8]` (guest-controlled `me.size` is U64) |
| 1008 | `me.size ∉ {1,2,4,8}` | non-scalar spec entry recorded and copied (see 1006) |
| 8002 (break) | `out.n >= 16` | same as 1005 (32-bit capture) |
| 8003 | `me.offset > si.dataSize \|\| me.size > si.dataSize - me.offset` | same as 1006 (32-bit capture) |
| 8004 | `me.size ∉ {1,2,4,8}` | same as 1008 (32-bit capture) |

### LIVE — readback setup/completion (`setupReadbacks`, `completeReadbacks`)

| Line | Condition | Failure |
|---|---|---|
| 3275 | `!cb` | null dereference iterating `cb->cmds` |
| 3279 | `!rb.w \|\| !rb.h \|\| rb.w > 4096 \|\| rb.h > 4096` | unbounded `w*h*4` host allocation for a guest-sized readback; zero-size pendings queued |
| 3282 | `!heap` | `memset(NULL, …)` after malloc failure — host crash |
| 3300 | `!waitForReadbackFlag(pend.flagPtr, 10000)` | readback that never arrived is copied to the guest anyway — **silent delivery of stale/uninitialised pixels** |
| 3302 | `!buf \|\| !buf->mem` | null dereference of `buf->mem` |
| 3304 | `!mem \|\| !mem->va` | null dereference of `mem->size` |
| 3308 | `buf->memOff + pend.dstBufOff >= mem->size` | the clamp on the next line computes `mem->size - …` as size_t — **underflow to a huge `pxBytes`**, then `writeGuestBytes` with it |

### LIVE — submit serialisation (`serializeSubmitV2`)

| Line | Condition | Failure |
|---|---|---|
| 3793 | `!si \|\| !di` | null dereference `si->w`/`di->w` in the resolve-copy extent |
| 3864 | `slot >= VK64_MAX_VERT_BINDINGS` | OOB read `pipe->vbStride[slot]`, OOB write `v2MaxVbUse[slot]` (both fixed-size arrays, guest-controlled slot) |
| 3888 (break) | `first >= VK64_MAX_SETS` | the following clamp computes `count = VK64_MAX_SETS - first` (U32) — **underflow to a huge count**; the emit loops then read `k.sets[first+i]` far OOB |
| 3975 | `!vb \|\| k.vbOffsets[i] >= vb->size` | null dereference, or `bound = vb->size - offset` underflows → `emitBufferData` with a huge length |

### LIVE — descriptor updates (both ABIs)

| Line | Handler | Condition | Failure |
|---|---|---|---|
| 5969 | vkUpdateDescriptorSets (64) | `!s` | null dereference `applyWrite64(*s, w)` |
| 5977 | vkUpdateDescriptorSets copies (64) | `!src \|\| !dst` | null dereference in `applyDescCopy(*src, …, *dst, …)` |
| 10180 | vkUpdateDescriptorSets (32) | `!s` | null dereference `applyWrite32(*s, w)` |
| 10188 | vkUpdateDescriptorSets copies (32) | `!src \|\| !dst` | null dereference in `applyDescCopy` |

### LIVE — buffer copies (immediate mode)

| Line | Handler | Condition | Failure |
|---|---|---|---|
| 6717 | vkCmdCopyBuffer (v1) | `r.srcOffset >= smem->size \|\| r.dstOffset >= dmem->size` | the clamps compute `end - off` in U64 — **underflow to a huge `n`** → `std::vector<U8>(n)` length_error / huge guest copy. (The v2 handler's comment documents this exact bug class; v1 kept it because its guard never fired.) |
| 6759 | vkCmdCopyBuffer2 | `sRel < src->memOff \|\| sRel >= smem->size` | same underflow; the handler's own comment asserts "sOff < sEnd … so no underflow" — an invariant only the (broken) guard established |
| 6760 | vkCmdCopyBuffer2 | `dRel < dst->memOff \|\| dRel >= dmem->size` | same, destination side |

### LIVE — image copies (row loops; 64-bit v1/v2, resolve, 32-bit)

| Line | Handler | Condition | Failure |
|---|---|---|---|
| 6884 | vkCmdCopyImage (64) | `sy >= src->h \|\| dy >= dst->h` | out-of-image row copied (wrong data within the allocation; past it, the also-broken end guard at 6891 was the only backstop) |
| 6891 | vkCmdCopyImage (64) | `sOff >= sEnd \|\| dOff >= dEnd` | clamp `n = sEnd - sOff` underflows U64 → huge copy |
| 6938 | vkCmdCopyImage2 (64) | row bounds | same as 6884 |
| 6945 | vkCmdCopyImage2 (64) | start bounds | same as 6891 |
| 7167 | vkCmdResolveImage (64) | row bounds | same as 6884 |
| 7174 | vkCmdResolveImage (64) | start bounds | same as 6891 |
| 9544 | vkCmdCopyImage (32) | `sy >= src->h \|\| dy >= dst->h` | out-of-image row copied via `g_mem32`; this loop has **no second guard at all** |

### LIVE — blits (64-bit vkCmdBlitImage + vkCmdBlitImage2, 32-bit vkCmdBlitImage/2)

Per-pixel nearest-neighbour loops. The region guard and the per-pixel
image-bounds guards are the only defences; the 64-bit loops add
memory-end guards (7054–7056 / 7121–7123), the 32-bit loop has **no
memory-end backstop** — an out-of-range pixel address goes straight to
`g_mem32->memcpyFromGuest/ToGuest`.

| Line(s) | Guard | Failure |
|---|---|---|
| 7038, 7106, 9648 | `srcW <= 0 \|\| srcH <= 0 \|\| dstW <= 0 \|\| dstH <= 0` | degenerate/mirrored region is processed per-pixel instead of dropped; pixels that happen to land in-bounds are still copied from an invalid region, and negative walks reach OOB through the (also broken) pixel guards |
| 7042, 7109, 9651 | `sy < 0 \|\| (U32)sy >= src->h` | out-of-image source row read (wrong pixel inside the allocation; OOB past it) |
| 7044, 7111, 9653 | `dyAbs >= dst->h` | out-of-image destination row written |
| 7047, 7114, 9656 | `sx < 0 \|\| (U32)sx >= src->w` | out-of-image source column read |
| 7049, 7116, 9658 | `dxAbs >= dst->w` | out-of-image destination column written |
| 7055, 7122 | `sOff + pxBytes > sEndB` | pixel read straddling the memory object's end |
| 7056, 7123 | `dOff + pxBytes > dEndB` | pixel write straddling the memory object's end |

### LIVE — execute/submit null command buffers (both ABIs)

| Line | Handler | Failure |
|---|---|---|
| 7341 | vkCmdExecuteCommands (64), `!sec` | null dereference iterating `sec->cmds` |
| 7443 | vkQueueSubmit (64), `!cb` | null dereference iterating `cb->cmds` |
| 7545 | vkQueueSubmit2 (64), `!cb` | null dereference iterating `cb->cmds` |
| 9789 | vkCmdExecuteCommands (32), `!secondary` | null dereference iterating `secondary->cmds` |
| 9986 | vkQueueSubmit (32), `!cb` | null dereference iterating `cb->cmds` |

### BENIGN (3) — subsumed by a downstream re-check

| Line | Guard | Subsumption proof |
|---|---|---|
| 7054 | `sOff >= sEndB \|\| dOff >= dEndB` (blit, 64-bit) | `sOff >= sEndB` implies `sOff + pxBytes > sEndB` (pxBytes ≥ 1 for any real format), which the next guard (7055) checks; the destination side is subsumed by 7056 identically. If pxBytes were 0, both pixel memcpys are no-ops. No observable difference. |
| 7121 | same (vkCmdBlitImage2) | subsumed by 7122/7123 identically |
| 7675 | `w <= 0` (VKPROF top-3 trap ranking) | `top` is zero-initialised (`TE top[3] = {}`) and entries only ever hold values `> top[k].w ≥ 0`; insertion requires `w > top[k].w`, which `w <= 0` can never satisfy. The print loop additionally stops at the first `top[k].w <= 0`. The fall-through is a no-op. |

Note on method: BENIGN is judged per site under "this guard alone broken,
the rest working". In the shipped code *all* links of several chains were
broken at once (e.g. the blit pixel chains), which is why compositions of
individually-defensible sites were still live defects in practice.

## The fix

One line (plus its comment block, rewritten in place — **zero net line
delta**, so no register citation moved):

```cpp
#define VK64_DROP_IF(action, ...) if (__VA_ARGS__) { vk64DropLog(#__VA_ARGS__, __LINE__); action; } else {}
```

Why redefine the macro instead of migrating the 58 sites to explicit
`if` forms (the fallback lane's local fix):

- The macro is the root cause; fixing it fixes all 58 sites and every
  future site. Per-site migration leaves a loaded macro in place for the
  next author.
- Migration would insert lines / rewrite 58 sites across the file and
  shift the register's 117 `vk64bridge.cpp` citations; the redefinition
  shifts nothing (citation audit before and after: 198 MATCH /
  14 MATCH-INHERITED / 0 DRIFT, byte-identical verdicts).
- Log output is unchanged: same stringised condition, same call-site
  line, same rate-limited `vk64DropLog`.
- The two row-380 fallback scans keep their explicit if-guard (still
  correct; their comments describe the do-while cause, which remains the
  accurate history of the 2026-10-25 trap).

Behaviour change is confined to the 55 LIVE sites' guarded paths — i.e.
to inputs the code always intended to reject. At the 3 BENIGN sites and
all 176 return sites, behaviour is provably unchanged.

## Tests + sabotage

New: `web/tests/suite/unit/vk-drop-macro-guard.test.mjs` (3 tests):

1. **Define shape**: the define is exactly the flow-safe form, defined
   once, and no bridge `#define` wraps an action in `do{}while(0)`.
2. **Site census**: a comment/string-aware parse of the bridge pins all
   234 sites — action counts (148/27/1/55/3) and the exact multiset of
   the 58 (action, condition) flow-control pairs.
3. **Compiled semantics**: the harness embeds the define line *extracted
   from the bridge source* (not a copy) and asserts, in compiled C++:
   continue skips the iteration, break exits a loop, break exits a
   switch without fallthrough, return/goto unchanged, the condition is
   evaluated exactly once, continue binds to the inner loop of nested
   loops, and the drop log receives the stringised condition once per
   hit. A second translation unit asserts that unbraced-if-arm use
   **fails to compile**.

Sabotage (all performed, all detected):

- Reverting the define to the do{}while(0) form turns the new test red
  (compiled scenarios fail: continue/break/switch/nested) — and also
  turns the pre-existing define pins red.
- Renaming the macro at each of the 58 flow-control sites, one at a
  time, turns the census test red **58/58**. (Two further textual
  matches of the pattern are the row-380 explanatory comments; they are
  comments, stripped by the census parser, and are not call sites.)
- The two pre-existing tests that pin the define's literal text
  (`no-silent-drops.test.mjs`, `vk-silent-drop-guard.test.mjs`) were
  updated to the new define text — same assertion, new spelling; their
  occurrence-count floors (≥200 / >100) are untouched and still met by
  the unchanged 234 sites.

Checked and deliberately not changed: the four compiled-slice tests that
define a local logging-free copy of the macro (`v2-image-blit`,
`v2-image-copy`, `v2-buffer-use`, `vulkan-image-view-aliases`) extract
recording helpers (`recordImageCopy`, `recordImageBlit`, …) whose
`VK64_DROP_IF` actions are all return-form — identical semantics under
both macro forms, so their local copies remain faithful for the code
they exercise.

## Verification

- WASM build (`BW64_BUILD_OWNER=vkmacro`, `wasm64-mt-jit`): **BUILD
  COMPLETE**; the only warning in the log is the pre-existing emscripten
  `-pthread + ALLOW_MEMORY_GROWTH` linker notice.
- Solo unit suite (box verified clear of other suites/builds first):
  **218 passed / 0 failed** (217-file baseline at `725000373` + the new
  guard file).
- Citation audit (`tools/vk-register-citation-audit.py`): **198 MATCH /
  14 MATCH-INHERITED / 101 UNCHECKABLE / 8 EXEMPT / 0 DRIFT** — identical
  to the pre-fix baseline, as designed (zero line delta).

## Addendum 2026-10-26 (portal-perf lane): census 241 -> 242

The vk32 BW64_VKPROF parity change (per-trap timers + present-boundary
breakdown for the 32-bit dispatcher in vk64bridge.cpp) mirrored the 64-bit
present boundary top-3 trapOther scan, adding one VK64_DROP_IF site:
continue on w <= 0 in the vk32 vkQueuePresentKHR boundary (the 64-bit twin
was already in the census). New census: 242 sites = 183 return-form + 59
flow-control (56 continue + 3 break); classification 56 LIVE / 3 BENIGN
(the new site is LIVE, same class as its twin). Pins updated in
web/tests/suite/unit/vk-drop-macro-guard.test.mjs and the register row.
