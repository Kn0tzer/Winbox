# 32-bit sysroot regeneration (2026-10-28, sysroot32 lane)

## The gap

`tools/rootfs64/apply-runtime-poststeps.sh` — the single automated
finisher for the shipped runtime set — could not run end-to-end: its
first step, `tools/rootfs32/stage-fonts.py`, reads its payload from
`BW32_SYSROOT`, default `/home/ubuntu/wine32-stage/img386`, a staging
tree that no longer existed on the box. The closure producer that
built the 32-bit glibc rootfs (`closure32.sh`) existed only as an
untracked copy inside `/home/ubuntu/backups/wine32-stage-20261020/`.
Any future 32-bit payload fix therefore had no reproducible path:
the base artifact could not be rebuilt, and the post-steps could not
be re-run from scratch. This lane reconstructs that path, scripted
and pinned. (Scope note: the tasking named the product
"wine32.zip"; the img386-dependent artifact is in fact
`glibc-rootfs32.zip` — wine32.zip's post-steps, the ALSA plugins and
the PE32 vkd3d pair, are staged from in-tree sources and never needed
img386. wine32.zip's *base* tree is a separate undocumented build —
see Residue.)

## What img386 was (pinned from the consumers)

A `dpkg-deb -x` extraction tree of Debian bookworm i386 packages,
deb layouts preserved (`lib/` and `usr/lib/` separate real
directories — the stage-fonts payload paths prove it: fontconfig is
read from `usr/lib/i386-linux-gnu/`, expat from
`lib/i386-linux-gnu/`, matching the two debs' internal layouts, not
an installed merged-usr system). The surviving consumers read
exactly:

