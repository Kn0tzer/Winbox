import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('Wine32 font export preserves input data, is idempotent, and records font hashes', () => {
  const script = resolve(repoRoot, 'tools/browser/stage-wine32-fonts.py');
  execFileSync('python3', ['-c', String.raw`
import hashlib, importlib.util, json, tempfile, zipfile
from pathlib import Path
spec = importlib.util.spec_from_file_location('fonts', ${JSON.stringify(script)})
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    wine = root / 'wine64.zip'
    glibc = root / 'glibc-rootfs32.zip'
    with zipfile.ZipFile(wine, 'w') as z:
        z.writestr('usr/share/wine/fonts/tahoma.ttf', b'regular font')
        z.writestr('usr/share/wine/fonts/tahomabd.ttf', b'bold font')
        z.writestr('usr/lib/ignored.so', b'not a font')
    with zipfile.ZipFile(glibc, 'w') as z:
        z.comment = b'preserved archive comment'
        info = zipfile.ZipInfo('lib/keep.so', (2020, 1, 1, 0, 0, 0))
        info.external_attr = 0o100755 << 16
        z.writestr(info, b'preserved input')
        z.writestr('usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', b'pinned fallback font')
    with zipfile.ZipFile(root / 'prefix32.zip', 'w') as z:
        z.writestr(m.REGISTRY_PATH, b'WINE REGISTRY Version 2\n\n[Other]\n"Keep"="value"\n')
        z.writestr('home/username/keep.txt', b'prefix input')
    original_wine = wine.read_bytes()
    m.stage_fonts(root)
    once = glibc.read_bytes()
    prefix_once = (root / 'prefix32.zip').read_bytes()
    record = (root / 'wine32-font-provenance.json').read_bytes()
    m.stage_fonts(root)
    assert glibc.read_bytes() == once
    assert (root / 'prefix32.zip').read_bytes() == prefix_once
    assert (root / 'wine32-font-provenance.json').read_bytes() == record
    assert wine.read_bytes() == original_wine
    with zipfile.ZipFile(glibc) as z:
        assert z.comment == b'preserved archive comment'
        assert z.read('lib/keep.so') == b'preserved input'
        assert (z.getinfo('lib/keep.so').external_attr >> 16) & 0o777 == 0o755
        assert len(z.namelist()) == len(set(z.namelist())) == 4
        assert z.read('opt/wine-stable/share/wine/fonts/tahoma.ttf') == b'regular font'
    with zipfile.ZipFile(root / 'prefix32.zip') as z:
        assert len(z.namelist()) == len(set(z.namelist())) == 2
        assert z.read('home/username/keep.txt') == b'prefix input'
        assert b'"Arial"="DejaVu Sans"' in z.read(m.REGISTRY_PATH)
    provenance = json.loads(record)
    assert provenance['glibc_rootfs32_sha256'] == hashlib.sha256(once).hexdigest()
    assert provenance['fonts']['opt/wine-stable/share/wine/fonts/tahoma.ttf'] == hashlib.sha256(b'regular font').hexdigest()
    # An incomplete font source must fail before changing the destination.
    with zipfile.ZipFile(wine, 'w') as z:
        z.writestr('usr/share/wine/fonts/tahoma.ttf', b'regular font')
    try: m.stage_fonts(root)
    except ValueError: pass
    else: raise AssertionError('missing bold font accepted')
    assert glibc.read_bytes() == once
    assert (root / 'prefix32.zip').read_bytes() == prefix_once
`], { stdio: 'pipe' });
});


test('Wine32 DirectWrite Arial replacement preserves registry content and is idempotent', () => {
  const script = resolve(repoRoot, 'tools/browser/stage-wine32-fonts.py');
  execFileSync('python3', ['-c', String.raw`
import importlib.util
spec = importlib.util.spec_from_file_location('fonts', ${JSON.stringify(script)})
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
section = r'[Software\\Wine\\Fonts\\Replacements]'
original = b'WINE REGISTRY Version 2\n\n[Other] 7\n"Arial"="keep"\n'
patched = m.configure_replacements(original)
assert original in patched
assert section.encode() in patched
assert b'"Arial"="DejaVu Sans"' in patched
assert m.configure_replacements(patched) == patched
existing = ('WINE REGISTRY Version 2\r\n\r\n' + section + ' 19\r\n#time=abc\r\n"Arial"="old"\r\n"Custom"="preserve"\r\n\r\n[After]\r\n"Arial"="unrelated"\r\n').encode()
updated = m.configure_replacements(existing)
assert updated.count(section.encode()) == 1
assert b'"Arial"="old"' not in updated
assert b'"Custom"="preserve"' in updated
assert updated.endswith(b'[After]\r\n"Arial"="unrelated"\r\n')
assert b'\n' not in updated.replace(b'\r\n', b'')
assert m.configure_replacements(updated) == updated
`], { stdio: 'pipe' });
});
