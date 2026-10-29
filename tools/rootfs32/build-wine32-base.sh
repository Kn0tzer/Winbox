#!/usr/bin/env bash
# build-wine32-base.sh — assemble the BASE tree of wine32.zip (the 32-bit
# Wine runtime archive) from pinned, durable inputs.
#
# Until 2026-10-28 this tree had no producer at all: only its post-steps
# (stage-alsa32.sh, stage-vkd3d-payloads32.sh) were scripted. The
# forensic census that produced this script is
# docs/reports/wine32-base-forensics-20261028.md. In short, the shipped
# base is an ASSEMBLY, not a single package:
#
#   1. WineHQ wine-stable-i386 10.0.0.0~noble-1 (Ubuntu pool build —
#      NOT the bookworm build the winehq10-x86_64 README pins for
#      the 64-bit backports: the i386 tree's bytes match noble, 1323
#      of 1337 files byte-identical, the 14 exceptions itemised
#      below; the bookworm build matches only 469). The two flavors
#      share the package name, version string, and file set, so only
#      the bytes (and each deb's own Version field) tell them apart;
#      this script's hash pin is the guard. Supplies the whole
#      WineHQ side: opt/wine-stable/{bin,lib}, share/man, and the
#      usr/ re-layout source.
#   2. Twelve of its unix modules (exactly those carrying undefined
#      __isoc23_* symbols in the noble deb) carry one extra
#      DT_NEEDED, added by `patchelf --add-needed libisoc23shim.so`
#      — reproduced byte-for-byte with patchelf 0.18.0. The set is
#      derived at build time (scan for undefined __isoc23 symbols)
#      and checked against the pinned expectation list; a changed
#      set fails closed.
#   3. Twenty-two unix modules are duplicated under `<name>.dll.so`
#      next to `<name>.so` (byte copies — the loader's alternate name).
#      The list is pinned; copies are taken from the final (post-
#      patchelf) .so files, and the regen's member-by-member
#      verification proves the equality against the shipped archive.
#   4. The share/wine DATA tree (wine.inf + the full nls set) in BOTH
#      share placements is Debian bookworm libwine 8.0~repack-4's
#      usr/share/wine — all 78 files byte-identical. (The WineHQ
#      10.0 main package's data differs; its generated NLS files and
#      wine.inf are NOT what ships.) The WineHQ i386 deb ships no share/wine
#      data of its own, and its fonts are deliberately not taken:
#      font payloads are the post-steps' business (stage-fonts.py,
#      stage-fonts32.sh, into glibc-rootfs32.zip).
#   5. The usr/ tree is a Debian-layout RE-LAYOUT of the same WineHQ
#      tree (usr/lib/wine/{wine,wine-preloader} from the deb's bin/,
#      usr/lib/i386-linux-gnu/wine/ = i386-unix/ minus the shim,
#      usr/share/wine/ = the same libwine data) plus a Debian bookworm
#      i386 system-library closure in usr/lib/i386-linux-gnu/ (the
#      DT_NEEDED/dlopen closure of the wine binaries against bookworm
#      — every member sha256-pinned to its bookworm deb below).
#   6. TWO members are locally built artifacts whose sources are
#      LOST (no build tree, script, or source file survives on the
#      box; both carry this host family's GCC banner, Ubuntu
#      13.3.0-6ubuntu2~24.04.1):
#        usr/lib/wine/wineserver32            3,794,568 B
#          sha256 ac990fd260af4be4c53b52405a815aab0ad54847fd1d6ccbe86a0c3018b58393
#          (a wine-10.0 source build — DWARF paths ../wine-10.0/server/;
#          WineHQ's packages ship no standalone i386 wineserver at the
#          Debian path, and Debian 8.0's wineserver32 is a different,
#          683,548-byte binary — both ruled out on bytes in the report)
#        libisoc23shim.so (two placements, identical bytes)  15,252 B
#          sha256 ef5e5696a194937691e034e2a0675fb1e7bcc9c5c9d69563ed013f4556321e09
#          (single-file shim, isoc23shim.c: __isoc23_strto*/scanf
#          wrappers versioned GLIBC_2.38, for running the noble-built
#          modules — which carry undefined __isoc23_* references —
#          against the guest's bookworm glibc 2.36, which lacks them)
#      They are taken from the CURRENTLY SHIPPED wine32.zip — itself
#      pinned by tools/browser/runtime-assets.json — and hash-verified
#      against the pins above before use (fail-closed). This is a
#      named preservation substitution, the same discipline as
#      regen-glibc-rootfs32.sh's glibc aux allow-list: the pipeline
#      runs end-to-end, and a future loss of the shipped archive is
#      detectable against these hashes. If either source is ever
#      identified, check its output against the pins and retire the
#      extraction.
#
# The base tree this script produces is the shipped zip MINUS its two
# scripted post-steps, which regen-wine32-zip.sh applies afterwards
# with the in-tree staging scripts:
#   - stage-alsa32.sh: usr/lib/i386-linux-gnu/alsa-lib/{pcm,ctl}_oss
#   - stage-vkd3d-payloads32.sh: the PE32 vkd3d-proton d3d12 pair
#     REPLACING the deb's bundled pair in opt/wine-stable/lib/wine/
#     i386-windows/ (which is why this base keeps the deb's pair:
#     d3d12.dll 642,962 B, d3d12core.dll 61,993 B).
#
# Usage:
#   build-wine32-base.sh [OUT_DIR] [--shipped /path/to/wine32.zip]
# OUT_DIR defaults to tools/rootfs32/wine32base-stage (scratch; the
# zips are build artifacts, not deliverables). Downloads are cached in
# $BW32_CACHE (default /tmp/bw32-wine32-cache) and hash-verified on
# every run. Requires: curl, dpkg-deb, patchelf, python3, sha256sum.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$HERE/wine32base-stage}"
SHIPPED="$REPO/web/runtime/wine32.zip"
if [ "${2:-}" = "--shipped" ]; then SHIPPED="${3:?--shipped needs a path}"; fi
CACHE="${BW32_CACHE:-/tmp/bw32-wine32-cache}"
mkdir -p "$CACHE"

