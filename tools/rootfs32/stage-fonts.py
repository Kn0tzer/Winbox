#!/usr/bin/env python3
"""Stage Wine's dlopen font dependencies into both 32-bit rootfs archives."""
from datetime import datetime, timezone
from pathlib import Path
import os
import shutil
import tempfile
import zipfile

repo = Path(__file__).resolve().parents[2]
sysroot = Path(os.environ.get('BW32_SYSROOT', '/home/ubuntu/wine32-stage/img386'))
assets = {
    'usr/lib/i386-linux-gnu/libfontconfig.so.1': sysroot / 'usr/lib/i386-linux-gnu/libfontconfig.so.1',
    'lib/i386-linux-gnu/libexpat.so.1': sysroot / 'lib/i386-linux-gnu/libexpat.so.1',
    'usr/share/fonts/truetype/dejavu/DejaVuSans.ttf': Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'),
    'usr/share/doc/libfontconfig1/copyright': sysroot / 'usr/share/doc/libfontconfig1/copyright',
    'usr/share/doc/libexpat1/copyright': sysroot / 'usr/share/doc/libexpat1/copyright',
    'usr/share/doc/fonts-dejavu-core/copyright': Path('/usr/share/doc/fonts-dejavu-core/copyright'),
}
payload = {name: path.read_bytes() for name, path in assets.items()}
for name, data in payload.items():
    if '.so.' in name and data[:5] != b'\x7fELF\x01':
        raise ValueError(f'{name} must be a 32-bit ELF library')
payload['etc/fonts/fonts.conf'] = b'''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <cachedir>/tmp/fontconfig</cachedir>
</fontconfig>
'''
stamp = datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
backup = Path('/home/ubuntu/backups') / f'bw64-fonts32-{stamp}'
for runtime in ('dist/runtime', 'web/runtime'):
    archive = repo / runtime / 'glibc-rootfs32.zip'
    with zipfile.ZipFile(archive) as source:
        if all(name in source.namelist() and source.read(name) == data for name, data in payload.items()):
            print(f'{runtime}: fonts already staged')
            continue
        saved = backup / runtime / archive.name
        saved.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(archive, saved)
        with tempfile.NamedTemporaryFile(dir=archive.parent, suffix='.zip', delete=False) as temporary:
            path = Path(temporary.name)
        try:
            with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as destination:
                destination.comment = source.comment
                for entry in source.infolist():
                    if entry.filename not in payload:
                        destination.writestr(entry, source.read(entry))
                for name, data in payload.items():
                    destination.writestr(name, data)
            path.replace(archive)
        finally:
            path.unlink(missing_ok=True)
        print(f'{runtime}: staged fonts; backup {saved}')
