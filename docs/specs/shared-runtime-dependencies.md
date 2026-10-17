# Shared runtime dependencies

The launcher must give demos and uploaded programs the same Windows environment.
Celeste currently starts without the Mono and OpenGL libraries supplied by the
direct-runtime acceptance test. Fix the environment rather than patching the game
or adding a Celeste launch condition.

Provide Wine Mono 7.4.0 (including its FNA/SDL dependencies) at a system path and
the maintained 32-bit GL bridge at the normal Linux library lookup paths. Mount
these through a shared, versioned, lazy archive in every runtime session, before
application files. Wine loads Mono only when a managed program requests it; no
engine or executable-name rule should be needed. Uploaded files take precedence
over shared dependencies. Preserve existing explicit diagnostic overlays.

Acceptance: the demo button and an uploaded Celeste folder/ZIP reach visible game
content without overlay/ldpath options. Unrelated managed executables run from
nested folders on x86 and x64 without extra files. A separate FNA program renders
and accepts input through the upload flow. Native applications still launch.
Missing dependency assets produce a startup error instead of indefinite loading.

The existing C++/Wine runtime stays unchanged. Build the supplemental archive from
the pinned Wine Mono input and checked-in GL bridge, with deterministic entries,
hashes and provenance. Fetch only archive metadata and files actually opened by
the guest. No new npm dependencies, user settings or game-specific patches.

Verification: focused browser regression before/after; `pnpm test`,
`pnpm typecheck`, `pnpm build:export`; real launcher browser tests against the
export and deployed site. Retain logs, screenshots and hashes in `test-results/`.
Commit verified changes locally; do not push.
