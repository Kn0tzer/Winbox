# FD-86: no Arial family on the 64-bit stack — prefix64 font replacement — 2026-11-03

Lane: `lane-fd86` (worktree `/home/ubuntu/wt-fd86`), based on
`lane-fd65` tip `ed23b91c4`. Fix commit `a3349c4cb`; this record
and the register closure are committed with it. Evidence:
`test-results/fd86/` in the lane worktree.

## What was filed

FD-86, filed on lane-fd65's register when FD-65 closed: after
FD-65 restored real text on the 64-bit stack, the win32-fonts
fixture's family enumeration showed the Arial family still
absent there — `FAMILIES=10 TAHOMA=1 ARIAL=0` — while the
32-bit stack serves Arial. Gecko does not need Arial; any
guest program that looks the family up by name does.

## Characterization (measured, in order)

1. The pinned (20261102) `prefix64.zip` (19,066,440 B,
   `b34385c6…`): its `user.reg` has no Fonts sections at all
   and zero Arial mentions. `system.reg` mentions Arial 8
   times, but nothing there serves the family — the fixture
   reads `ARIAL=0`.
2. The 32-bit stack serves Arial through one mechanism:
   `tools/browser/stage-wine32-fonts.py` writes
   `"Arial"="DejaVu Sans"` under
   `[Software\Wine\Fonts\Replacements]` in prefix32's
   `user.reg`, run by `build.mjs` at dist export, with
   completeness-checker coverage. The 64-bit stack had no
   counterpart anywhere: no staging step, no checker rule,
   no shipped payload entry.
3. The `apply-runtime-poststeps.sh` header deliberately
   excludes `stage-wine32-fonts.py` from the canonical-set
   post-steps (export-time only, never touches the canonical
   set). Design decision for this lane: bake the replacement
   into the canonical `prefix64.zip` through a canonical-set
   staging step on FD-65's `stage-fonts64.sh` pattern, so
   the re-pin + clean-bootstrap path reproduces it — an
   export-time-only step would leave the retained archive
   (and anyone bootstrapping from it) still broken.
4. The shipped win32-fonts suite test ran the fixture on
   x86 only, which is why the gap was invisible to the
   suite: its one leg exercised the stack that already
   worked.

## The fix

Commit `a3349c4cb`:

- New `tools/rootfs64/stage-font-alias64.py`: canonical-set
  staging for `web/runtime` and `dist/runtime` (when
  present, non-symlink). For each tree it verifies
  `usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` exists
  in the sibling `glibc-rootfs64.zip` (it refuses to stage
  a dangling alias), then applies the registry edit to
  `prefix64.zip`'s `user.reg`, backing the original up
  under `/home/ubuntu/backups/` first. The edit itself is
  imported from `tools/browser/stage-wine32-fonts.py`
  (`configure_replacements`, loaded via importlib) so the
  two stacks cannot drift in shape. Idempotent: a tree
  already carrying the replacement is skipped.
- `tools/rootfs64/apply-runtime-poststeps.sh`: the step
  is wired in immediately after `stage-fonts64.sh`.
- `tools/rootfs64/check_runtime_completeness.py`: new
  check `prefix64 Arial font replacement (FD-86)` — the
  staging edit must be a no-op on the shipped `user.reg`
  (no drift), the literal `"Arial"="DejaVu Sans"` entry
  must be present, and DejaVu Sans must ship in
  `glibc-rootfs64.zip`.
- `web/tests/suite/browser/win32-fonts.test.mjs`: runs
  both architectures, mirroring `win32-clock.test.mjs`;
  the fixture's assertion line is unchanged.
- Re-pin through the retained-archive procedure:
  `prefix64.zip` is the only changed member (19,133,895 B,
  sha256
  `b1bfa64e0863ccf73dfcb244416ac6e3e14fa1de1f491aab7dd2c78bc0131ee5`);
  new retained archive `browser-runtime-20261103.tar`
  (514,426,880 B, sha256
  `ba510043e55a78016f7ec33b191ad4de69bea30a95b3c8edb6afc0f8a5652860`)
  at `/home/ubuntu/backups/winbox-runtime-assets-20261103/`;
  `tools/browser/runtime-assets.json` re-pinned to release
  `runtime-assets-20261103`, `tools/browser/README.md`
  updated, and the runtime-artifact manifest regenerated
  (one census line changed; member count unchanged).
- Docs: `docs/known-gaps/runtime.md` post-step table row
  and the FD-86 addendum recording this fix.

## Proof (RED → GREEN, all on this lane's tree)

Evidence in `test-results/fd86/`.

RED:

