# Dirty Working Tree Inventory — Boxedwine64 `integrate/schema-v2`

- **Snapshot taken:** 2026-10-08 ~16:52–17:01 EDT (21:00–21:01 UTC); final count at 2026-10-08T21:00:53Z
- **Branch:** `integrate/schema-v2` (verified via `git branch --show-current`)
- **HEAD:** `dccec718` ("FsZip URL reliability fixes + doSignalWineboot non-blocking connect", committed 2026-10-08 ~13:00 EDT)
- **Dirty entries at snapshot:** **739** = 42 modified tracked files + 694 untracked + 1 stash (`wip-diagnostics`)
- **Method:** `git status --porcelain`, `git diff --stat`, and `git diff -w` (whitespace-insensitive) per file; binary identification via `git diff --numstat`; md5sums for binaries; provenance cross-referenced against `/home/hatch/workspace/boxwined64-coord/handoff.md` (2026-10-08 ~15:35 EDT + 16:51 session note) and `32bit-investigation.md`.
- **Read-only:** no files were added, committed, stashed, checked out, or modified; no builds or probes were run.

## ⚠️ LIVE-TREE WARNING

**The tree mutated during this inventory.** Count went 735 → 739 entries in ~50 minutes, and `source/emulation/cpu/normal/normalCPU.cpp` was edited at 21:00:13 UTC (a `BW64_INST_TRACE` ring-buffer tracer was added on top of the earlier INSTTRACE klog) while this report was being compiled. All numbers below are a snapshot as of 2026-10-08T21:00:53Z. A parallel lane (32-bit ntdll investigation) is actively editing. Quarantine snapshots should be taken immediately and treated as point-in-time.

## The CRLF→LF churn phenomenon

6 files show huge raw diffs that are mostly line-ending flips (HEAD = CRLF, worktree = LF). Raw vs whitespace-insensitive (`-w`) line counts:

| File | Raw diff lines | Real (`-w`) lines |
|---|---|---|
| `source/emulation/cpu/normal/normalCPU.cpp` | 876 | 64 (63+/1−; includes the 21:00 UTC tracer addition) |
| `source/kernel/ksocket.cpp` | 791 | 7 |
| `source/io/fszipopennode.cpp` | 480 | 12 |
| `source/io/fsfileopennode.cpp` | 318 | 86 |
| `source/io/fsfileopennode.h` | 122 | 14 |
| `source/io/fsopennode.h` | 152 | 4 |

Totals: **~5,173 raw diff lines vs ~2,582 real lines** across the 42 modified files. Practical consequences: (1) plain `git diff` is unreadable for these files — always use `-w`; (2) `git checkout -- <file>` would silently discard the small real changes hidden in the churn; (3) any future commit will show whole-file changes unless line endings are normalized first. Builds are unaffected (compiler ignores line endings).

---

## A. Modified tracked files (42) — by subsystem

### A1. 32-bit graphics bridges — MUST SNAPSHOT (uncommitted per policy, Stage-A trap-verified)

Rebuilt after `261e9976` (32-bit bridge templating) was reverted by `5323ae25` (crashed wasm at startup). Current approach: non-templated, separate 32-bit entry points. **No real 32-bit guest call has ever reached them** (gated on the ntdll 0xD3F8 crash); only the int-$0x80 trap dispatch was tested.

