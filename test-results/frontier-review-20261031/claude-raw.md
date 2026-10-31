# Merge review: pending cleanup-wave branches vs integration tip `ee526bb0c`

**How far this is verified.** Everything below comes from the evidence pack. I tried to check the riskiest points against the repo with git, but the read-only git commands were blocked on permission, so none of this has been confirmed against the actual trees. Each claim is tagged:
- **[E]**: shown directly in the pack (commit, diff, cherry line, report section).
- **[I]**: my inference from that evidence.

The commands that would settle each inference are listed at the end.

---

## Bottom line

1. **Do not merge `lane-cleanwave-kernelgate` whole.** All three of its kernel commits are superseded by `lane-cleanlog-ksubsys`, which is already in integ. Take only the audit tool and its record (`c6b1f0cbc`, `f6c5a74de`).
2. **There is one real design conflict.** `lane-cleanlog32` removes `kpanic("pf")` in `KMemory::logPageFault`, and `lane-cleanmem` deliberately keeps it. **cleanlog32 should win.**
3. **`cpu64.cpp` needs a hand-written merge.** `lane-clean-cpu` and `lane-alkhaser-int2d` both rewrite the same `raiseSyncFault` line inside `faultAtInstruction`. The combined version is given below.
4. **Some regressions won't show as textual conflicts.** These must be recomputed after merging, not hand-merged:
   - `known-silent.json` baselines (bridges lowered them on a tree without firstframe's +365 lines in `vk64bridge.cpp`).
   - The `vulkan.md` line citations (both bridges and firstframe re-pin them).
   - The test-registration count in `testMain.cpp`.
5. **Run the x64-guest test build at least once.** The reported suite counts suggest the lanes only ran the 32-bit build, so several new x64 tests may never have executed.

---

## Part 1: Composition

### Pre-flight

- **Use a clean worktree off `ee526bb0c`, not the main checkout.** The main checkout (on `master`) has all of `tools/rootfs64/` showing as unstaged deletions (session git status [E]). `lane-docsclean` and `lane-firstframe` both edit files there, so a stray `commit -a` from that tree would be destructive.
- **Check for whole-file line-ending rewrites:**
  - `lane-cleanwave-kernelgate`'s `ksignal.cpp` diff is +302/−289, and it removes and re-adds the identical copyright header line by line [E]. That is the signature of a line-ending or whitespace rewrite of the whole file [I].
  - `lane-cleanlog32`'s `cpu/common/cpu.cpp` at +1620/−1580 looks the same [I].
  - For the ksignal case, the recommendation below drops those commits, so it goes away.
  - For cpu.cpp: integ hasn't touched the file, so it merges cleanly, but it would wreck blame and make later merges on cpu.cpp conflict. Re-normalise to the file's original line endings before merging, or confirm with `git diff -w --stat`.

### Recommended order

| # | Branch | How | Conflicts to resolve |
|---|---|---|---|
| 1 | `lane-tooling-honesty` | merge | none [E] |
| 2 | `lane-cleanwave-kernelgate` | **cherry-pick `c6b1f0cbc` and `f6c5a74de` only** | none after the drop |
| 3 | `lane-cleanlog32` | merge | `kthread.cpp` vs integ, `testMain.cpp` |
| 4 | `lane-cleanmem` | merge | `kmemory.cpp` (vs cleanlog32), `testMain.cpp` |
| 5 | `lane-cleanfs` | merge | `log.cpp` (already in integ), `testMain.cpp` |
| 6 | `lane-clean-cpu` | merge | `cpu64.cpp` vs integ (expected clean) |
| 7 | `lane-alkhaser-int2d` | merge | `cpu64.cpp` (vs clean-cpu): hand-written merge |
| — | **Native gate** | | build both guest flavours; serial and 4-worker suite; audits |
| 8 | `lane-cleanup-bridges` | merge | `bridge-recovery.mjs`, `known-silent.json` vs integ (cleanweb) |
| 9 | `lane-firstframe` | merge | `runtime.html` vs integ; `vulkan.md` and `vkwebgpu.mjs` vs bridges |
| — | **JS gate** | | dist export; JS unit and legacy suites; scanner; citation audit; runtime completeness |
| 10 | `lane-mindustry` | merge | none (record only) |
| 11 | `lane-docsclean` | merge last | none [E]; then a follow-up index commit |

**Why this order:**

- **tooling-honesty first.** It makes the later checks trustworthy: `bw64-build.sh` stops printing BUILD COMPLETE for a failed export (`f92549354`), builds verify the exported dist (`366b1a227`), and d3d-conformance exits nonzero on failure (`56d091d83`) [E]. It also closes bridges follow-up #1 (`3c2f790bb`).
- **Native lanes are grouped together.** One build and one suite run then covers all the kernel and CPU interactions.
- **cleanlog32 before cleanmem.** cleanlog32's version of the overlapping line is the one that should win, so the conflict surfaces in a single, predictable place.
- **clean-cpu before alkhaser.** alkhaser's change is smaller and purely functional, so it is easier to re-apply on top of clean-cpu's logging than the other way round. The end code is the same either way [I].
- **bridges before firstframe.** Bridges' scanner change (`5c5c1bcf8`, which recognises `vk64Unsupported` as a log call) should be the scanner that measures firstframe's new `vk64bridge.cpp` code.
- **docsclean last.** `WINBOX-STATUS.md` and `game-compatibility.md` should describe the tree after the code lanes land, and every lane adds a `docs/reports/*.md` that the rewritten index doesn't list.

### Per-overlap resolutions

**(2) kernelgate vs integ (`kprocess.cpp`, `ksignal.cpp`): integ wins on both files.**
- **`e4ac8595a` and `a42386a55` (signalfd4):** kernelgate's own merge flag says "If theirs lands, drop my `e4ac8595a` and `a42386a55`" [E]. Theirs (`lane-cleanlog-ksubsys`) has landed.
- **`c7861481b` (ioctl):** the ksubsys report says `282060c4e` "kprocess: … ioctl reporting ungated", with the reporting moved to the single dispatch point in `KProcess::ioctl` [E]. That is the same fix on the same line.
  - Git cherry shows `+` for `c7861481b` [E], meaning the patches aren't textually identical (probably different comment wording) [I]. Merging it would cause a conflict and gain nothing.
- **After cherry-picking:**
  - Edit `recover_gate_audit.py`'s exemption list to remove the 11 panic-default exemptions. The report says to remove them "once that commit lands" (pathfinder `648241602`, now in integ via `lane-cleanlog`) [E].
  - Re-run the audit and expect 0 violations. The "27 tolerated" count will change [I].
  - Add a line to `f6c5a74de`'s record saying commits 1–2 were superseded by `282060c4e` / `4cf569006`.

**(3) cleanlog32 vs integ (`kthread.cpp`).** I can't see integ's `kthread.cpp` diff since `538f03db5`.
- **Rule:** cleanlog32 wins inside `signalTrap`, `signalIllegalInstruction`, `seg_mapper` and `seg_access` (its lane owns 32-bit fault delivery). Integ wins everywhere else.
- **What integ probably changed:** the pathfinder's syscall-layer conversions, or futex. The kernelgate report lists the 11 pathfinder sites, and cleanlog32 says the KThread-object `modify_ldt` panics were left alone [E/I].
- **If integ also touched `seg_mapper`, stop and compare by hand.**

**(4) cleanmem vs cleanlog32 (`kmemory.cpp`).** The two hunks are next to each other: cleanlog32 replaces `kpanic("pf");`, and cleanmem's hunk starts on the next lines, turning `performOnMemory` into a `bool` function and keeping `kpanic("pf")` as context [E]. Expect a conflict.
- **Resolution:** keep cleanlog32's comment in place of the kpanic, and keep cleanmem's entire `performOnMemory` change. The reasoning is decision D3 below.
- **Check:** the merged file must not bring back `kpanic("pf")`. If it does, cleanlog32's `testFaultDelivery32` will die; its sabotage proof shows exactly that failure [E].

**(3–5) `testMain.cpp`, a three-way union plus integ's own additions.** All the edits are additive [E].
- **Keep the `#ifndef BOXEDWINE_GUEST_X64` grouping:**
  - `testFaultDelivery32` and the four `testMemory*32` declarations go inside it.
  - `testMemoryMadviseLayer64` goes in the x64 block.
  - cleanfs registers its six entries in **both** `TEST_ENTRIES` arrays [E].
- **Check for duplicates:** cleanfs registers the "orphaned" `testMemfdCreate` (`102433f4c`). Confirm that integ (cleanlog or ksubsys) didn't also register it.
- **Expected 32-bit-flavour count:** integ's current count + 1 (cleanlog32) + 4 (cleanmem) + 6 (cleanfs) = **+11**.
  - This works because the reported suite is the 32-bit flavour: 648 → 649 for cleanlog32, whose entry is in the second array, and 652 = 648 + 4 for cleanmem [E].
  - That also implies cleanmem's `testMemoryMadviseLayer64`, cleanfs's x64-array entries and alkhaser's `testOpcodeFault64` cases were probably never run by their lanes [I]. **Build and run the x64-guest test flavour at the native gate.**
- **alkhaser adds 78 lines to `testOpcodeFault64.cpp` but doesn't touch `testMain.cpp`** [E]. Confirm the new cases are called from an already-registered entry. Otherwise they are orphaned, the same pattern cleanfs fixed.

**(5) cleanfs vs integ (`source/util/log.cpp`): take integ.** Git cherry marks `860b1ef7e` as `-`, meaning an equivalent patch is already in integ (pathfinder's `bw64RecoverLogOnce` ungating) [E]. Any conflict is between two identical intents.

**(6–7) clean-cpu × alkhaser (`cpu64.cpp`, `faultAtInstruction`).** Both rewrite `if (raiseSyncFault(sig, trapNo, 0, ipStart)) return 0;` [E]. The combined version:

```cpp
auto faultAtInstruction = [&](U32 sig, U32 trapNo, U32 errorCode = 0, S32 siCode = 0) -> U32 {
    /* BW64_UNIMPLDUMP block unchanged */
    static std::atomic<U64> insnFaultCount{0};
    const U64 faultCount = insnFaultCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const U32 faultPid = (thread && thread->process) ? thread->process->id : 0;
    if (raiseSyncFault(sig, trapNo, siCode, ipStart, errorCode)) {
        if (faultCount <= 64 || faultCount % 1024 == 0)
            klog_fmt("BW64-FAIL guest-insnfault pid=%u sig=%u trap=%u err=0x%x code=%d rip=0x%llx delivered=1 count=%llu", ...);
        return 0;
    }
    if (thread && thread->process) thread->process->signalProcess(sig);
    klog_fmt("BW64-FAIL guest-insnfault pid=%u sig=%u trap=%u err=0x%x code=%d rip=0x%llx delivered=0 count=%llu", ...);
    yield = true;
    return 0;
};
```

- Everything else from both branches applies as-is: alkhaser's `0xCD` and `0xCC` hunks, and clean-cpu's FX #GP, unimpl-opcode and JIT-instantiate hunks touch separate lines [E].
- Adding `err=` and `code=` to the log line is my suggestion, not either lane's. alkhaser's whole fix is that the error code was wrong, so it belongs in the line.
- I expect clean-cpu's conflict with integ to be clean: the pathfinder's edits are in `deliverGuestMemoryFault` / `validateInstructionFetch` (clean-cpu report) [E]. alkhaser's base is a different one (`692875bd8`), so confirm that integ didn't change `raiseSyncFault`'s signature in `syscall64.cpp` since then.

**(8) bridges vs integ (cleanweb).**
- **`bridge-recovery.mjs`:** bridges' change is a comment inside an empty catch [E]. If cleanweb already changed that line, take integ. Otherwise take bridges.
- **`known-silent.json`:** don't hand-merge it. Take the lower value per key from each side, then **regenerate with the scanner on the merged tree** and make the known-silent test pass. Bridges also adds the missing trailing newline [E], which collides with any cleanweb edit near the end of the file [I].

**(9) firstframe.**
- **`runtime.html`:** keep both sides. Firstframe adds one line (`window.bwVkSubgroups = …`) right above the adapter probe's `.catch(() => { /* flags stay unset … */ })` [E]. cleanweb is the likely editor of that catch [I]. Keep integ's catch plus firstframe's line.
- **`vkwebgpu.mjs`:** the hunks don't overlap. Bridges edits around lines 406 and 3778; firstframe around 645, 1203 and 1300 [E]. Expect a clean merge.
- **`docs/known-gaps/vulkan.md`:** use firstframe's content (it carries the substantive state change, +57/−56) and **re-pin every citation against the merged `vkwebgpu.mjs` and `vk64bridge.cpp`**. Bridges' `cb2db825a` "re-pin[s] citations" for its own line shifts, firstframe shifts lines in the same files, and the register is citation-audited (docsclean report) [E/I]. Whichever hunk git keeps, some citations will be stale.
- **Generated and binary artifacts:** regenerate `runtime-artifact-manifest.md` with `tools/rootfs64/gen_runtime_manifest.py` (it is machine-generated, per the docsclean report [E]) and run `check_runtime_completeness.py`. firstframe ships rebuilt `libvulkan.so.1` ×2 and the vkd3d probe DLLs [E].

### Interactions that won't appear as conflicts

- **H1. INT3 now logs a BW64-FAIL line every time.** With alkhaser, INT3 goes through `faultAtInstruction` (`066fd18ec`), so after composing, every delivered breakpoint emits a `guest-insnfault` line.
  - Anti-debug probes like Al-Khaser and assert paths will use up the shared 64-line budget quickly [I].
  - Real failures stay visible, because `delivered=0` always logs [E].
  - Tagging delivered faults with BW64-FAIL matches cleanlog32's 32-bit `guest-exception32 … delivered=1` [E], so I'd keep the convention. Accept it.
- **H2. alkhaser changes behaviour for INT3 with no handler, which its comment doesn't say.** The comment says "the fallback below still yields … as before" [E]. But the fallback now also calls `signalProcess(K_SIGTRAP)`, which the old code did not [E].
  - With the default SIGTRAP disposition, a handler-less 64-bit guest that executes INT3 now dies instead of continuing [I]. That is Linux-correct, and under Wine a handler is always installed. Worth one line in the merge commit.
- **H3. FX #GP sites now report a different address.** clean-cpu's sites switch from `raiseSyncFault(..., rip)` to `faultAtInstruction`, which uses `ipStart` [E]. They should be equal at that point; alkhaser's own `fetchByte(rip + opOff + 1)` treats `rip` as the instruction start [I]. Confirm.
- **H4. Possible fix for the parallel-suite crash.** clean-cpu traced the 4-worker host SIGSEGV (exit 139) to "the 32-bit `KThread::seg_mapper` fault-dump path" [E], and cleanlog32 deletes that dump [E].
  - **After step 3, run the 4-worker suite three times.** If exit 139 is gone, every lane's "serial only" caveat is explained. If not, it's a separate bug (D23).
- **H5. The unimpl-opcode log line gains a `BW64-FAIL ` prefix** (`c0bcdb399`) [E]. Any anchored `^CPU64: unimpl` grep in tools or tests would break. Grep for it.

---

## Part 2: Design decisions handed upward

**Format:** each decision lists **Options** (the realistic choices), **Rec** (my recommendation), **Why/risk**, and the **Source** in the pack.

### Kernel and fault delivery

**D1. Recovery switch vs errno answers.** Should invalid arguments get `-EINVAL` regardless of the switch, as ksubsys does for signalfd4 and knetlink setsockopt? Or should the errno stay gated as a "recovery action", as kernelgate does?
- **Rec:** ratify ksubsys, which is already in integ. Adopt a written rule: *the switch gates only departures from Linux behaviour (skipping instructions, fallbacks); errno answers that Linux itself gives are never gated.*
- **Why:** the pathfinder standard asks for "graceful honest answers"; silently accepting bad flags is not honest.
- **Risk:** a guest that only worked because bad flags were accepted now gets EINVAL. The new log line names the value, so it's diagnosable.
- **Source:** kernelgate "Merge-composition flags"; ksubsys signalfd4 and knetlink bullets.

**D2. futex with an unmapped address, switch off, hits a pre-existing kpanic** (kernelgate "Audited" bullet).
- **Rec:** under the D1 rule, return `-EFAULT` with a BW64-FAIL line, as Linux does.
- **Owner:** the kernel lane, as a follow-up; not a merge blocker.

**D3. `KMemory::logPageFault`'s terminal `kpanic("pf")`.** cleanlog32 removes it; cleanmem calls it "loud by design".
- **Rec:** cleanlog32 wins.
- **Why:** cleanlog32 shows that `runSignal` already applies the correct default disposition and that the panic pre-empted it. A guest page fault is a guest event, not a broken emulator invariant (pattern doc, ladder rung 5). cleanmem's justification ("already-unrecoverable") is true for the guest process, not for the emulator.
- **Risk:** cleanlog32 notes that `seg_mapper` with `throwException=false` now throws (see D5).
- **Source:** cleanlog32 "Audit findings" table and "Residual risks"; cleanmem "Deliberately left alone".

**D4. The kernelgate recover-gate audit: what to do with it.**
- **Rec:** land it (step 2), strip the 11 exemptions, and wire it into the test runner or CI once it shows 0 violations on the merged tree. Today it is only documented in a README, as far as the pack shows.
- **Missing evidence:** whether any runner calls it.

**D5. Callers of `seg_mapper(..., throwException=false)`.**
- **Rec:** audit the callers before or immediately after step 3.
- **Missing evidence:** the call-site list (grep `seg_mapper(` / `seg_access(`).
- **Source:** cleanlog32 "Residual risks".

**D6. Removing the `SEG_*` register/TEB/stack dump** (cleanlog32 `kthread.cpp` diff). This wasn't handed up explicitly, but it costs debugging capability that past investigations used ("Bug #5", "Portal load crash" comments [E]).
- **Options:** accept the removal, or restore the dump behind an environment variable.
- **Rec:** accept the removal now; optionally restore behind an env var such as `BW64_SEGDUMP`. That matches cleanmem keeping `STRAYWRITE` / `MEMRING` / `BW64_WATCH` as "load-bearing" env-gated diagnostics [E].
- **Check H4 first:** the dump may be the parallel-crash site.

**D7. sigInfo quirks** (cleanlog32 "Deliberately left alone").
- **INTO as SIGSEGV with trap 4: leave it.** As I understand Linux (arch/x86 traps: #OF and #BR deliver SIGSEGV), this is correct, not a quirk [I, from knowledge of Linux, not the pack].
- **`prepareFpuException` delivering via the SIGSEGV slot:** Linux delivers #MF/#XM as SIGFPE, so this is likely a real deviation [I].
  - **Rec:** a small follow-up lane.
  - **Missing evidence:** whether Wine's i386 SIGSEGV handler recognises trapno 16/19 (if it does, guests are unaffected today).

**D8. `KThread::modify_ldt` invalid-index kpanics** (cleanlog32 "left alone").
- **Rec:** return `-EINVAL` with a log line (Linux `write_ldt` behaviour).
- **Owner:** the kernel lane.
- **Missing evidence:** whether integ's `kthread.cpp` changes since `538f03db5` already touched these.

**D9. 32-bit INS-string and `int99` kpanics in `ops.h`** (clean-cpu "Handed off": fill with 0xFF, or deliver #GP?).
- **Rec:** deliver #GP via `CPU::prepareException`.
- **Why:** user-mode INS without I/O permission is #GP → SIGSEGV on Linux [I]. Once cleanlog32 lands, `prepareException` delivers every recognised fault without panicking [E], so this is cheap.
  - For `int99` with an unregistered index, deliver #GP with error code `(vector<<3)|2`, the same encoding alkhaser `26e10926a` uses on x64 [E].
  - 0xFF fill is only right if the emulator models `ioperm`/`iopl`.
- **Missing evidence:** what `syscall_ioperm`/`iopl` currently return.

**D10. `normal/normal_other.h:262,287` recover-gate sites.**
- **Rec:** close them as "pure action". kernelgate's census already classified them that way [E]. No 32-bit lane needs to re-audit.

**D11. Kernel-side `raiseSyncFault` callers log nothing** (`syscall64.cpp:5152`).
- **Options:** log at each caller, or move the `delivered=1` logging into `raiseSyncFault` itself and keep the `delivered=0` fallback line at the call sites.
- **Rec:** move the logging inside `raiseSyncFault`, as a follow-up after steps 6–7, so reporting lives in one place (the principle ksubsys applied to ioctl). Not during the merge.
- **Missing evidence:** the number of direct callers.

**D12. `_DEBUG` build of `decoder.cpp` fails under `-Werror=switch`.** The failure exists at baseline (clean-cpu, verified on the unmodified integ-tip file).
- **Rec:** a separate one-line fix (add the missing cases or a default) by the CPU lane. Don't relax `-Werror`. Not a blocker.

**D13. Process and fd limits** (ksubsys "left alone").
- **Fd-exhaustion EMFILE:** a dedicated lane. Converting it naively trades the panic for null dereferences across ~50 call sites [E]. Low priority unless a guest is seen near 0x4000 fds (no evidence either way).
- **`allocSHM`:** keep the panic.
- **Unix `sendto` ignoring `MSG_NOSIGNAL`:** follow-up. Risk: spurious SIGPIPE.

**D14. The signalfd4 mask-size disagreement.** ksubsys says a bad size "panicked"; kernelgate says it was silently accepted.
- **Behaviour is no longer at stake:** integ answers EINVAL either way.
- **Missing evidence:** `git show 538f03db5:source/kernel/ksignal.cpp`, the `syscall_signalfd4` body. ksubsys's sabotage proof ("restoring the signalfd4 mask-size `kpanic`") suggests a kpanic existed somewhere on that path [E], possibly only with the switch on. Fix whichever lane record is wrong.

### Memory and filesystem

**D15. X11 callers dereferencing `getRamPtr` without a null check** (cleanmem "Residual risks").
- **Rec:** schedule the audit for the X11/graphics lane.
- **Why:** cleanmem calls the change "strictly better", but a null dereference still kills the emulator (host SIGSEGV or wasm trap). The improvement is only that a BW64-FAIL line is logged first [I]. Not a blocker.

**D16. `BString::split("")` loops forever** (cleanfs "Found outside").
- **Rec:** guard inside `split` itself (return the whole string and log), not only at callers. One line; owner is the util layer.

**D17. `translateOpenError` collapses errnos to EINVAL.**
- **Rec:** widen the map now that the fs layer preserves errno exactly (ENOENT, EACCES, EEXIST, ENOTDIR, EISDIR, ENOSPC, EROFS, ELOOP, ENAMETOOLONG).
- **Owner:** the kernel lane, after step 5.
- **Missing evidence:** which errnos cleanfs now produces.

### Graphics bridges

**D18. Bridges follow-ups.**
- **(1) Marshal-generator panics:** resolved by tooling-honesty `3c2f790bb`. Verify that re-running the generator reproduces the committed `vk_host_marshal.cpp` byte for byte.
- **(2) `replayImmBlock` drops `glMultiTexCoord2fARB`, and (3) the 4444/5551 packed pixel formats have no unpack model:** defer until the new once-per-case lines show up in a demo-sweep log. Both are now loud, which was the lane's goal.
- **Splitting the very large bridge files:** defer.
- **`naga-rt.mjs`:** no action.

**D19. Monolithic `lane-firstframe` commit** (one commit, "six walls", plus binaries). This wasn't handed up.
- **Rec:** accept it as-is. It can't be bisected, but splitting it after the fact is expensive. Note in the merge commit that a revert removes all six fixes.

### Tooling and docs

**D20. Tooling.**
- **The 5 legacy test failures** (spirv corpus, four vkwebgpu frame/sampler cases; pre-existing at base): firstframe changes `spirvfix.mjs` and `vkwebgpu.mjs` [E], so run them at the JS gate before assigning an owner. Firstframe may change them [I].
- **`scripts/build-jit-wasm.sh` stale-unit claim:** run it once end to end.
- **Absolute-path probe-script imports:** leave them.

**D21. Docs.**
- **`stash@{0}`: do not drop it on the old proof**, because the backup it relied on is gone (docsclean flags). Rec: `git stash show -p stash@{0} > <durable path>/winbox-stash0.patch`, confirm the patch is complete, then decide.
- **The 54 broken links:** accept.
- **Reports index:** after step 11, add one commit that indexes the 8 new lane reports plus the 3 already-merged ones, and update `WINBOX-STATUS` for the firstframe and alkhaser outcomes.
- **Report location:** alkhaser keeps its report in `test-results/alkhaser-int2d/REPORT.md`, while the convention is that summaries move to `docs/reports/` (docsclean index [E]). Either move it or link it from the index.

**D22. When to resume the mindustry lane.**
- **Missing evidence:** the resume plan. The report excerpt is cut off at "Ruled out … A lost-wakeup deadlock among".
- **Rec:** merge the record now. Before resuming, re-run the baseline on the post-wave integ tip. The lane's measurements are on `8ebb588c5`, before the wave, and the wave changes fault delivery and logging that its thread dumps and RIP samples depend on [I].

**D23. Box-wide exit-139 crash in 4-worker runs.**
- **Rec:** decide after H4. If it survives cleanlog32, give it a lane. It is a host-side crash in the test harness under parallelism, which is an emulator bug, not just box load [I].

---

## Evidence I'd want before or during the merge

```sh
git diff 538f03db5 ee526bb0c -- source/kernel/kthread.cpp source/test/testMain.cpp \
    web/bridge-recovery.mjs web/tests/suite/known-silent.json            # steps 3–5, 8
git diff 8ebb588c5 ee526bb0c -- web/runtime.html                          # step 9
git diff 692875bd8 ee526bb0c -- source/emulation/cpu/cpu64.cpp source/kernel/syscall64.cpp  # step 7
git diff -w --stat 538f03db5 lane-cleanlog32 -- source/emulation/cpu/common/cpu.cpp        # line endings
git diff -w --stat 538f03db5 lane-cleanwave-kernelgate -- source/kernel/ksignal.cpp
git show 538f03db5:source/kernel/ksignal.cpp | grep -n -A40 syscall_signalfd4             # D14
grep -rn 'testMemfdCreate' $(git ls-files 'source/test/*')                                # duplicate registration
grep -rn 'testOpcodeFault64' source/test                                                  # alkhaser orphan check
grep -rn 'seg_mapper(\|seg_access(' source                                                # D5
grep -rn "^CPU64: unimpl\|'CPU64: unimpl" tools web/tests                                  # H5
grep -rn 'recover_gate_audit' tools web/tests .github                                      # D4
```

**Gates to record in the merge commits:**

- **Native gate (after step 7):**
  - Build both the 32-bit and x64-guest test flavours. Expect the 32-bit count to be integ's count + 11, with 0 failures, in a serial run.
  - Run the 4-worker suite three times (H4).
  - Run `recover_gate_audit.py` (0 violations) and `known-gaps-audit.mjs --check`.
- **JS gate (after step 9):**
  - Export the dist with the now-honest `bw64-build.sh`.
  - JS unit suite: about 333/0 is the reference from a tree with its own dist (tooling-honesty report).
  - Legacy suite, regenerated `known-silent.json`, the citation audit, and `check_runtime_completeness.py`.
