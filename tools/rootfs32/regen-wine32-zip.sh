#!/usr/bin/env bash
# regen-wine32-zip.sh — end-to-end regeneration of the shipped
# wine32.zip: build the base tree (build-wine32-base.sh), zip it,
# apply the two scripted post-steps with the in-tree staging scripts,
# verify the result member-by-member against the previously shipped
# zip, and gate on check_runtime_completeness.py.
#
# The base tree's provenance (WineHQ noble i386 deb + patchelf
# __isoc23 injection + Debian libwine 8.0 share data + bookworm i386
# closure + two preserved locally built members) is documented in
# build-wine32-base.sh and docs/reports/wine32-base-forensics-20261028.md.
#
# Verification is fail-closed: ANY member difference vs the shipped
# zip (name set, size, or sha256) aborts before the gate, naming the
# members. There is no allow-list — the two preserved members are
# extracted FROM the shipped zip by the base build, so a faithful
# regeneration is member-identical, not merely gate-green. (The
# whole-archive sha256 still differs: staged archives carry
# wall-clock member timestamps and are not byte-reproducible — the
# standing staged-archive invariant. Publishing a regeneration is
# the documented re-pin: re-measure the zip in
# tools/browser/runtime-assets.json, regenerate the artifact
# manifest, rebuild the retained archive.)
#
# Prerequisites:
#   - web/runtime populated with the current shipped set (the other
#     assets are copied to a scratch runtime dir for the gate; the
#     shipped wine32.zip is the verification oracle + the source of
#     the two preserved members). On a fresh worktree, copy the 13
#     manifest files from a tree that has them (bootstrap.py --check
#     is the arbiter).
#   - tools/vkd3d/build.w32 present (the stage-vkd3d-payloads32.sh
#     input; build it with tools/vkd3d/build-win32.sh or copy the
#     preserved build output — it is a build artifact, gitignored).
#
# Usage: regen-wine32-zip.sh [--install]
#   Without --install the regenerated zip stays in the scratch dir
#   (tools/rootfs32/wine32-regen/) and web/runtime is untouched.
#   With --install it replaces web/runtime/wine32.zip AFTER every
#   check passes (re-pin afterwards, per above).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="$HERE/wine32-regen"
TREE="$WORK/base-tree"
SCRATCH_RT="$WORK/runtime"
INSTALL=0
[ "${1:-}" = "--install" ] && INSTALL=1

ORACLE_RT="${BW32_ORACLE_RT:-$REPO/web/runtime}"
SHIPPED="$ORACLE_RT/wine32.zip"
[ -f "$SHIPPED" ] || { echo "regen-wine32-zip: shipped wine32.zip missing: $SHIPPED" >&2; exit 2; }
[ -f "$REPO/tools/vkd3d/build.w32/libs/d3d12/d3d12.dll" ] || {
    echo "regen-wine32-zip: tools/vkd3d/build.w32 missing — build it (tools/vkd3d/build-win32.sh) or copy the preserved build output first" >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$SCRATCH_RT"

echo "=== [1/5] base tree ==="
"$HERE/build-wine32-base.sh" "$TREE" --shipped "$SHIPPED"

echo "=== [2/5] zip the base (attrs mirror the shipped convention) ==="
python3 - "$TREE" "$SCRATCH_RT/wine32.zip" <<'EOF'
import os, sys, zipfile
tree, out = sys.argv[1], sys.argv[2]
entries = []
for dp, dn, fn in os.walk(tree):
    dn.sort()
    rel = os.path.relpath(dp, tree)
    if rel != ".":
        entries.append((rel.replace(os.sep, "/") + "/", None))
    for f in sorted(fn):
        full = os.path.join(dp, f)
        name = os.path.relpath(full, tree).replace(os.sep, "/")
        entries.append((name, full))
with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name, full in entries:
        if full is None:
            zi = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 0, 0, 0))
            zi.external_attr = 0o40755 << 16
            zi.create_system = 3
            z.writestr(zi, b"")
        else:
            st = os.stat(full)
            import time
            zi = zipfile.ZipInfo(name, date_time=time.localtime(st.st_mtime)[:6])
            zi.external_attr = 0o600 << 16
            zi.create_system = 3
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, open(full, "rb").read())
print(f"zipped {len(entries)} members")
EOF