- `source/vulkan/vk64bridge.cpp` (+~1229 real lines): full `vk32Bridge` implementation — 32-bit guest-memory helpers (`readStruct32`/`rd32_32`/etc.), 32-bit handle minting from `VK32_HANDLE_BASE` (disjoint from 64-bit range), witness fn (`"vk32: FIRST trap - 32-bit guest libvulkan.so.1 mapped"`) and unimplemented-fn handling, replacing the old "32-bit entry point (stub)" comment. **Also contains a 64-bit behavioral change:** the `case VK64_fn_vkCmdCopyImage: return 0;` silent-drop stub was **removed** (copy-image now flows to real handling). Flagged for the D3D9-64 regression lane (`vkQueueSubmit: bad/stale handle` investigation) — this changes what the page tier receives.
- `source/opengl/gl64bridge.cpp` (+138 real): `gl32Bridge` real implementation (GLX query/version/visual/context stubs returning opaque handles, `glXSwapBuffers` present path), replacing the stub.
- `source/vulkan/vk64bridge_abi.h` (+23): 20 new `VK64_fn_*` ids — `vkCmdFillBuffer`, `vkCmdClearColorImage`, `vkCmdClearAttachments(+KHR)`, `vkCmdCopyImage2(+KHR)`, `vkCmdCopyBufferToImage2(+KHR)`, `vkCmdBlitImage/2(+KHR)`, `vkCmdResolveImage(+KHR)`, `vkCmdDrawIndirect/IndexedIndirect(+Count+KHR)`, `vkCmdExecuteCommands`, `vkCmdDispatch/Base/Indirect` — commented "Game-breaking stub implementations (immediate CPU emulation)". Shared with the guest shims.
- `tools/rootfs64/libvk64/libvk64.c` (+325): KHR aliases for core-promoted physical-device queries (`vkGetPhysicalDeviceProperties2KHR` etc. — vkd3d queries KHR names) + wrappers trapping to the 20 new fnIds.
- `tools/rootfs64/libvk32/libvk32.c` (+59): 32-bit Vulkan ABI handle types (dispatchable = 32-bit, non-dispatchable = 64-bit).
- `tools/rootfs64/libvk64/libvulkan.so.1`: **binary** rebuilt, 80328 → 84848 bytes (+4520). This is the guest shim containing the new wrappers.
- Untracked companions (all part of this lane): `source/vulkan/vk32bridge_abi.h` (3.4 KB), `source/opengl/gl32bridge_abi.h` (789 B), `source/vulkan/vk32_guest.h` (407 KB, **generated** by tracked `tools/vulkan/gen/gen_vk32_guest.py` — regenerable, but keep the output until the build is proven reproducible), `source/test/cpu/testVk32Trap.{cpp,h}` + `source/test/cpu/test_bridge_stubs.cpp` (Stage A trap tests), wired into `source/test/testMain.cpp`.
- Verification status: Stage A trap tests pass per MEMORY.md (`0x564B0000 → -ENOSYS`, `0x474C0000 → -ENOSYS`); the 599/599 native test run includes the trap-witness tests. **Compiled into the current dist wasm**: `"FIRST trap"` strings found 4× in `dist/runtime/boxedwine64.wasm`.
- Safe to lose? **No.** This is weeks of lane work; losing it sets the 32-bit graphics lane back to the 261e9976 revert point.

### A2. VFS path cache + read-ahead — MUST SNAPSHOT (coherent uncommitted feature)

Follow-on to the committed `dccec718` FsZip fixes. Related probes: `web/tests/pathcache-bench-probe.mjs` (untracked), `test-results/vkboot-nopathcache2`.

- `source/io/fs.cpp` (+217 real): `Fs::pathCache` — LRU (4096 entries) canonical-path→node cache with hit/miss atomics; `BW64_NOPATHCACHE` / `BW64_PATHCACHE_STATS` env gates; `clearPathCache()`, `logPathCacheStats()`, `invalidatePathCachePrefix()`.
- `source/io/fs.h` (+58): cache key/entry structs + a detailed coherency contract comment (positive entries validated on hit; negative entries carry parent `childrenVersion`; prefix invalidation on remove/rename; pathCacheMutex is a leaf lock).
- `source/io/fsnode.cpp` (+22) / `source/io/fsnode.h` (+12): `removedFromParent` + `childrenVersion` atomics; invalidation on add/remove child.
- `source/io/fsfilenode.cpp` (+6): invalidate on path change.
- `source/io/fsdynamiclinknode.h` (+1): `isDynamicLink() → true` (dynamic-link walks never cached).
- `source/io/fsfileopennode.cpp` (+86 real) / `source/io/fsfileopennode.h` (+14): 64 KB read-ahead buffer (`readBuffer`, `expectedPos` tracking to avoid `getFilePointer` syscalls, periodic hit-rate klog). Comment cites the ELF/PE loader's 52B/56B reads with backward seeks.
- `source/io/fsopennode.h` (+4 real): `internalRead` moved to protected + override in subclass.
- `source/io/fszipopennode.cpp` (+12 real): `length()`/`seek()`/`readNative()` now use `zipNode->length()` directly instead of `node->length()` — comment explains `FsFileNode::length()` can return 0 for ZIP entries via stale nativePath stat while the scan's `zipInfo` has the correct `uncompressed_size`. (Relates to the disproven "zipInfo.length=0" theory in `wineboot-theories-disproven.md`.)
- Verification status: **unverified as a committed unit** (no fix commit; no in-browser proof cited). Likely compiled into the 14:52 UTC wasm (built from the dirty tree) — assumed, not string-verified.
- Safe to lose? **No** — coherent, reviewed-quality feature work; re-deriving the coherency protocol would be expensive.

