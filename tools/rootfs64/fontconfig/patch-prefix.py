#!/usr/bin/env python3
"""Install Wine font fallback configuration in a private prefix ZIP."""
from pathlib import Path
import argparse
import re
import tempfile
import zipfile

ALIASES = {name: 'Liberation Sans' for name in
           ('Arial', 'Tahoma', 'Verdana', 'Trebuchet MS', 'GorDIN',
            'GorDIN Regular', 'GorDIN Bold', 'GorDIN Semibold', 'GorDIN Light')}
ALIASES['Courier New'] = 'Liberation Mono'
SECTION = r'[Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes]'

def configure_registry(data):
    text = data.decode('utf-8')
    nl = '\r\n' if '\r\n' in text else '\n'
    text = text.replace('\r\n', '\n')
    start = text.find(SECTION)
    if start < 0:
        text += '\n' + SECTION + '\n'
        start = text.find(SECTION)
    end = text.find('\n[', start + len(SECTION))
    if end < 0:
        end = len(text)
    block = text[start:end].rstrip()
    for name, family in ALIASES.items():
        entry = f'"{name}"="{family}"'
        pattern = '^"' + re.escape(name) + '"=.*$'
        if re.search(pattern, block, re.M):
            block = re.sub(pattern, entry, block, flags=re.M)
        else:
            block += '\n' + entry
    return (text[:start] + block + '\n\n' + text[end:]).replace('\n', nl).encode('utf-8')

def patch_prefix(prefix):
    config = Path(__file__).with_name('fonts.conf').read_bytes()
    with zipfile.ZipFile(prefix) as old:
        entries = [(info, old.read(info)) for info in old.infolist()]
    registries = [info.filename for info, data in entries if info.filename.endswith('/.wine/system.reg')]
    if not registries:
        raise ValueError('prefix contains no Wine system.reg')
    with tempfile.NamedTemporaryFile(dir=prefix.parent, suffix='.zip', delete=False) as temporary:
        temp = Path(temporary.name)
    try:
        with zipfile.ZipFile(temp, 'w') as new:
            for info, data in entries:
                if info.filename == 'etc/fonts/fonts.conf':
                    continue
                if info.filename in registries:
                    data = configure_registry(data)
                new.writestr(info, data)
            info = zipfile.ZipInfo('etc/fonts/fonts.conf')
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            new.writestr(info, config)
        temp.replace(prefix)
    finally:
        temp.unlink(missing_ok=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    patch_prefix(parser.parse_args().prefix)
