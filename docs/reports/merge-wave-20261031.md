# Merge wave 2026-10-31 — cleanup campaign + demo-class branches into integ

Branch `integ/winbox-20261022b`, from tip `538f03db5`. Sixteen lane
branches merged in the order below; every merge committed locally;
nothing pushed; master untouched; the live site untouched. An
independent frontier review of this composition (Claude Code, on the
box) verified the composed tree before the last merges landed; its
record is `test-results/frontier-review-20261031/REVIEW.md` (raw output
beside it as `claude-raw.md`).

## The queue, in merge order

| # | Branch | Tip | Merge commit |
|---|--------|-----|--------------|
| 1 | lane-cleanlog (pathfinder) | 648241602 | d47767ce3 |
| 2 | lane-cleanweb | 485c9f5d2 | e3c59f072 |
| 3 | lane-cleanlog-ksubsys | 7c5a7a7b2 | ee526bb0c |
| 4 | lane-cleanwave-kernelgate | f6c5a74de | 5b4621721 |
| 5 | lane-cleanlog32 | 62e6537b8 | 402050940 |
| 6 | lane-cleanmem | 8f363a7ed | c278455fd |
| 7 | lane-cleanfs | bf81364d2 | d818e4dcf |
| 8 | lane-clean-cpu | bc6a4177f | 4f52918db |
| — | batch gate: native build + serial suite 660 tests, 0 FAILED | | |
| 9 | lane-tooling-honesty | ed5d59686 | 9b8107425 |
| 10 | lane-cleanup-bridges | 137f8294d | (auto-merge) |
| 11 | lane-firstvisit | 5306152b6 | d51fa408e |
| 12 | lane-defect-register | 64604f91e | 636e5ac80 |
| 13 | lane-mindustry (record-only, per review D22) | 8f5d22d49 | 59dcaba4c |
| 14 | lane-alkhaser-int2d (hand-merge) | 259e794c9 | a5a670a10 |
| 15 | lane-firstframe | 728b81976 | bf6b7b33f |
| 16 | lane-docsclean (last, per the review) | 803592c86 | (final merge) |

Coordinator commits on top: `0fe10c714` (audit exemption strip),
`a06a155f1` (packaging re-pin + manifest regeneration), `9d6c2329e`
(JS gate composition fixes). All sixteen branches verified ancestors
of the final HEAD.

## Composition decisions (where branches touched the same code)

- **kernelgate × ksubsys (`ksignal.cpp`)**: resolved OURS (ksubsys) for
  every hunk. signalfd4 validation is ungated EINVAL in both modes with
  no duplicate reporting. kernelgate's two signalfd4 commits were
  effectively dropped; its audit tool, README and report landed. The
  frontier review independently reached the same outcome (its Part 1.1
  / D14: both lanes' claims about the base were half-right — there was
  a switch-gated EINVAL at the gate AND a panic deeper in).
  `kprocess.cpp` ioctl took kernelgate's version: one unconditional
  `bw64RecoverLogOnce`.
- **Exemption strip (review D4)**: the pathfinder having landed, the
  11 panic-default exemptions were removed from
  `tools/conformance/recover_gate_audit.py`. Re-run on the final tree:
  45 tolerated, **0 violations** — the 11 sites are genuinely
  converted, so the tolerated count did not drop.
- **cleanfs duplicate pathfinder commit** (`860b1ef7e`): auto-merged
  against the original with no net diff in `log.cpp`. No action.
- **logPageFault**: cleanlog32's panic removal won over cleanmem's
  deliberate keep (final count 0), although cleanmem merged after;
  verified in the composed tree by the review.
