# Wine 10.0 i386 win32u unix module rebuilt with the desktop-wait fix

`web/runtime/wine32.zip` is staged from the WineHQ `wine-stable`
10.0.0.0 noble i386 deb (see `tools/rootfs32/build-wine32-base.sh` and
`docs/reports/wine32-base-forensics-20261028.md`). That module carries
the same two desktop-initialization impatience defects that FD-01
fixed in the 64-bit set (defect register FD-02):

* `get_desktop_window` waits only 10 s for the just-started desktop
  process to go input idle; on timeout the server fabricates a
  detached desktop window the real desktop process can never adopt —
  its own desktop window creation is refused — so the graphics
  driver is never published.
* `load_desktop_driver` probes that publication exactly once; on a
  miss it latches the null driver permanently for the process.

On hardware explorer wins both races easily; under emulation the
desktop parade takes minutes, so first-attempt 32-bit session starts
die the same way the 64-bit ones did. The 64-bit remedy could not
reach this module: it is a different vintage (Wine 10, not Wine 8)
from a different provenance (a WineHQ deb, not the tree's Debian
source build), and the backport pipeline built only the 64-bit set.

The file in this directory is **the same Wine 10.0 code the payload
ships**, rebuilt from the pinned `wine-10.0` source tarball with one
patch — `tools/buildWine/patches/wine10-win32u-desktop-wait.patch` —
which makes both waits patient (300 s), exactly as the Wine 8 patch
does for the 64-bit set. The patch's functions live in the unix half
of the module, so only `win32u.so` is rebuilt; the PE half
(`win32u.dll`) ships untouched from the deb. Wine 10's
`load_desktop_driver` differs slightly from Wine 8's (a null-guid
fallback when the guid atom lookup fails, and a negated callback
result); the patch preserves both behaviours and retries only a
missing publication — a named driver or a driver error remains a
definitive, immediate answer, and a desktop that is already up costs
nothing (the first probe is the stock probe).

Unlike the Wine 8 backports, this module is built **with freetype
and fontconfig compiled in**: the stock module's font driver dlopens
`libfreetype.so.6` and `libfontconfig.so.1` at runtime and the guest
ships both libraries, so a `--without` build would silently strip
font rendering from every 32-bit guest. The build checks the
configure result fail-closed (`HAVE_FREETYPE`, `SONAME_LIBFREETYPE`,
`SONAME_LIBFONTCONFIG` must be defined). The build's interface was
verified against the deb's stock module before the patch was
trusted: identical defined-symbol set (449), identical `DT_NEEDED`
(`ntdll.so`, `libm.so.6`, `libc.so.6`), identical glibc version
requirements (max `GLIBC_2.35`); the undefined-symbol set differs by
one libc symbol each way (`dlerror` in the deb build, `strncmp` in
this one — a toolchain artifact of WineHQ's noble build host; both
resolve from the guest's glibc). The patched module's defined and
undefined symbol sets are identical to the stock rebuild's, it
carries no undefined `__isoc23_*` symbols (so the base build's
patchelf injection set is unchanged), and the 300 000 ms constant
and the 500 ms / ~600-probe retry loop are present in its
disassembly.

## Rebuild

    tools/rootfs32/build-wine10-win32u.sh

It downloads the pinned tarball (sha256-verified), applies the patch
with `git apply`, assembles a private sysroot from pinned bookworm
i386 debs (freetype/fontconfig/vulkan headers + link libraries — the host
has no i386 dev packages; the versions match the guest's runtime
libraries), builds a native tools tree and a `--host=i686-linux-gnu`
tree, and copies `win32u.so` here, printing its sha256 for
re-pinning.

## Packaging integrity

sha256 of this exact file, verified by
`tools/rootfs64/check_runtime_completeness.py` (source hash + all
four shipped `wine32.zip` member hashes) and pinned in
`tools/rootfs32/build-wine32-base.sh` (`BACKPORT32_SHA`):

| file | bytes | sha256 |
| --- | --- | --- |
| win32u.so | 1502028 | 9d538e799c0b88b2c7ee1e15b9d71a979414d65de878459470d6395c12542df8 |

Staged into `wine32.zip` by `tools/rootfs32/build-wine32-base.sh`
(substitution right after the deb tree is copied in, so the
`.dll.so` duplicate and both `usr/` copies inherit the fixed bytes)
and shipped by `tools/rootfs32/regen-wine32-zip.sh`, whose
member-by-member verification sanctions exactly these four members
differing from the previously shipped zip, each at this pin.

The win32u backport is built WITH Vulkan support (FD-149): the i686
configure carries the bookworm libvulkan development files in its
private sysroot and exports `__wine_get_vulkan_driver`, because
win32u is where Wine's Vulkan driver interface lives — a module
built `--without-vulkan` answers every graphics-driver load with
"Wine was built without Vulkan support" and no 32-bit Vulkan (hence
no DXVK D3D9) session can start. The build script fails closed if
the product carries that stub string or lacks the SONAME marker.
