# Documentation index

_The map of everything written down about this project (this
file: `docs/INDEX.md`): where it lives, what owns each subject,
and what kind of truth each document carries. Start at `docs/WINBOX-STATUS.md` for where the current
truth lives; this index is how the library itself is organized._

Three kinds of document, three different rules:

- **Living documents** describe the project as it is. They are kept
  accurate, and where a value can rot they name its source instead
  of restating it.
- **Design records** (`docs/specs/`) are the requirements a piece of
  work was built to, written before it shipped. They are evidence of
  intent, not descriptions of current behavior; each names its
  living owner.
- **Dated records** (`docs/reports/`) are what was measured or
  decided on the date in the name, against the build it names. They
  are never edited after the fact; their index is
  `docs/reports/README.md`.

## Living documents — one owner per subject

| Subject | Owner | Notes |
| --- | --- | --- |
| Where the truth lives; project status | `docs/WINBOX-STATUS.md` | States roles and sources, never rotating values. |
| How the runtime fits together | `docs/architecture.md` | Component map; names the maintained source for each responsibility. |
| The development queue (found defects) | `docs/defect-register.md` | Open section is the queue; a closure names its fixing commit and proof. Entries are never deleted. |
| Compatibility gaps, per area | `docs/known-gaps/` | Standing registers, guarded by tests — see below. |
| Demo compatibility verdicts | `docs/game-compatibility.md` | Verified evidence only; its header names the measurement each row rests on. Read by a unit test; keep its per-game rows. |
| Failure logging and recovery standard | `docs/failure-logging-and-recovery.md` | The pattern standard every lane follows; the `bwlog` API itself is in `include/bwlog.h`. |
| Vulkan wire format | `docs/vulkan-protocol.md` | The vk64 → WebGPU frame protocol; parsing lives in `web/vk-protocol.mjs` and tests pin the format. |
| Folder access behavior | `docs/folder-access-modes.md` | How the direct and copy modes behave for the user. Its design record is `docs/specs/folder-access.md`. |
| CPU JIT design | `docs/jit-experiment.md` | First-phase design and implementation notes (see its status header); current state is the register and the dated performance records. |
| Build and probe coordination | `BUILD_PROTOCOL.md` (repo root) | The build wrapper, probe slots, and evidence rules. |
| Day-to-day workflow | `docs/workflow.md` | Worktrees, builds, probes, and waiting on the shared box. |
| Test catalog | `web/tests/CATALOG.md` | Commands and evidence levels for the test suites. |

## The known-gaps registers (`docs/known-gaps/`)

Machine-guarded standing registers. Tests read them, so their
formats are load-bearing: change content, not structure, and run
the guarding tests after any edit.

- `coverage.md` — the coverage register: what has been audited,
  area by area. Guarded by the known-gaps audit test.
- `vulkan.md` — the Vulkan register. A unit test verifies its
  file:line citations against the source (the citations check), so
  lines other lanes cite must not be renumbered casually.
- `gl.md` — the OpenGL register.
- `kernel.md` — the kernel/syscall register.
- `runtime.md` — the runtime register. Guarded by the
  runtime-completeness test.
- `tests.md` — the test-infrastructure register.
- `runtime-artifact-manifest.md` — **generated** by
  `tools/rootfs64/gen_runtime_manifest.py`. Never hand-edit.
- `allowlist.json` — the audit's allowlist data.

## Design records (`docs/specs/`)

Requirements written before the work shipped (October 2026
build-out). Each file's header names its living owner.

- `chromium-networking.md` — early relay design for Chromium HTTP;
  shipped networking grew past it (see `docs/architecture.md`).
- `common-dependencies.md` — shared runtime payloads: the pinned
  redistributables, plus the same-environment-for-demos-and-uploads
  record merged into it in November 2026.
- `folder-access.md` — folder access requirements; behavior is
  owned by `docs/folder-access-modes.md`.
- `launcher-redesign.md` — the replacement launcher's requirements;
  outcome record: `docs/reports/launcher-redesign-20261017.md`.
- `portal-stencil.md` — one graphics repair's requirements.
- `startup-observations.md` — how a starting program is reported.

## Dated records (`docs/reports/`)

