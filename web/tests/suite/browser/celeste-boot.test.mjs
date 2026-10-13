// Boot the actual DRM-free Celeste archive; stage and provenance are measured.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkIncludes } from '../lib/assert.mjs';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
export default class CelesteBootTest extends ProbeTest {
  constructor() { super({ tag: 'celeste-boot', timeoutMs: 1200000 }); }
  async run({ page }) {
    const game = await this.mountArchive(process.env.CELESTE_ZIP || '/home/ubuntu/kn0tzer/games/celeste-drmfree.zip', 'celeste.zip');
    const mono = process.env.MONO32_DIR || 'tools/rootfs64/mono32';
    const overlay = resolve(this.dir, 'celeste-overlay.zip');
    execFileSync('python3', ['-c', `import zipfile,pathlib,sys
root=pathlib.Path(sys.argv[2]).resolve()
if not (root/'mono/mono-2.0/bin/libmono-2.0-x86.dll').is_file():raise SystemExit('MONO32_DIR must contain Wine Mono')
with zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_STORED) as z:
 z.write('tools/rootfs64/libgl32/libGL.so.1','home/username/apps/libGL.so.1')
 for p in sorted(root.rglob('*')):
  if p.is_file():z.write(p,'home/username/apps/'+p.relative_to(root).as_posix())
`, overlay, mono]);
    const mounted = await this.mountArchive(overlay, 'celeste-overlay.zip');
    await this.bootGuest(page, { arch: 'x86', gamezip: game.url, gamezipsize: game.bytes,
      overlay: mounted.url, overlaysize: mounted.bytes, p: 'Celeste.exe', exe: 'Celeste.exe', args: '[]',
      session: '0', ldpath: '/home/username/apps' });
    await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
    const stage = await this.waitForLog(page, /GAME DISPLAYED/, 900000);
    check(stage.length > 0, 'Actual Celeste must complete loading and display its game window');
    checkIncludes(this.consoleLines.join('\n'), 'wine', '32-bit Wine must execute Celeste');
    check(!!this.buildInfo.wasm_sha256, 'BUILD_INFO wasm hash must match actual runtime');
    await page.screenshot({ path: this.dir + '/celeste-stage.png' });
  }
}
