"""Stage the versioned desktop helper over each pinned Wine prefix."""
import pathlib
import sys
import zipfile
import hashlib
import json

root = pathlib.Path(__file__).resolve().parents[2]
destination = pathlib.Path(sys.argv[1])
provenance = {}
for arch, helper in [('64', 'deskpin.exe'), ('32', 'deskpin32.exe')]:
    archive = destination / f'prefix{arch}.zip'
    replacement = (root / 'tools/rootfs64/gltest' / helper).read_bytes()
    paths = {f'home/username/{helper}'}
    if arch == '64':
        paths.add(f'home/username/.wine/drive_c/{helper}')
    temporary = archive.with_suffix('.staged.zip')
    with zipfile.ZipFile(archive) as old, zipfile.ZipFile(temporary, 'w') as new:
        for entry in old.infolist():
            if entry.filename not in paths:
                new.writestr(entry, old.read(entry))
        for path in sorted(paths):
            entry = zipfile.ZipInfo(path, date_time=(2026, 10, 15, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100755 << 16
            new.writestr(entry, replacement)
    temporary.replace(archive)
    with archive.open('rb') as stream:
        prefix_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    provenance[arch] = {
        'helper': helper,
        'helper_sha256': hashlib.sha256(replacement).hexdigest(),
        'prefix_sha256': prefix_hash,
    }
(destination / 'desktop-pin-provenance.json').write_text(json.dumps(provenance, sort_keys=True, indent=2) + '\n')
