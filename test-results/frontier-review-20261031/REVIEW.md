# Frontier review — cleanup-campaign merge composition + handed-up design decisions (2026-10-31)

**What this is.** An independent review by Claude Code (box install, Opus, high
effort), commissioned under the project owner's standing instruction to use the
frontier agents for reviews and decisions. Claude was given an evidence pack
(per-branch commits/files measured against the live integration tip, pairwise
overlap lists, conflict-risk file lists, diffs for shared files, and the
deferred-items sections of every lane report) and asked for (1) a composition
plan for the pending branches and (2) a recommendation on every design decision
the campaign lanes explicitly handed upward. Claude's verbatim output is
preserved next to this file as `claude-raw.md`.

**Claude's word is advice, not proof.** Every load-bearing claim below was
spot-checked against the actual git state and code by the reviewing lane.
Verdicts are tagged: VERIFIED (checked true), CORRECTED (checked, with the
correction stated), EXECUTED (the merge lane already did it — checked in the
composed tree), STILL OPEN (actionable now).

**Timing.** The pack was measured against integ tip `ee526bb0c`. The merge lane
was working in parallel throughout the review and landed, in order:
`d47767ce3` cleanlog, `e3c59f072` cleanweb, `ee526bb0c` ksubsys (the pack tip),
then `5b4621721` kernelgate, `402050940` cleanlog32, `c278455fd` cleanmem,
`d818e4dcf` cleanfs, `4f52918db` clean-cpu. Still pending at record time:
lane-cleanup-bridges, lane-tooling-honesty, lane-docsclean, lane-alkhaser-int2d,
lane-firstframe, lane-mindustry, lane-firstvisit, lane-defect-register.

---

## Part 1 — Composition (Claude's plan, with spot-check verdicts)

Claude's recommended order was: tooling-honesty → kernelgate (cherry-pick only)
→ cleanlog32 → cleanmem → cleanfs → clean-cpu → alkhaser → [native gate] →
bridges → firstframe → [JS gate] → mindustry → docsclean last. The merge lane's
actual order differed (wave branches first, in its own sequence). Where the two
diverged, the composed tree was checked for the outcome Claude's order was
designed to protect:

1. **Kernelgate: Claude said do NOT merge whole — cherry-pick only the audit
   tool + record (`c6b1f0cbc`, `f6c5a74de`); its three kernel commits are
   superseded by ksubsys.** CORRECTED as to method, EXECUTED as to outcome:
   the merge lane merged the branch whole (`5b4621721`, all five commits in the
   second parent). Spot-check of the composed tree: `syscall_signalfd4` in
   integ answers `-K_EINVAL` ungated with exactly ONE `LogOnce` site per key
   (flags count 1, mask_size count 1) — i.e. ksubsys's behaviour won the
   conflict resolutions and no duplicate reporting survived. No corrective
   action needed. One residue is STILL OPEN: the audit's exemption list still
   contains the 11 panic-default entries (recover_gate_audit.py lines 36–46:
   ugetrlimit, clock_gettime, clock_gettime64, prlimit64, modify_ldt,
   clock_nanosleep, sigprocmask, sigtimedwait, socketcall, ipc, vm86) that
   Claude — and the kernelgate record itself — said to delete once the
   pathfinder landed. It has landed. Delete them and re-run the audit.
2. **`KMemory::logPageFault`'s `kpanic("pf")`: Claude said cleanlog32's removal
   must win over cleanmem's deliberate keep.** EXECUTED: base had the panic
   (kmemory.cpp:1135); cleanlog32's branch removes it; cleanmem's branch kept
   it (line 1194); the composed integ tree has count 0 — the removal won even
   though cleanmem merged after cleanlog32. VERIFIED in the composed tree.