### A3. Small fixes / optimizations — KEEP (low risk, mostly obvious)

- `source/kernel/ksocket.cpp` (+7 real): `patchWinebootEventCreate` now called only for 32-bit processes (was on every `sendmsg`).
- `source/kernel/knativesocket.cpp` (+9/−4): stack buffer (4096 B) for small `sendmsg` instead of heap alloc — wineserver-path optimization.
- `source/kernel/kunixsocket.cpp` (+4/−1): SIGPIPE fix — `thread->cpu64` → `thread->process->is64Bit`; comment references the 32-bit-runSignal-on-64-bit-thread mis-delivery class.
- `source/kernel/loader/loader.cpp` (−1): removed `loader = interpreter;` in `ElfLoader::inspectNode` (ELF interpreter handling).
- `source/kernel/loader/loader64.cpp` (+3/−1): `klog_fmt` `%x`/`unsigned int` type fix.
- `source/kernel/devs/devdsp.cpp` (+4): `ioctl64` for 64-bit-guest OSS ioctls (M5 sound bridge; wine winealsa → libasound → /dev/dsp route). Audio path **never exercised** — unverified.
- `source/emulation/cpu/jit/jitCodeGen.cpp` (+4): skip `FLAGS_*64` in JIT flag codegen ("Phase 0: no 64-bit guest can execute yet") instead of panicking in `getCF()`.
- `source/sdl/multiThreaded/threadedMainloop.cpp` (−1): **removed a stray trailing `#endif`**. Directive-balance check: HEAD's copy ends at depth −1 (one extra `#endif`, likely introduced by `7aa15d64`); the worktree copy is balanced (depth 0) and is the compilable one — proven by the 13:13 UTC `.o` and 15:04 UTC test binary built from it. (My first read of this diff inverted the story; the balance script corrected it.)
- `dist/app.mjs` (+3/−3): `launch()` gains `arch` param, passed as `?arch=` (32/64-bit launcher contract from `1d473769`).
- `dist/vkwebgpu.mjs` (+76/−2): unified leveled logging (`bwLog`, `bwLogRl`, `?loglevel=`/`?logfilter=` URL config, `window.__bwLogConfig`) + trace logs in `bwVkChunk` and v2 READBACK decode. Page-tier diagnostic infra.
- `project/linux/makefile` (+12/−6): native test build excludes `source/emulation/cpu/tests/*` harnesses (own `main()`), excludes `gl64bridge.cpp`/`vk64bridge.cpp` (need `BOXEDWINE_GUEST_X64`+OpenGL; Stage A tests trap dispatch only), adds `source/x11wire/*.cpp`.

### A4. Diagnostic instrumentation — KEEP UNTIL LANES CLOSE (do not bake into release blindly)

- `source/emulation/cpu/normal/normalCPU.cpp` (+64 real): (1) `NEXT()` macro now emits `klog_fmt("INSTTRACE %x %x", …)` **unconditionally** for EIP in `[0xF82A, 0xFBC0]` — the 32-bit ntdll crash region (32bit-investigation: `vkboot32-esptrace1/2`, `vkboot32-insttrace1`); (2) **added 21:00 UTC during this inventory**: `BW64_INST_TRACE` ring-buffer tracer (4096 entries: EIP, GPRs, FS base, raw bytes, tid; `bw64DumpInstTrace()` dumps the tail on fault; call site `#ifdef`-gated, empty stub otherwise). The NEXT() klog is NOT ifdef-gated — a perf hazard for any 32-bit code in that range.
- **Not in the current dist wasm**: 0 `"INSTTRACE"` hits in `dist/runtime/boxedwine64.wasm` (built 14:52 UTC, before the instrumentation was added). **Consequence:** the next wasm rebuild from this tree will bake the unconditional NEXT() logging in. The D3D9-64/input/audio lanes should know their next build carries it.
- `dist/vkwebgpu.mjs` bwLog (A3) also belongs here functionally.

### A5. Test / probe infrastructure — KEEP

