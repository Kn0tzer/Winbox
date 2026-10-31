# FD-01: the desktop-wait fix now ships in both halves of win32u — 2026-10-31

Lane: `lane-fd01-so`, off integ `f3fd57d58`.
Register: FD-01 updated (stays OPEN per its own re-closure terms —
merge, deploy, and a live first-visit re-measurement remain);
FD-45 opened (found along the way, not fixed).

## What was wrong

FD-01's two impatience defects live in Wine 8.0's desktop
initialization: `get_desktop_window` waits only 10 s for the
just-started desktop process to go input idle, and
`load_desktop_driver` latches the null driver permanently on its
single publication probe. The project's backport patch
(`tools/buildWine/patches/wine8-win32u-desktop-wait.patch`) fixes
both — but the functions live in the **unix half** of win32u, and
the pipeline only ever rebuilt the PE half (`win32u.dll`). The
shipped `win32u.so` stayed byte-identical to Debian stock
(sha256 `5dac5821...`), so the patch shipped in no binary
(FD-43). With FD-40's builtin-signature fix restoring real
dispatch, the stock unix half's races are the live FD-01 defect:
the hardened four-arm measurement put the true first-attempt
start rate at **0/10 on every healthy build**.

## The fix (payload pipeline, so it survives every rebuild)

- `tools/rootfs64/build-wine8-backports.sh` — a third build tree
  (`--host=x86_64-linux-gnu --enable-win64`, same patched
  wine-8.0 source, same native tools tree) builds
  `dlls/win32u/win32u.so`; the product is committed at
  `tools/rootfs64/wine8-backports-x86_64/win32u.so` next to the
  DLLs and printed for re-pinning with them. (The
  `--enable-win64` flag is required: without it Wine's
  configure demands 32-bit development libraries this pipeline
  has never needed.)
- `tools/rootfs64/stage-wine8-backports.sh` — the merge-script
  staging path now swaps the `x86_64-unix/win32u.so` member
  with the same skip/verify discipline as the five DLLs.
- `tools/rootfs64/build-wine64-zip.sh` — the container staging
  path now stages `win32u.so` alongside its PE half. The same
  edit closes a latent gap found on the way: this path never
  staged `win32u.dll` either (its pre-check and staging loops
  named only the other four backport DLLs), so a pure
  container rebuild would have shipped Debian's stock
  `win32u.dll` and lost the PE backport silently.
- `tools/rootfs64/check_runtime_completeness.py` — the backport
  pin set gains `win32u.so` (1,428,136 B, sha256
  `bffd9036f5c6149e...`). Pinning only the DLL is what let the
  patch ship in no binary; the unix half is pinned now.
- `tools/browser/runtime-assets.json` + the runtime artifact
  manifest — the staged container re-pinned (see audit below).

The built module was verified against the stock one before
staging: same ELF64 x86-64 shape, same `DT_NEEDED` set
(`ntdll.so`, `libm.so.6`, `libc.so.6`), same exported
`__wine_*` entry points, and the patched 300,000 ms constant
present in the binary (the patch itself was verified applied
in the source tree before the build: the 300 s wait in
`winstation.c`, the 600-attempt retry loop in `driver.c`).

## Member-level audit

Base: the live container (`78dde7c2...`, copied read-only from
the deployed site). Staged result: 1,257 members, name set
identical, **exactly two content differences** —
`x86_64-windows/win32u.dll` `a8ca5fbb...` -> `49b87438...`
(unsigned -> stamped backport, the FD-40 fix) and
`x86_64-unix/win32u.so` `5dac5821...` -> `bffd9036...`
(stock -> patched, this fix). Against the container currently
pinned on integration (`e777d052...` content: the same base
with the FD-40 swap already applied), the only difference is
`win32u.so`. New container: 209,611,866 B, sha256
`d71ca2751a9f...`. A pre-fix control container (the staged
container with only the `win32u.so` member swapped back to
stock bytes) came out at 209,691,093 B — byte-size-identical
to the pinned pre-fix container, cross-checking the audit.

## The measurement

