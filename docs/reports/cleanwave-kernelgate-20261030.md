# Cleanup wave — kernel reporting-gate lane record (2026-10-30)

Branch `lane-cleanwave-kernelgate`, worktree `/home/ubuntu/wt-cleanwave-kernelgate`,
based on integration tip `538f03db5`. Follows the pathfinder standard in
`docs/failure-logging-and-recovery.md` (branch `lane-cleanlog`) and the
clean-up skill (found at `/root/.bb/skills/clean-up/SKILL.md`; the
`global-skills/<hash>` path in the lane brief no longer exists on the
box — that cache directory is empty).

## Mandate

Nowhere in the kernel may the mere *reporting* of a fact sit behind the
recovery switch (`bw64RecoverEnabled()`, off by default). Recovery
*actions* stay gated exactly where they were. Multi-part blocks that mix
reporting with actions are separated so the reporting always happens.

## Method

Census first, edits second. Every `bw64RecoverEnabled()` mention and
every `bw64RecoverLogOnce` call site in `source/` was read in context
(29 LogOnce call sites outside the helper's own file). Each site was
classified before any edit:

- **pure action** — the block performs a recovery action; no failure
  report is gated (the failure is reported on the unconditional
  fall-through path). Left alone.
- **loud pair** — `if (switch) LogOnce(...); else kwarn...` or the
  braced equivalent: the failure is reported in both modes; the switch
  selects wording/rate-limiting only. Left alone (the pattern record
  explicitly blesses this shape; verified per site, not assumed).
- **panic-default** — graceful branch behind the switch, `kpanic` as
  the default. All 11 such sites are exactly the set the pathfinder
  already converted on `lane-cleanlog` (commit `648241602`). Not
  duplicated here, so the two branches compose without conflicts.
- **gated reporting** — a fact reported only with the switch on.
  Fixed (below).

## Changed

1. `c7861481b` — `KProcess::ioctl` (`source/kernel/kprocess.cpp`).
   An unsupported ioctl returned `-K_ENOTTY` in both modes, but the
   report fired only with the switch on. The switch is removed from
   the condition; ENOTTY behaviour unchanged.
2. `e4ac8595a` + `a42386a55` — `syscall_signalfd4`
   (`source/kernel/ksignal.cpp`). With the switch off, an unsupported
   flag word or mask size was silently accepted — no line anywhere.
   Both checks are now loud pairs: the switch keeps controlling only
   the action (the EINVAL rejection); with it off, a `kwarn` names
   the offending value and the outcome (accepted). Switch-on
   behaviour and all success paths unchanged.
3. `c6b1f0cbc` — `tools/conformance/recover_gate_audit.py` (+ README
   section). Mechanical regression check for this invariant: a
   `bw64RecoverLogOnce` inside a switch-gated `if` fails the check
   unless the if/else reports in both branches or the (file, key)
   pair carries a written exemption in the script. Sabotage-proven:
   re-gating the ioctl report makes it fail, naming file, line and
   key; a synthetic fresh-key gated report fails while a loud pair
   and an ungated report pass. Current state of the tree: 27 gated
   reports tolerated with justification, 0 violations.

## Audited, deliberately unchanged

- `cpu64.cpp` (unimplemented-opcode skip) and
  `normal/normal_other.h` (32-bit invalid-opcode skip): pure recovery
  actions. The in-block `klog` reports the action taken; with the
  switch off the failure itself is reported on the fall-through path
  (the unconditional unimpl-opcode line in `cpu64.cpp`; SIGILL
  delivery in the 32-bit path — its own logging is the 32-bit
  fault-delivery lane's census item).
- Loud pairs, each verified in context: `KProcess::fcntl` default,
  `KNativeSocketObject::ioctl`, all six `knativesocket` sockopt
  single-line sites, both `knetlink` getsockopt sites.
- `knetlink` setsockopt (both sites): loud in both modes (off-mode
  `kwarn` with the value on the next statement). What the switch
  gates there is only the EINVAL return — an action — so it stays.
  Noted asymmetry: with the switch off these return success (0)
  where getsockopt returns EINVAL in both modes; changing a return
  value is an action change, outside this lane's mandate.
- `KThread::futex` entry check and dispatch tail: with the switch
  off, an unsupported futex op reaches the dispatch tail, which
  reports it (`kwarn` with the op value). The gate controls only the
  early ENOSYS reject. One behavioural note: with the switch off, an
  unsupported op with an unmapped address reaches the pre-existing
  `kpanic` on the futex address before the tail report; the panic is
  loud and pre-dates the recovery experiment — left as found.
- The 11 panic-default sites (ugetrlimit, clock_gettime,
  clock_gettime64, prlimit64, modify_ldt, clock_nanosleep,
  sigprocmask, sigtimedwait, socketcall, ipc, vm86): present in this
  tree in original form because this branch is based on the
  integration tip, which does not yet contain `lane-cleanlog`.
  Deliberately untouched — the pathfinder's commit converts exactly
  these, and duplicating the conversion guarantees a merge conflict.
  The audit script's exemption list names all 11 and says to remove
  the exemptions once that commit lands.
- `KProcess::itimerCheckWhich`: already the model shape (ungated
  once-per-key report + unconditional EINVAL). No change.
- `source/util/log.cpp` (the helpers): the pathfinder's commit owns
  the helper-level ungating of `bw64RecoverLogOnce`. Not touched.
- No `BW64_RECOVER` env reads exist outside the helper and the CPU
  self-tests; no other switch spelling gates reporting in the kernel.

## Verification

- Build: full `make -C project/linux test` completed (417 objects,
  test binary produced). One compile (testDevDspCapture.cpp) was
  killed by a signal during the first attempt, at peak box load with
  all wave lanes building; an incremental restart completed clean.
  Both changed translation units were also compiled standalone with
  the build's exact flags, clean, before the full build finished.
- Native suite (`Build/Test/boxedwine`, all 648 entries): the
  governing result is **648 passed / 0 failed**, from the
  single-worker run (`0 0 1`): "0 tests FAILED in 434s", exit 0,
  648 OK lines. Path there, for the record: two default 4-worker
  attempts died at exit 139 after 610 OK / 0 FAILED - the harness's
  documented death on the loaded box (the pathfinder recorded the
  same signature at 611 passing, 0 failures); a first single-worker
  attempt printed the full 648 OK and "0 tests FAILED in 362s" but
  exited 1 amid a flood of host-exception klog lines under peak
  load; the identical command on a quieter box exited 0.
- JS unit suite (`web/tests/suite/runner.mjs --unit`): 264 passed /
  69 failed, run twice - with and without `BW_TEST_DIST` borrowed
  from another lane's tree; the failure lists are identical. All 69
  failures are tests that inspect an in-tree built export or staged
  runtime payloads (the graphics bridges, shader translation,
  export closure, runtime completeness, provenance): this worktree
  never produced a dist export, the documented fresh-worktree
  condition - lanes whose trees carry an export report 331-334
  passes with the same suite, and the pathfinder's record carries
  the same class of tree-dependent failures. This lane changes no
  JS or web file; kernel behaviour is governed by the native suite
  above.
- The recover-gate audit passes on the final tree (27 gated reports
  tolerated with justification, 0 violations) and is
  sabotage-proven in both directions (see Changed, item 3).

## Composition note for the coordinator

This branch is based on the integration tip and does not contain
`lane-cleanlog`. The two compose cleanly: the pathfinder ungates the
`bw64RecoverLogOnce` helper itself (so reports print in the default
configuration) and converts the 11 panic-default sites; this lane
ungates/reshapes the remaining call sites and adds the audit. After
both land, the audit's 11 panic-default exemptions should be deleted
(the sites will no longer be gated at all).

## Merge-composition flags (please read before merging)

- **signalfd4 was treated by two lanes.** `lane-cleanlog-ksubsys`
  (complete) also changed `syscall_signalfd4`: it ungated the
  validation itself, so bad flags/mask sizes now return EINVAL in
  both modes there. My commits keep the EINVAL behind the switch
  and ungate only the reporting, per this lane's mandate ("actions
  stay gated"). The two treatments edit the same lines and cannot
  both apply - pick one. If theirs lands, drop my `e4ac8595a` and
  `a42386a55`; the recover-gate audit still passes (their form has
  no gated report at all). One factual discrepancy worth a second
  look either way: their record says bad mask sizes "panicked";
  in the base code I read, the switch-off path silently *accepted*
  bad flags and mask sizes - I found no panic on that path in
  ksignal.cpp.
- **knetlink setsockopt:** my "asymmetry left alone" note above is
  superseded - `lane-cleanlog-ksubsys` fixed the fall-through to
  success and the sockopt-length panics in knetlink/knativesocket.
  I edited neither file; nothing to reconcile on my side.
- `lane-cleanlog-ksubsys` also edited `kprocess.cpp` (execve) -
  different function from my ioctl edit; expect a clean merge.