| Consumer | Paths read from img386 |
| --- | --- |
| `tools/rootfs32/stage-fonts.py` | `usr/lib/i386-linux-gnu/libfontconfig.so.1`, `lib/i386-linux-gnu/libexpat.so.1`, `usr/share/doc/libfontconfig1/copyright`, `usr/share/doc/libexpat1/copyright` (+ DejaVuSans and its copyright from the *host* `/usr/share`) |
| `closure32.sh` (restored) | BFS seeds `usr/lib/wine/wine`, `usr/lib/wine/wineserver32`, `lib/ld-linux.so.2`, all of `usr/lib/i386-linux-gnu/wine/i386-unix/*.so`; dlopen seeds `libfreetype.so.6`, `libgnutls.so.30`, `libGL.so.1`, `libvulkan.so.1`, `libgcc_s.so.1`; the `.so` universe it indexes; the eight X libraries its extras loop stages by name |
| `tools/build-prefix32.sh` | fallback only: `usr/i686-w64-mingw32/lib/zlib1.dll` (Debian's libwine ships no i386-windows `zlib1.dll`; verified — the fallback was the operative source) |

## What the shipped artifact actually is (byte-provenance)

The shipped `glibc-rootfs32.zip` (3,875,770 B, 154 members) is a
**hybrid of three vintages**, each proven by sha256 against the
published packages:

1. **Closure base — Debian bookworm i386.** All 15 non-glibc,
   non-poststep libraries match bookworm packages exactly
   (libdbus-1-3 1.14.10-1~deb12u1, zlib1g 1.2.13.dfsg-1,
   libbz2-1.0 1.0.8-5+b1, libcap2 1:2.66-4+deb12u3+b1,
   liblzma5 5.4.1-1+deb12u2, libgpg-error0 1.46-1,
   libgcc-s1 12.2.0-14+deb12u1, and the eight X libraries at their
   bookworm revisions). The preserved 2026-10-20 backup zip is the
   same closure at an earlier stage of its life (35 members,
   bookworm glibc 2.36-9+deb12u14, before post-steps and before the
   glibc swap).
2. **Font payload — bookworm, later point vintage.**
   libfontconfig1 2.14.1-4 and libexpat1 **2.5.0-1+deb12u4** match
   byte-for-byte (u1/u2/u3/u5 expat builds were tried and rejected
   on bytes; u4 is exact). The DejaVu members come from the build
   host's `/usr/share` and match this box's Ubuntu 24.04 files.
3. **The glibc set — Ubuntu noble, hand-swapped.** All 19 files of
   the `lib/i386-linux-gnu/` glibc set (plus `lib/ld-linux.so.2`)
   are Ubuntu noble `libc6` **2.39-0ubuntu8** — 15 of 19 match the
   release-pocket deb byte-for-byte, including `libc.so.6` (banner:
   "Ubuntu GLIBC 2.39-0ubuntu8") and the dlopen-only libraries
   (libnss_*, libresolv, libanl, …) that no DT_NEEDED closure
   stages — i.e. the swap copied the package's whole library
   directory into the artifact, replacing bookworm's glibc. Why
   this was done is not recorded anywhere; the swap itself was a
   hand step and is now scripted (adjustment A in
   `regen-glibc-rootfs32.sh`).

The remaining four glibc files do not match that deb — see Residue.

## The reconstructed pipeline (committed, `tools/rootfs32/`)

- **`build-img386.sh [OUT_DIR]`** — downloads 26 pinned packages
  (sha256-pinned in the script; Debian bookworm pools on
  deb.debian.org / security.debian.org — the pools
  `prepare-gstreamer.py` already pins from — snapshot.debian.org
  file URLs for the two versions the live pools no longer carry,
  libexpat1 2.5.0-1+deb12u4 and libz-mingw-w64 1.2.13+dfsg-1, and
  the Ubuntu archive pool for the noble overlay), verifies every
  hash before extraction, extracts with `dpkg-deb -x` into OUT_DIR
  (default: the historical `/home/ubuntu/wine32-stage/img386`, so
  every consumer's default path works again), keeps the noble
  libc6 in a separate `.glibc-noble/` overlay, and fails closed if
  any consumer-named path is absent. The wine seed packages
  (wine32 + libwine 8.0~repack-4) double as
  `tools/build-prefix32.sh`'s `stage` tree (same layout:
  `usr/lib/wine` + `usr/lib/i386-linux-gnu` + `usr/share/wine`).
- **`closure32.sh`** — the closure producer, restored verbatim from
  the backup copy (provenance in its header); IMG/OUT are now
  environment parameters with the historical defaults. Produces
  `rootfs32/` (the native-run rootfs `build-prefix32.sh` consumes)
  and the base `glibc-rootfs32.zip`.
- **`regen-glibc-rootfs32.sh`** — the end-to-end orchestrator:
  build img386 → closure → assembly adjustments → the 32-bit
  post-steps in `apply-runtime-poststeps.sh` order (stage-fonts
  with `BW32_SYSROOT`, stage-fonts32, stage-alsa32) →
  member-by-member verification against the previously shipped zip
  → `check_runtime_completeness.py`. Assembly adjustments, each
  proven by the shipped artifact: (A) replace the closure's
  bookworm glibc members with the noble overlay's full library
  set; (B) stage six `/lib`-resident libraries explicitly by
  SONAME from their pinned debs (`libdbus-1.so.3`, `libcap.so.2`,
  `liblzma.so.5`, `libbz2.so.1.0`, `libgpg-error.so.0`,
  `libgcc_s.so.1`) — on the extracted tree the restored BFS does
  not reach them; in the original tree they were transitive deps
  of the seed set (libdbus via libpulse, libcap/liblzma via
  libsystemd, the freetype/gnutls tails for the rest). Publishing
  after a regeneration is the documented re-pin (re-measure the
  zip in `tools/browser/runtime-assets.json`, regenerate the
  artifact manifest, rebuild the retained archive) — the same
  step every restage performs, since staged archives carry
  wall-clock member timestamps and are not byte-reproducible.

## Proof (this lane, worktree off integ `8bf268112`)

- Regenerated zip vs shipped zip: **154 members, identical member
  set; 150 members byte-identical**; the only differences are the
  four glibc aux members below.
- `check_runtime_completeness.py` on the regenerated set:
  **0 failures, 5 skips** (the pristine-state profile: the
  support-pack, manifest, CJK-cache and prefix64-built checks skip
  by name pre-build, as designed). Control: the integ tree's own
  checker reports 0 failures / 1 skip on its own set. (An early run
  mixed this lane's pre-restage checker pins with the integ tree's
  post-restage wine64.zip and failed the vkd3d pin check on that
  account; with the era-correct wine64.zip the failure is gone —
  it was never in the regenerated artifact.)
- `tools/browser/bootstrap.py --check` with the regenerated zip
  and the one-entry re-pin applied to a scratch copy of
  `runtime-assets.json` (glibc-rootfs32: 3,877,987 B,
  sha256 `30e88797…`): **exit 0, "Runtime assets verified
  (13 files)"**, completeness gate green inside.
- Fail-closed: the same regen without `--allow-glibc-aux-diff`
  exits 1 at the verification step, naming the four members with
  sizes and hashes, before the checker runs.
- `closure32.sh` on the reconstructed tree stages 30 files; its
  BFS missing list names the dlopen/transitive tails
  (libpulse, libgnutls's dep chain, libusb, …) that are absent
  from the extracted tree by design — those libraries ride
  wine32.zip's `usr/` tree in the guest union.

## Residue (named, quantified)

Four members of the shipped zip could not be reproduced from any
durable pinned input, and the regen fails closed on them unless
`--allow-glibc-aux-diff` substitutes the noble-release bytes
(same package as the other 15 glibc files):

| Member | Shipped bytes | Shipped sha256 |
| --- | --- | --- |
| `lib/i386-linux-gnu/libanl.so.1` | 13,736 | `909e55681ef286c6f941b9b8fcc5d3ac60b8b095d90c87fe3cddfef650171ff5` |
| `lib/i386-linux-gnu/libnsl.so.1` | 100,300 | `567f769667fc0923a750733a0328dde1d8b70079a3ae37e40388c5281e662069` |
| `lib/i386-linux-gnu/libpcprofile.so` | 13,852 | `0c8181eef79c7df5b891096a711af97cda79324d648b7b0b92d464242bc946b5` |
| `lib/i386-linux-gnu/libthread_db.so.1` | 42,952 | `472d351611a9970469dfb0ae44a8ab1cebd37da4df5ac2455ca6b1aa0c17deb3` |

170,840 uncompressed bytes of the 3.9 MB archive. Candidates
checked and rejected on bytes: Debian glibc 2.36-9+deb12u14,
Ubuntu 2.39-0ubuntu8 (i386 native and the amd64 `libc6-i386`
/lib32 build), Ubuntu 2.39-0ubuntu8.9, Debian glibc 2.41
(trixie). None of the four is DT_NEEDED by any guest binary
(libthread_db is a debugger library; libanl/libnsl/libpcprofile
are dlopen auxiliaries), so the substitution is low-risk, but it
is a substitution and is reported as one. If their source is ever
identified, its files can be checked against the hashes above in
one command and the allow-list retired.

Out of this lane's scope, named for the next pass: **wine32.zip's
base tree** (a Wine 10.0 `usr/` tree plus the WineHQ
`opt/wine-stable/` tree) has no committed producer at all — only
its post-steps are scripted. And `stage-fonts.py`'s DejaVu members
are read from the build host, so regenerating on a non-Ubuntu-24.04
host drifts those two members (the checker does not pin them; the
manifest census records them).

## Lost vs reconstructed vs invented

- **Lost:** the original img386 tree; the original closure
  invocation's exact seed environment (the BFS's reachability into
  the six explicitly-staged libraries); the provenance of the four
  glibc aux members; the motive for the noble glibc swap.
- **Reconstructed from evidence:** img386's required contents
  (consumer-pinned); every package version above (byte-matched);
  `closure32.sh` (surviving backup copy); the assembly adjustments
  (proven by the shipped artifact's member set and bytes).
- **Newly invented:** the pin table + hash-verified fetch in
  `build-img386.sh`; the `.glibc-noble/` overlay convention; the
  fail-closed member verification with its named allow-list in
  `regen-glibc-rootfs32.sh`. Nothing else in the tree changed:
  `stage-fonts.py`, `apply-runtime-poststeps.sh` and
  `build-prefix32.sh` run unmodified against the rebuilt tree.
