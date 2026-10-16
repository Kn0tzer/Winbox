import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';

test('Wine32 font export preserves input data, is idempotent, and records font hashes', () => {
  const script = resolve('tools/browser/stage-wine32-fonts.py');
  execFileSync('python3', ['-c', `
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
    original_wine = wine.read_bytes()
    m.stage_fonts(root)
    once = glibc.read_bytes()
    record = (root / 'wine32-font-provenance.json').read_bytes()
    m.stage_fonts(root)
    assert glibc.read_bytes() == once
    assert (root / 'wine32-font-provenance.json').read_bytes() == record
    assert wine.read_bytes() == original_wine
    with zipfile.ZipFile(glibc) as z:
        assert z.comment == b'preserved archive comment'
        assert z.read('lib/keep.so') == b'preserved input'
        assert (z.getinfo('lib/keep.so').external_attr >> 16) & 0o777 == 0o755
        assert len(z.namelist()) == len(set(z.namelist())) == 3
        assert z.read('opt/wine-stable/share/wine/fonts/tahoma.ttf') == b'regular font'
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
`], { stdio: 'pipe' });
});
