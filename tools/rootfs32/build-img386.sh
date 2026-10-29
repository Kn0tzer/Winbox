#!/usr/bin/env bash
# build-img386.sh — rebuild the 32-bit sysroot staging tree ("img386")
# from pinned, durable inputs only.
#
# What img386 is (consumer-pinned; see
# docs/reports/sysroot32-regeneration-20261028.md for the full evidence):
# a dpkg-deb -x extraction tree of Debian bookworm i386 packages (deb
# layouts preserved: lib/ and usr/lib/ are separate real directories),
# holding exactly what the surviving consumers read from it:
#
#   * tools/rootfs32/stage-fonts.py (first step of
#     tools/rootfs64/apply-runtime-poststeps.sh):
#       usr/lib/i386-linux-gnu/libfontconfig.so.1
#       lib/i386-linux-gnu/libexpat.so.1
#       usr/share/doc/libfontconfig1/copyright
#       usr/share/doc/libexpat1/copyright
#   * tools/rootfs32/closure32.sh (the DT_NEEDED closure producer):
#       usr/lib/wine/wine, usr/lib/wine/wineserver32 (BFS seeds)
#       usr/lib/i386-linux-gnu/wine/i386-unix/*.so (BFS seeds)
#       usr/lib/i386-linux-gnu/{libfreetype.so.6, libgnutls.so.30,
#         libGL.so.1, libvulkan.so.1}, lib/i386-linux-gnu/libgcc_s.so.1
#         (explicit dlopen seeds)
#       the library universe its readelf BFS indexes, and the eight X
#       libraries its extras loop stages by name.
#   * tools/build-prefix32.sh (fallback source only):
#       usr/i686-w64-mingw32/lib/zlib1.dll (Debian libwine omits the
#       i386-windows zlib1.dll its user32 imports; package libz-mingw-w64)
#
# Plus one overlay kept SEPARATE inside the tree (subdir .glibc-noble/):
# Ubuntu noble libc6 2.39-0ubuntu8 (i386). The shipped
# glibc-rootfs32.zip's glibc member set is that package's library
# directory, not bookworm's — proven byte-for-byte on 15 of its 19
# files (the other four are the documented residue; see the report and
# regen-glibc-rootfs32.sh). The overlay is not merged into the Debian
# tree: the zip assembly step consumes it, nothing else reads it.
#
# Every download is pinned by sha256 and fetched from the project's
# existing pinned channels: the Debian bookworm pools
# (deb.debian.org / security.debian.org — the same pools
# tools/rootfs64/prepare-gstreamer.py pins from), snapshot.debian.org
# file URLs for the two bookworm versions the live pools no longer
# carry, and the Ubuntu archive pool for the noble overlay (the host
# release's own archive). A hash mismatch aborts before extraction.
#
# Usage: build-img386.sh [OUT_DIR]
#   OUT_DIR defaults to /home/ubuntu/wine32-stage/img386 (the historical
#   path every consumer defaults to). The deb cache lives under
#   ${BW32_DEB_CACHE:-/tmp/bw32-debs} and is reused across runs.
#   Set BW32_IMG386_FORCE=1 to rebuild an existing OUT_DIR.
set -euo pipefail

OUT="${1:-${BW32_IMG386:-/home/ubuntu/wine32-stage/img386}}"
CACHE="${BW32_DEB_CACHE:-/tmp/bw32-debs}"
mkdir -p "$CACHE"

for tool in curl dpkg-deb sha256sum; do
    command -v "$tool" >/dev/null || {
        echo "build-img386: required host tool '$tool' not found" >&2; exit 2; }
done

if [ -e "$OUT/usr/lib/wine/wine" ] && [ "${BW32_IMG386_FORCE:-}" != "1" ]; then
    echo "build-img386: $OUT already populated — set BW32_IMG386_FORCE=1 to rebuild"
    exit 0
fi

DEB=http://deb.debian.org/debian/pool/main
SEC=http://security.debian.org/debian-security/pool/main
UBU=http://archive.ubuntu.com/ubuntu/pool/main
SNAP=http://snapshot.debian.org/file

