#!/usr/bin/env bash
# regen-glibc-rootfs32.sh — regenerate web/runtime/glibc-rootfs32.zip
# from durable inputs, end to end:
#
#   build-img386.sh  ->  closure32.sh (DT_NEEDED closure, base zip)
#   ->  assembly adjustments (below)  ->  the 32-bit post-steps of
#   apply-runtime-poststeps.sh (stage-fonts, stage-fonts32, stage-alsa32)
#   ->  member-by-member verification against the previously shipped zip
#   ->  check_runtime_completeness.py
#
# The assembly adjustments exist because the shipped artifact proves
# them (docs/reports/sysroot32-regeneration-20261028.md):
#
#   (A) The glibc member set is Ubuntu noble libc6 2.39-0ubuntu8's
#       library directory (all 19 files under lib/i386-linux-gnu/ plus
#       lib/ld-linux.so.2), NOT the bookworm closure's. The closure
#       stages bookworm glibc; this step replaces those members with
#       the noble overlay's bytes (img386/.glibc-noble/).
#   (B) Six /lib-resident libraries the shipped zip carries are not
#       reached by the restored closure's BFS on this extracted tree
#       (in the original installed tree they were transitive deps of
#       the seed set — libdbus via libpulse, libcap/liblzma via
#       libsystemd, libbz2/libgpg-error via the freetype/gnutls tails,
#       libgcc_s via the GL/Vulkan loaders). They are staged
#       explicitly, by SONAME, from their pinned debs in img386.
#
# Four of the noble-glibc members could NOT be reproduced from any
# pinned source (checked: Debian 2.36-9+deb12u14, Ubuntu 2.39-0ubuntu8
# i386 and amd64-lib32, Ubuntu 2.39-0ubuntu8.9, Debian 2.41):
#   lib/i386-linux-gnu/libanl.so.1       13736 B sha256 909e5568…
#   lib/i386-linux-gnu/libnsl.so.1      100300 B sha256 567f7696…
#   lib/i386-linux-gnu/libpcprofile.so   13852 B sha256 0c8181ee…
#   lib/i386-linux-gnu/libthread_db.so.1 42952 B sha256 472d3516…
# The verification step fails closed on exactly those four unless
# --allow-glibc-aux-diff is passed, in which case the noble-release
# bytes are substituted and the difference is printed, never silent.
# Any OTHER member difference fails closed unconditionally.
#
# Prerequisites: the checkout's web/runtime set is present (restore via
# tools/browser/bootstrap.py or copy the pinned set); the previously
# shipped glibc-rootfs32.zip in web/runtime is the verification
# reference (override with --reference). Host DejaVuSans + copyright
# are read by stage-fonts.py from the host /usr/share (on the Ubuntu
# 24.04 build host they match the shipped members byte-for-byte).
#
# Options:
#   --reference ZIP          verification reference (default: the
#                            current web/runtime/glibc-rootfs32.zip)
#   --allow-glibc-aux-diff   accept the four named glibc aux members
#                            differing (noble-release bytes substituted)
#   --keep-work              keep the scratch dir (img386, closure
#                            staging) for inspection; default deletes it
# Work dir: ${BW32_WORK:-$(mktemp -d /tmp/bw32-regen.XXXXXX)}
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
cd "$REPO"

REFERENCE=""
ALLOW_AUX=0
KEEP_WORK=0
while [ $# -gt 0 ]; do
    case "$1" in
        --reference) REFERENCE="$2"; shift 2 ;;
        --allow-glibc-aux-diff) ALLOW_AUX=1; shift ;;
        --keep-work) KEEP_WORK=1; shift ;;
        *) echo "regen-glibc-rootfs32: unknown option $1" >&2; exit 2 ;;
    esac
done

WORK="${BW32_WORK:-$(mktemp -d /tmp/bw32-regen.XXXXXX)}"
mkdir -p "$WORK"
echo "regen: work dir $WORK"

[ -f web/runtime/glibc-rootfs32.zip ] || {
    echo "regen: web/runtime/glibc-rootfs32.zip missing — restore the runtime set first" >&2; exit 2; }
[ -f web/runtime/wine32.zip ] || {
    echo "regen: web/runtime/wine32.zip missing — stage-alsa32 needs it" >&2; exit 2; }
REFERENCE="${REFERENCE:-$WORK/reference.zip}"
[ "$REFERENCE" = "$WORK/reference.zip" ] && cp web/runtime/glibc-rootfs32.zip "$REFERENCE"

# stage-fonts.py unconditionally processes dist/runtime as well as
# web/runtime. Mirror the monolith's world: if this checkout has no
# dist/runtime, create a minimal one for the run and remove it after.
CREATED_DIST=0
if [ ! -d dist/runtime ]; then
    mkdir -p dist/runtime
    cp web/runtime/glibc-rootfs32.zip web/runtime/wine32.zip dist/runtime/
    CREATED_DIST=1
fi
cleanup() {
    [ "$CREATED_DIST" = "1" ] && rm -rf dist/runtime
    [ "$KEEP_WORK" = "1" ] || rm -rf "$WORK"
}
trap cleanup EXIT

# 1. img386-equivalent + closure base zip.
BW32_DEB_CACHE="${BW32_DEB_CACHE:-$WORK/debs}" \
    "$HERE/build-img386.sh" "$WORK/img386"
