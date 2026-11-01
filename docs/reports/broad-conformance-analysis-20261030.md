# Broad conformance analysis — 2026-10-30

What stands between this code and (1) DaVinci Resolve, (2) every demo
program working, and (3) how the project tests itself against what the
implementation is *supposed* to do, so whole categories of unknown bugs
are caught before a program hits them.

Method note. Every load-bearing claim below is either (a) quoted from
this tree's code or records with the location named, or (b) produced by
the two new census tools in `tools/conformance/`, whose full output is
in `test-results/pe-census/` and `test-results/syscall-census/` in this
worktree and regenerates in seconds. Headline census findings were
independently re-verified with `objdump` against the staged binaries.
Where a statement is a hypothesis rather than a finding, it is labelled
one. Base tree: integ tip `100fccf9f`; runtime measured = the deployed
runtime at `/home/ubuntu/kn0tzer/winbox/runtime` (build `efa0189a1`),
i.e. exactly what a user's browser downloads today.

---

## 1. DaVinci Resolve

Resolve's published minimums (Blackmagic Design, Resolve 19/21
readmes): Windows 10 64-bit; **16 GB system memory**; a GPU with at
least 2–4 GB VRAM that supports **OpenCL 1.2 or CUDA 11+**; a CPU with
**AVX2** support (Intel 6th-gen / Ryzen 1000 or newer); quad-core class
CPU. Resolve enforces the GPU-compute requirement itself: with no
OpenCL/CUDA device it exits at startup with a "no OpenCL capable GPU"
error rather than degrading.

What this emulator presents to a guest, from the code:

| Requirement | The emulated machine today | Source |
|---|---|---|
| 16 GB RAM | **1 GiB** total, by deliberate design ("The emulated machine RAM size: 1 GiB") | `source/kernel/ksystem.cpp`, `KSystem::guestRamInfo` |
| Quad-core CPU | **1 CPU** (`guestCpuCount()` returns 1; CPUID leaf 1 also reports one logical processor) | `source/kernel/ksystem.cpp`; `source/emulation/cpu/cpu64.cpp` CPUID |
| AVX2 | CPUID ceiling is **SSE4.2 + POPCNT**; the handler's own comment: "Report the implemented baseline through SSE4.2/POPCNT; AVX, BMI, and other unsupported extensions stay clear." No AVX of any generation executes. | `cpu64.cpp`, CPUID leaf 1/7 handling |
| OpenCL 1.2 | `opencl.dll` is staged and the Linux-side `libOpenCL.so.1` loader is in the rootfs — but there are **zero ICDs** behind it (no `*.icd`, no POCL, no Mesa CL in `glibc-rootfs64.zip`), and the string "opencl" appears nowhere in the emulator source. `clGetPlatformIDs` can only ever return no platforms. | staged zip listings; `grep -ri opencl source/ include/` = no hits |
| CUDA 11+ | No `nvcuda`/CUDA runtime anywhere in the staged set or the source. (The `vk*Cuda*NV` names in the Vulkan bridge are the unrelated VK_NVX binary-import extension traps.) | staged zip listings |
| 2–4 GB VRAM | The Vulkan bridge advertises a **2 GiB** device-local heap — at the old floor, below the current 4 GB one. Moot while no compute API exists. | `source/vulkan/vk64bridge.cpp`, `fillMemoryProperties` |

**Conclusion.** Resolve is blocked by four independent floor failures,
and not one of them is a "Resolve bug." Each is a property of the
emulated machine or a whole missing API:

1. **No GPU-compute API exists in the stack at all.** This is the hard
   wall: Resolve's entire processing engine is OpenCL/CUDA kernels.
   The route that fits this project's architecture — and the strategy
   of broad fixes over per-program work — is an OpenCL implementation
   over the existing Vulkan/WebGPU bridge, the same move the project
   already made for D3D12 (vkd3d-proton over the vk bridge). Wine
   already ships the `opencl.dll` front-end; what is missing is any
   ICD behind it. When that capability lands for its own sake, every
   OpenCL program benefits, and Resolve becomes a test case rather
   than a target.
