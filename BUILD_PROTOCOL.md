# Build and probe coordination

The build wrapper is the only writer of the generated emulator pair.
The page export, WASM/JavaScript and `BUILD_INFO.json` must agree. A probe
must not observe a build replacing its runtime files.

```sh
tools/bw64-build.sh                # compile, export, verify hashes/provenance
tools/bw64-build.sh --export-only  # refresh page using the verified compiled pair
tools/bw64-build.sh --check        # idle / live / initializing / dead owner
```

The wrapper resolves its repository from its own path, restores pinned
runtime inputs, takes an atomic lock and refuses live heavy probe slots. It
checks only `slot-*` files; light `light-*` suite slots do not block builds. A
live owner keeps the lock regardless of age. An incomplete/malformed owner
record blocks the build; readers also wait. Only a validated dead PID can be
reclaimed.
`BW64_BUILD_WAIT_SECONDS` bounds the wait. `EMSDK` selects the SDK when emcc
is not already active; `JOBS` controls compilation.

Coordination is scoped to the tree whose `dist/` is at stake (FD-224),
because a build replaces only its own tree's `dist/`. The build publishes
its claim under a file named for its tree, records its tree in the lock
info, and drains/validates only its own tree's probes; a probe in another
tree, or testing a remote dist, never blocks it. The claim is published
only while draining for a write or holding the lock — a build merely
queued for the lock publishes no claim, so probes in its tree can start
while it waits. The legacy single `build-pending` file still blocks every
tree, fail closed, until it is gone.

## Fresh copies and build seeds

A fresh worktree has no build directory (`project/emscripten/Build/` is
ignored), so without help its first build compiles all ~270 files from
scratch even though the project's builds are byte-for-byte reproducible.
The preparation step (`tools/winbox-wt-prep.sh`, installed at
`/home/ubuntu/scripts/winbox-wt-prep.sh`) therefore also installs a
**build seed** when the shared store holds one for the worktree's exact
HEAD commit: the seed is the `Build/Wasm64Mt` directory of a verified
from-scratch build of that commit, and the seeded copy's first build only
recompiles what changed after the commit — the same incremental behaviour
an already-built copy has always had. The prep verifies the seed's commit
and program-file hash before installing it, relocates the mirrored
source paths to the new worktree, and refreshes timestamps so the build
treats the seed as up to date. A missing or mismatched seed is not an
error: the copy simply starts unseeded, exactly as before.

Publish a seed after a verified build of a commit (for example the
integration tip at the end of a merge gate) with:

```sh
tools/winbox-publish-build-seed.sh <built-worktree>
```

(installed at `/home/ubuntu/scripts/winbox-publish-build-seed.sh`). It
refuses a worktree with modified tracked files or a `BUILD_INFO.json`
that does not match the worktree's HEAD, and refreshes the store's
`SHA256SUMS` so the prep's store verification covers the seed. Only the
standard browser target (`wasm64-mt`) is seeded. To force a from-scratch
build in any copy, delete `project/emscripten/Build/` and build as
usual; the wrapper's from-scratch path is unchanged.

The browser export replaces `deskpin.exe`/`deskpin32.exe` in the pinned
prefix archives with the versioned helpers under `tools/rootfs64/gltest/`.
`desktop-pin-provenance.json` records helper and resulting prefix hashes and
is included in the browser evidence fingerprint. The provenance test checks
every other archive member against the pinned base.
The export also adds the pinned Wine64 package's architecture-independent
fonts to Wine32's data directory in `glibc-rootfs32.zip`.
`wine32-font-provenance.json` records their hashes and the resulting archive;
its provenance test verifies every original archive member is unchanged.
Wine32's exported user registry also exposes the pinned DejaVu Sans font as
Arial through Wine's DirectWrite/GDI replacement list. Its registry hash is
recorded in the font provenance; all other registry data is preserved. The
desktop-helper provenance records prefix hashes after both export steps.
Both provenance records are included in the browser evidence fingerprint. Rebuild both helpers
from `deskpin.c` with the matching MinGW compiler after changing that source.

Do not run raw `make wasm64-mt` against the shared output. Use
`BW64_BUILD_OWNER` to label a build. The diagnostic override
`BW64_BUILD_FORCE=1` bypasses the probe guard and can invalidate a live probe;
use it only after stopping the affected probe.

Probes keep the existing coordination helpers:

```js
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
const slot = await acquireProbeSlot();
await waitForBuildIdle();
try {
  // Run the probe and record dist/runtime/BUILD_INFO.json with its summary.
} finally {
  await slot.release();
}
```

Callers release slots during cleanup; later probes reclaim abandoned slots
whose PID is dead. Direct probe callers use the heavy pool by default, with
two slots; browser suite `ProbeTest` instances use the light pool by default,
also with two slots. Heavy slots coordinate game, profile, replay, and
`celeste-boot` probes. Each kind has a FIFO wait queue and a four-hour default
wait (`PROBE_SLOT_TIMEOUT_MS`), so callers do not need retry loops. Slot files
contain only the PID; `info-<slotfilename>.json` sidecars hold owner details.
Use `node web/tests/probe-slots.mjs status` to inspect holders and waiters for
both kinds. The legacy `/tmp/bw64-probe.lock` check still applies to both
kinds. Keep the runtime lock/slot directories shared across local processes.
Tests of lock behavior use isolated temporary directories.

Raw outputs live in ignored `test-results/<tag>/`. Keep the summary, hashes,
console errors and screenshots together. Curated reports are committed under
`docs/reports/`; recorded API counts do not verify correct pixels or gameplay.
