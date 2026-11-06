# Code organization pass — 2026-11-05

Lane: `lane-codeorg`, worktree `/home/ubuntu/wt-codeorg`, based on the
integration tip `48d27952a`. Organization only: no product behavior was
changed, no product defect was found (nothing was filed on the defect
register), and nothing was pushed. Evidence logs for everything below
are in `test-results/codeorg/` in this worktree.

## What landed

### 1. Removed `include/proccpuinfo.h` — an unused duplicate header
Commit `295570595`.

- `diff -b` against `include/cpuinfo.h` is empty: the two files are
  identical except whitespace (md5 `b09d990a…` vs `372842c2…`).
- Nothing includes it: full-text search for `proccpuinfo.h` and for
  `#include` of it across `source/`, `platform/`, `include/`, `web/`,
  `tools/` returns only the file itself.
- `cpuinfo.h` is the live header (included by
  `source/sdl/startupArgs.cpp`, `source/test/cpu/testCpuSystem.cpp`,
  `source/test/process/testGuestCpuTopology.cpp`); the one function
  the duplicate declared, `openCpuInfo`, is defined in
  `source/kernel/proc/proccpuinfo.cpp`, which includes only
  `boxedwine.h`, and its callers use `cpuinfo.h`.
- Proof: it is referenced by no translation unit, so no compiled
  output changes; the full suites below pass with it gone.

### 2. Split the KProcess clone family out of `kprocess.cpp`
Commit `ae3d7c274`.

`source/kernel/kprocess.cpp` was 5,333 lines covering every KProcess
operation. Lines 2531–3254 — the `K_CLONE_*` flag definitions, the
`clone_args` layout, and `clone3` / `clone` / `clone64` / `clone364` /
`forkProcess64` — are one self-contained topic (process creation) and
now live in `source/kernel/kprocessclone.cpp` (778 lines);
`kprocess.cpp` is 4,613 lines. The seam was verified before cutting:

- the block references none of `kprocess.cpp`'s file-local helpers
  (`addTaskStatNode`, the stack-setup trio, `selectMappedFileForRange`,
  `rewriteServerDirPath`, `internalAccess`, `itimerCheckWhich`, the
  wineboot patchers, `prctlRejectLog`, the `HWCAP_*` defines);
- the flag definitions, `struct clone_args`, and the block's one
  file-local constant `K_CLONE_PIDFD` are used nowhere outside it;
- all five functions are `KProcess` members declared in `kprocess.h`,
  so no header or call-site changes were needed, and both makefiles
  glob `source/kernel/*.cpp`, so the new file builds automatically.
- The moved text is byte-identical (the extraction was mechanical,
  by line range, with boundary assertions).

### 3. Two source-text guard tests repointed at the new file
Commits `1d791f005`, `28b2a5299`.

The proof cycle caught this (see below): two tests read
`kprocess.cpp` as text and assert the clone implementation's
properties live there.

- `source/test/process/testSyscall64DispatchGuard.cpp` (native)
  asserted six clone3-validation strings in `kprocess.cpp`; it now
  reads `kprocessclone.cpp` as well and applies the unchanged
  assertions to the concatenation, and a missing clone file fails
  the guard. All six strings verified present exactly once in the
  new file.
- `web/tests/suite/unit/elf-loader-mmap.test.mjs` asserted
  clone-ordering properties (`newThread->clone(thread)` windows,
  EIP step-over text) in `kprocess.cpp`; its `kproc` text now
  concatenates both files, restoring two silently weakened
  assertions (`EIP at the int 0x80`, `step over`) to full strength.
- The other seven unit tests that read `kprocess.cpp` were audited
  mechanically: every string they assert on still lives in
  `kprocess.cpp` itself, so they needed no change. Two test comments
  pointing at "the K_CLONE_* defines in kprocess.cpp" were updated
  to the new location.

## Proof (baselines vs after, all from preserved logs)