for tool in curl dpkg-deb patchelf python3 sha256sum; do
    command -v "$tool" >/dev/null || { echo "build-wine32-base: missing host tool: $tool" >&2; exit 2; }
done
[ -f "$SHIPPED" ] || { echo "build-wine32-base: shipped wine32.zip not found: $SHIPPED (needed for the two preserved members)" >&2; exit 2; }

fetch() { # fetch <url> <file> <sha256> <bytes>
    local url="$1" file="$2" sha="$3" bytes="$4" path="$CACHE/$2"
    if [ ! -f "$path" ]; then
        echo "build-wine32-base: downloading $2"
        curl -fsL -o "$path" "$url"
    fi
    local got_sha got_bytes
    got_sha="$(sha256sum "$path" | cut -d' ' -f1)"
    got_bytes="$(stat -c%s "$path")"
    if [ "$got_sha" != "$sha" ] || [ "$got_bytes" != "$bytes" ]; then
        echo "build-wine32-base: PIN MISMATCH for $2: got sha256 $got_sha ($got_bytes B), want $sha ($bytes B)" >&2
        exit 1
    fi
}

# --- pinned primary inputs -------------------------------------------------
WINEHQ_URL="https://dl.winehq.org/wine-builds/ubuntu/pool/main/w/wine/wine-stable-i386_10.0.0.0~noble-1_i386.deb"
fetch "$WINEHQ_URL" wine-stable-i386_10.0.0.0~noble-1_i386.deb \
    5ba523409f013b12953f577bc3322aeae599bb16afac66c59ebe2dde7b337bfa 110481570
LIBWINE8_URL="http://deb.debian.org/debian/pool/main/w/wine/libwine_8.0~repack-4_i386.deb"
fetch "$LIBWINE8_URL" libwine_8.0~repack-4_i386.deb \
    c992aad6a47d81b719514f5d06c5df10267f1e40a6f4f3b4e3f655cbbf308d57 95293888

