#!/bin/bash
# closure32.sh — compute the i386 DT_NEEDED closure natively via readelf,
# stage it under $OUT/rootfs32/, zip glibc-rootfs32.zip.
#
# PROVENANCE (2026-10-28, sysroot32 lane): this script survived ONLY as
# an untracked copy in /home/ubuntu/backups/wine32-stage-20261020/ when
# /home/ubuntu/wine32-stage was deleted; it is restored here verbatim
# except that IMG/OUT are environment-parameterizable (defaults are the
# historical paths) and this header. The zip it produces is the BASE
# glibc-rootfs32.zip; the shipped artifact additionally carries the
# adjustments applied by regen-glibc-rootfs32.sh (the noble glibc set +
# the explicit /lib stage list) and the apply-runtime-poststeps.sh
# post-steps (fonts, DejaVu faces, ALSA config). See
# docs/reports/sysroot32-regeneration-20261028.md.
set -euo pipefail
IMG="${BW32_IMG386:-/home/ubuntu/wine32-stage/img386}"
OUT="${BW32_STAGE_OUT:-/home/ubuntu/wine32-stage}"
STAGE="$OUT/rootfs32"
rm -rf "$STAGE"; mkdir -p "$STAGE"

# Build a name -> path index of every .so in the extracted tree.
declare -A IDX
while IFS= read -r f; do
  b="$(basename "$f")"
  if [ -z "${IDX[$b]+x}" ]; then IDX[$b]="$f"; fi
done < <(find "$IMG/lib/i386-linux-gnu" "$IMG/usr/lib/i386-linux-gnu" "$IMG/lib" "$IMG/usr/lib" -name "*.so*" \( -type f -o -type l \) 2>/dev/null | sort)

needed_of() { readelf -d "$1" 2>/dev/null | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/'; }

declare -A SEEN     # real paths, for BFS termination
declare -A NAMES    # SONAME -> real path, for staging under loader-visible names
queue=(
  "$IMG/usr/lib/wine/wine"
  "$IMG/usr/lib/wine/wineserver32"
  "$IMG/lib/ld-linux.so.2"
  # Explicit extras (dlopen'd at runtime, not DT_NEEDED by wine) — seed them so
  # the BFS pulls their transitive deps too.
  "$IMG/usr/lib/i386-linux-gnu/libfreetype.so.6"
  "$IMG/usr/lib/i386-linux-gnu/libgnutls.so.30"
  "$IMG/usr/lib/i386-linux-gnu/libGL.so.1"
  "$IMG/usr/lib/i386-linux-gnu/libvulkan.so.1"
  "$IMG/lib/i386-linux-gnu/libgcc_s.so.1"
)
for so in "$IMG"/usr/lib/i386-linux-gnu/wine/i386-unix/*.so; do queue+=("$so"); done

missing=()
while [ ${#queue[@]} -gt 0 ]; do
  f="${queue[0]}"; queue=("${queue[@]:1}")
  real="$(readlink -f "$f")"
  if [ -n "${SEEN[$real]+x}" ]; then continue; fi
  SEEN[$real]=1
  while IFS= read -r need; do
    [ -n "$need" ] || continue
    hit="${IDX[$need]:-}"
    if [ -n "$hit" ]; then
      hitreal="$(readlink -f "$hit")"
      NAMES[$need]="$hitreal"
      if [ -z "${SEEN[$hitreal]+x}" ]; then queue+=("$hitreal"); fi
    else
      missing+=("$need (wanted by $f)")
    fi
  done < <(needed_of "$f")
done

echo "=== closure: ${#SEEN[@]} files, ${#NAMES[@]} staged names ==="
if [ ${#missing[@]} -gt 0 ]; then
  echo "=== MISSING (${#missing[@]}): ==="
  printf "%s\n" "${missing[@]}" | sort -u
fi

# Stage each SONAME under its loader-visible name (deref content like the
# 64-bit copy()). Guest ld.so looks up DT_NEEDED by SONAME, so the .so.N
# names must exist, not just the real files.
for name in "${!NAMES[@]}"; do
  src="${IDX[$name]:-}"
  [ -n "$src" ] || continue
  dir="$STAGE$(dirname "${src#$IMG}")"
  mkdir -p "$dir"
  cp -aL "${NAMES[$name]}" "$dir/$name"
done
# The loader INTERP itself: /lib/ld-linux.so.2 (name + content).
mkdir -p "$STAGE/lib"
cp -aL "$IMG/lib/ld-linux.so.2" "$STAGE/lib/ld-linux.so.2"

# Explicit extras (dlopen'd at runtime, not DT_NEEDED) — staged under
# usr/lib/i386-linux-gnu for the future wine32.zip overlay step.
for x in "$IMG/usr/lib/i386-linux-gnu/libfreetype.so.6" \
         "$IMG/usr/lib/i386-linux-gnu/libgnutls.so.30" \
         "$IMG/usr/lib/i386-linux-gnu/libGL.so.1" \
         "$IMG/usr/lib/i386-linux-gnu/libvulkan.so.1" \
         "$IMG/lib/i386-linux-gnu/libgcc_s.so.1"; do
  if [ -e "$x" ]; then
    rel="${x#$IMG}"; mkdir -p "$STAGE$(dirname "$rel")"; cp -aL "$x" "$STAGE$rel"
  else
    echo "NOTE: extra missing: $x"
  fi
done
for x in libXrender.so.1 libXcursor.so.1 libXfixes.so.3 libXcomposite.so.1 \
         libXi.so.6 libXinerama.so.1 libXxf86vm.so.1 libXrandr.so.2; do
  src="$IMG/usr/lib/i386-linux-gnu/$x"
  if [ -e "$src" ]; then
    cp -aL "$src" "$STAGE/usr/lib/i386-linux-gnu/$x"
    mkdir -p "$STAGE/lib/i386-linux-gnu"
    cp -aL "$src" "$STAGE/lib/i386-linux-gnu/$x" 2>/dev/null || true
  else
    echo "NOTE: X11 lib missing: $x"
  fi
done
mkdir -p "$STAGE/etc"; : > "$STAGE/etc/ld.so.cache"
mkdir -p "$STAGE/tmp" "$STAGE/run/user/1000" "$STAGE/home/username" "$STAGE/var/tmp" "$STAGE/root"
printf %s \
  "VFppZjIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAQAAAAAAABVVEMAVFppZjIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAQAAAAAAABVVEMAClVUQzAK" \
  | base64 -d > "$STAGE/etc/localtime"

echo "=== staged: $(find "$STAGE" -type f | wc -l) files, $(du -sh "$STAGE" | cut -f1) ==="
echo "=== zipping ==="
rm -f "$OUT/glibc-rootfs32.zip"
( cd "$STAGE" && zip -qry9 "$OUT/glibc-rootfs32.zip" lib etc tmp run home var )
ls -lh "$OUT/glibc-rootfs32.zip"
