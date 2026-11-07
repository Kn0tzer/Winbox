# Frontier review D2/D7/D8 — futex, FPE delivery, modify_ldt: fix verified and closed out

Branch `lane-d278`, base integ `faf95d26c`. Fix commit
`f998e2e01` ("D2/D7/D8: host-measured futex, modify_ldt and
FPE-delivery answers"). The lane that wrote the fix ended
without filing its closeout; this record is the follow-up
closeout: every claim in the fix commit's message was
re-verified from the preserved evidence and from the code
diff before the register was updated. No product code
changed in the closeout.

## The three items and the fix

- **D8 = FD-09 (`modify_ldt` panics on invalid indexes).**
  The host's answers, measured with raw `modify_ldt`
  syscalls: a byte count other than one descriptor is
  -EINVAL (checked first), an unreadable descriptor
  pointer is -EFAULT, an out-of-range index is -EINVAL.
  The guest returned the first two shapes only by falling
  into the third's kernel panic. `KThread::modify_ldt`
  now answers the measured matrix, and each rejection
  logs one counted `BW64-FAIL syscall.modify_ldt` line
  (throttled to the first 16 and powers of two after).
- **D2 (futex on an unmapped word) — filed as FD-193 in
  this closeout.** On the 32-bit side a shared operation
  on an unmapped word hit the "Could not find futex
  address" kernel panic, and a private WAIT proved
  neither the word nor the timeout pointer before
  parking — it read the null page and parked on whatever
  it found there. On the 64-bit side the page lookup
  materialized a fresh zero page for an unmapped word,
  so operations silently worked on a phantom word. The
  host's measured matrix: -EFAULT for every operation on
  an unmapped word except a 64-bit private WAKE /
  WAKE_BITSET, which wakes nothing and returns 0; a
  bitset operation with a zero mask is -EINVAL. The
  guest now answers the same; the 32-bit rejections log
  counted `BW64-FAIL syscall.futex` lines, and the
  bitset zero-mask rule the 64-bit side already had is
  enforced on the 32-bit side too.
- **D7 (FPE delivery) — filed as FD-194 in this
  closeout.** A divide quotient overflow delivered
  SIGFPE with FPE_INTOVF and trap 4; the host delivers
  FPE_INTDIV and trap 0, exactly as for divide-by-zero
  (the INTOVF / trap-4 pair belongs to INTO, whose host
  delivery is SIGSEGV and is unchanged). And
  `CPU::prepareFpuException` delivered x87 exceptions
  through the SIGSEGV slot; the host delivers SIGFPE
  with trap 16, and the guest's handler classifies traps
  16/19 only inside its SIGFPE handler, so the old slot
  could never produce the right Windows exception. Both
  sites now match the host measurements (raw signals on
  real x86-64 and aarch64 kernels).

Six new native tests pin the matrices:
`testFutexUnmapped32` and `testFutexSharedUnmapped32`
(main suite 614, 615), `testModifyLdtInvalid32` and
`testModifyLdtIndex32` (616, 617), `testFpeDelivery32`
(618), and `testFutexUnmapped64` (x64 suite 6).

## Verification (closeout lane, from the preserved logs)

Evidence read directly from `test-results/d278/` in the
lane's working copy — not from any summary:

- **Red (base `faf95d26c`).** All six tests fail.
  614 dies in a guest page fault at the unmapped address
  (`red-main-614.log`); 615 prints the filed panic text
  "Could not find futex address: 50000000"
  (`red-main-615.log`); 616 faults and reports
  "modify_ldt write with count 8 did not return -EINVAL"
  (`red-main-616.log`); 617 prints the filed panic text
  "syscall_modify_ldt invalid index: 8192"
  (`red-main-617.log`); 618 shows both wrong deliveries
  verbatim — "idiv overflow delivered sig=8 code=2
  trap=4 … expected SIGFPE/FPE_INTDIV/trap 0" and
  "prepareFpuException delivered … segv slot sig=8;
  expected SIGFPE/FPE_FLTDIV/trap 16 in the SIGFPE slot
  only" (`red-main-618.log`); x64 6 reports the private
  WAIT and shared WAKE on an unmapped address "did not
  return -EFAULT" (`red-x64-6.log`). The neighbouring
  control test 613 (port IO / int 0x99 delivery) passes
  on the base (`red-control-portio.log`), so the harness
  itself is not the failure.
- **Green (branch).** All six tests pass, each log
  ending "0 tests FAILED" (`green-main-614.log` …
  `green-main-618.log`, `green-x64-6.log`), and the
  futex / modify_ldt green logs show the counted
  `BW64-FAIL syscall.*` rejection lines the fix adds.
- **The diff itself** (`cpu.cpp`, `kthread.cpp`) was
  read in full and matches the commit message's
  description site for site, including the two
  deliberate non-changes (INTO delivery; the read form
  of `modify_ldt` keeping its entry-shaped contract).
- **One caveat, stated plainly:** the preserved
  `full-main-green.log` is truncated — it stops mid-list
  at 530 OK lines of the announced 697 with no summary
  line, so it does not by itself prove a full-suite
  green. The full-suite verdict for this branch rests on
  the checking-queue run below, not on that log.

## Register changes (this closeout)

- FD-09 carries the FIXED marker naming `f998e2e01` and
  an update paragraph with the measured matrix.
- The D2 and D7 defects, never previously filed, are
  filed as FD-193 and FD-194 and marked FIXED in the
  same pass, each naming the fixing commit and its
  tests. Both numbers are lane-local: FD-193 is also
  claimed on `lane-fd176` for an unrelated filing and
  FD-194 is taken by the PlugPlay filing in the batch-10
  settlement; the composition audit settles both.
- Section B gains the `lane-d278` row; Section C gains
  CL-134 (FD-09), CL-135 (FD-193), CL-136 (FD-194), all
  lane-local and marked as such (CL-134 is also claimed
  on the batch-10 branches).

## Checks

Submitted through the checking queue
(`winbox-project/checks/submit-check lane-d278`) three
times; the queue verdicts are still owed, for reasons
outside the branch:

- The first submission (tip `4b0c846c2`) died before any
  pipeline existed: its preparation clone raced another
  lane's submission inside the shared scratch clone and
  the checkout failed on objects that are in fact
  present in the repository (verified directly
  afterwards).
- The second submission created pipeline #27 on the
  exact tip. Its verify-source step passed — mounted
  commit `4b0c846c2`, read-only mount enforced, tree
  pristine — but during the next step another lane's
  submission replaced the shared source tree, and the
  prepare-workspace log records the tree vanishing file
  by file (`checks/evidence/pipeline-27/queue-logs/
  209-prepare-workspace.log`). The pipeline produced no
  check verdicts: the remaining step logs are empty and
  its verdict step reports every result file missing.
- The third submission was stood down before firing:
  while it waited for a clear field, the box's free
  disk fell to 5.6–5.7G — below the 6G floor this
  closeout works under — with other lanes' pipelines
  still running and consuming.

So the light-check verdicts for this branch (citations,
JS unit, JS legacy, the full native suites, run-native)
remain owed to a rerun once the disk recovers and the
submission contention clears. The branch's proof in the
meantime is the preserved native evidence verified
above: the six new tests red on the base and green on
the fix, with the neighbouring control test green on
both — the same tests the queue's native suites run
(main 614–618 and x64 6, inside the suites' 697 / 117
totals at this tip).

## Environment notes

- The closeout ran while the box's disk sat at roughly
  7–12G free after the 2026-11-06 disk emergency; no new
  stores or scratch were created, and the working copy's
  generated state was stripped at the end (test-results
  evidence stays).
- All heavy work ran through fresh SSH sessions, which
  inherit the SSH listener's cores 0–2 pin; the Minecraft
  server's core 3 was not touched.