2. **No AVX/AVX2 in the CPU64 emulator.** Also a category item: modern
   Windows software increasingly treats AVX2 as baseline. The work is
   "implement the AVX2 instruction families correctly in both engines
   and only then advertise them in CPUID" — the differential harness
   (`tools/cpu-diff`) is the instrument that can prove correctness
   family by family.
3. **The machine definition is 1 GiB / 1 CPU.** Raising it is not a
   constant flip: the 1 GiB figure was chosen so Wine and the JVM size
   their heaps sanely inside a browser tab's real limits, and the
   memory managers' committed-page accounting hangs off it
   (`guestRamInfo` sums both managers). It moves when the underlying
   managers can truthfully back more, and it should move as a machine
   definition change with its own tests, not as an app workaround.
4. **Scale headroom, stated as unproven rather than as a blocker:**
   Resolve's installer is ~4 GB (the lazy-fetch machinery is proven at
   609 MB on Celeste, not at 4 GB), and Resolve runs dozens of threads
   and processes against a session layer whose start reliability is
   still being fixed (section 2, layer 1). These settle by measurement
   when 1–3 land, not by speculation now.

Nothing in this list is advanced by touching Resolve itself. That is
the point of the strategy: Resolve compatibility arrives as a side
effect of the compute API, the CPU surface, and the machine definition
becoming correct — or it does not arrive at all.

---

## 2. The fifteen demo programs

The live site offers exactly 15 demos (verified against the deployed
`demos.mjs`: notepad, explorer, snake, tetris, doom, celeste, chromium,
portal, gangbeasts, quake3, mindustry, winmerge, alkhaser,
godot-squash, unigine-tropics). Their blockers separate cleanly into
four layers. The first layer was measured fresh for this analysis; the
rest are the project's own records, with their dates kept attached
because the records span five different builds.

### Layer 0 — can the program even load? (measured 2026-10-30)

`tools/conformance/pe_census.py` resolved every PE import of every
demo against the deployed runtime's export tables, splitting regular
vs. delay-load imports and load-path (entry closure) vs. dead bundled
files. Totals: 4,328 staged PEs censused for the runtime self-check
(106,596 named imports), and all 15 demos.

- **13 of 15 demos have a completely clean load path**: every regular
  and delay import reachable from the entry executable resolves.
- **godot-squash cannot load at all as staged.**
  `SquashTheCreeps.exe` imports `user32.dll!GetPointerPenInfo` as a
  *regular* import — the executable has no delay-import directory at
  all — and the staged `user32.dll` does not export it. It exports the
  rest of the family (`GetPointerDevices`, `GetPointerInfo`,
  `GetPointerType`, `GetPointerTouchInfo`,
  `GetPointerTouchInfoHistory`); the pen variant is simply absent.
  Verified twice: by the census and by `objdump` on both files. This
  demo has never had a run; its blocker was found by a script, in
  seconds, without a browser. The fix class is "complete the
  pointer-input family in the staged user32" — one fix that unblocks
  every program importing it, which is precisely the category effect
  the new strategy asks for.
- **mindustry's launcher path is clean, but its runtime path has two
  real gaps:** the bundled `steam_api64.dll` regular-imports
  `kernel32.dll!CopyFile2`, which neither staged `kernel32.dll` nor
  its forward target `kernelbase.dll` exports (objdump-verified: 0
  hits in both); and the bundled JRE's `net.dll` delay-imports
  `iphlpapi.dll!GetAnycastIpAddressTable`, absent while its sibling
  `GetUnicastIpAddressTable` is present. Neither explains the known
  stage-C stall by itself (layer 2); they are independent latent
  failures on paths a real session exercises.
- **Everything else flagged is provably benign**, and the census is
  what makes "provably" honest: Portal's three items are in dead SDK
  files the loader never pulls in (`mysql_wrapper.dll`,
  `bugreporter.dll`, `datamodel.dll`); WinMerge's two are in its
  bundled msys2 shell DLL, not the app; Chromium's three
  (`ndfapi.dll`, `bthprops.cpl`, `powrprof.dll!PowerReadACValue`) are
  all *delay* imports of `chrome.dll` — optional OS features Chrome
  probes for at runtime, not load requirements.