BW32_IMG386="$WORK/img386" BW32_STAGE_OUT="$WORK/stage" \
    "$HERE/closure32.sh"

# 2. Assembly adjustments on the base zip.
python3 - "$WORK/stage/glibc-rootfs32.zip" "$WORK/img386" "$WORK/assembled.zip" <<'PYEOF'
import sys, zipfile
from pathlib import Path
base, img, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
noble = img / '.glibc-noble/usr/lib/i386-linux-gnu'
GLIBC_SET = ['ld-linux.so.2', 'libBrokenLocale.so.1', 'libanl.so.1',
             'libc.so.6', 'libc_malloc_debug.so.0', 'libdl.so.2',
             'libm.so.6', 'libmemusage.so', 'libnsl.so.1',
             'libnss_compat.so.2', 'libnss_dns.so.2', 'libnss_files.so.2',
             'libnss_hesiod.so.2', 'libpcprofile.so', 'libpthread.so.0',
             'libresolv.so.2', 'librt.so.1', 'libthread_db.so.1',
             'libutil.so.1']
EXPLICIT = ['libdbus-1.so.3', 'libcap.so.2', 'liblzma.so.5',
            'libbz2.so.1.0', 'libgpg-error.so.0', 'libgcc_s.so.1']
replace = {}
for name in GLIBC_SET:
    replace['lib/i386-linux-gnu/' + name] = (noble / name).read_bytes()
replace['lib/ld-linux.so.2'] = (noble / 'ld-linux.so.2').read_bytes()
for name in EXPLICIT:
    replace['lib/i386-linux-gnu/' + name] = \
        (img / 'lib/i386-linux-gnu' / name).read_bytes()
with zipfile.ZipFile(base) as src, \
     zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as dst:
    seen = set()
    for info in src.infolist():
        if info.filename in replace:
            dst.writestr(info.filename, replace[info.filename])
            seen.add(info.filename)
        else:
            dst.writestr(info, src.read(info.filename))
    for name, data in replace.items():
        if name not in seen:
            dst.writestr(name, data)
            print(f'assembled: + {name} (not in closure base)')
print(f'assembled: glibc set replaced from noble overlay; '
      f'{len(EXPLICIT)} explicit /lib members staged')
PYEOF

# 3. Install + the 32-bit post-steps, in apply-runtime-poststeps.sh order.
cp "$WORK/assembled.zip" web/runtime/glibc-rootfs32.zip
cp "$WORK/assembled.zip" dist/runtime/glibc-rootfs32.zip
BW32_SYSROOT="$WORK/img386" python3 tools/rootfs32/stage-fonts.py
tools/rootfs64/stage-fonts32.sh
tools/rootfs64/stage-alsa32.sh web/runtime
tools/rootfs64/stage-alsa32.sh dist/runtime

# 4. Verify member-by-member against the reference.
python3 - "$REFERENCE" web/runtime/glibc-rootfs32.zip "$ALLOW_AUX" <<'PYEOF'
import sys, zipfile, hashlib
ref_path, new_path, allow_aux = sys.argv[1], sys.argv[2], sys.argv[3] == '1'
AUX = {'lib/i386-linux-gnu/libanl.so.1',
       'lib/i386-linux-gnu/libnsl.so.1',
       'lib/i386-linux-gnu/libpcprofile.so',
       'lib/i386-linux-gnu/libthread_db.so.1'}
def members(p):
    with zipfile.ZipFile(p) as z:
        return {i.filename: (i.file_size, hashlib.sha256(z.read(i.filename)).hexdigest())
                for i in z.infolist()}
ref, new = members(ref_path), members(new_path)
problems = []
if set(ref) != set(new):
    problems.append(f'member set differs: missing={sorted(set(ref)-set(new))} '
                    f'extra={sorted(set(new)-set(ref))}')
aux_diff = []
for name in sorted(set(ref) & set(new)):
    if ref[name] != new[name]:
        (aux_diff if name in AUX else problems).append(name)
for name in aux_diff:
    print(f'known-diff (glibc aux): {name}: shipped {ref[name][0]} B '
          f'sha256 {ref[name][1][:12]}… != regenerated {new[name][0]} B '
          f'sha256 {new[name][1][:12]}…')
if problems:
    print('VERIFY FAILED:', *problems, sep='\n  ', file=sys.stderr)
    sys.exit(1)
if aux_diff and not allow_aux:
    print('VERIFY FAILED: the four glibc aux members differ and '
          '--allow-glibc-aux-diff was not passed (see script header).',
          file=sys.stderr)
    sys.exit(1)
print(f'verify: {len(new)} members, {len(new)-len(aux_diff)} byte-identical '
      f'to the shipped zip, {len(aux_diff)} known glibc-aux differences')
PYEOF

# 5. The completeness gate for the whole runtime set.
python3 tools/rootfs64/check_runtime_completeness.py

echo "regen: glibc-rootfs32.zip regenerated and verified."
echo "regen: publishing is the documented re-pin — re-measure"
echo "  web/runtime/glibc-rootfs32.zip in tools/browser/runtime-assets.json"
echo "  (bytes + sha256), regenerate the artifact manifest"
echo "  (tools/rootfs64/gen_runtime_manifest.py), and rebuild the"
echo "  retained runtime-assets archive before committing the set."
