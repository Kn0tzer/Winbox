# Game compatibility (verified evidence only)

Statuses below summarize the 2026-10-30 all-demos sweep: every demo on
the live site tried end-to-end the way a person uses it — open the site,
click the demo's card, wait, look at screenshots — all against the one
live build `5741034f3`. The full record, with per-demo screenshots and
logs, is `test-results/demo-sweep-20261030/REPORT.md` in the integration
worktree. Unless a row names another source, the sweep is its evidence.
Rows marked † were refined afterwards by lane work on unmerged
branches; the live build does not contain those fixes yet. A status is
a claim about the build named here, not about any other checkout.
"Mapped" means Wine opened a window. A title/menu or playable state
requires its own screenshot evidence.

_Update 2026-11-05:_ the live build has changed many times since the
sweep (it now serves the batch-7 build, commit `4006ddb58`). The rows
below have **not** been re-measured on the current build; they remain
the last full-sweep evidence. Two rows carried "not yet merged or
deployed" notes from lane work — both fixes have since been composed
and shipped in later batches, and those rows are updated in place
below. A fresh sweep is the only way to move the other rows.

## Working (9)

| Demo | Verified status |
| --- | --- |
| Notepad | Works: a real window with menus, text cursor and status bar. |
| File Explorer | Works: folder window with full navigation tree. Needed a second attempt — the first hit a 7-minute cap while still starting. |
| WinMerge | Works: folder comparison runs to completion with correct different / identical / only-in-one-side results. Fastest start of the sweep (~33 s). |
| Snake | Works. One cosmetic flaw: the dash in the game-over message renders as garbled characters. |
| Tetris | Works: board mid-game, visibly animating. |
| DOOM | Works: in-game in the first level, HUD shown, animating. |
| Chromium | Works: full browser window on both attempts. The screenshots are the evidence — an idle browser presents no frames for an activity check to count. |
| Celeste | Works, very slowly: animated title reached and held (also confirmed on this build by `test-results/celeste-liveproof-20261030/`); first frame ~5 min; title runs at ~0.3 FPS. Keyboard input at the title has not been directly exercised. |
| Portal | Works: main menu animating. Menu speed 2.1 FPS by the standard measurement (260 frames / 125 s; evidence `test-results/fps-rerun-20261030/portal/`). The previous 1.4 FPS baseline was measured under roughly double the machine load; the load-matched comparison is 1.8 → 1.9 FPS, so the gain is small. |

## Not working (6)

| Demo | Verified status |
| --- | --- |
| Gang Beasts | Never shows a frame — across the site's own restarts plus two fresh attempts. Behind the starting screen, the game stalls during engine startup. |
| Quake III Arena | No frame in 12 minutes on one attempt; a frozen, garbled smear of geometry on the other. |
| Mindustry † | The bundled Java runtime starts, prints one warning, then goes quiet at the same point in both attempts. Lane diagnosis (`docs/reports/mindustry-jvm-stall-20261030.md` on branch `lane-mindustry`): not a deadlock and not a load failure — the Java engine itself keeps executing; whether it is looping or crawling through startup is the open question. |
| Al-Khaser † | Crash dialog on both attempts: the checker crashes instead of reporting. The crash class is fixed on branch `lane-alkhaser-int2d` (record: that worktree's `test-results/alkhaser-int2d/REPORT.md`) — through the website path the checker now passes the point where it died and completes its debugger-detection section, stopping later at a separate documented failure. That fix has since been merged and shipped (batches after 2026-10-30); the demo itself has not been re-run end-to-end on the current live build. |
| Godot Squash the Creeps † | Starts and works through its graphics setup, but never shows a frame in two 12-minute attempts. On branch `lane-firstframe` (record: that worktree's `test-results/firstframe-20261030/REPORT.md`) its D3D12 driver now fully initializes through the website path; exactly one shader module still blocks the first frame. That work has since been merged and shipped, and on the current shipped build the demo opens its window and builds its graphics pipelines — the same single shader module still blocks the first frame, because the browser's shader language has no equivalent for one operation it uses (register FD-72). |
| Unigine Tropics | A window opens behind the scenes; no frame is ever displayed, two attempts. |

## Verification recipe

Run a fresh verification against a named export and retain the full summary,
console, and composited screenshots with the exact source commit and
`dist/runtime/BUILD_INFO.json` build ID:

```sh
/home/ubuntu/winbox-project/scripts/winbox-wait-load.sh
DEMO=notepad JIT=0 OUT=test-results/game-readiness/notepad-jit0 \
  node web/tests/demo-matrix.mjs
```

Repeat for the relevant `DEMO` values (`notepad`, `explorer`, `winmerge`,
`snake`, `tetris`, `doom`, `chromium`, `celeste`, `portal`, `gangbeasts`,
`quake3`, `mindustry`, `alkhaser`, `godot-squash`, `unigine-tropics`) and JIT
modes that the program supports. Quake III and Mindustry can also be probed
with the GL game probe:

```sh
/home/ubuntu/winbox-project/scripts/winbox-wait-load.sh
GAME=q3 OUT=/tmp/winbox-finish/work-p2tests/q3 \
  node tools/bench/gl-game-probe.mjs
```

Use `GAME=mindustry` for Mindustry. Capture long enough to pass startup, menu,
and first gameplay transitions; save screenshots at each state and send input
only after the guest is visibly ready. To claim visible rendering, require the
demo matrix summary to show the launcher overlays hidden and the PNG screenshot
to meet its non-dark-pixel and distinct-color checks. Retain the corresponding
`bw64: METRICS` frame count. To claim playable behavior, also record the before
and after screenshots plus the exact input and game response. A mapped window,
shader/pipeline activity, or an adjacent graphics fixture is not game evidence.
