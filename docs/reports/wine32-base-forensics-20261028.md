# wine32.zip base-tree forensics (2026-10-28, wine32base lane)

## The gap

`web/runtime/wine32.zip` (239,760,722 B, sha256 `955cc41c…`, 1,926
members) is assembled from a base tree plus in-tree post-steps. The
post-steps were scripted (`stage-alsa32.sh`,
`stage-vkd3d-payloads32.sh`, both wired into
`apply-runtime-poststeps.sh`), and the sibling 32-bit glibc archive's
base was reconstructed by the sysroot32 lane — but the wine32 base
tree itself had no committed producer and no recorded provenance:
the runtime register's header even listed it as "regenerable via
`build-wine64-zip.sh`, `tools/build-prefix32.sh`,
`build-prefix64.sh`", none of which produces it (they build the
64-bit zips and the two prefixes). Any future 32-bit Wine payload
change would have been archaeology again. This lane identifies the
base from the bytes, scripts the regeneration, and proves it.

## Census (shipped archive, by provenance class)

| Class | Members | Source (byte-proven) |
| --- | ---: | --- |
| WineHQ wine-stable-i386 10.0.0.0~**noble**-1 tree (`opt/wine-stable/{bin,lib}`, `share/man`) | 1,337 files | §identification |
| — of which patchelf'd (`__isoc23` DT_NEEDED injected) | 12 files | §injection |
| — of which `.dll.so` duplicates added | 22 files | §duplicates |
| — `libisoc23shim.so` added (2 placements) | 1 file ×2 | §preserved |
| Debian bookworm libwine 8.0~repack-4 share data (`share/wine` in both trees) | 78 files ×2 placements | §share-data |
| usr/ re-layout copies of the WineHQ tree | 301 files + 2 loaders | §relayout |
| Debian bookworm i386 system-library closure (`usr/lib/i386-linux-gnu/*.so`) | 71 files | §closure |
| Locally built `usr/lib/wine/wineserver32` (preserved) | 1 file | §preserved |
| Post-step: ALSA→OSS plugins (in-tree `alsa32/`) | 2 files | `stage-alsa32.sh` |
| Post-step: PE32 vkd3d-proton d3d12 pair (replaces the deb's pair) | 2 files | `stage-vkd3d-payloads32.sh` |
| Directory entries (incl. empty `share/wine/fonts/`, `usr/lib/i386-linux-gnu/gstreamer-1.0/`) | 32 | — |

Total: 1,926 members = 1,894 files + 32 directory entries. The
accounting closes exactly — opt/: 1,337 (deb) + 22 duplicates + 1
shim + 78 share data = 1,438; usr/: 2 loaders + 301 wine modules
(279 deb + 22 duplicates) + 1 shim + 78 share data + 71 closure +
2 ALSA post-step + 1 wineserver32 = 456. See the verifier in
`regen-wine32-zip.sh` step 4, which diffs the regenerated archive
against the shipped one member-by-member.

## Identification — the Wine build is WineHQ **noble**, not bookworm

The register and READMEs pin "WineHQ wine-stable 10.0" without a
distro flavor. The obvious candidate — the bookworm build the
project itself pins for the 64-bit backport DLLs
(`tools/rootfs64/winehq10-x86_64/README.md`) — matches the file SET
exactly but only 469 of 1,337 files byte-for-byte. The **noble**
build of the same package version
(`wine-stable-i386_10.0.0.0~noble-1_i386.deb`, 110,481,570 B,
sha256 `5ba523409f013b12953f577bc3322aeae599bb16afac66c59ebe2dde7b337bfa`,
dl.winehq.org Ubuntu pool) matches **1,323 of 1,337** byte-for-byte.
The 14 exceptions are fully explained: the d3d12 pair (the vkd3d
post-step's pins, §post-steps) and the 12 patchelf'd unix modules
(§injection). The trap is real: the 64-bit backports came from the
bookworm deb, the 32-bit tree from the noble deb — same version
string, different build.

## Injection — the 12 patchelf'd unix modules

`bcrypt, crypt32, mountmgr, nsiproxy, ntdll, opengl32, secur32,
winealsa, winebth, winebus, winex11, winspool` (`.so`, in
`i386-unix/`) each differ from the noble deb by exactly one added
DT_NEEDED, `libisoc23shim.so`, and the ELF restructure patchelf
performs. They are **exactly** the modules carrying undefined
`__isoc23_*` dynamic symbols in the noble deb (rule verified by
scanning its `i386-unix` modules; the bookworm build's modules
carry none), and `patchelf --add-needed libisoc23shim.so`
(patchelf 0.18.0) on the deb's files reproduces the shipped bytes
**byte-for-byte** (demonstrated on bcrypt.so, sha256
`a4b473b0…` both ways; the regeneration proof covers all 12). The
motive is consistent with the era: noble-built modules reference
glibc 2.38's C23 strto*/scanf symbols; the guest's 32-bit glibc was
then bookworm 2.36 (the noble glibc swap the sysroot32 lane found
was a separate, later hand step), so the modules needed the shim to
load at all — `web/tests/vkboot32-wine10shim1.mjs` is the boot probe
from that fight.

## Duplicates — the `.dll.so` copies

22 unix modules exist in the zip under both `<name>.so` and
`<name>.dll.so` (avicap32, bcrypt, crypt32, ctapi32, dnsapi, dwrite,
kerberos, localspl, msv1_0, netapi32, ntdll, odbc32, opencl,
opengl32, qcap, secur32, win32u, winedmo, winegstreamer, winevulkan,
wpcap, ws2_32), in both the opt tree and the usr re-layout. Every
`.dll.so` is byte-identical to its `.so` sibling in the shipped
archive (verified for all 22); the deb ships only the `.so` names.
The regeneration carries the pinned list and re-verifies the
byte-equality invariant.

## Share data — Debian libwine 8.0, both placements

The noble i386 deb ships no `share/wine` data at all (only man
pages). All 78 files of the zip's `share/wine` trees — `wine.inf`
(unchanged, it literally reads `;; Version: Wine 8.0`) and the full
`nls/` set including the generated tables (`locale.nls`,
`sortdefault.nls`, `norm*.nls`, `l_intl.nls`) that differ between
Wine versions — are byte-identical to Debian bookworm
`libwine_8.0~repack-4_i386.deb`'s `usr/share/wine` (95,293,888 B,
sha256 `c992aad6a47d81b719514f5d06c5df10267f1e40a6f4f3b4e3f655cbbf308d57`).
The same 78 files appear at both `opt/wine-stable/share/wine/` and
`usr/share/wine/`. (68 of them also match the WineHQ 10.0 main
package's data — codepage tables are stable across versions; the 9
version-discriminating files all say 8.0.) The fonts subdirectories
ship empty in both placements: Debian splits the fonts into
`fonts-wine`, and this project's font payloads are post-step
business for `glibc-rootfs32.zip`.

## Re-layout — the usr/ tree

The usr/ tree is the same WineHQ build re-laid to Debian paths (the
layout `tools/build-prefix32.sh` and the 32-bit launcher expect):
`usr/lib/wine/wine` and `wine-preloader` are byte-identical copies
of the deb's `opt/wine-stable/bin/` files;
`usr/lib/i386-linux-gnu/wine/` is `i386-unix/` copied wholesale
(including the patchelf results and the `.dll.so` duplicates) minus
`libisoc23shim.so`, which instead sits at
`usr/lib/i386-linux-gnu/libisoc23shim.so` where the loader's search
path finds it for every consumer. `usr/share/wine/` mirrors the
libwine data as above. None of these bytes is Debian wine 8.0's:
Debian's own `usr/lib/wine/wine` is 13,924 B (the zip's is the
deb's 13,672 B WineHQ loader) — the re-layout is of WineHQ bytes
only.

## Closure — the 71 system libraries are bookworm i386 debs

The 71 members under `usr/lib/i386-linux-gnu/` (excluding `wine/`,
`alsa-lib/`, and the shim) are the DT_NEEDED/dlopen closure of the
Wine binaries against Debian **bookworm i386** — every one of the
71 is byte-attributed to a specific package version (64 distinct
debs): 31 match debs still in the live bookworm pool, 40 match only
via snapshot.debian.org's version history (the pool has moved on —
e.g. libx11-6 `2:1.8.4-2+deb12u2`, libpng16-16 `1.6.39-2+deb12u6`,
libgnutls30 `3.7.9-2+deb12u7`, libglvnd0 `1.6.0-1`). The full pin
table (member → package, version, deb sha256, size, URL — pool URL
or snapshot's content-addressed file URL) lives in
`tools/rootfs32/build-wine32-base.sh` and is hash-verified on every
run. Method notes: candidates came from bookworm's Contents-i386
index; `.so` members in debs are usually symlinks, so comparisons
must read through them (a first pass that rejected symlinks scored
1/71); the noble (Ubuntu) pool was tested for sampled members and
ruled out. The set is NOT the project's in-tree libgl32/libvk32
shims — the zip carries the real GLVND/OpenCL/Vulkan loader set.

## Post-steps (not base; already scripted)

Applied by `regen-wine32-zip.sh` after the base zip is built, with
the in-tree staging logic: the ALSA→OSS pair
(`usr/lib/i386-linux-gnu/alsa-lib/libasound_module_{pcm,ctl}_oss.so`,
via `stage-alsa32.sh` against the sibling glibc archive — the zip's
only two members with 0644 attributes; everything else is 0600) and
the PE32 vkd3d-proton pair replacing the deb's bundled
`d3d12.dll`/`d3d12core.dll` in `i386-windows/` (the completeness
checker's `VKD3D32_PINS`: `872c3d65…` / `71e9c174…`;
`stage-vkd3d-payloads32.sh` proper stages into a tree's own runtime
dirs, so the regen applies the same payload to the scratch zip
directly and asserts the full pins).

## Preserved members — the residue (2 files, 3 placements)

Two base members are locally built artifacts whose sources do not
survive anywhere on the box (searched: the repo, all worktrees,
`/home/ubuntu/backups`, `/tmp`): no build tree, no script, no source
file. Both carry this host family's compiler banner (GCC Ubuntu
13.3.0-6ubuntu2~24.04.1).