Everything measured or decided, by date: lane reports, merge and
deploy records, censuses, matrices, and investigation journals —
including `status-snapshots-20261030-20261105.md`, the dated state
sections that used to live in the status file. The complete,
mechanically verified listing is the "Complete listing" section of
`docs/reports/README.md`. Sub-collections with their own indexes:
`bridge-coverage/` (Vulkan/OpenGL coverage matrices and bug notes),
`cpu-differential/` (instruction-form comparisons),
`frametime-profile-pong2/` (timing method and ranked measurements),
`d3d9-complex/`, `wine32-final/`, `dirty-tree-inventory-20261008/`.

A few records live committed under `test-results/` rather than
here; they are named in `docs/reports/README.md`:
`test-results/alkhaser-int2d/REPORT.md`,
`test-results/frontier-review-20261031/REVIEW.md` (with its raw
review text, `test-results/frontier-review-20261031/claude-raw.md`),
and `test-results/site-chromium-20261030/REPORT.md`.

Two records are read by tests and must keep working:
`d3d-conformance-MATRIX.md` (consumed by the D3D entirety and game
capability tests) and `winbox-metrics-contract-20261018.md` (the
frame-metrics contract).

## Tool-local documents (outside `docs/`)

Project-owned notes that live beside the tool they describe. Most
are dated results or method records, not living documents.

- The vkd3d port's working papers: `tools/vkd3d/CURRENT_STATUS.md`,
  `tools/vkd3d/FEASIBILITY.md`, `tools/vkd3d/ICD_WORK_ITEMS.md`,
  `tools/vkd3d/PROBE_PLAN.md`, `tools/vkd3d/BUILD.md`,
  `tools/vkd3d/bda-research/r15-design-draft.md`,
  `tools/vkd3d/patches/README.md`, `tools/vkd3d/probe/PROBE_RESULT.md`,
  `tools/vkd3d/probe/VULKAN1_DLL_README.md`,
  `tools/vkd3d/tests/README.md`.
- `tools/dxvk/BUILD.md` — how the DXVK payload is built.
- `tools/cpu-diff/METHOD.md` and `tools/cpu-diff/BUGS.md` — the CPU
  differential method and its found-bug list;
  `tools/cpu-diff32/README.md` — the 32-bit variant.
- Dated benchmark results (bench tools write here; treat contents
  as records): `tools/bench/jit64-results.md`,
  `tools/bench/jit-results-r2.md`, `tools/bench/jit-results-r3.md`,
  `tools/bench/mem64-results.md`.
- Per-tool notes: `tools/conformance/README.md`,
  `tools/d3dtests/README.md`, `tools/openglTest/README.md`,
  `tools/regression/README.md`, `tools/vkcov/METHOD.md`,
  `tools/browser/README.md`.
- Rootfs payload notes: `tools/rootfs64/README.md`,
  `tools/rootfs64/fonts32/README.md`,
  `tools/rootfs64/fonts64/README.md`,
  `tools/rootfs64/wine8-backports-x86_64/README.md`,
  `tools/rootfs64/winehq10-x86_64/README.md`, and
  `tools/rootfs32/wine10-backports-i386/README.md` (the Wine 10
  backport set for the 32-bit rootfs: what was backported and why).
- SPIR-V fixture notes: `testdata/spirv/dxvk-live/README.md`,
  `testdata/spirv/geometry/README.md`,
  `testdata/spirv/rejected/README.md`,
  `testdata/spirv/subgroup/README.md`.
- `project/emscripten/buildInstructions.md` — emscripten build
  notes; points at `BUILD_PROTOCOL.md` for coordination.
- Test-area notes beside the test catalog: `web/tests/README.md`,
  `web/tests/AUDIO_VERIFY_README.md`,
  `web/tests/diagnostics/README.md`,
  `web/tests/parallel-probe-README.md`, `web/tests/suite/README.md`.

## Not project documentation

Vendored upstream trees carry their own documentation, owned by
their upstreams and replaced when the vendor pin moves:
`lib/` (asmjit, glew, imgui, pugixml, simde, softfloat,
tiny-process), `tools/dxvk/src/`, and `tools/vkd3d/src/`. They are
excluded from this index and from the project's accuracy passes.

## Keeping the library coherent

- One owner per subject. A new document about an owned subject is
  a merge into the owner, not a new file.
- Anything dated goes in `docs/reports/` with the date in its name
  and is never edited afterwards; corrections are new records.
- A living document that starts carrying rotating values (hashes,
  counts, verdicts) is wrong: move the values to their source and
  point at it.
- Add every new document to this index (or to the reports listing)
  in the same commit that creates it.
- Before removing a document, prove its disuse: no references from
  code, tests, scripts, or other documents, and no subject that
  only it owns.