# --- pinned closure inputs (bookworm i386) ---------------------------------
# One line per shipped closure member: member-path|package|version|deb sha256|deb bytes|url
# FILLED FROM closure-attribution (see report §closure).
CLOSURE_PINS=(
"usr/lib/i386-linux-gnu/libFLAC.so.12|libflac12|1.4.2+ds-2|b321d509ffd07118debf00ed5683a028ce13922842d7c2650598d7c60841fa5b|166732|http://deb.debian.org/debian/pool/main/f/flac/libflac12_1.4.2+ds-2_i386.deb"
"usr/lib/i386-linux-gnu/libGL.so.1|libgl1|1.6.0-1|56127382186908408be0f87fa009ef6e322e0d6079fd84a4e9135de18949d6f6|82112|https://snapshot.debian.org/file/90e40f8e96fd1a1c58a48181aab67700718b4fd0"
"usr/lib/i386-linux-gnu/libGLX.so.0|libglx0|1.6.0-1|3c7df84621b49ce93355183041c58a5540e46e35b799209a1fade10a61dfb114|36580|https://snapshot.debian.org/file/fb0df3145524050b881934fe7b4402499b8873f0"
"usr/lib/i386-linux-gnu/libGLdispatch.so.0|libglvnd0|1.6.0-1|057b79f23b040f7d7fab1b5ec2d7c027c195c827b8a136d14ee2ab8a513aecf9|42728|https://snapshot.debian.org/file/cfdca6b1f1f5d2e39d0d5aaa961eb3c4482f429a"
"usr/lib/i386-linux-gnu/libOpenCL.so.1|ocl-icd-libopencl1|2.3.1-1|18be5fb67320c3b7f699b14826d6ca6fd70662d150a3945a6be180e4018ae0a5|45448|http://deb.debian.org/debian/pool/main/o/ocl-icd/ocl-icd-libopencl1_2.3.1-1_i386.deb"
"usr/lib/i386-linux-gnu/libX11-xcb.so.1|libx11-xcb1|2:1.8.4-2+deb12u2|4d96d62f7204767e68d936648c70e717f6d5915b03f74ee159140111dbd0062c|192380|https://snapshot.debian.org/file/b2497f49c65ec2562826a2ec463ec0f0264d702e"
"usr/lib/i386-linux-gnu/libX11.so.6|libx11-6|2:1.8.4-2+deb12u2|df42dab90a6f0d064ba0aa9f0857056021b1d0c37c82ac495b83a953a5e52799|782456|https://snapshot.debian.org/file/e97b904c500d485334fa121d6307ec6a5a60b36f"
"usr/lib/i386-linux-gnu/libXau.so.6|libxau6|1:1.0.9-1|3003e98e7ecef061ec2f1df1fc106f55b9c051469bc2ab4be743b85851ea7d25|20004|https://snapshot.debian.org/file/6c0c1fa6a6acaf7b009e790cd99611b620d5e687"
"usr/lib/i386-linux-gnu/libXcomposite.so.1|libxcomposite1|1:0.4.5-1|0ee1b09337cd00581f2923170be8d0334f60614edfd2b0754b9aef7e0d087178|16884|https://snapshot.debian.org/file/6c47eb34d6f22ed0544a158001671198ee24238e"
"usr/lib/i386-linux-gnu/libXcursor.so.1|libxcursor1|1:1.2.1-1|6a9b8a2db0884bc2e8335a845433ee7bebace56621d3c65f7134578c455d587c|42360|https://snapshot.debian.org/file/6cc1120163667cb3d410f202cdbeff9ad6ad52a9"
"usr/lib/i386-linux-gnu/libXdmcp.so.6|libxdmcp6|1:1.1.2-3|186935868181faae5f7956ab250a7a135b28d139d163a575050ad31bbfd56e33|26684|https://snapshot.debian.org/file/52521611458be18aabc3c936297422545304b031"
"usr/lib/i386-linux-gnu/libXext.so.6|libxext6|2:1.3.4-1+b1|ebb222c1d9c1fbb98d1663e8f18f7b9fd4747133494cec8c566a86037c5f26cf|55260|https://snapshot.debian.org/file/04cbff8c1b9003e9ac2b3fbbf81301078f383019"
"usr/lib/i386-linux-gnu/libXfixes.so.3|libxfixes3|1:6.0.0-2|35539777d881fbff6f93a71571fb065932efa1bf2392cc3a19b33df996352249|23044|https://snapshot.debian.org/file/e8479bc569097fcce573450bb5b1969fc7048633"
"usr/lib/i386-linux-gnu/libXi.so.6|libxi6|2:1.8-1+b1|bd083eb583445cc13138520ce8b0753bed467bf9582eb75a286615a2833c4aa9|86184|https://snapshot.debian.org/file/2162f0090aa59a77cb01c9160cf83178607ade89"
"usr/lib/i386-linux-gnu/libXinerama.so.1|libxinerama1|2:1.1.4-3|8640bf38f7c6f948eb88a94ac6192c6b726506fa6e2316b0b061fde3e00d0154|18056|https://snapshot.debian.org/file/c5cdccdbd4edf81d8638b042bb1a97cf7c4c172f"
"usr/lib/i386-linux-gnu/libXrandr.so.2|libxrandr2|2:1.5.2-2+b1|1f619d4db7107c0c6344183e5e0254c6ee8d4da74d7ee46f52cdef327354fd4b|40820|https://snapshot.debian.org/file/6b12c6ef0f8aa7e73249f43d17cb5621bbca3ad1"
"usr/lib/i386-linux-gnu/libXrender.so.1|libxrender1|1:0.9.10-1.1|5368cbe5f3a78f6518e156536676c3acd7d596d45612c23c1c4bd04f1886e6c7|34116|https://snapshot.debian.org/file/4c37025294b1e45eff7d28d54fef68563d1d533e"
"usr/lib/i386-linux-gnu/libXxf86vm.so.1|libxxf86vm1|1:1.1.4-1+b2|42f33f431d669d23cdb5912e804f6a694200b4ea51fca6ca283c6a5804a0f9dc|21684|https://snapshot.debian.org/file/7bbbfb3052d83ab40ed7071e63fee5735b120f58"
"usr/lib/i386-linux-gnu/libasound.so.2|libasound2|1.2.8-1+b1|f5257503c01bc56bf089e2f4d75c5d20a9c28fa16fd0a777aa64af8c439f50c8|388148|http://deb.debian.org/debian/pool/main/a/alsa-lib/libasound2_1.2.8-1+b1_i386.deb"
"usr/lib/i386-linux-gnu/libasyncns.so.0|libasyncns0|0.8-6+b3|de353333e0bec51306ee6b12c309693dd2108d25b69e60b70bd1c639c1376cde|13420|https://snapshot.debian.org/file/70cf5fe05d943e82ab838f34c76a5e042b8e2a86"
"usr/lib/i386-linux-gnu/libbrotlicommon.so.1|libbrotli1|1.0.9-2+b6|75b14942a601dbac638595a9d827f91c12a9f95fb7503f2dc5471fcf57bd4bc1|274952|http://deb.debian.org/debian/pool/main/b/brotli/libbrotli1_1.0.9-2+b6_i386.deb"
"usr/lib/i386-linux-gnu/libbrotlidec.so.1|libbrotli1|1.0.9-2+b6|75b14942a601dbac638595a9d827f91c12a9f95fb7503f2dc5471fcf57bd4bc1|274952|http://deb.debian.org/debian/pool/main/b/brotli/libbrotli1_1.0.9-2+b6_i386.deb"
"usr/lib/i386-linux-gnu/libbsd.so.0|libbsd0|0.11.7-2|527d1696a70421227c3de01a82a3eceaeb9f78a927eac0d8a1b41924199bf48b|120856|https://snapshot.debian.org/file/c44167d52857e09ca149d1f801032c22ed665192"
"usr/lib/i386-linux-gnu/libcapi20.so.3|libcapi20-3|1:3.27-3+b1|d8d94c02603f1dd357ca6b430cdc95e7a2e69570962d3670d6a2ba5a14ce300f|29144|https://snapshot.debian.org/file/85c666d75696c1a6cc56603df118bd4829c3e811"
"usr/lib/i386-linux-gnu/libdw.so.1|libdw1|0.188-2.1|48f28edaf9e23ed61747bd2d417dc0a4b577bef85bbed28c5d07f8be8e5312c5|265368|http://deb.debian.org/debian/pool/main/e/elfutils/libdw1_0.188-2.1_i386.deb"
"usr/lib/i386-linux-gnu/libelf.so.1|libelf1|0.188-2.1|d55c01c10209b1746e6637449357d3feb3de8c8b0102d8286553a07260d718c2|179320|http://deb.debian.org/debian/pool/main/e/elfutils/libelf1_0.188-2.1_i386.deb"
"usr/lib/i386-linux-gnu/libexif.so.12|libexif12|0.6.24-1+deb12u1|6906595caf1df219b2578fc494768ad39d06229c60d8a980cfd0795b7d7e96e5|394108|https://snapshot.debian.org/file/6634730b21adb611b914712fcd1cdc6cd96e97fd"
"usr/lib/i386-linux-gnu/libffi.so.8|libffi8|3.4.4-1|21b4d7de1cea73aa8d594106e473c1c6b6f33fd11cb66af5db35f9317f5f9a60|21084|https://snapshot.debian.org/file/be3b6b7d5b7327254966223190b483c08dee85f9"
"usr/lib/i386-linux-gnu/libfreetype.so.6|libfreetype6|2.12.1+dfsg-5+deb12u4|5945f490940c8c7dd5054aa58a0fdeac5b362ce0ef648e9cce1d7b68a0914fc7|409064|http://deb.debian.org/debian/pool/main/f/freetype/libfreetype6_2.12.1+dfsg-5+deb12u4_i386.deb"
"usr/lib/i386-linux-gnu/libgcrypt.so.20|libgcrypt20|1.10.1-3+deb12u1|737d4f16368232608bfa2320fc6a12420062ca43d0b7fce0158d8f49ccd3bc46|679268|https://snapshot.debian.org/file/88f7cc84fdb3cf260a2044e586570c51f17bee0e"
"usr/lib/i386-linux-gnu/libglib-2.0.so.0|libglib2.0-0|2.74.6-2+deb12u9|b29fac9486146be26bc1bc9bacd2d0ba86874856594fa2bba84a3e395f880d68|1473636|http://deb.debian.org/debian/pool/main/g/glib2.0/libglib2.0-0_2.74.6-2+deb12u9_i386.deb"
"usr/lib/i386-linux-gnu/libgmodule-2.0.so.0|libglib2.0-0|2.74.6-2+deb12u9|b29fac9486146be26bc1bc9bacd2d0ba86874856594fa2bba84a3e395f880d68|1473636|http://deb.debian.org/debian/pool/main/g/glib2.0/libglib2.0-0_2.74.6-2+deb12u9_i386.deb"
"usr/lib/i386-linux-gnu/libgmp.so.10|libgmp10|6.2.1+dfsg1-1.1|996d31008cebdda7acb16431b70b8c2021fd0da544ec643597b172b6a7707e48|573712|http://deb.debian.org/debian/pool/main/g/gmp/libgmp10_6.2.1+dfsg1-1.1_i386.deb"
"usr/lib/i386-linux-gnu/libgnutls.so.30|libgnutls30|3.7.9-2+deb12u7|0018a71515bd08ecb10e88450098414a3d64f264d0986f12e952f0797f4825c2|1409032|http://deb.debian.org/debian/pool/main/g/gnutls28/libgnutls30_3.7.9-2+deb12u7_i386.deb"
"usr/lib/i386-linux-gnu/libgobject-2.0.so.0|libglib2.0-0|2.74.6-2+deb12u9|b29fac9486146be26bc1bc9bacd2d0ba86874856594fa2bba84a3e395f880d68|1473636|http://deb.debian.org/debian/pool/main/g/glib2.0/libglib2.0-0_2.74.6-2+deb12u9_i386.deb"
"usr/lib/i386-linux-gnu/libgphoto2.so.6|libgphoto2-6|2.5.30-1|ee22cfb639671abe2ae6b0cf2d9be03af5d559eddd912392fe9ec19db531559e|758172|https://snapshot.debian.org/file/11b129abbd5deed23fd9e0ad03a524a6f106a299"
"usr/lib/i386-linux-gnu/libgphoto2_port.so.12|libgphoto2-port12|2.5.30-1|be2b6827dd97ede0c41ceb7384f492003bad675260e0c3b7f308857d37a22d97|136980|https://snapshot.debian.org/file/17e851fb3b556fcd9541d6b691f33c0f3ae7edc7"
"usr/lib/i386-linux-gnu/libgstaudio-1.0.so.0|libgstreamer-plugins-base1.0-0|1.22.0-3+deb12u6|9a8802e06368d90a8251debfdebb663404d5751b8bf8bcbfaa9f65539a5de281|968596|http://deb.debian.org/debian/pool/main/g/gst-plugins-base1.0/libgstreamer-plugins-base1.0-0_1.22.0-3+deb12u6_i386.deb"
"usr/lib/i386-linux-gnu/libgstbase-1.0.so.0|libgstreamer1.0-0|1.22.0-2+deb12u1|a5b8d9d3d967199c21a316a2533ee9891deb0defe4593c93d1ca569b0e5a89a6|1221024|http://deb.debian.org/debian/pool/main/g/gstreamer1.0/libgstreamer1.0-0_1.22.0-2+deb12u1_i386.deb"
"usr/lib/i386-linux-gnu/libgstreamer-1.0.so.0|libgstreamer1.0-0|1.22.0-2+deb12u1|a5b8d9d3d967199c21a316a2533ee9891deb0defe4593c93d1ca569b0e5a89a6|1221024|http://deb.debian.org/debian/pool/main/g/gstreamer1.0/libgstreamer1.0-0_1.22.0-2+deb12u1_i386.deb"
"usr/lib/i386-linux-gnu/libgsttag-1.0.so.0|libgstreamer-plugins-base1.0-0|1.22.0-3+deb12u6|9a8802e06368d90a8251debfdebb663404d5751b8bf8bcbfaa9f65539a5de281|968596|http://deb.debian.org/debian/pool/main/g/gst-plugins-base1.0/libgstreamer-plugins-base1.0-0_1.22.0-3+deb12u6_i386.deb"
"usr/lib/i386-linux-gnu/libgstvideo-1.0.so.0|libgstreamer-plugins-base1.0-0|1.22.0-3+deb12u6|9a8802e06368d90a8251debfdebb663404d5751b8bf8bcbfaa9f65539a5de281|968596|http://deb.debian.org/debian/pool/main/g/gst-plugins-base1.0/libgstreamer-plugins-base1.0-0_1.22.0-3+deb12u6_i386.deb"
"usr/lib/i386-linux-gnu/libhogweed.so.6|libhogweed6|3.8.1-2|d54aad2cf3e5104c80a2ed03511f7502558607df5d535f77e441e13bb26e01a8|330636|http://deb.debian.org/debian/pool/main/n/nettle/libhogweed6_3.8.1-2_i386.deb"
"usr/lib/i386-linux-gnu/libidn2.so.0|libidn2-0|2.3.3-1+b1|49167c0ddd749810a5242b60c7e280860f4fbcb7b4ee9ee456f44d97cd5baef5|125528|https://snapshot.debian.org/file/00b370b6a0aafa3b952142976a28baa71a08aa01"
"usr/lib/i386-linux-gnu/libltdl.so.7|libltdl7|2.4.7-7~deb12u1|82f6171fe2adf0f50e184d7072ddb407645f0f27fc1b0cad90032680bae3317f|394736|https://snapshot.debian.org/file/f5f72b0567f0c3d09a96fda3d4643d076028cc30"
"usr/lib/i386-linux-gnu/liblz4.so.1|liblz4-1|1.9.4-1|6ca34fa9139be02a118dd22b969f34018fc4628b02acd1d21c4621d16057ac6a|65220|http://deb.debian.org/debian/pool/main/l/lz4/liblz4-1_1.9.4-1_i386.deb"
"usr/lib/i386-linux-gnu/libmd.so.0|libmd0|1.0.4-2|5416c6fcbfb73c21d1e7bf1aeeb3d7efc1d6cf7274c2332ec9d5648d04e5d0ca|30488|https://snapshot.debian.org/file/0ad8dd3aed661a872a9a608e947ee3fc23551a49"
"usr/lib/i386-linux-gnu/libmp3lame.so.0|libmp3lame0|3.100-6|fdc9724815021ee8181e1aac46b52d1498a9d1ffc6dd98bb115b8c1dfe2ec1a3|364996|http://deb.debian.org/debian/pool/main/l/lame/libmp3lame0_3.100-6_i386.deb"
"usr/lib/i386-linux-gnu/libmpg123.so.0|libmpg123-0|1.31.2-1+deb12u1|7a7c29b2882a05610f89091ceff67773ec79d6558294226c1905b0a833430428|149604|http://deb.debian.org/debian/pool/main/m/mpg123/libmpg123-0_1.31.2-1+deb12u1_i386.deb"
"usr/lib/i386-linux-gnu/libnettle.so.8|libnettle8|3.8.1-2|3e928be29ce533c1e2d9e1bdf520a2925688b01b329533e71551ebfab7f6a402|301444|http://deb.debian.org/debian/pool/main/n/nettle/libnettle8_3.8.1-2_i386.deb"
"usr/lib/i386-linux-gnu/libogg.so.0|libogg0|1.3.5-3|2914d7e7a37d8e82339cebc43b0b4f71b561623dceb5ebe12156837e3de86318|24112|https://snapshot.debian.org/file/d116d5b082eae532732081b5b988ecdb4f6a54a2"
"usr/lib/i386-linux-gnu/libopus.so.0|libopus0|1.3.1-3|eec578cb2f9e67fdbd08ee21d187f19ae6698494dfc46e71d2f7ba231d561f71|195236|http://deb.debian.org/debian/pool/main/o/opus/libopus0_1.3.1-3_i386.deb"
"usr/lib/i386-linux-gnu/liborc-0.4.so.0|liborc-0.4-0|0.4.33-2|60f4342f9ddd8b65de91bb7c66fb8c711b04db8ba309aadb04a91bc023b29c52|173208|http://deb.debian.org/debian/pool/main/o/orc/liborc-0.4-0_0.4.33-2_i386.deb"
"usr/lib/i386-linux-gnu/libp11-kit.so.0|libp11-kit0|0.24.1-2|f8e5a6f2201eb0317e6149ec95fc08e5c6879fe1247e729e0635dfd9c9f063b3|335372|http://deb.debian.org/debian/pool/main/p/p11-kit/libp11-kit0_0.24.1-2_i386.deb"
"usr/lib/i386-linux-gnu/libpcap.so.0.8|libpcap0.8|1.10.3-1|63d4d9603c9f993dd5d878a06f2755601e75735ea6558080b11a743bb3dae390|163784|https://snapshot.debian.org/file/a21d19986a7b5d9e163675f14c0c913f27978151"
"usr/lib/i386-linux-gnu/libpcre2-8.so.0|libpcre2-8-0|10.42-1+deb12u2|ced5373613eb41c47d0a2a2eacf353db329e99c4755a07a2df73b59f2c08cacd|259212|https://snapshot.debian.org/file/f3c02e63840ba6885b7878bb2d0f4b0ed5349cb1"
"usr/lib/i386-linux-gnu/libpng16.so.16|libpng16-16|1.6.39-2+deb12u6|2e9b486b343f59c06b492807776b0c1ebc8be86fa6063d77cd701f393efe0b04|284920|https://snapshot.debian.org/file/8d562580688c6ef34b7a0b3f0d0215d131bca5d5"
"usr/lib/i386-linux-gnu/libpulse.so.0|libpulse0|16.1+dfsg1-2+b1|fc8cac04e3cd077b70f94eb1fd6cd42b4c5f7aae27cda7bc0c5bcea307537056|274716|http://deb.debian.org/debian/pool/main/p/pulseaudio/libpulse0_16.1+dfsg1-2+b1_i386.deb"
"usr/lib/i386-linux-gnu/libsndfile.so.1|libsndfile1|1.2.0-1+deb12u1|6cf51c091b37c866d9a7aa0bb5011c1e93546cff6fc96827aaaf906eedc7fbd5|219504|https://snapshot.debian.org/file/7446b59ec51e9a281fb085b0d2621336b0502f0f"
"usr/lib/i386-linux-gnu/libsystemd.so.0|libsystemd0|252.39-1~deb12u2|0b28141a9c39a6be717b1e561d61cf8301a740182f480a246d4664e3aec52a41|353668|http://deb.debian.org/debian/pool/main/s/systemd/libsystemd0_252.39-1~deb12u2_i386.deb"
"usr/lib/i386-linux-gnu/libtasn1.so.6|libtasn1-6|4.19.0-2+deb12u1|b21b8068b333245ddeaafd2903f55f7b986217e80e060670f21af23b19cf8a04|50368|https://snapshot.debian.org/file/ed8b4eb15d0fd042e50d75c647ca195d8acd6024"
"usr/lib/i386-linux-gnu/libudev.so.1|libudev1|252.39-1~deb12u2|4c7b427632d2ce9ac822cb00a8376e79edbe42dadc0414629de27885e82d13df|112592|http://deb.debian.org/debian/pool/main/s/systemd/libudev1_252.39-1~deb12u2_i386.deb"
"usr/lib/i386-linux-gnu/libunistring.so.2|libunistring2|1.0-2|04aed8ddaa8f92fcdb378bd92eb23a78bda0f3e3be59501329b1968362492781|435364|https://snapshot.debian.org/file/2c504cfa0912afc9739ecc8e60a35b493365ecc4"
"usr/lib/i386-linux-gnu/libunwind.so.8|libunwind8|1.6.2-3|39d3ddf6f813d3137f70fc3c05f6cfb6b3ad17251430df7cef7463b1adf6d29a|49020|https://snapshot.debian.org/file/66938f026136a6cd074b20d0e05be5b695084432"
"usr/lib/i386-linux-gnu/libusb-1.0.so.0|libusb-1.0-0|2:1.0.26-1|c4e1a66b847160bd3826d5e5e66c86e0b8aafdfe9b29802831068e242080fab5|65588|https://snapshot.debian.org/file/bd590cc313158f65aecee0d5c72349bfd3eaa82b"
"usr/lib/i386-linux-gnu/libvorbis.so.0|libvorbis0a|1.3.7-1|56bb3970d0a9fbf07c6d027b8ab8fecd386b47e14bce7fb3e2ac93a0abe7f7d7|91876|https://snapshot.debian.org/file/59c39d73c5d5e79fd0b82f5bfe4a7db40310fd21"
"usr/lib/i386-linux-gnu/libvorbisenc.so.2|libvorbisenc2|1.3.7-1|04de77bb41d5062f135ebebf210f8f558080a4c5b8ba9cc216883c957207fb7e|74636|https://snapshot.debian.org/file/a364e3349c05b5ab471160786f2d700d149a3f46"
"usr/lib/i386-linux-gnu/libvulkan.so.1|libvulkan1|1.3.239.0-1|a0b91b40d5e6cd29993a66f0a922d455cebdcf770f43ca14c60916c3bdc18284|124720|http://deb.debian.org/debian/pool/main/v/vulkan-loader/libvulkan1_1.3.239.0-1_i386.deb"
"usr/lib/i386-linux-gnu/libxcb.so.1|libxcb1|1.15-1|58eac13b8d4614a9804b8fbfd8090a4da725ee70ebda435cb807144348d8a216|148040|https://snapshot.debian.org/file/dd2d299c94d5775162183444dfd08608c732211b"
"usr/lib/i386-linux-gnu/libzstd.so.1|libzstd1|1.5.4+dfsg2-5|b9009648837c1fa095ca3b79eec3e6358df3946dd0a70c0f66ec3558695af8d5|274116|https://snapshot.debian.org/file/fdbb17476fc20aa53e98c87dc23304b6ad104eec"
"usr/lib/i386-linux-gnu/pulseaudio/libpulsecommon-16.1.so|libpulse0|16.1+dfsg1-2+b1|fc8cac04e3cd077b70f94eb1fd6cd42b4c5f7aae27cda7bc0c5bcea307537056|274716|http://deb.debian.org/debian/pool/main/p/pulseaudio/libpulse0_16.1+dfsg1-2+b1_i386.deb"
)