- `source/test/cpu/testPushPop.cpp` (+48) / `.h` (+1) / `source/test/testMain.cpp` (+4): `push [esp+disp8]` ordering regression test for the ntdll 0xD3F8 investigation (displacements `0x18/0x24/0x3c` from the crash sites). **Verified: 599/599 native tests pass.** `testMain.cpp` also wires `testVk32TrapWitness` / `testGl32TrapWitness` (Stage A).
- `web/tests/input3-probe.mjs` (+8), `web/tests/tri10swap-probe.mjs` (+4): slot-system + `waitForBuildIdle()` integration (probe-lane discipline from AGENTS.md / BUILD_PROTOCOL.md).
- `web/tests/suite/browser/boot32-vkboot.test.mjs` (+2/−1): URL fix → `/runtime/index.html?arch=x86&test=vkboot`.
- `web/tests/suite/lib/probe-base.mjs` (+3/−1): import-path fix + `server.url` assignment.

### A6. Build outputs / binaries — KEEP AS ARTIFACTS

- `dist/runtime/boxedwine64.wasm`: **binary**, 2925769 → 3010804 bytes (+85 KB). Built **2026-10-08T14:52:24Z** by `agent:3710995` from `c912f821` + 726 dirty files (per `dist/runtime/BUILD_INFO.json`; `web/runtime/BUILD_INFO.json` identical). md5 `e2ee30de8d72125bf1bdee635dcc0aad`; `dist` and `web/runtime` copies identical. Contains: vk32/gl32 bridges, VFS path-cache work (assumed), `dccec718`'s FsZip fixes (were uncommitted in the tree at build time). Does NOT contain: INSTTRACE (added later). **Every probe run after 14:52 UTC used this binary** (vkboot64-fixtest/fresh1, vkboot32-pristine1, tri10swap reverify, input3, audio-verify, dxvk-d3d9-64-verify1).
- `dist/runtime/boxedwine64.js`: regenerated emscripten glue (export names reminified, e.g. `__wasm_call_ctors` → `Vh`; −12 KB). Rebuild artifact, no hand-written change.
- `dist/runtime/wine32.zip`: **binary**, 210394150 → 210395551 bytes (+1401). md5 `d4aaeecabab3c747d8c5683cffe0e4dc` (dist ≡ web copy). Per `32bit-investigation.md`: **restored to pristine (md5-verified)** after Owner E/F/G binary-patch contamination (0xF87C NOP + 0xFB4F unknown patch). **HEAD's copy (`6bf8ef6bbf4f0c2dfe9791dcedfa555a`) is the contaminated one — never `git checkout` this file.**
- `dist/runtime/wine64.zip`: **binary**, −616 bytes vs HEAD (md5 `237f19e072b0c1b918f118548bc3d0bb`). Provenance unknown (possibly the `vkboot64-prefixrevert1` prefix experiments, 13:36 UTC). Flagged, not explained.
- `dist/runtime/prefix32.zip`: **binary**, same size (5915477) but content differs (md5 `00fc34049129a8640fb5af5ebd388510`). Provenance unknown.
- `tools/dxvk/src`: submodule pointer unchanged (`0cf05780`); worktree dirty only — nested `include/spirv` + `include/vulkan` submodule pointer bumps and `subprojects/.wraplock` (meson setup residue). Low value; the DXVK build already consumed it.

### A7. The stash — DO NOT DROP

- `stash@{0}`: `wip-diagnostics`, created **2026-10-07 00:31 UTC**, 3 files:
  - `source/kernel/devs/devdsp.cpp`: identical `ioctl64` ifdef change as the worktree → redundant, already preserved in worktree.
  - `source/kernel/kunixsocket.cpp`: **1178 real (non-whitespace) changed lines NOT present in the worktree** (worktree has only the 4-line SIGPIPE fix). An older/larger kunixsocket experiment — superseded or stashed-for-later; content unknown without deeper reading.
  - `source/emulation/cpu/normal/normal_other.h`: `CALL_E32_ZERO` diagnostic klog (call-to-zero tracing) — not in worktree, not in wasm.

---

## B. Untracked files (694) — by area

### B1. `source/` (6) — all 32-bit-bridge lane, MUST SNAPSHOT
`vulkan/vk32bridge_abi.h`, `opengl/gl32bridge_abi.h`, `vulkan/vk32_guest.h` (407 KB generated), `test/cpu/testVk32Trap.{cpp,h}`, `test/cpu/test_bridge_stubs.cpp`. None are in `.gitignore` (checked) — simply never added.