Driver: the hardened-readiness lane's
`tools/browser/startup-measurement.mjs`, **unchanged** — same
deskpin-anchored harness shape (`cmd /c echo vkboot`,
session=1, persist=0, fresh=1, arch=x64, bwretry=2 so the
restart stopgap's budget is pre-exhausted and the first
attempt is what is measured), fresh browser profile per boot,
verdicts classified **offline** from the complete console
stream under the hardened rules (plausible deskpin handle +
>=5 accepted X11 connections + explorer.exe opened; an
NTSTATUS-shaped handle cannot certify). Arms are two dists
built from this branch's tree (`Browser export verified`;
dependency archive `de592964f772`, identical to every recent
gate) differing only in the staged container's `win32u.so`
member: **fixed** = patched, **pre-fix** = stock. Box load is
logged per boot; this measurement ran while a parallel merge
gate was driving load to 13-19 (the four-arm baseline ran at
7.8-10.6).

| arm | window | n | certified | anatomy |
|---|---|---|---|---|
| fixed | 360 s | 3 | 0 | X11 3/3/1, explorer opened, workload ran, **no failure signature** — every boot still advancing when the window closed |
| fixed | 900 s | 5 | **5** | handle `0x10048` every time, X11 = 5, explorer opened, no signature |
| pre-fix | 360 s | 3 | 0 | two died with the FD-01 signature (`dead-after-signature`, X11 = 2); one never started anything (X11 = 0, no explorer) |
| pre-fix | 900 s | 2 | 0 | both `dead-after-signature`, X11 = 2 |

Reading: the defect is the latch, not the clock. Given time,
the pre-fix build does not recover — its desktop is refused
and the null driver is permanent, at either window. The fixed
build's boots complete the full parade every time they are
given room: the fix's own waits (up to 300 s for input idle,
up to ~300 s of driver-publication retries) plus the parade
itself exceed the 360 s window the pre-fix measurement used,
at this box load — which is why the 360 s fixed arm shows
progress without certification, and why the window was
extended for both arms rather than the result being read off
the short window. Under the hardened rules the fixed build's
first-attempt rate is **5/5** against the pre-fix **0/5**
(same driver, same day) and the four-arm pre-fix reference
**0/10** (same driver, 360 s, load 7.8-10.6).

Scope, carried from FD-43: this is the pin-anchored harness
shape with the rescue disabled — the shape whose rate FD-01's
re-closure names. The live site's full-Wine session shape is a
different shape; FD-01 closes only after this fix merges,
deploys, and the live first-visit rate is re-measured through
the public site.

## Suites on this branch's tree

- Dist build: `Browser export verified` (the only trustworthy
  line); dependency archive `de592964f772` as at every gate.
- Completeness checker: **0 failures** (1 skip — the standard
  "prefix64 built artifact not present" skip seen at gates),
  including the new `win32u.so` pin (source + shipped member).
- JS unit suite against the new dist: 339 passed / 1 failed on
  the full run — the failure is `unix-connect-handoff`, which
  **passes standalone (1/1)** re-run immediately after; the
  full run crossed a load spike to ~19 from the parallel gate.
  This branch changes no JS or emulator source, so the suite's
  baseline (340/0 on integ `f3fd57d58`) stands with the flake
  attributed, in the family of load-correlated timing failures
  the project has recorded before.
- `boot64-smoke` on the fixed dist: **PASS** (422 s under the
  day's load; its assertions are wineserver-stream evidence —
  winstation reached — and never consumed the readiness flag).
- Native suite: not re-run on this branch — the branch changes
  no emulator source (pipeline scripts, pins, one payload
  binary); the identical source tree's full native gate passed
  661/661 at the FD-41/FD-42 merge on integ `f3fd57d58`, this
  branch's base.

## Found along the way

- **FD-45 (registered, not fixed):** the shipped support
  archive contains `usr/lib/x86_64-linux-gnu/libvulkan.so.1`
  twice with different bytes (185,464 B / 457,224 B), so the
  Vulkan library a guest loads is not determined by the
  archive's pinned hash alone. Pre-existing — that archive is
  the canonical `de592964f772` every recent gate verified.
- The `build-wine64-zip.sh` win32u.dll staging gap described
  above was fixed in this lane as part of the pipeline change
  (it is the same "one half ships, the other doesn't" shape as
  FD-01 itself, on the container path).

Nothing deployed. Nothing pushed. Master and the live site
untouched; the live container was only ever copied *from*.
Generated state stripped from this tree after the gate;
measurement evidence in `test-results/fd01so-20261031/`.