3. **Line-ending rewrites: Claude flagged kernelgate's ksignal.cpp (+302/−289)
   and cleanlog32's cpu.cpp (+1620/−1580) as whole-file rewrites.** VERIFIED:
   with `-w` they collapse to +15/−2 and +82/−42. Base cpu.cpp was a CRLF file
   (1580 CR lines); integ's cpu.cpp now has 0 — the conversion was accepted by
   the cleanlog32 merge. Settled; expect blame churn on that one file, nothing
   more to do.
4. **cpu64.cpp hand-merge for clean-cpu × alkhaser: STILL OPEN and now
   imminent.** VERIFIED both sides rewrite the same line:
   alkhaser's diff changes `raiseSyncFault(sig, trapNo, 0, ipStart)` to
   `raiseSyncFault(sig, trapNo, siCode, ipStart, errorCode)`; clean-cpu (now in
   integ — `guest-insnfault` present, 2 hits) wraps that call in its logging
   funnel. Claude's combined form (keep the funnel's counting/log lines, pass
   alkhaser's `siCode`/`errorCode` through, optionally add `err=`/`code=` to
   the log line since the error code is what alkhaser fixed) is in
   `claude-raw.md` Part 1 (6–7). Also carry Claude's notes H1/H2 into the merge
   commit message: INT3 deliveries now emit a BW64-FAIL line (accept — matches
   the 32-bit convention), and the INT3 fallback now also calls
   `signalProcess(K_SIGTRAP)` where the old code only yielded (Linux-correct;
   Wine always installs a handler).
5. **testMain.cpp registrations: VERIFIED consistent.** Integ now shows
   testMemfdCreate ×3 (1 declaration + both TEST_ENTRIES arrays — cleanfs's
   dual registration, no duplicate), testFaultDelivery32 ×2, testOpcodeFault64
   ×2 (declaration + registration). Claude's orphan worry for alkhaser is
   settled: its new cases live inside the already-registered
   `testOpcodeFault64Delivery`. Claude's expected 32-bit count delta (+11)
   remains a gate-time check, not verified here.
6. **cleanfs's cherry-picked pathfinder commit (`860b1ef7e`): EXECUTED without
   incident** — it is in the cleanfs merge's second parent; the identical
   change merged cleanly against the pathfinder original. No action.
7. **Bridges → firstframe composition: STILL OPEN.** VERIFIED firstframe's
   numbers: vk64bridge.cpp +365/−18, web/runtime.html +1/−0 (exactly the one
   subgroup line). Claude's instructions stand: merge bridges first (its
   scanner change should be what measures firstframe's bridge code); on
   runtime.html keep integ's catch + firstframe's added line; do NOT
   hand-merge known-silent.json — regenerate baselines with the scanner on the
   merged tree; re-pin every vulkan.md citation against the merged
   vkwebgpu.mjs/vk64bridge.cpp (both branches shift lines in citation-audited
   files); regenerate the runtime-artifact manifest and run the completeness
   check for firstframe's rebuilt payloads.
8. **Tooling-honesty first: STILL OPEN.** It is still unmerged while the gates
   it makes honest (build script, conformance exit codes) are about to be
   relied on. Claude's reasoning stands — merge it before the JS gate so the
   gate's dist export is produced by the honest build script.
9. **Docsclean last + a follow-up index commit: STILL OPEN** (docsclean,
   mindustry, firstvisit, defect-register all pending). Claude's point stands
   and has grown: the index follow-up should now also cover the first-visit and
   defect-register reports.
10. **Pre-flight warning (main checkout):** Claude warned the main checkout
    shows tools/rootfs64 as unstaged deletions and merges must not run from
    there. Consistent with the pre-existing main-tree damage already on record;
    merges are running in the integ worktree. No new action.

## Part 2 — Design decisions handed upward (Claude's recommendations, verified where checkable)

