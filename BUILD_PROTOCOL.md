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
runtime inputs, takes an atomic lock and refuses live probe slots. A live
owner keeps the lock regardless of age. An incomplete/malformed owner record
blocks the build; readers also wait. Only a validated dead PID can be reclaimed.
`BW64_BUILD_WAIT_SECONDS` bounds the wait. `EMSDK` selects the SDK when emcc
is not already active; `JOBS` controls compilation.

The browser export replaces only `deskpin.exe`/`deskpin32.exe` in the pinned
prefix archives with the versioned helpers under `tools/rootfs64/gltest/`.
`desktop-pin-provenance.json` records helper and resulting prefix hashes and
is included in the browser evidence fingerprint. The provenance test checks
every other archive member against the pinned base. Rebuild both helpers
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
whose PID is dead. Existing helpers bound the wait. Keep the
runtime lock/slot directories shared across local processes. Tests of lock
behavior use isolated temporary directories.

Raw outputs live in ignored `test-results/<tag>/`. Keep the summary, hashes,
console errors and screenshots together. Curated reports are committed under
`docs/reports/`; recorded API counts do not verify correct pixels or gameplay.