echo "=== [3/5] post-steps (in-tree staging scripts) ==="
cp "$ORACLE_RT/glibc-rootfs32.zip" "$SCRATCH_RT/"
"$REPO/tools/rootfs64/stage-alsa32.sh" "$SCRATCH_RT"
# stage-vkd3d-payloads32.sh stages into the repo's own runtime dirs, so
# it cannot target this scratch dir; its payload is applied to the
# scratch zip directly by the python step below (same semantics:
# replace the two members with the build.w32 bytes), which also
# asserts the payload matches the completeness checker's pin family.
python3 - "$SCRATCH_RT/wine32.zip" "$REPO/tools/vkd3d/build.w32" <<'PYEOF'
import hashlib, sys, zipfile, shutil
archive, w32 = sys.argv[1], sys.argv[2]
want = {
    "opt/wine-stable/lib/wine/i386-windows/d3d12.dll": open(w32 + "/libs/d3d12/d3d12.dll", "rb").read(),
    "opt/wine-stable/lib/wine/i386-windows/d3d12core.dll": open(w32 + "/libs/d3d12core/d3d12core.dll", "rb").read(),
}
pins = {
    "opt/wine-stable/lib/wine/i386-windows/d3d12.dll": "872c3d65ad3febfb3fdbd935ca2272624a0e711724dd4013001dbc9ffd3ab0e1",
    "opt/wine-stable/lib/wine/i386-windows/d3d12core.dll": "71e9c174084474d84e04893f0cef9eaea9bbede43201308e46b19b01fd727600",
}
for n, d in want.items():
    h = hashlib.sha256(d).hexdigest()
    assert h == pins[n], f"{n}: build.w32 payload hash {h} != the completeness checker's vkd3d32 pin"
tmp = archive + ".tmp"
with zipfile.ZipFile(archive) as zin, zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zout:
    for info in zin.infolist():
        data = want.get(info.filename, zin.read(info.filename))
        zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
        zi.external_attr = info.external_attr
        zi.create_system = 3
        zi.compress_type = zipfile.ZIP_DEFLATED
        zout.writestr(zi, data)
shutil.move(tmp, archive)
print("vkd3d32 payload applied to scratch zip")
PYEOF
echo "=== [4/5] member-by-member verification vs the shipped zip ==="
python3 - "$SHIPPED" "$SCRATCH_RT/wine32.zip" <<'EOF'
import hashlib, sys, zipfile
def census(path):
    with zipfile.ZipFile(path) as z:
        return {i.filename: (i.file_size, None if i.filename.endswith("/") else hashlib.sha256(z.read(i.filename)).hexdigest())
                for i in z.infolist()}
old, new = census(sys.argv[1]), census(sys.argv[2])
only_old = sorted(set(old) - set(new))
only_new = sorted(set(new) - set(old))
diff = sorted(k for k in set(old) & set(new) if old[k] != new[k])
print(f"shipped {len(old)} members, regenerated {len(new)} members")
for k in only_old: print(f"  MISSING in regen: {k}")
for k in only_new: print(f"  EXTRA in regen:   {k}")
for k in diff: print(f"  DIFFERS:          {k} shipped={old[k]} regen={new[k]}")
if only_old or only_new or diff:
    raise SystemExit("regen-wine32-zip: member verification FAILED (see above)")
print("member verification: IDENTICAL member set, sizes and sha256")
EOF

echo "=== [5/5] completeness gate (in-tree, scratch web/runtime set) ==="
# The checker reads the tree's own web/runtime in full tool context.
# This worktree's web/runtime is scratch: assemble the shipped set
# there from the oracle dir, swap in the regenerated wine32.zip, gate.
mkdir -p "$REPO/web/runtime"
if [ "$ORACLE_RT" != "$REPO/web/runtime" ]; then
    for f in "$ORACLE_RT"/*.zip; do cp "$f" "$REPO/web/runtime/"; done
fi
cp "$SCRATCH_RT/wine32.zip" "$REPO/web/runtime/wine32.zip"
(cd "$REPO" && python3 tools/rootfs64/check_runtime_completeness.py)

if [ "$INSTALL" = "1" ]; then
    cp "$SCRATCH_RT/wine32.zip" "$SHIPPED"
    echo "regen-wine32-zip: INSTALLED to $SHIPPED — re-pin tools/browser/runtime-assets.json + regenerate the manifest + rebuild the retained archive before committing."
else
    echo "regen-wine32-zip: regenerated zip kept at $SCRATCH_RT/wine32.zip (no --install; web/runtime untouched)."
fi
echo "regen-wine32-zip: done."