- **D1 (ratify ksubsys's rule): errno answers Linux itself gives are never
  gated by the recovery switch; the switch gates only departures from Linux
  behaviour.** EXECUTED in the composed tree (signalfd4 verified ungated).
  Worth writing into the pattern doc as the standing rule.
- **D2 (futex, unmapped address, switch off → pre-existing kpanic): return
  -EFAULT with a BW64-FAIL line.** Not checked against code here; the audit's
  exemption list confirms a futex exemption exists (recover_gate_audit.py
  line 49). Kernel-lane follow-up, not a merge blocker.
- **D3 (logPageFault panic): cleanlog32 wins.** EXECUTED — see Part 1.2.
- **D4 (recover-gate audit): land it, strip the 11 exemptions, wire into a
  runner once at 0 violations.** Audit landed; exemption strip STILL OPEN
  (Part 1.1); runner wiring unverified — no evidence in the pack that any
  runner calls it.
- **D5 (seg_mapper/seg_access callers with throwException=false): audit the
  call sites.** Open follow-up; needs a grep + per-caller read.
- **D6 (SEG_* dump removal costs debugging capability): accept; optionally
  restore behind an env var (BW64_SEGDUMP).** The removal is already merged.
  Claude's H4 makes this testable: the dump path is his suspect for the
  4-worker exit-139 suite crash — run the 4-worker suite 3× at the native
  gate; the result decides whether a restore is even wanted.
- **D7 (sigInfo quirks): INTO-as-SIGSEGV is Linux-correct, leave it;
  prepareFpuException delivering via the SIGSEGV slot is a likely real
  deviation (Linux delivers SIGFPE) — small follow-up lane, after checking
  whether Wine's i386 handler recognises trapno 16/19.** Inference on Claude's
  part (from Linux knowledge, self-tagged); the Wine-handler evidence is the
  missing piece.
- **D8 (modify_ldt invalid-index panics): return -EINVAL with a log line.**
  Open; kernel-lane follow-up.
- **D9 (32-bit port-IO: 12 INS handlers + int99 panic on unregistered index —
  fill 0xFF or deliver a fault?): Claude recommends delivering #GP via
  CPU::prepareException, with error code (vector<<3)|2 for int99, mirroring
  the x64 INT fix.** VERIFIED mechanically sound: the 12 INS kpanics exist
  exactly as the CPU lane reported (ops.h:3709–3753, INSB/INSW/INSD ×4 each);
  the int99 panic is at ops.h:3339; and prepareException is a live mechanism
  in the 32-bit core (common_other.cpp EXCEPTION_BOUND, common_sse.cpp
  EXCEPTION_GP, cpu.cpp dispatch). One missing input Claude named: what
  syscall_ioperm/iopl currently return (decides whether 0xFF-fill could ever
  be the honest model). Coordinator decision recorded here: adopt #GP
  delivery; the iopl check is a pre-implementation grep, not a reason to
  reopen the choice.
- **D10 (normal_other.h recover-gate sites): close as pure actions.** Matches
  the kernelgate census. No action.
- **D11 (kernel-side raiseSyncFault callers log nothing): move the delivered=1
  logging inside raiseSyncFault as a post-merge follow-up.** Open.
- **D12 (_DEBUG decoder.cpp -Werror failure, pre-existing at baseline):
  separate one-line fix, don't relax -Werror.** Open, non-blocking.
- **D13 (fd-exhaustion EMFILE panic): dedicated lane when scheduled — a naive
  conversion trades the panic for null derefs across ~50 call sites; keep the
  allocSHM panic; MSG_NOSIGNAL follow-up for unix sendto.** Consistent with
  the ksubsys lane's own record. Open.
- **D14 (signalfd4 mask-size: ksubsys said "panicked", kernelgate said
  "silently accepted"): RESOLVED by spot-check.** The base code had BOTH: the
  syscall gate returned EINVAL only with the switch on (kernelgate's reading
  of the gate), and a `kpanic_fmt("syscall_signalfd4 unknown mask size")`
  sat deeper in the function (ksubsys's reading). Each lane described the
  half it was looking at. Behaviour is settled (ungated EINVAL in integ);
  the kernelgate record is the one to correct if either is annotated.
- **D15 (X11 callers dereferencing getRamPtr unchecked): schedule the audit
  for a graphics/X11 lane; a host null-deref still kills the emulator, the
  BW64-FAIL line just lands first.** Open (already on the coordinator's list
  from the cleanmem lane).
- **D16 (BString::split("") infinite loop): guard inside split itself, not
  only at callers.** Open; util layer, one line.
- **D17 (translateOpenError collapses errnos to EINVAL): widen the map now
  that the fs layer preserves errno.** Open; kernel lane, after the fs merge
  (which has landed).
- **D18 (bridges follow-ups): ⚠️ ONE PART SUPERSEDED.** Claude recommended
  deferring the gl64bridge fake answers (dropped texture settings, fabricated
  blank vertex data, packed pixel formats, dropped replay call) "until the
  new once-per-case lines show up in a demo-sweep log." The project owner has
  since ruled the opposite: those are OPEN DEFECTS queued as development work
  (defect register FD-15), and logging them was explicitly not fixing them.
  Do NOT follow Claude's deferral on this item. (Claude wrote before that
  ruling was in the material given to it.) His other D18 points stand: the
  marshal-generator item is closed by tooling-honesty; bridge-file splitting
  deferred; naga-rt no action.
- **D19 (firstframe's single monolithic commit): accept as-is; note in the
  merge commit that a revert removes all six fixes.** Stands.
- **D20 (tooling leftovers): re-run the 5 pre-existing legacy failures at the
  JS gate before assigning an owner (firstframe touches the same files and
  may change them); run build-jit-wasm.sh once end-to-end; leave the frozen
  probe scripts' absolute imports.** Open, gate-time.
- **D21 (docs): never drop stash@{0} on the old proof — export
  `git stash show -p` to a durable path first (matches the coordinator's
  existing warning); accept the 54 pre-existing broken links; move or
  index-link alkhaser's report out of test-results/.** Open.
- **D22 (mindustry resume): merge the record now; re-baseline on the
  post-wave integ before resuming — its measurements predate the wave's
  fault-delivery and logging changes.** Stands; the branch is still pending.
- **D23 (4-worker exit-139): decide after the H4 test at the native gate; if
  it survives cleanlog32, it gets its own lane (host-side harness crash =
  emulator bug, not "box load").** Open, gate-time.

## Part 3 — Claude inferences the coordinator should treat as unproven

- **"Several new x64 tests may never have executed"** (from suite-count
  arithmetic: cleanmem's 652 = 648+4 while one of its tests is x64-named).
  Plausible, not proven. Actionable form: run the x64-guest test flavour at
  the native gate and compare counts — do not quote the inference as fact.
- **H3 (FX #GP fault address ipStart vs rip equality)** — Claude self-tagged
  inference; confirm during the alkhaser hand-merge.
- **D7's SIGFPE deviation** — depends on Wine-handler evidence not yet
  gathered.

## Coordinator's still-open checklist (distilled)

1. Strip the 11 panic-default exemptions from recover_gate_audit.py; re-run
   the audit; then decide on runner wiring (D4).
2. Merge tooling-honesty before the JS gate (Part 1.8).
3. alkhaser: hand-merge cpu64.cpp per Part 1.4; carry H1/H2 notes in the
   merge commit.
4. Bridges → firstframe per Part 1.7 (scanner-regenerated baselines, citation
   re-pins, manifest regeneration).
5. Native gate: run BOTH guest flavours; run the 4-worker suite 3× (H4/D23);
   grep for anchored `CPU64: unimpl` consumers (H5).
6. Follow-up lanes from Part 2: D9 (port-IO #GP — decision made, implement),
   D2/D8/D11/D17 (kernel), D16 (util), D15 (X11 audit), D13 (fd-exhaustion,
   low priority), D7 (SIGFPE, evidence first), D12 (decoder _DEBUG).
7. Docsclean last, with the index follow-up extended to the first-visit and
   defect-register reports (Part 1.9); stash export before any drop (D21).
8. Mindustry: merge record; re-baseline before resuming (D22).