rm -rf "$OUT"
mkdir -p "$OUT/opt" "$OUT/usr/lib/wine" "$OUT/usr/lib/i386-linux-gnu/wine" \
         "$OUT/usr/lib/i386-linux-gnu/pulseaudio" "$OUT/usr/share" "$OUT/.extract"

echo "=== extracting WineHQ noble i386 deb ==="
dpkg-deb -x "$CACHE/wine-stable-i386_10.0.0.0~noble-1_i386.deb" "$OUT/.extract/winehq"
cp -a "$OUT/.extract/winehq/opt/wine-stable" "$OUT/opt/"

echo "=== extracting the two preserved members from the shipped zip ==="
python3 - "$SHIPPED" "$OUT/.extract" <<'EOF'
import hashlib, sys, zipfile
shipped, dest = sys.argv[1], sys.argv[2]
pins = {
    "usr/lib/wine/wineserver32": "ac990fd260af4be4c53b52405a815aab0ad54847fd1d6ccbe86a0c3018b58393",
    "opt/wine-stable/lib/wine/i386-unix/libisoc23shim.so": "ef5e5696a194937691e034e2a0675fb1e7bcc9c5c9d69563ed013f4556321e09",
}
with zipfile.ZipFile(shipped) as z:
    for name, pin in pins.items():
        data = z.read(name)
        got = hashlib.sha256(data).hexdigest()
        if got != pin:
            raise SystemExit(f"build-wine32-base: preserved member {name} hash mismatch: {got} != {pin}")
        out = dest + "/" + name.rsplit("/", 1)[-1] + ".preserved"
        open(out, "wb").write(data)
        print(f"preserved {name}: sha256 verified ({len(data)} B)")