- **Consequence for the Chromium investigation:** Chromium's entire
  load path resolves (517 named imports on the load path, 0
  unresolved). Whatever is wrong with Chromium through the site, it is
  behavioural, not loader-level. One hypothesis class is now closed
  with evidence.
- **Runtime self-check finding — the D3DKMT question.** The staged
  vkd3d-proton `d3d12core.dll` (both architectures) regular-imports
  nine `gdi32.dll` functions that the staged `gdi32.dll` does not
  export: `D3DKMTOpenResource2`, `D3DKMTOpenResourceFromNtHandle`,
  `D3DKMTOpenSyncObjectFromNtHandle`, `D3DKMTQueryResourceInfo`,
  `D3DKMTQueryResourceInfoFromNtHandle`, `D3DKMTShareObjects`,
  `D3DKMTDestroyAllocation`, `D3DKMTDestroyKeyedMutex`,
  `D3DKMTDestroySynchronizationObject`. Staged gdi32 exports a
  15-function D3DKMT subset covering adapter/device/statistics only
  (both sides objdump-verified). D3D12 rendering demonstrably works on
  this exact set (the campaign's probes pass), so the module loads in
  practice; what is unproven is the exact surface those nine functions
  implement — shared resources and keyed mutexes, i.e. DXGI shared
  handles and D3D11↔D3D12 interop, which no current probe exercises.
  Recorded here as an open conformance question with a concrete
  settling experiment (a guest probe that creates and shares a heap
  through vkd3d), not as a convicted bug. The only other self-check
  item is cosmetic: a prefix-bundled hex editor delay-imports
  `kernel32!GetDurationFormat`, which is absent.

### Layer 1 — does the session start at all?

Before any demo's own code runs, the session itself must start, and
this is the layer that currently gates all 15. Measured through the
real page path on the shipped build (§4.68 of the campaign report):
**5 of 12 sessions started (41.7%)**. Dead boots share one signature
(the desktop-init "explorer failed to start" death, §4.27), and
intervention tests proved a dead session cannot be rescued from
inside. The merged launcher fix (§4.68, integ `100fccf9f`, not yet
deployed at the time of writing) watches for that signal and restarts
the boot automatically — 6 of 7 starts succeeded at load ≲8 in its
proof window — but it is a mitigation around an unfound cause, and the
cause is in the code: per the user's standing correction, box load is
not an accepted explanation for it. Finding that cause is the single
highest-value reliability item in the project, because every demo and
every future program passes through this door.

### Layer 2 — does it run correctly once started?

Per-demo state from the project's records (`docs/game-compatibility.md`,
`docs/WINBOX-STATUS.md` open problems, campaign report). Evidence dates
matter: no two rows rest on the same build.

| Demo | State on last evidence | Named remaining item |
|---|---|---|
| notepad, explorer | Rendered (Oct 17–20 acceptances) | None known; evidence is old — verification debt, not known breakage |
| snake, tetris | Mapped in matrix runs (Oct 20–22); loader-clean per census | Gameplay/input never formally established |
| doom | Runs; **500 ms frame floor**: in steady state the guest is blocked ~96% of each cycle inside its own pacing loop as serviced by Wine's timing/message APIs (frame-floor investigation, 2026-10-27) | The responsible wait not convicted to a line of code |
| celeste | Title/menu render; lazy-fetch stall fixed and proven (609 MB, zero stalls) | Menu glyph tiles render fragmented (gl.md visual row); in-level play unverified on current builds |
| chromium | Full acceptance passed Oct 16–17 (UI, typing, JS, HTTP); user reports it then worked every time through the site | Currently fails through the site; loader layer now ruled out (layer 0); behavioural cause under separate investigation |
| portal | Menu and first level rendered with correct textures (Oct 19–23) | `D24_UNORM_S8_UINT` MSAA rejection named in the status record; 500 ms pacing observed by the user; full traversal unverified |
| gangbeasts | Unity title rendered (Oct 22 build); on later builds presents occur but the page output stays blank downstream of `vkQueuePresentKHR` | Present-to-page path unattributed; playable match unverified |
| quake3 | Reached active game with sparse/distorted rendering (Oct 23); later: presents stop while GL calls continue — a guest-progress fault | kernel.md's epoll row is the open register item; see the census hypothesis below |
| mindustry | Stalls after the `[RAM]` log with zero presents ("stage-C") | kernel.md K-M2 hypothesis (interpreter control flow) open; plus the two layer-0 gaps on its runtime path |
| winmerge | Never given a full run on a current build (solo gates paused Oct 26) | Loader-clean per census; otherwise unmeasured |
| alkhaser | Never given a full run on a current build | Loader-clean per census; it is itself a conformance instrument (section 3) |
| godot-squash | Never run | **Cannot load** — `GetPointerPenInfo` (layer 0) |
| unigine-tropics | Never run | Loader-clean per census; its run is the first real D3D10 program test |