| Member | Size | sha256 |
| --- | ---: | --- |
| `usr/lib/wine/wineserver32` | 3,794,568 | `ac990fd260af4be4c53b52405a815aab0ad54847fd1d6ccbe86a0c3018b58393` |
| `opt/wine-stable/lib/wine/i386-unix/libisoc23shim.so` = `usr/lib/i386-linux-gnu/libisoc23shim.so` | 15,252 | `ef5e5696a194937691e034e2a0675fb1e7bcc9c5c9d69563ed013f4556321e09` |

Ruled out on bytes: WineHQ's packages ship no standalone i386
wineserver at the Debian path (the main deb's `bin/wineserver` is a
744,588 B amd64-era tool); Debian wine32 8.0's `wineserver32` is a
different 683,548 B binary (sha256 `2a91df0a…`). The shipped
wineserver32's DWARF paths (`../wine-10.0/server/…`) show a local
build from a wine-10.0 source tree with a sibling build directory;
the shim is a single-file build (`isoc23shim.c`). A byte-identical
rebuild was judged out of reach (configure flags, build layout,
and the i386 dev environment are all unrecorded) and set aside in
favor of preservation, the sysroot32 discipline: the regeneration
takes both members from the currently shipped wine32.zip — itself
pinned in `tools/browser/runtime-assets.json` — and verifies their
sha256 before use, fail-closed. A future loss of the shipped
archive is detectable against the hashes above; if either source is
ever identified, check its output against these pins and retire
the extraction.