### B2. `web/tests/` (20) — probe scripts & docs, KEEP
`build-lock.mjs` (build coordination used by probes — referenced by `BUILD_PROTOCOL.md`), 7 audio probes + `AUDIO_VERIFY_README.md`, `vkboot32-{esptrace2,freshwasm,nop1,ownerf1}.mjs` (ntdll investigation variants), `vkboot64-{fixtest,fresh1,verdict1}.mjs` (64-bit boot fix evidence), `tri9pong-slot.mjs`, `tri11-93-stats.mjs`, `input3-setupapi-verify.mjs`, `tmp-jrcxz-boot-probe.mjs` (JRCXZ regression), `pathcache-bench-probe.mjs`, `click-test.mjs`, `audio-{direct-p,gz,boot2-probe,tone-probe-v2}.mjs`, `parallel-probe-README.md`.

### B3. Top-level (26) — diagnostic probes + protocol doc, KEEP (small)
25 one-off `.mjs` probes: `wbdiag{,2,4}-probe.mjs` + `wb-probe{,2,3}.mjs` (wineboot c00000e5), `vkboot32-probe{,2}.mjs`, `vkboot-{persist,minimal}-probe.mjs`, `bind32/bind64/bindconnect-probe.mjs` (Unix-socket bind experiments — relate to ksocket work), `wineserver-measure-probe.mjs`, `wscount-hex-probe.mjs`, `mmap-probe.mjs`, `fsnode-verify-probe.mjs`, `dt3diag-probe.mjs`, `dispatcher-test-probe.mjs`, `desktoptest-seh-probe.mjs`, `checktmp-probe.mjs`, `verify-sh-fix.mjs`, `b64-probe.mjs`, `wineboot-probe.mjs`; plus `BUILD_PROTOCOL.md` (build/probe coordination protocol: one writer of `dist/runtime/boxedwine64.{wasm,js}`, `bw64-build` wrapper, `/tmp/bw64-build.lock.d/`).

### B4. `tools/` (23) — test programs & build wrapper
- `tools/bw64-build.sh` (5.8 KB, 2026-10-08 12:44): the build wrapper from BUILD_PROTOCOL.md — **KEEP, it's load-bearing infra**.
- `tools/d3dtests/` (3.7 MB): `d3d10dev`, `scene10`, `scene9`, `tri11_93` sources + built PEs.
- `tools/desktoptest/` (1.1 MB): deskpin desktop tests (`desktoptest{,2,3}`, `pthread_once_*`) sources + PEs.
- `tools/dxvk/tri9/` (1.6 MB): `tri9-32.exe`, `tri9input{,2}.c/.exe`, `fpsinput.c/.exe`.
- `tools/openglTest/` (1004 KB): `glhello`, `trigl` sources + PEs.
- `tools/vkd3d/probe/` (1.2 MB): `scene12.c/.exe`, `winevulkan.dll`.
- All are regenerable from source (sources are present); binaries are convenience. Disposable if space matters, but harmless.

### B5. `dist/` (5) — mixed
- `dist/ptest.zip` / `dist/ptest2.zip` (172 KB each): `pthread_once_test.exe` + `runtest.bat` — desktoptest payloads. Disposable.
- `dist/runtime/BUILD_INFO.json`: build provenance (see A6). KEEP.
- `dist/runtime/glibc-rootfs32.zip` (2.2 MB): untracked rootfs zip (2026-10-08 06:47). Provenance unclear; small — keep until identified.
- `dist/runtime/libvulkan.so.1` (80 KB, 2026-10-08 06:07): **stale copy of the OLD guest shim** (md5 differs from the rebuilt `tools/rootfs64/libvk64/libvulkan.so.1`). Safe to delete once noted — it is not what the build uses.

### B6. `web/runtime/` (5)
- `boxedwine64.js` / `boxedwine64.wasm`: md5-identical copies of `dist/runtime/` (the served copies). Keep in sync or delete; they duplicate dist.
- `wine64-launcher.js` (84 KB): untracked launcher script. Provenance unclear — keep until identified.
- `BUILD_INFO.json`: duplicate of dist's. Harmless.
- **`ziXQMNcy` (162 MB, 2026-10-08 13:49, mode 600): CORRUPT/TRUNCATED.** `PK\x03\x04` header, first entry `usr/`, but no ZIP central directory — Python `zipfile` rejects it; tail bytes are mid-stream deflate data. Unidentifiable partial download (not portal.zip, not glibc-rootfs32.zip — md5s differ). **Safe to delete; it is unusable as-is.** Flagged explicitly because of its size.