- **Alkhaser hand-merge (review Part 1.4)** — the only true conflict
  besides kernelgate/firstframe, one hunk in `cpu64.cpp`. Composed
  form: clean-cpu's counting funnel (BW64-FAIL guest-insnfault, first
  64 then every 1024th) extended with alkhaser's `errorCode`/`siCode`
  parameters, passed through as
  `raiseSyncFault(sig, trapNo, siCode, ipStart, errorCode)`; both log
  lines gained `err=0x%x code=%d`. Two accepted behavior notes (also in
  merge commit `a5a670a10`): INT3 deliveries now emit a guest-insnfault
  line, and handler-less INT3 now also `signalProcess(K_SIGTRAP)` via
  the funnel fallback where the old code only yielded (Linux-correct;
  Wine always installs a handler). The review's H3 was confirmed during
  the merge: `ipStart = rip` at step() entry, so funnel-routed FX #GP
  sites report the same address as the old direct calls.
- **Firstframe (review Part 1.7 / D19)**: three conflicting files.
  `web/runtime.html`: kept firstframe's
  `window.bwVkSubgroups = adapter.features.has('subgroups');` with
  integ's loud `reportBridgeFailure` catch (not the branch's silent
  catch). `check_runtime_completeness.py`: the auto-merge union was
  hand-verified — firstframe's VKD3D shim pins AND firstvisit's win32u
  pin both present. Runtime assets + artifact manifest: took integ's
  pin at merge time, then re-staged and re-pinned (next section). The
  lane is a single monolithic commit: a revert removes all six D3D12
  fixes together.
- **Bridges**: auto-merged cleanly, including `known-silent.json` and
  `bridge-recovery.mjs` (overlap with cleanweb). The merged scanner
  baselines match the merged tree: the no-silent-failure JS test passes
  as merged, so the review's regenerate-baselines protocol is satisfied
  by verification rather than regeneration.

## Packaging and staging on the merged tree

The honest build script's completeness gate initially failed the dist
build (merged pins vs the staged `wine64.zip`) — it printed FATAL and
did **not** claim BUILD COMPLETE, which is the tooling-honesty lane
working as designed. Staging was redone with the two relevant steps
directly (`stage-wine8-backports.sh`, `stage-vkd3d-payloads.sh`;
`apply-runtime-poststeps.sh` is not usable in this tree — it dies in
the 32-bit font staging on a missing `/home/ubuntu/wine32-stage`
library). Canonical-source verification: this tree's own
`tools/vkd3d/build.w64` outputs match the merged pins (d3d12.dll
`a96b4e9d…`, d3d12core.dll `3d3bfe43…`); the main tree's build.w64
outputs do **not** match — never stage from the main tree. After
staging: completeness checker 0 failures / 4 skips; re-pinned
wine64.zip = 209,670,021 bytes, sha256
`94357c3c7f5ed782a4bc19e52fac36262530f8825ff5b56f5ee2df3a49def704`;
`bootstrap.py --check` = "Runtime assets verified (13 files)". The
artifact-manifest drift the first dist attempt caught was repaired by
regenerating with `gen_runtime_manifest.py` (exactly one line changed:
the wine64.zip row). Final dist build: **"Browser export verified:
34 .mjs syntax files; runtime version substituted; dependency archive
de592964f772 verified"**, RC=0. One benign artifact noted: a Python
zipfile `UserWarning: Duplicate name:
'usr/lib/x86_64-linux-gnu/libvulkan.so.1'` during build.mjs (warning
only; the libvk ELF files are not wine64.zip members and no checker
section flags them).

## Claims verified, not just merged

- **First-visit lane**: its report's raw evidence (per-run
  `summary.json` in `wt-firstvisit/test-results/firstvisit/`) matches
  its claims. Cause: two Wine 8.0 win32u impatience defects (10 s
  input-idle fuse; single-probe driver latch). Fix: backport patch
  `tools/buildWine/patches/wine8-win32u-desktop-wait.patch` (waits to
  300 s; probe retries), win32u.dll (354,218 B, sha `a8ca5fbb…`) in the
  backport set. Before 0/6 started, after 6/6 (median ready 35.9 s).
  The 32-bit WineHQ 10 win32u has the same defects and is NOT covered
  (register FD-02). FD-01 stays open until deploy + live re-measure.