1. Checker against the unfixed canonical set
   (`checker-red.log`): the only failure was
   `prefix64 Arial font replacement (FD-86): prefix64.zip
   user.reg: Arial font replacement missing or drifted
   (FD-86)` — 1 failure, 6 skips, everything else PASS or
   loud SKIP.
2. Export gate (`build-baseline.log`, BUILD_EXIT=1): the
   first build on this branch ran the edited checker
   against the still-unfixed payload and `build.mjs`
   refused the export — the gate working as designed. That
   build's wasm was byte-identical to the shipped build
   (`1d46405a…`, git_sha `ed23b91c4`).
3. Browser RED (`win32-fonts-red.log`): with only the
   checker edit stashed for one build (restored
   immediately after) so the unfixed payload could reach
   the browser, the extended win32-fonts test failed —
   the x86 leg green, the x64 leg throwing
   `win32-fonts: GDI=1 FAMILIES=10 TAHOMA=1
   MAPPING=00000000 ARIAL=0 METRICS=0 RESULT 4`, the filed
   FD-86 signature.

GREEN:

4. Staging: `stage-font-alias64.py` staged both trees;
   originals backed up to
   `/home/ubuntu/backups/winbox-font-alias64-20261103-020854/`;
   `user.reg` now carries
   `[Software\Wine\Fonts\Replacements]` +
   `"Arial"="DejaVu Sans"`.
5. Checker on the re-pinned set (`checker-midstate.log`):
   exit 0, no FAIL lines. `bootstrap.py --check`
   (`bootstrap-check.log`): 0 failures, 1 standing skip,
   "Runtime assets verified (13 files)".
6. Archive procedure: rebuilding the 20261102 tar from
   its pinned files first reproduced it byte-for-byte
   (514,365,440 B, `7df0cdc2…` — determinism proven before
   trusting the procedure); during the new archive's
   build all 12 unchanged members were asserted
   byte-identical to their prior pins.
7. Clean-bootstrap reproduction: a fake root holding only
   `bootstrap.py` + the new `runtime-assets.json` restored
   from the new tar — "Restored and verified 13 runtime
   assets" — and the restored `prefix64.zip`'s `user.reg`
   carries the replacement.
8. Fixed build (`build-fixed.log`): BUILD_EXIT=0,
   "Browser export verified"; the exported
   `dist/runtime/prefix64.zip` verified to carry the
   Arial replacement (the export's `prepare_prefixes`
   sanitize preserves the section); wasm unchanged
   (`1d46405a…`) — a payload-only change.
9. win32-fonts PASS (`win32-fonts-green.log`, 150,117 ms):
   both architectures — x86 unregressed, x64 asserting
   `ARIAL=1 METRICS=1 RESULT 0`.
10. JS unit suite (`js-unit.log`): 348 passed, 0 failed
    (this branch's baseline; includes the
    runtime-completeness unit coverage of the checker).
11. Gecko stage unregressed: the shipped
    common-dependencies test was run twice on the fixed
    build while the shared box was heavily contended
    (load 10–20 on 4 CPUs); both runs stopped at the
    Gecko stage's fixed 180 s wall-clock window with the
    fixture spawned and its threads waiting and zero
    ABORT / missing-font lines in the console
    (`commondep2`/`commondep3` logs, preserved console) —
    the stage never reached its print. A long-window
    evidence variant of the same stage — identical boot,
    fixture, and assertions, stage window raised to
    600 s, evidence-only file deleted after the run —
    PASSed in 322,425 ms (`mshtml-evidence.log`,
    `mshtml-evidence-console.log`): `MSHTML: TITLE
    boxedwine gecko`, `MSHTML: TEXT embedded html
    works`, `MSHTML: RESULT 0`, zero ABORT lines and
    zero `unable to find a usable font` lines in the
    whole run console. Baseline for comparison: on the
    parent branch the same stage passed the shipped test
    outright twice (FD-65 record, ~264 s full runs on a
    quieter box). The shipped test's downstream
    audio-capture stage is FD-85, untouched here.

## Also filed

Nothing new. FD-85 stands open on its own lane; this
lane's runs neither reached nor changed its stage.

## Deliberately not done

- No export-time 64-bit staging in `build.mjs`: the
  replacement lives in the canonical payload so
  bootstraps reproduce it; the 32-bit export step is
  untouched.
- No real Arial or metric-compatible face added: DejaVu
  Sans is the same substitute the 32-bit stack serves;
  its metrics are the product's standing behavior on
  both stacks now.
- No change to the shipped common-dependencies test.
- Nothing pushed, deployed, or merged. This branch
  stacks on lane-fd65's tip; the merge that composes it
  must take the `runtime-assets-20261103` manifest and
  archive superseding 20261102.
