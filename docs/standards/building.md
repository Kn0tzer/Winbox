# Standard: building

Three kinds of build exist. Each has exactly one entry point; do not
invoke the underlying makefiles or compilers directly for work you
intend to prove anything with.

## 1. The browser build (the shipped artifact)

From your tree's root:

    tools/bw64-build.sh

- The script is the one writer of `dist/` and the compiled pair in
  `web/runtime/`. It takes the shared build lock
  (`/tmp/bw64-build.lock.d`) and waits up to 600 s for it. To see
  whether a build is running: `tools/bw64-build.sh --check` prints
  `BUILD IDLE` or the current owner. Never bypass or delete the
  lock; a build that cannot get it fails loudly, which is correct.
  On a busy box the default wait can expire while another lane's
  build is still running — the sanctioned response is to raise the
  wait and name your build, the way lanes already do:

      BW64_BUILD_WAIT_SECONDS=14400 BW64_BUILD_OWNER=lane-<name> tools/bw64-build.sh

  The owner label is what `--check` shows everyone else, and it is
  recorded as the build's `builder`, so an unnamed long build is
  indistinguishable from a stuck one.
- The script also waits for running browser probes to drain before
  it builds (`waiting for probes to drain` in its output) — builds
  and probes share one coordination system, so a build queued
  behind long-running probes is normal, not stuck.
- Success is exactly one line in the output: **`Browser export
  verified`**. No other line — not "build complete", not a summary
  from another agent — is proof of a build.
- The build stamps its identity into `dist/runtime/BUILD_INFO.json`
  (also staged into `web/runtime/`). For any build you cite, record
  `git_sha` (must equal the tree's HEAD it claims to build),
  `git_dirty_files`, the wasm sha256, and the wasm size. The stamp
  describes the tree as the build found it, uncommitted changes
  included; a gate build runs on the clean composed tree and must
  show `git_dirty_files` 0.
- A build picks up whatever is on disk in your tree, including a
  half-saved edit. Build only from a state you could commit.
- Variants, selected by environment, never by editing the script:
  - `BW64_BUILD_TARGET=wasm64-mt-jit` builds the JIT variant into
    its own build directory. The two variants' outputs are **not**
    interchangeable.
  - `BW64_PROFILE=1` adds profiling function names at link time;
    it is recorded in `BUILD_INFO.json` as `profiling`.
- `tools/bw64-build.sh --export-only` re-exports `dist/` from an
  existing build directory without recompiling.

## 2. The native builds (test and release binaries)

In `project/linux/`, the make targets are the builds:

| Target | Product | Used by |
| --- | --- | --- |
| `make release` | `Build/Release/boxedwine` | the release configuration |
| `make test` | `Build/Test/boxedwine` | the main native suite |
| `make x64-test` | `Build/X64Test/boxedwine` | the x64 native suite |
| `make x64` | `Build/X64/boxedwine` | the 64-bit configuration |
| `make x64-cache` | cache-instrumented configuration | cache work |

- At a gate, **all five targets must exit 0**. A configuration that
  does not compile is a defect, not an inconvenience: file it.
- Compiles route through the shared compiler cache prepared by the
  fresh-copy standard, so a file compiled in any copy is reused by
  the others while its inputs and flags are unchanged. A cold first
  build of a target still compiles everything; that is expected.

## 3. What building never does

- Building never proves behaviour. A verified build plus the checks
  in the checks standard is the smallest unit of proof.
- Builds for a gate are made from a clean bootstrap of the gate
  tree (see the combining standard), never from a lane tree's
  leftovers.

Verified literally: 2026-11-05 —
`docs/reports/standards-verification-20261105.md`.
