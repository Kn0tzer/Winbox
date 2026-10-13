import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('game ZIP conversion preserves entries and supports direct reads deep inside stored files', () => {
  const dir = mkdtempSync(join(tmpdir(), 'gamezip-stored-'));
  try {
    execFileSync('python3', ['-c', `
import zipfile,sys,hashlib
from pathlib import Path
sys.path.insert(0,'tools')
from optimize_gamezip import optimize
p=Path(sys.argv[1]);src=p/'source.zip';dst=p/'stored.zip'
data=bytes(range(256))*4096
with zipfile.ZipFile(src,'w',zipfile.ZIP_DEFLATED) as z:
 z.writestr('home/username/apps/assets/',b'')
 z.writestr('home/username/apps/assets/large.bin',data)
 z.writestr('home/username/apps/empty.bin',b'')
original=hashlib.sha256(src.read_bytes()).digest()
optimize(src,dst)
assert hashlib.sha256(src.read_bytes()).digest()==original
with zipfile.ZipFile(dst) as z:
 assert all(e.compress_type==zipfile.ZIP_STORED for e in z.infolist())
 with z.open('home/username/apps/assets/large.bin') as f:
  f.seek(900003);assert f.read(511)==data[900003:900514]
 assert z.testzip() is None
try:optimize(src,src);raise AssertionError('Source replacement accepted')
except ValueError:pass
assert not list(p.glob('.gamezip-*'))
`, dir], { stdio:'pipe' });
  } finally { rmSync(dir, { recursive:true, force:true }); }
});