Baselines were measured on the untouched tree first
(`baseline-js.log`, `baseline-native-main.log`).

| Suite | Baseline | After |
| --- | --- | --- |
| JS legacy (`node --test web/tests/*.test.mjs`) | 115 pass / 0 fail | 115 pass / 0 fail |
| JS unit (353 tests) | all pass except `provenance-artifacts` (needs a built `dist/`) | pass/fail set identical; with `dist/` built, `provenance-artifacts` also passes — suite fully green |
| Native main (`Build/Test/boxedwine 0 0 1`) | 0 tests FAILED in 549 s | 0 tests FAILED in 291 s |
| Native x64 (`Build/X64Test/boxedwine 0 0 1`) | not captured (see note) | 0 tests FAILED in 27 s |
| vkd3d run-native | 69 PASS historically | 69 PASS / 0 FAIL |
| Browser build (`tools/bw64-build.sh`) | seed wasm `9b51d60101ab…` | "Browser export verified", EXIT=0 |

Note on the x64 baseline: the x64 native build was still running
when the split landed, so no pre-change x64 suite run exists. The
post-change x64 suite is 0 FAILED, consistent with every recent
lane's x64 result on this code line (111–114 tests, 0 failures).

The wasm output is **not** byte-identical after the split
(`79a9482621…` vs the seed's `9b51d60101ab…`), and cannot be: moving
code into a new translation unit changes link layout. Function-level
comparison against the preserved seed
(`/home/ubuntu/boxedwine64-shared/build-seed/48d27952a…/Wasm64Mt/boxedwine64.wasm`):
same function count (10,589); 10,460 of 10,589 function bodies
identical after normalizing call/global indices; the 129 differing
bodies per side have matching size profiles and differ in
layout-dependent address constants (sampled directly:
`i32.const 324508` vs `i32.const 324236`); total instruction count
differs by 16 of 1,917,8xx (two bodies, +11 and +5 instructions,
consistent with inlining variation). The behavioral proof is the
suite table above — the moved clone code is exercised directly by
the native clone tests (`32-bit clone flag dispositions`,
`32-bit clone3 ABI validation`, the clone32/clone-sharing tests)
and the JS fork/process tests, all green.

The proof cycle earned its keep once: the first post-split native
run reported **1 test FAILED** (`x64 syscall fallback guard`) while
the baseline was 0. Root cause was the source-text coupling in
item 3 above, not a behavior change — the moved code is
byte-identical, and the guard now passes.

## Dead-code survey (what was checked, and how)

Three methods were tried; only one is sound on this codebase:

- **Textual reference counting** (name occurrences per file) flags
  ~665 names, but most are false positives: handlers registered
  through token-pasting macros (the `x11_*` family, `syscall_*`
  dispatch) are invisible to text search.
- **Object-file analysis** (local symbols with no relocation in
  their own object, addends resolved) produced 97 plausible plain
  functions in project code — and every spot-checked one was a
  false positive: at -O2 a static that is called and fully inlined
  leaves an unreferenced out-of-line copy indistinguishable from
  dead code. All five kernel/decoder files it nominated
  (`syscall.cpp`, `kprocess.cpp`, `ksocket.cpp`,
  `knativesocket.cpp`, `decoder.cpp`) came back CLEAN under the
  compiler check. This method must not be used for removals here.
- **Compiler warnings** (each TU recompiled syntax-only with its
  exact build command plus `-Wunused-function`, in both native
  configurations — guest-32 `test` and guest-64 `x64-test`):
  **zero** "defined but not used" findings. Coverage is complete:
  all 301 project translation units, in both configurations
  (603 TU-configuration checks across `test-results/codeorg/scan2.log`
  and `tcheck1–4.log`, every result line CLEAN). A candidate would
  also have had to pass a repo-wide textual absence check before
  any removal; none reached that stage.

Conclusion: after the many earlier cleanup lanes, the hand-written
code has no compiler-detectable dead static functions in the
checked configurations. The emscripten configuration was not
separately warning-scanned; it compiles the same sources with the
guest-64 configuration that `x64-test` covers.

Other surveys that came back clean:

- Duplicate files: the only duplicate pair in the project sources
  was `proccpuinfo.h`/`cpuinfo.h` (item 1). Remaining duplicates
  are vendored or generated (asmjit mirrors, VK/GL generated
  headers) and out of scope for hand organization.
- Orphan web modules: every top-level `web/*.mjs` has at least one
  reference; none is dead.
- `source/opengl/gl32bridge_abi.h` looks unreferenced from C++ but
  is read as text by three JS unit tests — not dead.
- `source/util/karray.h` is never included by C++ but is a named
  fixture of the known-gaps audit allowlist — removing it is
  entangled with that checker; left in place.
- `source/x11/x11common.cpp` (4,192 lines) reads as one coherent
  job (Xlib implementations plus their dispatch table) — not a
  split candidate.

## Proposed, not landed (with the evidence) — all six decided in the follow-up pass below

1. **`source/emulation/cpu/ops.h` cluster (5,215 lines).**
   `ops.h` (3,753 lines) and `setcc.h` (450) are never `#include`d
   anywhere, and no `.d` file from either native build references
   them. `ops.h` is the only includer of `incdec.h`, `pushpop.h`,
   `strings_op.h`, `shift_op.h`, and its `#include "arith.h"` does
   not resolve anywhere in the tree, so as-is it cannot even be
   compiled. The live interpreter headers are the `normal_*` /
   `jit*` / `dynamic_*` families. **Against removal:** commit
   `76a91f702` (2026-10-25) describes ops.h's SIDT/SGDT handlers as
   the live interpreter's, aligned deliberately — a maintainer
   statement that conflicts with the mechanical evidence. Per this
   lane's rule (a reorganization that cannot be proven safe does
   not land), a maintainer needs to adjudicate which is true
   before these 5,215 lines move.
