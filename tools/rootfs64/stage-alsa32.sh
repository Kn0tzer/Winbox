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
# Fail early and by name if a required tool is missing, instead of dying
# mid-run with exit 127 on a minimal box.
command -v zip >/dev/null || { echo "stage-alsa32: zip not found (install zip)" >&2; exit 2; }
command -v unzip >/dev/null || { echo "stage-alsa32: unzip not found (install unzip)" >&2; exit 2; }

cd "$SOURCE"
zip -q "$WINE32" \
    usr/lib/i386-linux-gnu/alsa-lib/libasound_module_pcm_oss.so \
    usr/lib/i386-linux-gnu/alsa-lib/libasound_module_ctl_oss.so
zip -q -r "$GLIBC32" etc/asound.conf usr/share/alsa

unzip -Z1 "$WINE32" | grep -x 'usr/lib/i386-linux-gnu/alsa-lib/libasound_module_pcm_oss.so' >/dev/null
unzip -Z1 "$WINE32" | grep -x 'usr/lib/i386-linux-gnu/alsa-lib/libasound_module_ctl_oss.so' >/dev/null
unzip -Z1 "$GLIBC32" | grep -x 'etc/asound.conf' >/dev/null
unzip -Z1 "$GLIBC32" | grep -x 'usr/share/alsa/alsa.conf' >/dev/null
echo "stage-alsa32: i386 ALSA-to-OSS bridge staged in $RUNTIME"
