# FD-39 — Texture settings are no longer silently dropped (2026-10-31)

Branch `lane-fd39`, off integ tip `f3fd57d58`. Fix commit
`ad93c01ea`. Register: FD-39 resolved in part (the silent drop is
gone; an effect-only residue stays open on the entry), FD-45
added for the query-surface gap found along the way.

## The conflict, and how it was settled

FD-15 left this set dropped + once-logged because both obvious
answers were wrong: accept-and-drop pretends support, and
INVALID_ENUM can fail Wine's Direct3D device creation, which
polls the GL error slot while creating its device. The register
itself named the way out — "the bridge's error reporting can be
scoped so a refusal reaches only the call that asked" — and
this lane implements the state-side form of that scoping:

- **The bridge owns the state for exactly this closed set**
  (LOD bias, GENERATE_MIPMAP, border color, depth-texture
  mode, texture swizzle). A set is validated: an invalid
  *value* records the standard error a real desktop driver
  raises (that is conformant — drivers do refuse bad enum
  values, and no program can be depending on a refusal *not*
  happening for a value it should never pass). A *valid* set
  never touches the host error slot at all, so the pollution
  FD-15 feared cannot occur. The value is stored per texture,
  and queries answer from the store with spec defaults.
- What made "support" dishonest before was pretending the
  value reached a sampler. It still does not, where no backend
  can express it — and that residue is now an explicit,
  narrowed register statement (below), not a silent drop.

## Per-setting outcome

- **GL_UNSIGNED_SHORT_5_6_5_REV (0x8364) — SUPPORTED.** Added
  to the unpack table next to FD-15's pair, following the
  table's established _REV rule (components packed from the
  least significant bit: R shift 0, G shift 5, B shift 11 —
  the same rule the existing 4_4_4_4_REV / 1_5_5_5_REV /
  8_8_8_8_REV entries follow). `texImageBytes` already counted
  this type at 2 bytes/texel; uploads now unpack to real RGB
  instead of reaching the host still packed (an enum GLES3
  does not accept at all).
- **GENERATE_MIPMAP (0x8c55) — SUPPORTED on the WebGPU
  backend.** Validated and stored; when the base level of a
  2D texture is (re)defined, the backend regenerates the full
  chain with its existing `generateMipmap` machinery (cube
  maps once all six faces have a base level, mirroring
  `generateMipmap`'s own completeness rule; rgba8 formats,
  generateMipmap's own limit). On the C++ / legacy-host route
  the set is stored and queryable; the host accepts the pname
  without error and its fixed-function effect is not modelled
  there.
- **LOD bias (0x8501), border color (0x100d), depth-texture
  mode (0x884b), swizzle (0x8e42–0x8e46) — VALIDATED, STORED,
  QUERYABLE on both routes** (C++ bridge store for the
  scalar entry points it traps; the WebGPU backend's params
  map on its route). Spec defaults answer before any set
  (depth mode LUMINANCE, swizzle identity, generate-mipmap
  FALSE, LOD bias 0, border color transparent black).
- **CLAMP_TO_BORDER wrap (0x812d) — SUPPORTED on the WebGPU
  backend**: accepted, stored, and wired to WebGPU
  `clamp-to-border` in the sampler. WebGPU's border color is
  fixed at transparent black, which is exactly GL's default
  border color; a guest-set non-default color remains stored
  state (see residue).
- **Residue (stays open on FD-39, effect-only):** the
  *sampling effects* of LOD bias, a non-default border color
  (WebGPU samplers have no borderColor field), and texture
  swizzle / depth-texture mode in shader sampling. These are
  now honest state — set, validated, queryable — whose
  rendering effect no backend here can yet express. Closing
  the residue needs sampler/shader work in a backend, not a
  different answer at the API surface.

## Scope boundaries found while working (registered, not hidden)

- The bridge ABI traps only the scalar texture-parameter
  entry points (glTexParameteri/f in, glGetTexParameteriv
  out). Vector-form calls — the form that carries a full
  border color — never reach this bridge, and the WebGPU
  backend implements no texture-parameter query at all.
  Registered as **FD-45** (uncharacterized) rather than
  silently left.
- The C++ store answers the scalar query form. A scalar set
  of a vector pname (border color, SWIZZLE_RGBA) updates
  component 0 only; the code says so at the store.

## Proof

- **Red/green.** `web/tests/suite/unit/gl-fd39-texparams
  .test.mjs` drives the real WebGPU backend with a recording
  device (acceptance + storage + validation per pname,
  GENERATE_MIPMAP regeneration producing levels 1 and 2 on
  base redefinition, CLAMP_TO_BORDER reaching the sampler as
  `clamp-to-border`, error slot clean after valid sets).
  `gl-fd39-honest-answers.test.mjs` compiles slices of the
  real bridge source into a native harness (the FD-15
  pattern): the real fixup unpacks 5_6_5_REV (pure red /
  green / blue / white / half-red texels decode exactly, and
  the same bytes under the non-REV type decode differently,
  proving the model is distinct); the real store classifies,
  validates, defaults, and round-trips per texture; the real
  dispatcher cases keep stored sets off the host and out of
  the error slot while forwarded pnames behave exactly as
  before. On the pristine base the JS test fails and the
  harness reports 30+ failed checks across every case; with
  the fix both pass.
- **Regression subsets.** All GL suite files green: the two
  FD-15 files, gl-webgpu (core, texture, fixed-function,
  clear, readback), gl-query-bookkeeping, gl-shader-compat,
  gl-guest-memory, gl-shim-coverage, abi-gl-layout,
  opengl-entirety. Full JS unit suite: 340 passed, 2 failed: no-silent-failure (that run read the store block before its scanner-driven restructure; the final code passes the scanner standalone with no baseline change) and provenance-artifacts (asserts on a built dist this lane did not produce -- see the build note).
- **Staging verification.** The lane tree's runtime assets
  were restored and hash-verified against the pinned manifest
  ("Runtime assets verified (13 files)", completeness 0
  failures). The pinned wine64 container exists nowhere on
  the box as a file, so it was reproduced member-for-member
  from the live base container with the committed stamped
  win32u.dll substituted — the result is byte-identical to
  the pin (sha256 `e777d052…`, 209,691,093 bytes), the same
  reproduction the FD-41/FD-42 merge record documents.
- **Full dist build: not run on this lane.** The box's disk
  sat at ~2.7 GB free with the critical-path readiness
  measurement actively writing to it; a multi-GB build there
  risked the measurement's evidence for a check the merge
  gate re-runs anyway. Every changed C++ region (store,
  validation, both dispatcher set cases, the query case, the
  unpack case) is compiled verbatim by the native harness
  above; no new symbols, includes, or build inputs were
  introduced. The merge gate's fresh build remains the
  whole-tree compile proof, as with prior lanes.

Not deployed, not pushed; the live site and master untouched.