A hypothesis the syscall census generates for quake3, labelled as
such: the x86-64 kernel dispatches `epoll_wait` (232) and
`epoll_pwait2` (441) but **not `epoll_pwait` (281)** — the variant
glibc's `epoll_pwait()` actually issues. If any host-side component in
the Q3 path waits through `epoll_pwait`, it receives ENOSYS. One grep
through the stack's components settles whether anything calls it; the
census exists to produce exactly this kind of cheap, checkable
hypothesis instead of another week of per-program archaeology.

### Layer 3 — performance shape

Three structural facts bound every demo's frame rate: the guest is a
**single-CPU** machine; the 32-bit JIT is slower than the interpreter
(accepted, ~1.26–1.29× on startup workloads); and the Doom pacing
floor (layer 2) shows at least one wait in the guest/Wine timing path
quantising frames to 500 ms while the shared host layers measure
clean. Per-program FPS work is a separate lane's brief; the
conformance point is that pacing is a *contract* (a 1 ms sleep should
wake in ~1 ms, a performance counter should advance monotonically at
its stated frequency) and contracts can be probed directly, without a
game — see section 3, item 6.

### The meta-finding

The demo matrix was last composed on 2026-10-26 and its per-demo
evidence spans at least five different builds. On the *current*
deployed build there is no complete, same-build, 15-demo acceptance
record at all. "All demos working" is therefore not merely untrue —
it is currently **unmeasured**, which is why breakages keep being
discovered by the user instead of by the project. Layer 0 is now
measured and nearly clean; layers 1–2 need the same treatment on one
build.

---

## 3. Testing the implementation against what it is supposed to do

### The shift

The project's registers (`docs/known-gaps/`) are good — and they are
encounter-driven: nearly every row exists because a program hit it.
The enumeration was always the set of things already known to be
wrong. The shift is to enumerate from the **specification side**:
for every surface, take the authoritative external list (the Linux
syscall tables, the PE import/export contract, the Vulkan registry,
the x86 ISA, the Windows SDK export lists, documented API semantics),
diff it mechanically against the implementation, and disposition
*every* entry. The enumeration is a program, re-run on every change,
so new surface or lost surface fails loudly instead of shipping
silently. A census with unexamined red rows is the backlog — it
replaces per-program triage as the thing the project works from.

### What yesterday's coverage actually was, counted

- Vulkan register: 380 rows, all dispositioned — but the bridge
  coverage matrix that backs it exercised 266 vk64 handlers of a
  ~700-function API, and its GL sibling found 96 stubbed cases of 306
  (2026-10-09). Encounter-complete, spec-incomplete.
- CPU differential (`tools/cpu-diff`): 167 instruction forms across
  three curated sets, of the thousands the ISA defines. Even so it
  convicted three real bug classes (IMUL flag computation, FLD
  double-push, ADC/SBB auxiliary-carry) that no program had yet
  reported — the method works; its coverage is the gap.
- Kernel syscalls: never enumerated until today.
- PE loader surface: never enumerated until today.

### Put in place by this analysis (running, with findings)

1. **`tools/conformance/pe_census.py`** — the loader surface
   (section 2, layer 0). Caught a demo that cannot load before anyone
   ran it, closed the loader hypothesis for Chromium with evidence,
   and surfaced the D3DKMT conformance question. Re-run cost: ~35 s.