### B7. `project/linux/Build/` — native build outputs
`.o` files + `Build/Test/boxedwine` test binary (15:04 UTC, 8.7 MB — the binary that ran 599/599). Regenerable. Disposable.

### B8. `test-results/` — 547 dirs + 711 files, **155 MB** — probe evidence, SELECTIVELY KEEP
Each dir is typically `console.log` + `summary.json`. Key evidence dirs (referenced by handoff/investigation notes — keep):
`vkboot32-pristine1` (ntdll crash on pristine ntdll), `dxvk-d3d9-64-verify1` (D3D9-64 regression evidence, 17:08 UTC), `input3-input3-1791478901724` (relMouse:false), `audio-verify-1791478991164` (infra failure), `vkboot64-{fixtest,fresh1,verdict1}` (64-bit boot fix), `dxvk-tri10swap-1791478832186` + `dxvk-tri10swap-r56reverify7` (D3D10 spirvfix R5/R6), `vkboot32-{esptrace1,esptrace2,insttrace1,nop1,ownere1,ownerf1,ownerg1,cleanwasm1,freshwasm1,freshwasm2}` (ntdll investigation), `vkboot64-prefixrevert1`, `vkboot-nopathcache2`, `updatets-diag1`, `audio-verify-1`, `portal-e2e-2` (if present).
The rest (hundreds of older probe dirs) are superseded evidence — disposable in bulk, but cheap to keep.

---

## C. Verified vs assumed ledger

**Verified by direct evidence in this inventory:**
- Counts: 739 dirty entries (42 modified tracked, 694 untracked) at 2026-10-08T21:00:53Z; tree grew during inventory.
- The 6 CRLF→LF churn files and real-vs-raw line counts (via `git diff -w --numstat`).
- `threadedMainloop.cpp`: worktree balanced (depth 0), HEAD has the extra `#endif`; worktree `.o` (13:13 UTC) + test binary (15:04 UTC) prove it compiles.
- `dist/runtime/boxedwine64.wasm` provenance (BUILD_INFO: 14:52:24 UTC, from `c912f821`+726 dirty, builder agent:3710995); dist ≡ web/runtime copies (md5).
- `wine32.zip` dist ≡ web copy; differs from HEAD; investigation notes claim pristine restore.
- Current wasm contains vk32/gl32 bridges (`"FIRST trap"` ×4) and does NOT contain INSTTRACE (0 hits) or CALL_E32_ZERO (0 hits).
- `boxedwine64.js` change is rebuild reminification only.
- Stash `wip-diagnostics` exists (2026-10-07 00:31 UTC) with kunixsocket content not in the worktree.
- `ziXQMNcy` is a truncated, unusable 162 MB file.
- `tools/dxvk/src` dirty = nested submodule pointer bumps + `.wraplock` only; top pointer unchanged.
- Branch is `integrate/schema-v2`, HEAD `dccec718`; no staged changes; nothing was committed/pushed by this inventory.