- **H5**: no anchored `^CPU64: unimpl` consumers exist in web/tests or
  tools; the two substring consumers still match the new
  BW64-FAIL-prefixed line.

## JS gate composition fixes (coordinator commit 9d6c2329e)

First full JS run on the merged tree: 333 passed / 5 failed. All five
were composition fallout, classified by bisect in the lane trees
against a pre-merge base control (base passes all):

- `unix-connect-handoff`, `native-socket-safety`, `browser-socket-wakeup`:
  the tests slice production functions into stub harnesses; ksubsys's
  pattern conversion made the slices call `bw64RecoverEnabled()` /
  `bw64RecoverLogOnce()` / `kwarn_fmt` and use `K_ENAMETOOLONG`, none
  of which the harness preludes defined. Preludes learned the stubs
  (production semantics re-checked: the graceful returns are ungated,
  only the reporting flavor is gated). One `browser-socket-wakeup`
  assertion was updated to the deliberate new contract — an unusable
  wakeup pipe now degrades (BW64-FAIL log, monitor not started) instead
  of panicking; the safety properties (no thread started, pipe state
  −1/−1) are still asserted by the surrounding lines.
- `fs-directory-publication`: cleanfs changed `Platform::listNodes`
  to return bool and `loadChildren` now calls
  `Fs::doesNativePathExist`; the harness stubs followed the new
  signatures. No assertion changed.
- `vk-register-citations`: re-pinned against the composed
  `vkwebgpu.mjs`. 24 primary drift rows (one uniform merge shift), 10
  duplicate/companion rows re-pinned to their primaries' new sites,
  and the best-effort-cleanup-catch row re-pinned to the
  content-corresponding line (its literal window had moved). Final:
  all 4 subtests pass; uncheckable count back to 98 (ceiling 102).

Final JS run on the merged tree: **338 passed, 0 failed, RC=0**.

## Native gates on the final tree

- Test flavour serial (`boxedwine 0 0 1`): build RC=0; **660 tests,
  0 FAILED** in 214 s.
- x64-test flavour: build RC=0; the run segfaults partway (exit 139)
  after ≥40 passing tests of 58. This is the pre-existing base defect
  documented at the pre-merge tip by the first-visit report ("x64-test
  full-run segfault after 33 passes"); no merged lane touched it and
  this wave does not fix it.
- 4-worker parallel suite (review Part 3, H4/D23 — does cleanlog32's
  seg-dump removal cure the parallel crash?): **No.** The merged tree
  floods `probably about to crash, could not find emulation
  instruction that caused exception` (11.26 M lines / 945 MB in under
  4 minutes; run killed) and hangs. A base control built from the
  pre-merge tip floods identically (237,857 flood lines inside a
  20 MB-capped run): the failure predates the wave in the same form.
  One additional parallel-only observation on the merged tree: the
  setitimer expiration test failed under 4 workers (SIGALRM never
  queued); it passes serially. Parallel mode remains broken and is
  not the trusted gate; the serial run is.

## Audits on the final tree

- `recover_gate_audit.py`: 45 tolerated, 0 violations.
- `check_runtime_completeness.py` (dist present): 0 failures, 1 skip.
- Vulkan citation audit: 4/4 subtests (inside the JS run above).

## What this wave deliberately did not do

- No deploy, no live-site change, no push. The formal final full gate
  and the deploy decision belong to the coordinator.
- Found-but-unfixed defects found along the way live in
  `docs/defect-register.md` (FD-01…FD-38), including FD-15 (the five
  gl64bridge fake answers — open by the user's ruling, not fixed by
  the bridges lane's logging) and FD-02. Follow-up engineering items
  from the frontier review (its Part 2 D-numbers) remain future lanes.
