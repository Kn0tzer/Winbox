# Game compatibility (verified evidence only)

Statuses below summarize the latest explicit evidence currently recorded in this
worktree as of 2026-10-24. These runs use different source/runtime builds; a
status is not a claim that the current checkout was rebuilt or re-probed.
“Mapped” means Wine opened a window. A title/menu or playable state requires its
own screenshot evidence.

| Demo | Current verified status | Evidence |
| --- | --- | --- |
| Notepad | Painted on the 2026-10-17 live acceptance (286,296 nonblack pixels). The 2026-10-22 manager matrix also records mapping; it does not include a newer pixel measurement. | `docs/reports/launcher-redesign-20261017.md` (`test-results/launcher-*`); `docs/WINBOX-STATUS.md` matrix, source HEAD noted as `723b3a056` |
| Snake | Mapped in the 2026-10-22 manager matrix. Gameplay and input are not established. JIT=1 failed to map on the separate 2026-10-20 merge build. | `docs/WINBOX-STATUS.md`; `docs/reports/winbox-finish-20261020.md` |
| Celeste | Colored content and title screen rendered in earlier shared-runtime acceptance; separate earlier prologue evidence covers movement, jumping, and landing. Current build playability is not re-verified. | `docs/reports/shared-runtime-20261017.md` (`launcher-dependencies-games`, `launcher-dependencies-upload-reopen`); `docs/reports/winbox-finish-20261020.md` |
| Portal | Main menu and Relaxation Vault scene rendered; OCR confirmed all menu entries. Portal traversal and broader level acceptance remain unverified. | `docs/reports/portal-deepseek-20261022.md` (`portal-bc2-20261022`, `demo-portal-bc4-20261022`); latest stored-archive rerun `docs/reports/portal-boot-deepseek-20261023.md` (`demo-portal-prefetch-20261023`) |
| Gang Beasts | Build 10 rendered the Unity title screen (characters, truck, logo). A playable match and input response are not verified. | `docs/reports/gangbeasts-sonnet-20261022.md`, `test-results/gb-pr9/final.png`; fix commit recorded there as `2122062f1` |
| Doom | The manager's latest matrix records mapping and rendering with JIT off. A playable level is not verified; the 2026-10-20 profiling run showed only the title/intro with JIT on, while its JIT-off retry did not map. | `docs/WINBOX-STATUS.md`; `docs/reports/winbox-finish-20261020.md`; `docs/reports/winbox-profile-20261020.md` |
| Quake III | Reached `CS_ACTIVE` and displayed the Q3DM1 scene/HUD on the 2026-10-23 integration runtime. Rendering remained sparse and distorted; a later W-key probe changed the view during a hold, but normal playable performance is not established. | `docs/reports/q3-webgpu-luna-20261022.md`, Build 8/9 evidence directories and summaries under `/tmp/winbox-finish/r3/work-luna-q3/evidence/`; source correction/fix commits are described in the report |
| Mindustry | Unverified. Latest documented Build 7 runs stayed black without a menu or presents; JIT-off reached invalid instruction decoding and the JIT-on run stopped progressing. | `docs/reports/mindustry-luna-20261023.md`, Build 7 progress check; summaries and captures under `/tmp/winbox-finish/r11` and `/tmp/winbox-finish/r13` |
| Explorer | Mapped and rendered the desktop in the 2026-10-20 merged-build matrix. Explorer task/function behavior is not tested. | `docs/reports/winbox-finish-20261020.md` |
| Chromium | Chromium 109 rendered and ran the offline smoke page; typing, a Count click, exact text/checksum, and a colored canvas were verified. Restricted HTTP relay access passed; general Internet/remote browsing remains unverified. | `docs/reports/chromium-20261016.md` (`test-results/chromium-software-smoke/`); `docs/reports/chromium-networking-20261017.md` (`suite-chromium-network-final-http`, `suite-chromium-network-final-offline`) |

## Verification recipe

Run a fresh verification against a named export and retain the full summary,
console, and composited screenshots with the exact source commit and
`dist/runtime/BUILD_INFO.json` build ID:

```sh
/home/ubuntu/scripts/winbox-wait-load.sh
DEMO=notepad JIT=0 OUT=test-results/game-readiness/notepad-jit0 \
  node web/tests/demo-matrix.mjs
```

Repeat for the relevant `DEMO` values (`notepad`, `snake`, `celeste`, `portal`,
`gangbeasts`, `doom`, `explorer`, `chromium`) and JIT modes that the program
supports. Quake III and Mindustry use the GL game probe:

```sh
/home/ubuntu/scripts/winbox-wait-load.sh
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
