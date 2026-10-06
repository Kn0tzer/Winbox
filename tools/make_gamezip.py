#!/usr/bin/env python3
"""Package a game directory into a lazy-loadable bw64url zip.

Usage:
  make_gamezip.py <src_dir> <out.zip> [--deflated]

The zip contains home/username/apps/<relpath> entries (relative to guest root),
matching the layout that bwPrepareFilesystem used to create in MEMFS.

- Default is ZIP_STORED (no compression): best for HTTP Range fetching because
  the C++ fszipurl layer can serve byte ranges directly without decompressing
  whole files first. Use --deflated to trade CPU for smaller downloads.
- zip64 is enabled automatically for >4GB archives (e.g. Portal ~5.3GB).

Serve the resulting zip over HTTP with `Accept-Ranges: bytes` support, then
launch the runtime with:
  ?gamezip=<url>&gamezipsize=<bytes>&exe=<path/to/game.exe>

Example:
  python3 tools/make_gamezip.py ~/portal-dist /srv/games/portal.zip
  # then open: /wine/?gamezip=/games/portal.zip&gamezipsize=5690838528&exe=portal/hl2.exe
"""
import os
import sys
import zipfile

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 2:
        print(__doc__)
        return 1
    src_dir, out_zip = args
    compress = zipfile.ZIP_DEFLATED if '--deflated' in sys.argv[1:] else zipfile.ZIP_STORED
    if not os.path.isdir(src_dir):
        print(f"error: not a directory: {src_dir}", file=sys.stderr)
        return 1

    count = 0
    total = 0
    with zipfile.ZipFile(out_zip, 'w', compress, allowZip64=True) as zf:
        for root, dirs, files in os.walk(src_dir):
            dirs.sort()
            for fn in sorted(files):
                full = os.path.join(root, fn)
                rel = os.path.relpath(full, src_dir).replace(os.sep, '/')
                arc = 'home/username/apps/' + rel
                zf.write(full, arc)
                count += 1
                total += os.path.getsize(full)
    size = os.path.getsize(out_zip)
    print(f"wrote {out_zip}: {count} files, {total} bytes source -> {size} bytes zip")
    print(f"launch: ?gamezip=<url>&gamezipsize={size}&exe=<exe path>")
    return 0

if __name__ == '__main__':
    sys.exit(main())
