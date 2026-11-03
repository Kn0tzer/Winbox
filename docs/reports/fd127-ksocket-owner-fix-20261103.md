# FD-127 — fix record: socket creation allocated its descriptor in the wrong process (lane-fd127fix)

Lane: `lane-fd127fix` (worktree `/home/ubuntu/wt-fd127fix`), based on
`lane-godotpark` tip `eef642795` (the FD-127 filing). Final tip:
`f6f010eb4`. Date: 2026-11-03. Register: FD-127 moved from Section A
to the Section B row for this branch (closure number settles at
composition); FD-128 filed in Section A from this lane's verification
captures.

## The filed defect

FD-127 (filed by the godotpark lane,
`docs/reports/fd127-godot-device-setup-park-20261103.md`): during
vkd3d-proton's D3D12 device setup in the Godot demo, immediately after
`vkd3d_init_device_caps`, the main thread's thread creation
(`NtCreateThreadEx` on Wine's unix side) issues a `pipe2(fds,
O_CLOEXEC)` that never returns inside the emulator. The filing
established the freeze frame byte-identically across three captures
on two shipped builds, excluded the attributable host states its dump
can see, and left the block unnamed inside the pipe2 handler /
`ksocketpairFds` path. Its closing terms specified the
instrumentation, the re-run capture, and the success shape (window
mapped + first vkCreateShaderModule, gates green).

## Investigation — the instrument

A gated step ring (`include/bwstepdiag.h`; steps recorded with plain
atomic stores into a 256-entry in-memory ring, no locks, no logging;
the guest thread dump prints the ring's tail). Console logging was
the first attempt (v1) and was abandoned as the primary channel
because console delivery transits the main thread — a missing line
cannot distinguish "never reached" from "not delivered". The ring
carries a per-record checksum from v3 so torn reads are marked, not
believed. All call sites are gated on `BW_THREAD_DUMP=1`, the thread
dump's own switch, so production runs record nothing. The ring and
its notes are retained on the branch (see "Notes for the merge
lane").

Four captures on the batch-4 code line (recipe: the FD-70/deploy
Godot capture configuration, driver `web/tests/fd127-godot.mjs`,
thread dumps every 30 s; evidence under `test-results/fd127/`):

- **diag1** (console lines only): the freeze reproduces at the filed
  call — `pipe2 enter / pair enter / pair locked` for pid 10 tid 11,
  then nothing. The fd-table lock was acquired immediately (no
  contention), the log mutex was free at dump times, and a per-thread
  host CPU sampler showed ~0 accumulation during the freeze:
  **blocked, not spinning**.
- **diag2** (ring v2): the trail ends inside
  `KProcess::allocFileDescriptor`, after its fd-table lock
  acquisition, before its return — on a process object whose id
  reads 0, while the pair's own steps carry the requesting thread
  (pid 10, tid 11).
- **diag3** (ring v3, alloc tail + addChild + inotify sweep notes):
  healthy allocations bracketing the freeze complete the full span,
  and their inotify leg shows step 31 — **no live inotify
  instances** — on every call. The inotify path is exonerated. The
  frozen trail still ends after the allocation lock, before the
  handle is chosen.
- **diag4** (ring v4, notes inside `getNextFileDescriptorHandle` +
  fd-table mutex addresses): the healthy allocation just before the
  freeze runs on the guest's real process (id 10) with its fd-table
  mutex at a heap address (encoded bits 1951939 ≈ 0x1DC89B0 — the
  same mutex the socketpair helper holds, per its own address note),
  and the handle scan returns handle 24. The frozen call's
  allocation runs on a process object with **id 0 whose fd-table
  mutex address encodes to bits 1 (≈ linear address 0x10)**; the
  handle scan is entered with that same degenerate address and its
  lock is never acquired (no step 38, 120+ s, two dumps). That is
  the block.

## The defect

`ksocket()` created the socket object and then allocated its
descriptor with `KThread::currentThread()->process->allocFileDescriptor(...)`
at all three creation sites (AF_UNIX, AF_NETLINK, AF_INET) — it took
the owning process from the ambient current thread instead of from
the thread the socket was being created for. On the ordinary guest
syscall path the two are the same thread, so the defect is invisible.
Serviced under the unix-side dispatch binding (the path Wine's
`NtCreateThreadEx` → `wine_server_handle_to_fd` → pipe2 takes), the
ambient current thread's process is a degenerate id-0 object whose
fd table does not live in real heap memory: its "mutex" sits at
linear address ~0x10. The first acquisition of that lock (in
`allocFileDescriptor`) succeeds against the zero/static memory
there; the second acquisition (in `getNextFileDescriptorHandle`,
same call) never returns, and the requesting guest thread — which
waits for the pair — freezes with it. The descriptor would in any
case have been filed in the wrong process's table.

## The fix

- `f670bd7de` — `ksocket(KThread* thread, U32 domain, U32 type,
  U32 protocol)` takes the owning thread explicitly and allocates in
  `thread->process` at all three creation sites. Callers pass the
  thread they are servicing: the socketpair helper (both creations),
  the 64-bit `socket` handler (`cpu->thread`, which it already
  null-checks), and both 32-bit socket paths (`cpu->thread`). A null
  owner answers -ENOSYS, mirroring the 64-bit handler's existing
  guard.
- `e5dd3c1c0` — retires the addChild/inotify step notes (27–36):
  they had cleared their path, and a unit test compiles `addChild`'s
  source standalone, which the notes broke (`fs-dynamic-link-cache`
  failed on them; green again after the retirement, verified
  standalone).
- `9cc7245b6` — trims the superseded v1 console diag lines from
  `ksocket`'s AF_UNIX branch (the ring carries the same facts,
  drop-proof), and updates the `browser-network` unit harness for
  the new signature (its stub now passes its stub thread as the
  owner; K_ENOSYS and diag stubs added; green standalone).
- `f6f010eb4` — the pipe2 handler's diag gate uses the portable
  `bwStepDiagEnabled()` (`bwThreadDumpEnabled()` is Emscripten-only;
  the native x64 build exposed this).

## Verification

Same capture, fixed build (evidence `test-results/fd127/diag5/`,
`diag6/`; preserved console streams alongside):

- **diag5** (build of `f670bd7de`): caps 193.1 s → first
  vkCreateShaderModule 197.3 s → first window mapped 226.0 s.
- **diag6** (final tip `f6f010eb4`, BUILD_INFO git_sha = tip, dirty
  0): caps 257.6 s → first vkCreateShaderModule 261.6 s (1,216
  bytes, hash f598618b6233f21a) → pipeline creations (32 traps) →
  queue submits (6) → first window mapped 299.3 s; pid 10
  progressing (`min_noprogress_ms=0`). The ring shows pair
  completions end to end (steps 14–17) and allocations completing on
  real tables.

Gates on the final tree: native suites serial — main 675/675 and
x64 95/95, **0 FAILED**; JS unit suite **352 passed / 0 failed**;
browser build `Browser export verified` / BUILD COMPLETE (support
archive c4dbe35986a5 as pinned).

## Filed from this lane

**FD-128** — the demo's next blocker, measured in both verification
captures: within a second of the window mapping, during the first
frames' image work, a WASM worker dies with `Uncaught RuntimeError:
operation does not support unaligned accesses` (boxedwine64.js);
guest dumps stop and no present is observed. Full account and
closing terms in its register entry.

## Notes for the merge lane

- The step-ring instrument (`include/bwstepdiag.h`, notes in
  syscall64's pipe/pipe2 handlers, `ksocketpairFds`, `ksocket`,
  `KProcess::allocFileDescriptor` / `getNextFileDescriptorHandle`) is
  retained deliberately: gated on BW_THREAD_DUMP=1, inert otherwise,
  and it is the instrument that convicted this defect. Steps 27–36
  are retired in the header (their call sites were removed).
- `web/tests/fd127-godot.mjs` is the lane's capture driver (the FD-70
  capture with a 600 s map wait and live console streaming).
- Register edits on this branch: FD-127's Section A entry is
  replaced by FD-128's filing; the Section B row for `lane-fd127fix`
  carries FD-127's closure text. Number/closure settlement at
  composition is the merge lane's, per standing practice.
- Evidence: `test-results/fd127/` in the worktree (build logs
  build-diag*.log / build-fix*.log, capture dirs diag1–diag6 with
  preserved console streams, CPU sampler outputs, suite logs
  js-unit-fix*.log / native-*-fix1.log).