**Assumed / not verified:**
- That `wine32.zip` is truly pristine (relies on the investigation note's "md5-verified" claim; no independent reference hash was checked here).
- That the VFS path-cache code is in the 14:52 wasm (built from the dirty tree, but not string-verified).
- Provenance of `dist/runtime/wine64.zip` (−616 B) and `prefix32.zip` (content change) — unknown; guessed prefix experiments.
- That the 20 new `VK64_fn_*` ids have matching implementations in `vk64bridge.cpp`'s switch (the diff hunks show the ABI + shim side; the bridge-side cases were not individually enumerated here).
- That no other lane edited files between the last check and this report (the tree is live).

---

## D. Quarantine recommendations (concrete, prioritized)

1. **Snapshot NOW to outside the repo** (tree is live; numbers already drifted). Suggested command for the operator (NOT run by this agent — read-only task):
   `mkdir -p /home/ubuntu/backups/dirty-tree-20261008 && tar -czf /home/ubuntu/backups/dirty-tree-20261008/must-keep.tar.gz -C /home/ubuntu/boxedwine64 <file list> && cp dist/runtime/BUILD_INFO.json /home/ubuntu/backups/dirty-tree-20261008/`
   Must-keep file list:
   - `source/vulkan/vk64bridge.cpp source/opengl/gl64bridge.cpp source/vulkan/vk64bridge_abi.h source/vulkan/vk32bridge_abi.h source/opengl/gl32bridge_abi.h source/vulkan/vk32_guest.h`
   - `tools/rootfs64/libvk32/libvk32.c tools/rootfs64/libvk64/libvk64.c tools/rootfs64/libvk64/libvulkan.so.1`
   - `source/io/fs.cpp source/io/fs.h source/io/fsnode.cpp source/io/fsnode.h source/io/fsfilenode.cpp source/io/fsdynamiclinknode.h source/io/fsfileopennode.cpp source/io/fsfileopennode.h source/io/fsopennode.h source/io/fszipopennode.cpp`
   - `source/emulation/cpu/normal/normalCPU.cpp source/kernel/ksocket.cpp source/kernel/knativesocket.cpp source/kernel/kunixsocket.cpp source/kernel/loader/loader.cpp source/kernel/loader/loader64.cpp source/kernel/devs/devdsp.cpp source/emulation/cpu/jit/jitCodeGen.cpp source/sdl/multiThreaded/threadedMainloop.cpp`
   - `source/test/cpu/testPushPop.cpp source/test/cpu/testPushPop.h source/test/cpu/testVk32Trap.cpp source/test/cpu/testVk32Trap.h source/test/cpu/test_bridge_stubs.cpp source/test/testMain.cpp`
   - `dist/app.mjs dist/vkwebgpu.mjs project/linux/makefile tools/bw64-build.sh BUILD_PROTOCOL.md`
   - `dist/runtime/boxedwine64.wasm dist/runtime/boxedwine64.js dist/runtime/wine32.zip dist/runtime/wine64.zip dist/runtime/prefix32.zip dist/runtime/BUILD_INFO.json`
   - Web probes: `web/tests/build-lock.mjs web/tests/vkboot64-fixtest.mjs web/tests/vkboot64-fresh1.mjs web/tests/tri9pong-slot.mjs web/tests/tri11-93-stats.mjs web/tests/pathcache-bench-probe.mjs`
   - Evidence: `test-results/vkboot32-pristine1 test-results/dxvk-d3d9-64-verify1 test-results/input3-input3-1791478901724 test-results/vkboot64-fixtest test-results/vkboot64-fresh1 test-results/dxvk-tri10swap-1791478832186`
2. **Do NOT `git stash drop`** — `wip-diagnostics` holds kunixsocket work absent from the worktree.
3. **Do NOT `git checkout --`** `dist/runtime/wine32.zip` (restores the binary-patched copy), the 6 CRLF-flipped files (discards hidden real changes), or `dist/runtime/wine64.zip` / `prefix32.zip` (provenance unknown).
4. **Safe to delete now:** `web/runtime/ziXQMNcy` (162 MB, corrupt — biggest single saving), `dist/ptest.zip`, `dist/ptest2.zip`, `dist/runtime/libvulkan.so.1` (stale shim copy).
5. **Before the next wasm build:** decide on `normalCPU.cpp` — it now carries unconditional INSTTRACE logging in `NEXT()` plus the ifdef'd `BW64_INST_TRACE` ring buffer. Either keep (32-bit lane needs it) with the D3D9-64/input/audio lanes aware, or revert the NEXT() klog to keep the next build clean. Also note the build will include the `vkCmdCopyImage` behavior change (A1) — the D3D9-64 regression lane should diff bridge behavior against the 14:52 wasm with this in mind.
6. **Line endings:** before any commit, normalize the 6 churned files (or commit with explicit `-w` review). Recommend `git add -p`-style selective staging — never `git add -A` on this tree.
7. **test-results/ hygiene (optional):** keep the key evidence dirs listed in B8; the other ~500 dirs are superseded and can be archived/deleted when space matters. Nothing in `test-results/` is needed for a build.
8. **Generated-file note:** `source/vulkan/vk32_guest.h` (407 KB) is generated by the tracked `tools/vulkan/gen/gen_vk32_guest.py`; if the generator is deterministic, the output is regenerable — but keep the current copy until a rebuild proves it.