2. **`web/tests/wine10-clean-boot*.mjs` duplicate family.**
   Twelve 77-line entrypoints; `wine10-clean-boot.mjs` and
   `wine10-clean-boot-tmp.mjs` are byte-identical (md5
   `00248acfaa5d9f5294cb32991a4eed05`). But
   `web/tests/diagnostics/README.md` documents deliberate
   retention of these historical probe entrypoints (a third
   identical copy, `probe-tmp5.mjs`, is retained there on
   purpose). Deleting them would contradict a documented
   decision; revisit the decision first if this is wanted.
3. **`powerOf2` duplicated helper.** The same 8-line static
   helper lives at `source/util/bheap.cpp:22` and
   `source/util/bnativeheap.cpp:24`, used in both. Both files
   include only `boxedwine.h` and their own headers; there is no
   existing shared home, and inventing a header for one 8-line
   function is worse organization than the duplication. Revisit
   if a third user appears.
4. **stb_image unused format loaders.** `source/util/stb_image.cpp`
   compiles the whole vendored stb_image; the project's callers
   (`player.cpp`, `platformhelper.cpp`) use `stbi_load`, and most
   format loaders (GIF/HDR/BMP/PNM paths, ~60 internal functions)
   are unreferenced in the native objects. The right lever is the
   wrapper's configuration (`STBI_NO_*` defines), not editing
   vendored code — and choosing which formats the product keeps
   is a product decision, out of scope for an organization pass.
5. **`syscall.cpp` / `syscall64.cpp` splits.** Both are large
   (4,811 / 8,090 lines) and topical handler clusters exist, but
   the handlers are registered in dispatch tables inside the same
   files, so extraction needs extern declarations per moved
   handler — a wider, riskier change than the kprocess seam,
   which needed none. Left as a follow-up for a lane that can
   give it a full proof cycle of its own.