EOF
cp "$OUT/.extract/wineserver32.preserved" "$OUT/usr/lib/wine/wineserver32"
chmod 755 "$OUT/usr/lib/wine/wineserver32"
SHIM="$OUT/.extract/libisoc23shim.so.preserved"

echo "=== patchelf: __isoc23 DT_NEEDED injection (rule-derived set) ==="
UNIXDIR="$OUT/opt/wine-stable/lib/wine/i386-unix"
EXPECTED_INJECT="bcrypt.so crypt32.so mountmgr.so nsiproxy.so ntdll.so opengl32.so secur32.so winealsa.so winebth.so winebus.so winex11.so winspool.so"
derived=""
# (grep without -q on purpose: under pipefail, a -q early exit SIGPIPEs
# the upstream grep and flips large modules to a false negative.)
for so in "$UNIXDIR"/*.so; do
    if readelf --dyn-syms -W "$so" 2>/dev/null | grep " UND " | grep "__isoc23" >/dev/null; then
        derived="$derived $(basename "$so")"
    fi
done
derived="$(echo $derived | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')"
if [ "$derived" != "$EXPECTED_INJECT" ]; then
    echo "build-wine32-base: injection set drifted: derived [$derived] != expected [$EXPECTED_INJECT]" >&2
    exit 1
fi
for name in $EXPECTED_INJECT; do
    patchelf --add-needed libisoc23shim.so "$UNIXDIR/$name"
    readelf -dW "$UNIXDIR/$name" | grep "NEEDED.*libisoc23shim\.so" >/dev/null || {
        echo "build-wine32-base: patchelf did not add the DT_NEEDED to $name" >&2; exit 1; }
done
cp "$SHIM" "$UNIXDIR/libisoc23shim.so"
echo "injected + shim placed: $EXPECTED_INJECT"

echo "=== .dll.so duplicates (pinned list) ==="
DLL_SO_DUPS="avicap32 bcrypt crypt32 ctapi32 dnsapi dwrite kerberos localspl msv1_0 netapi32 ntdll odbc32 opencl opengl32 qcap secur32 win32u winedmo winegstreamer winevulkan wpcap ws2_32"
for name in $DLL_SO_DUPS; do
    cp "$UNIXDIR/$name.so" "$UNIXDIR/$name.dll.so"
done

echo "=== usr/ re-layout ==="
cp "$OUT/opt/wine-stable/bin/wine" "$OUT/usr/lib/wine/wine"
cp "$OUT/opt/wine-stable/bin/wine-preloader" "$OUT/usr/lib/wine/wine-preloader"
chmod 755 "$OUT/usr/lib/wine/wine" "$OUT/usr/lib/wine/wine-preloader"
for f in "$UNIXDIR"/*; do
    [ "$(basename "$f")" = "libisoc23shim.so" ] && continue
    cp -a "$f" "$OUT/usr/lib/i386-linux-gnu/wine/"
done
cp "$SHIM" "$OUT/usr/lib/i386-linux-gnu/libisoc23shim.so"

echo "=== share/wine data from Debian libwine 8.0 (both placements) ==="
dpkg-deb -x "$CACHE/libwine_8.0~repack-4_i386.deb" "$OUT/.extract/libwine8"
mkdir -p "$OUT/opt/wine-stable/share/wine"
cp -a "$OUT/.extract/libwine8/usr/share/wine/." "$OUT/opt/wine-stable/share/wine/"
cp -a "$OUT/.extract/libwine8/usr/share/wine" "$OUT/usr/share/wine"

echo "=== bookworm i386 system-library closure (pinned debs) ==="
for line in "${CLOSURE_PINS[@]}"; do
    IFS='|' read -r member pkg ver debsha debbytes url <<<"$line"
    debfile="${pkg}_${ver}_i386.deb"
    fetch "$url" "$debfile" "$debsha" "$debbytes"
    rm -rf "$OUT/.extract/one"; mkdir -p "$OUT/.extract/one"
    dpkg-deb -x "$CACHE/$debfile" "$OUT/.extract/one"
    src="$OUT/.extract/one/$member"
    [ -e "$src" ] || { echo "build-wine32-base: $member absent from $debfile" >&2; exit 1; }
    mkdir -p "$OUT/$(dirname "$member")"
    cp -aL "$src" "$OUT/$member"
done

# The empty gstreamer plugin dir is a fossil of the pre-split era (the
# plugin closure rides the support pack since 2026-10-25); the shipped
# member set includes it, so the base keeps it, empty, on purpose.
mkdir -p "$OUT/usr/lib/i386-linux-gnu/gstreamer-1.0"

rm -rf "$OUT/.extract"
echo "=== base tree assembled at $OUT ==="
find "$OUT" -type f | wc -l
du -sh "$OUT"