2. **`tools/conformance/syscall_census.py`** — the kernel surface.
   First count of the unexamined surface: **x86-64: 168 of 363 named
   syscalls have no dispatch case; i386: 223 of 440 table slots are
   null, and 22 further numbers (440–461, including `futex_waitv`)
   fall off the end of the table entirely.** Most of the list is
   legacy surface nothing calls — which is now a *stated, checkable*
   property of the list instead of an assumption. The disposition
   pass over these lists is register work in the existing style
   (implemented / ENOSYS-is-correct / unreachable-with-reason), and
   entries like `epoll_pwait` (281), `accept4` (288), `dup3` (292),
   `signalfd4` (313) — modern calls real libraries issue — go first.

### Next, in the order the evidence argues for

3. **Vulkan/GL full-spec census.** Same script shape as the syscall
   census: enumerate the Khronos `vk.xml` function list against both
   bridges' trap tables, and the GL entry-point list against the
   libgl64/libgl32 handler tables. The existing 380-row register then
   becomes the disposition layer over a *complete* enumeration.
   The pattern already exists in miniature and works: the bridge's
   `vk-capabilities.json` manifest is diffed against the code by a
   unit test, precisely so advertised capabilities cannot drift from
   implemented ones.
4. **DLL export completeness census.** The reverse of the loader
   census: for each staged Windows DLL, diff its exports against the
   documented surface for that DLL. Every absent export is a program
   that cannot load, found before the program is in the test set.
   `GetPointerPenInfo` is the type specimen; the census generalises
   it from one demo's bad luck to a per-DLL number. (The PE-parsing
   half already exists in `pe_census.py`.)
5. **CPU differential expansion.** Grow `tools/cpu-diff` from curated
   sets to generated form/operand fuzzing against its qemu oracle,
   prioritising untouched families (string ops, x87 transcendentals,
   SSE shuffle/rounding behaviour, atomics, and — when it lands —
   AVX2, proven family by family before CPUID advertises it).
6. **Semantic probe families.** Presence is not behaviour: a resolved
   function can still return wrong answers (the pointer family could
   be "completed" with stubs and Godot would load and still fail).
   The condwait audit is the model: take one contract family
   (synchronisation primitives, timers, waitable objects, file and
   section semantics, performance-counter monotonicity) and write
   guest probes from the *contract*, run in the native harness with
   no browser involved. The session-start death and the Doom pacing
   floor both live in this territory — timing and waiting contracts
   are the project's most failure-dense surface and the least
   systematically probed.
7. **Environment fidelity as a suite.** Al-Khaser — already a demo —
   is a ready-made conformance suite for "does this environment
   behave like a real Windows machine" (timing sources, CPUID
   consistency, artefact residue). Dispositioning every one of its
   checks against the current build converts an entire category of
   future program refusals into a checklist.

### What this buys

The next `GetPointerPenInfo` is found by a script on an ordinary day,
not by a user clicking a demo. One disposition fix — completing the
pointer family — unblocks every program that imports it, including
programs nobody has tried yet. That is the difference between
whack-a-mole and the strategy the project has now adopted: the code
approaches "on paper, no known compatibility issues," and the paper
is generated by tools that re-check it on every change.

---

## Appendix — reproducing the numbers

```sh
# Loader census (~35 s, no browser, no build)
python3 tools/conformance/pe_census.py \
  --runtime-dir /home/ubuntu/kn0tzer/winbox/runtime \
  --games-dir /home/ubuntu/kn0tzer/games \
  --out test-results/pe-census

# Syscall census (<2 s)
python3 tools/conformance/syscall_census.py \
  --source-dir . \
  --unistd32 /usr/x86_64-linux-gnu/include/asm/unistd_32.h \
  --out test-results/syscall-census
```

Caveats, stated once and applicable throughout: import resolution is
a necessary condition, not a behavioural proof; API-set imports
(4,702 in the self-check) resolve through Wine's api-set database,
which the census counts but does not validate; ordinal imports (89 in
the self-check) are counted, not name-checked; dynamically loaded
libraries do not appear in import tables, so bundled native libraries
were censused as files in their own right (for Celeste, all 557 named
imports across its bundled native libraries resolve).