## The reconstructed pipeline

- `tools/rootfs32/build-wine32-base.sh` — assembles the base tree
  from pinned inputs only: the WineHQ noble deb + Debian libwine
  8.0 deb (both sha256-pinned fetches), the rule-derived patchelf
  injection (+shim placement), the pinned `.dll.so` duplication,
  the usr/ re-layout, the 71 pinned closure debs, the preserved
  members (hash-verified extraction from the shipped zip), and the
  empty `gstreamer-1.0/` fossil directory (a leftover of the
  pre-2026-10-25-split era; the plugin closure rides the support
  pack, but the shipped member set includes the empty directory,
  so the base keeps it, deliberately).
- `tools/rootfs32/regen-wine32-zip.sh` — base → zip (directory
  entries for every directory, files at the shipped 0600
  convention, DEFLATED) → post-steps → **member-by-member
  verification against the shipped zip** (name set, sizes, sha256;
  no allow-list) → completeness gate. `--install` publishes to
  `web/runtime/` after every check passes (followed by the standing
  re-pin: runtime-assets.json, artifact manifest, retained
  archive). Whole-archive byte identity is NOT expected — staged
  archives carry wall-clock member timestamps (the standing
  staged-archive invariant); member identity is the invariant.

## Proof (2026-10-28, this lane's worktree)

