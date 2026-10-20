#!/usr/bin/env bash
# Add the staged i386 ALSA-to-OSS bridge to the two 32-bit runtime archives.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
RUNTIME="${1:-$REPO/web/runtime}"
[[ -d "$RUNTIME" ]] || { echo "stage-alsa32: runtime directory missing: $RUNTIME" >&2; exit 2; }
[[ ! -L "$RUNTIME" ]] || { echo "stage-alsa32: refuse to update a symlinked runtime directory" >&2; exit 2; }
RUNTIME="$(cd "$RUNTIME" && pwd)"
WINE32="$RUNTIME/wine32.zip"
GLIBC32="$RUNTIME/glibc-rootfs32.zip"
SOURCE="$HERE/alsa32"
[[ -f "$WINE32" && -f "$GLIBC32" ]] || { echo "stage-alsa32: expected wine32.zip and glibc-rootfs32.zip in $RUNTIME" >&2; exit 2; }
[[ -f "$SOURCE/usr/lib/i386-linux-gnu/alsa-lib/libasound_module_pcm_oss.so" ]] || { echo "stage-alsa32: missing i386 pcm_oss plugin" >&2; exit 2; }
[[ -f "$SOURCE/usr/lib/i386-linux-gnu/alsa-lib/libasound_module_ctl_oss.so" ]] || { echo "stage-alsa32: missing i386 ctl_oss plugin" >&2; exit 2; }

cd "$SOURCE"
zip -q "$WINE32" \
    usr/lib/i386-linux-gnu/alsa-lib/libasound_module_pcm_oss.so \
    usr/lib/i386-linux-gnu/alsa-lib/libasound_module_ctl_oss.so
zip -q -r "$GLIBC32" etc/asound.conf usr/share/alsa

unzip -Z1 "$WINE32" | grep -qx 'usr/lib/i386-linux-gnu/alsa-lib/libasound_module_pcm_oss.so'
unzip -Z1 "$WINE32" | grep -qx 'usr/lib/i386-linux-gnu/alsa-lib/libasound_module_ctl_oss.so'
unzip -Z1 "$GLIBC32" | grep -qx 'etc/asound.conf'
unzip -Z1 "$GLIBC32" | grep -qx 'usr/share/alsa/alsa.conf'
echo "stage-alsa32: i386 ALSA-to-OSS bridge staged in $RUNTIME"