# filename|sha256|url   (sha256 is over the .deb itself)
PINS=(
"libc6_2.36-9+deb12u14_i386.deb|76b12e06be66ec3fc2c1791d7d8cf34c2b828ed210e3913feeeb8edc9688f820|$DEB/g/glibc/libc6_2.36-9+deb12u14_i386.deb"
"libwine_8.0~repack-4_i386.deb|c992aad6a47d81b719514f5d06c5df10267f1e40a6f4f3b4e3f655cbbf308d57|$DEB/w/wine/libwine_8.0~repack-4_i386.deb"
"wine32_8.0~repack-4_i386.deb|8d2ea88c4f45e813401e10f139456098f21a9a68440b36ea0da4644a2cbe21f8|$DEB/w/wine/wine32_8.0~repack-4_i386.deb"
"libfontconfig1_2.14.1-4_i386.deb|f581ad1f64c1efadd4df10bb09d7e1a036c7df0db3fde325c941a149a829216f|$DEB/f/fontconfig/libfontconfig1_2.14.1-4_i386.deb"
"libexpat1_2.5.0-1+deb12u4_i386.deb|3e91954b72929bd8598e7a0db37daf0c9c743f798c440c0b1b3cfc0aa8f1ea5a|$SNAP/3a536b341ee5bb2202642667f9d669b8678e58a9"
"zlib1g_1.2.13.dfsg-1_i386.deb|3ca84a1245347652b7e1f8694db222851f7f1e7060515b21e1439b0cf84f96e1|$DEB/z/zlib/zlib1g_1.2.13.dfsg-1_i386.deb"
"libdbus-1-3_1.14.10-1~deb12u1_i386.deb|aaed4e79c1efdf58ee6b43d179a9cf07cd44fc5826eecfb5fcb37c96d5be6ad5|$DEB/d/dbus/libdbus-1-3_1.14.10-1~deb12u1_i386.deb"
"libbz2-1.0_1.0.8-5+b1_i386.deb|275c8f3e8d82f0c58f286e7ec0ca3001380895906be1f1f5b4d929f4469a54a2|$DEB/b/bzip2/libbz2-1.0_1.0.8-5+b1_i386.deb"
"libcap2_2.66-4+deb12u3+b1_i386.deb|ec2df8503ee282fa5ca129cb0272bb2b0c05b9969aa8568bca567ab62bca15ad|$DEB/libc/libcap2/libcap2_2.66-4+deb12u3+b1_i386.deb"
"liblzma5_5.4.1-1+deb12u2_i386.deb|833ccfd933fa965977c8d1a1b9a92660a231bab5c691628e4aba3c551f640730|$SEC/x/xz-utils/liblzma5_5.4.1-1+deb12u2_i386.deb"
"libgpg-error0_1.46-1_i386.deb|9e4109555b4971a2466bdda6fdc3ca90d47a1d0c4b8d7f11ad5720d0e0028392|$DEB/libg/libgpg-error/libgpg-error0_1.46-1_i386.deb"
"libgcc-s1_12.2.0-14+deb12u1_i386.deb|56b95a550b342418f6c4765d5c9b08df2c7167134767e44f1f1bc82ced363ece|$DEB/g/gcc-12/libgcc-s1_12.2.0-14+deb12u1_i386.deb"
"libxfixes3_6.0.0-2_i386.deb|35539777d881fbff6f93a71571fb065932efa1bf2392cc3a19b33df996352249|$DEB/libx/libxfixes/libxfixes3_6.0.0-2_i386.deb"
"libxrandr2_1.5.2-2+b1_i386.deb|1f619d4db7107c0c6344183e5e0254c6ee8d4da74d7ee46f52cdef327354fd4b|$DEB/libx/libxrandr/libxrandr2_1.5.2-2+b1_i386.deb"
"libxinerama1_1.1.4-3_i386.deb|8640bf38f7c6f948eb88a94ac6192c6b726506fa6e2316b0b061fde3e00d0154|$DEB/libx/libxinerama/libxinerama1_1.1.4-3_i386.deb"
"libxcomposite1_0.4.5-1_i386.deb|0ee1b09337cd00581f2923170be8d0334f60614edfd2b0754b9aef7e0d087178|$DEB/libx/libxcomposite/libxcomposite1_0.4.5-1_i386.deb"
"libxi6_1.8-1+b1_i386.deb|bd083eb583445cc13138520ce8b0753bed467bf9582eb75a286615a2833c4aa9|$DEB/libx/libxi/libxi6_1.8-1+b1_i386.deb"
"libxxf86vm1_1.1.4-1+b2_i386.deb|42f33f431d669d23cdb5912e804f6a694200b4ea51fca6ca283c6a5804a0f9dc|$DEB/libx/libxxf86vm/libxxf86vm1_1.1.4-1+b2_i386.deb"
"libxcursor1_1.2.1-1_i386.deb|6a9b8a2db0884bc2e8335a845433ee7bebace56621d3c65f7134578c455d587c|$DEB/libx/libxcursor/libxcursor1_1.2.1-1_i386.deb"
"libxrender1_0.9.10-1.1_i386.deb|5368cbe5f3a78f6518e156536676c3acd7d596d45612c23c1c4bd04f1886e6c7|$DEB/libx/libxrender/libxrender1_0.9.10-1.1_i386.deb"
"libfreetype6_2.12.1+dfsg-5+deb12u4_i386.deb|5945f490940c8c7dd5054aa58a0fdeac5b362ce0ef648e9cce1d7b68a0914fc7|$DEB/f/freetype/libfreetype6_2.12.1+dfsg-5+deb12u4_i386.deb"
"libgnutls30_3.7.9-2+deb12u7_i386.deb|0018a71515bd08ecb10e88450098414a3d64f264d0986f12e952f0797f4825c2|$DEB/g/gnutls28/libgnutls30_3.7.9-2+deb12u7_i386.deb"
"libgl1_1.6.0-1_i386.deb|56127382186908408be0f87fa009ef6e322e0d6079fd84a4e9135de18949d6f6|$DEB/libg/libglvnd/libgl1_1.6.0-1_i386.deb"
"libvulkan1_1.3.239.0-1_i386.deb|a0b91b40d5e6cd29993a66f0a922d455cebdcf770f43ca14c60916c3bdc18284|$DEB/v/vulkan-loader/libvulkan1_1.3.239.0-1_i386.deb"
"libz-mingw-w64_1.2.13+dfsg-1_all.deb|b9e73ca486bc2aa66da86b5724220d2dd7c8299636b1df74c3414757a2170615|$SNAP/bdf436274231960d3dccebb81d7b55a0b0d3afd7"
)
NOBLE_PIN="libc6_2.39-0ubuntu8_i386.deb|8928db539155135cc6943677d6ee158fe6380653188716e16daf9de7094e6476|$UBU/g/glibc/libc6_2.39-0ubuntu8_i386.deb"