6. **`common_sse.cpp` helpers flagged by object analysis.**
   Thirteen SSE helper functions are unreferenced in the aarch64
   native objects; they are host-architecture-conditional code
   (the callers live in x86-host JIT paths). A single-host proof
   cannot show them dead; removing them risks breaking a host
   this box cannot build. Not landed.

## Lane state

- Branch `lane-codeorg`; commits below, with this record committed
  on top (tip quoted in the lane's handoff report).
- Commits: `295570595` (header removal), `ae3d7c274` (split),
  `1d791f005` (native guard), `28b2a5299` (JS guard), plus this
  record.
- Generated state stripped before handoff (`dist/`, `web/runtime/`,
  `project/linux/Build/`, `project/emscripten/Build/`,
  `__pycache__`); evidence logs remain in `test-results/codeorg/`.
- Never pushed; no other tree was modified.
- Follow-up pass (same day): branch `lane-codeorg-decisions` continues
  from this lane’s tip `74957e749` in worktree
  `/home/ubuntu/winbox-project/worktrees/wt-codeorg-decisions` and
  decides all six proposals below. Its commits: `0ab0103d7` (proposal 1
  removal + register citation), plus the record-amendment commit that
  adds the decisions section. Evidence logs in
  `test-results/codeorg-decisions/` in that worktree. Never pushed.

## Proposal decisions — follow-up pass (2026-11-05)

Every proposal above is decided here, on its evidence. One was carried
out; five are closed where they stood, each with its deciding evidence
and reason. No product defect was found in this pass, so nothing was
filed on the defect register.

### 1. `ops.h` cluster — DECIDED: removed (carried out)

Commit `0ab0103d7` deletes `ops.h`, `setcc.h`, `incdec.h`, `pushpop.h`,
`strings_op.h`, `shift_op.h` (5,215 lines) and repoints the one
load-bearing citation.

The conflict the original pass left open is adjudicated as follows.
Commit `76a91f702` describes ops.h’s SIDT/SGDT handlers as the live
interpreter’s, aligned deliberately to the established values (SIDT
limit 1023 base 0; SGDT limit 0x7f base 0). Those exact values are
stored by `normal_sidt` / `normal_sgdt` in
`source/emulation/cpu/normal/normalCPU.cpp` (lines 332 and 340),
which are registered in `normalOps[]` and dispatched in the
interpreter switch; `testSgdtSidtStores` exercises those engines, not
ops.h. The commit’s ops.h edit was inert and its effective part was
the JIT change in the same commit. The mechanical evidence stands
uncontradicted: the six files are `#include`d by nothing (exhaustive
search two ways — `git grep` over every tracked file and a full-tree
text search; the only outside mentions are two prose references and
one test comment), appear in no build dependency file, and `ops.h`
cannot compile at all because its `#include "arith.h"` resolves
nowhere in the tree. The four satellites are included only by `ops.h`
itself; the `shift.h` / `strings.h` they include stay, as they are
live (`srcgen.cpp` emits an include of `shift.h` into generated
sources).

Proof, to the standard of the landed work: the full suite set on the
removal tree matches this record’s branch-tip results exactly —
browser build "Browser export verified" (exit 0), native main 689
tests 0 FAILED, native x64 113 tests 0 FAILED, vkd3d run-native
69 PASS / 0 FAIL, JS legacy 115 pass / 0 fail, JS unit 353 passed /
0 failed (fully green with `dist/` built, including the known-gaps
audit test and `tools/known-gaps-audit.mjs --check` standalone,
exit 0). Stronger still: because the removed files were never
compiled, the built program is byte-identical to this lane’s own
build of the unmodified tip — wasm sha256 `79a9482621…`, the
same hash recorded above. Zero compiled bytes changed.

One citation had to move with the removal:
`docs/known-gaps/kernel.md`’s SIDT/SGDT row cited
`ops.h:3536-3575` as the fix location; it now cites
`normal/normalCPU.cpp:332-347` and credits the live handlers, which
is where the fix’s values actually live. Two prose mentions of
ops.h remain standing and are still true as history: a comment in
`source/test/cpu/testMisc.cpp` ("the legacy ops.h handlers stored
0/0") and `docs/jit-experiment.md` (a design record, which by the
documentation rules is not edited).

### 2. `wine10-clean-boot*.mjs` duplicates — DECIDED: kept (closed)

Deciding evidence: `web/tests/diagnostics/README.md` documents this
family as retained historical experiments. It names
`wine10-clean-boot.mjs` and `wine10-clean-boot-tmp.mjs` as "the
retained" entrypoints, notes a third identical copy was retained on
purpose, and states the set is historical experiments to inspect
before running, not a regression suite. The pair’s byte-identity
was re-verified this pass (md5 `00248acfaa5d…`, both files). A
documented retention decision governs these files; deleting one twin
would contradict that decision, and an organization pass does not
override a recorded decision on duplication alone. Revisit only by
revisiting the README decision explicitly.

### 3. `powerOf2` duplicated helper — DECIDED: kept as-is (closed)

The original evidence re-verified: the two static copies
(`source/util/bheap.cpp:22`, `source/util/bnativeheap.cpp:24`) are
byte-identical 8-line helpers, and each file includes only
`boxedwine.h` and its own header, so no existing shared home exists.
This pass adds the deciding fact: the name `powerOf2` is already
taken by a third, semantically different function
(`source/util/bstring.cpp:60` — `int` argument, level counted from
`SMALLEST_LEVEL`). Hoisting the heap helper into any shared scope
would put two same-named functions with different meanings side by
side — worse organization than the duplication — and making one
heap include the other’s header would invent a false dependency
between unrelated allocators. Revisit trigger unchanged: a third
user of the *same* semantics.

### 4. stb_image unused format loaders — DECIDED: out of scope (closed)

Choosing which image formats the product keeps changes what programs
running on the system can load, so the `STBI_NO_*` configuration is a
product decision, not organization; under the current pause on
development it is not carried out here. Verified this pass:
`stbi_load`’s only callers are `source/util/player.cpp` (twice) and
`source/ui/opengl/platformhelper.cpp`, loading user-supplied files
— the format set is observable behavior. The vendored file itself
was not edited; per the original proposal, the wrapper configuration
is the right lever when a product decision is taken.

### 5. `syscall.cpp` / `syscall64.cpp` splits — DECIDED: not carried out (closed)

Both tests the landed `kprocess` split met fail here, on evidence
re-verified this pass. (a) Single coherent job per file: each file
is one guest-syscall surface for one width (entry points: the
in-file `syscallFunc` dispatch table; `ksyscall64`), and its size is
the breadth of that one job, not several unrelated jobs. (b) No
zero-change seam: dispatch lives inside the same files (`static
const SyscallFunc syscallFunc[]` at `syscall.cpp:2100`; the
`ksyscall64` switch in `syscall64.cpp`), and the files carry 195 and
102 file-local statics respectively, so every extracted handler
would need a linkage change plus a new declaration — the split
cannot meet the byte-identical-move, no-header-change standard the
landed split was held to. A change of that shape belongs to a
dedicated development effort with its own proof cycle if it is ever
wanted, not to organization tidy-up under the development pause.

### 6. `common_sse.cpp` helpers — DECIDED: kept (closed)

Deciding evidence: the helpers are declared in
`source/emulation/cpu/common/common_sse.h`, which sits in the live
include chain of the x86-host JIT (`source/emulation/cpu/jit/jitCallSSE.h`
includes `../common/common_sse.h`). This box is aarch64
(`uname -m`), where that host path is not built or exercised, so the
only available evidence — absence from aarch64 objects — cannot
distinguish dead code from host-conditional code; the compiler-warning
survey that proved the rest of the tree clean was itself only run in
the two aarch64 native configurations. Removal is unprovable on this
machine and risks breaking x86-host builds that cannot be compiled
here. Closed as unprovable with the available evidence, not as dead.
