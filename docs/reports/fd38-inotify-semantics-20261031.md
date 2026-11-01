# FD-38 — inotify semantics fixed and pinned; delivery gate stays closed (enablement park isolated)

Lane: `lane-fd38`, off integ `51d0aff98`, 2026-10-31.

## Outcome in one paragraph

The defect register's "one semantics defect was never isolated" is
isolated and fixed: seven distinct semantics defects in the inotify
machinery, each measured against the host kernel first and now pinned
by native tests. Real delivery nevertheless stays disabled behind the
loud ENOSYS refusal, because the consumer (the Chromium site session)
still parks at startup with delivery enabled — and this lane's
bisection proves the park is caused by enablement itself, not by any
event semantics: no event is lost (the watched directories generate
none during the boot), and the same park reproduces with the pristine
machinery the moment init succeeds. FD-38 stays open, narrowed to
that enablement mechanism.

## Host-kernel reference semantics (measured, /tmp/fd38ref during the lane)

- Plain re-add of a watch **replaces** the mask; `IN_MASK_ADD` merges;
  `IN_MASK_CREATE` on an existing watch returns EEXIST.
- `rm_watch` leaves already-queued events ahead of `IN_IGNORED`; a
  second `rm_watch` of the same wd returns EINVAL.
- Unlink of a watched file: `IN_ATTRIB`, then `IN_DELETE_SELF`, then
  `IN_IGNORED`.
- Rename of a watched file: exactly one `IN_MOVE_SELF`; the watch
  follows the file (later writes deliver on the same wd).
- Two consecutive chmods coalesce to one `IN_ATTRIB`.
- `mkdir` in a watched directory: `IN_CREATE|IN_ISDIR`.
- Name field `len` = (strlen+1) rounded up to a multiple of 16
  (1–15-char names → 16; 16–25 → 32).
- Directory self events: `IN_ATTRIB` carries `IN_ISDIR`;
  `IN_DELETE_SELF` and `IN_MOVE_SELF` do not.

## Defects found and fixed