fetch() { # name sha url
    local name="$1" sha="$2" url="$3" path="$CACHE/$1"
    if [ -f "$path" ] && echo "$sha  $path" | sha256sum -c - >/dev/null 2>&1; then
        echo "build-img386: cached $name"
        return 0
    fi
    echo "build-img386: fetching $name"
    curl -fsSL --max-time 300 -o "$path" "$url"
    echo "$sha  $path" | sha256sum -c - >/dev/null || {
        echo "build-img386: SHA256 MISMATCH for $name — refusing to extract" >&2
        rm -f "$path"; exit 1; }
}

rm -rf "$OUT"
mkdir -p "$OUT"
for pin in "${PINS[@]}"; do
    IFS='|' read -r name sha url <<<"$pin"
    fetch "$name" "$sha" "$url"
    dpkg-deb -x "$CACHE/$name" "$OUT"
done
# The noble glibc overlay stays separate (see header).
IFS='|' read -r name sha url <<<"$NOBLE_PIN"
fetch "$name" "$sha" "$url"
mkdir -p "$OUT/.glibc-noble"
dpkg-deb -x "$CACHE/$name" "$OUT/.glibc-noble"

# Fail closed unless every consumer-named path exists.
for f in \
    usr/lib/i386-linux-gnu/libfontconfig.so.1 \
    lib/i386-linux-gnu/libexpat.so.1 \
    usr/share/doc/libfontconfig1/copyright \
    usr/share/doc/libexpat1/copyright \
    usr/lib/wine/wine \
    usr/lib/wine/wineserver32 \
    usr/lib/i386-linux-gnu/libfreetype.so.6 \
    usr/lib/i386-linux-gnu/libgnutls.so.30 \
    usr/lib/i386-linux-gnu/libGL.so.1 \
    usr/lib/i386-linux-gnu/libvulkan.so.1 \
    lib/i386-linux-gnu/libgcc_s.so.1 \
    usr/i686-w64-mingw32/lib/zlib1.dll \
    .glibc-noble/usr/lib/i386-linux-gnu/libc.so.6 \
    .glibc-noble/usr/lib/i386-linux-gnu/ld-linux.so.2 ; do
    [ -e "$OUT/$f" ] || { echo "build-img386: MISSING consumer path $f" >&2; exit 1; }
done
ls "$OUT/usr/lib/i386-linux-gnu/wine/i386-unix/"*.so >/dev/null || {
    echo "build-img386: no i386-unix modules extracted" >&2; exit 1; }

echo "build-img386: img386-equivalent staged at $OUT"
echo "build-img386: note — tools/build-prefix32.sh's 'stage' tree is the"
echo "  same wine32+libwine extraction: point STAGE at a tree holding"
echo "  usr/lib/wine + usr/lib/i386-linux-gnu + usr/share/wine (this"
echo "  tree qualifies) or extract those two debs separately."