`regen-wine32-zip.sh` end-to-end (oracle: the shipped runtime set
era-matched to this branch's checker pins):

- Base tree: 1,892 files assembled; zipped 1,924 members; +2 ALSA
  post-step members = 1,926.
- **Member verification: IDENTICAL member set, sizes and sha256**
  vs the shipped wine32.zip (1,926/1,926).
- **Completeness gate: `check_runtime_completeness.py` — 0
  failures, 5 skips** (the pristine-tree profile: the skips are
  the support-pack/dist-dependent checks that run at build time).

Era note: the gate compares the whole runtime set, and this
branch's checker predates the vkd3d64 restage merge — gating
against the current integ runtime set fails exactly one check
(`vkd3d payloads`, on wine64.zip's restaged d3d12.dll vs this
branch's pins), an oracle-era mismatch, not a wine32 defect; with
the era-matched set (pre-restage wine64.zip from
`/home/ubuntu/backups/winbox-vkd3d64-restage-20261028-135228/`) the
gate is clean, and every wine32-specific check also passes against
the integ set.

## Verdict

**(b) PARTIALLY regenerable — with a two-member preservation
residue.** Every member of wine32.zip except `wineserver32` and
`libisoc23shim.so` (3 placements, 3,825,072 B) is reproducible from
pinned durable inputs by the committed scripts, proven
member-identical; the two exceptions are preserved from the
shipped archive under sha256 pins, fail-closed, exactly the
discipline the sysroot32 lane set for its glibc residue. No
runtime.md row needs to open for this: the register's wine32
provenance story is corrected by addendum (the header's
"regenerable via" list did not include any producer of this
archive; it now has one), and the preservation residue is named
here and in the base script's header.

## Methods note — two traps this investigation hit and now documents

1. **Crossed pin pairing.** An early pass of this census recorded
   the *bookworm* deb's sha256 next to the *noble* deb's URL (the
   two flavors share name, version string, and file set). The
   regen's fail-closed hash pin caught it on the first run —
   before anything was committed — which is what the pin is for.
   The scripts pin URL + sha256 + size as a triple for every input.
2. **Tarfile censuses lie about alias files.** A recheck that read
   debs through Python `tarfile` (never extracting to disk)
   inverted the flavor conclusion wholesale: Wine debs store many
   PE DLLs as hardlink/alias members, and `tarfile` resolves them
   to the wrong bytes (it reported the bookworm deb matching 1,323
   files; the disk-extracted truth is 469 — noble matches 1,323).
   Every census number in this report was re-derived from
   `dpkg-deb -x` disk extractions. Do not census packages through
   in-memory tar readers.