1. **Rename produced a phantom delete+create pair and killed watches
   on the renamed node.** `FsFileNode::rename` detached via
   `removeNodeFromParent()` (IN_DELETE to parent watchers plus
   IN_ATTRIB/IN_DELETE_SELF/**IN_IGNORED + watch destruction** on the
   node itself) and re-attached via `addChild()` (phantom IN_CREATE),
   around the correct MOVED_FROM/TO pair. Fix: `removeNodeFromParent`
   and `addChild` take a `notify` flag; rename passes `false`; the
   only rename notifications come from `KInotify::notifyRenamed`.
   (fsnode.h/fsnode.cpp, fsfilenode.cpp)
2. **Name field padded to a multiple of 4**, not 16 — a reader walking
   records with the kernel's layout desynchronizes on any named event
   whose padded length differs. (kinotify.cpp readNative)
3. **Re-add merged the mask unconditionally**; Linux replaces it
   unless IN_MASK_ADD. A watcher that narrowed its mask kept
   receiving the old kinds. (kinotify.cpp addWatch)
4. **Unlink ordering**: IN_DELETE_SELF was swept before the link-count
   IN_ATTRIB; Linux reports IN_ATTRIB first. (kinotify.cpp
   notifyNodeRemoved)
5. **IN_ISDIR on directory self events** was set for every self type;
   Linux omits it on IN_DELETE_SELF and IN_MOVE_SELF. (kinotify.cpp
   sweep)
6. **Guest utimensat generated no IN_ATTRIB anywhere**:
   `FsFileNode::setTimes` now calls `KInotify::notifyAttrib` on
   success. (fsfilenode.cpp)
7. **IN_DONT_FOLLOW ignored** in `inotify_add_watch`: the path was
   always resolved with link following. (kprocess.cpp)

Also corrected: the kinotify.cpp header's stale claim that load-time
IN_CREATE suppression does not exist (it does, via
`childrenLoadInProgress`); the two documented non-deliveries
(IN_ACCESS, mmap-write IN_MODIFY) are registered as FD-59.

## Test evidence

- New/changed pins in `testInotifyEvents` (x64 syscall level) and
  `testInotifyEventsProcessApi` (KProcess level), plus the dispatch
  guard `testInotify64Syscalls`:
  - the file-watch section (rename → exactly one IN_MOVE_SELF, watch
    follows the file; unlink → [IN_ATTRIB, IN_DELETE_SELF,
    IN_IGNORED]) was **observed red before fix 1** (three failures:
    no single MOVE_SELF, watch did not follow, wrong unlink sequence)
    and green after.
  - the mask-replace, name-padding (len == 16), phantom-pair, and
    dir-self ISDIR pins are anchored to the host reference runs
    above; they were not each observed red individually — stated
    plainly.
- Because the guest-visible gate stays closed (below), the two
  delivery tests construct their `KInotify` instances directly
  (exactly as `inotify_init1` builds them), register them, and drive
  every watch/read through the real syscall and KProcess paths; both
  also pin that `inotify_init1` currently returns `-K_ENOSYS`. The
  dispatch guard pins ENOSYS for `inotify_init`/`inotify_init1`
  through the real x64 dispatch.
- Native suites on the final tree (full serial, the gate mode):
  x64 **61/61**, main **661/661**.
- JS suite: **342/342** (the fs-dynamic-link-cache slice harness
  needed its `addChild` stub updated to the new signature; its
  assertions are unchanged).
- Dist: "Browser export verified"; completeness 0 failures / 1 skip;
  dependency archive `de592964f772`, identical to the integ gates.

## The consumer investigation (site rig, Chromium demo)

Rig: `site-driver.mjs` from the 2026-10-30 investigation, fresh
profile per run, serving only the lane's dist. Runs preserved under
`test-results/fd38/` on this branch.

| Build | Result |
|---|---|
| Full semantics fix, delivery enabled | **park** — guest log frozen at 12,216 bytes (original signature: 12,217), outcome timeout |
| Full fix + INOTIFYTRACE instrumentation | **park** — evidence below |
| Control: pristine base `51d0aff98`, refusal intact | **pass** — window mapped at 40 s, `window-mapped+frame` |
| B1: enablement only (pristine machinery + working init) | **park** — log frozen at the same point (12,785 bytes incl. trace lines) |
| B2: B1 + instances forced non-blocking | **park** — log frozen at 12,743 bytes incl. trace lines |
| Final branch state (refusal restored) | **pass** — see below |

Traced-run findings (78 trace lines, preserved):

- The only inotify user is the guest wineserver (pid 16): blocking
  instances, one per request batch — matching Wine 8's server, which
  keeps one global inotify fd and releases it when its change list
  empties (server/change.c, read at the wine-8.0 tag during the lane).
- Instance 2's watch on `C:\windows` was still pending at the freeze;
  **only two events were queued in the entire boot, both rm_watch
  IN_IGNOREDs**, and the one the server read came back correctly.
  Zero real events existed to lose: after the watches were placed, no
  watched directory mutated (the prefix is pre-baked; C:\windows never
  churns during this boot, unlike a real Wine install).
- The server was not wedged in a read: after its last inotify read it
  kept opening fds and servicing file activity; guest threads kept
  spawning. No BLOCKING-WAIT line ever printed.
- Poll readiness is correct (`isReadReady` = queue non-empty;
  `waitForEvents` parents the condition) — verified in source.

Wine-source facts that bound the mechanism: with inotify available
the server completes a directory wait only when a change record lands
for that directory; under the refusal it arms dnotify instead
(`fcntl F_NOTIFY`, which this emulator does not implement — the
"unknown command: 1026" lines in working logs) and the same waits pend
with no possible completion either. The pending C:\windows wait is a
long-lived background watch that pends identically in the passing
configuration, so it is not the stall. The stall therefore sits in how
the wineserver's possession of a working inotify fd (its lifecycle,
/proc/self/fd watch placement, presence in the server's fd space)
interacts with the emulator's process/fd machinery — outside event
delivery. Root-causing that is the narrowed FD-38 remainder; it was
not finished in this lane and is not disguised as a semantics issue.

## Gate decision

`KProcess::inotify_init1` keeps returning `-K_ENOSYS`, with the
comment rewritten to the current truth: semantics fixed and pinned;
the gate is held by the enablement park; do not reopen without the
site-rig consumer proof passing. The shipped product behavior is
unchanged (guests fall back to polling), and the final branch dist
was re-proven on the rig: `window-mapped+frame` (mapped at 49 s,
frame 12 s later; run under `test-results/fd38/site-chromium-final2/`).
A first final-dist run under load average ~6 mapped at 58 s but
presented no frame inside the wait window (`site-chromium-final/`);
the rerun on the quieter box presented the frame, matching the
pristine control.

## Manifest re-pin (lane-local, for the merge lane)

`tools/browser/runtime-assets.json` and
`docs/known-gaps/runtime-artifact-manifest.md`: wine64.zip sha
re-pinned `8ea33c43…` → `d8284ce9…`. The byte-exact pinned archive no
longer exists anywhere on the box; the available archive is
member-identical (1,257 members; the single content difference vs the
live base is the FD-40 win32u.dll stamp) with a different zip
envelope. Content-neutral; every recent gate's dependency archive
still computes to `de592964f772`.

## Register actions on this branch

- FD-38: rewritten in place — semantics portion fixed (this record),
  entry stays open narrowed to the enablement park.
- FD-58 (filed provisionally as FD-55; renumbered at merge): `KProcess::chmod` returns success without
  changing anything; 32-bit fchmod logs IGNORED.
- FD-59 (filed provisionally as FD-56; renumbered at merge): IN_ACCESS and mmap-write IN_MODIFY are
  never generated (documented non-deliveries).
- FD-60 (filed provisionally as FD-57; renumbered at merge): `KProcess::link` copies content instead
  of creating a hard link.
