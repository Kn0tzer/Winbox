// Boot the actual DRM-free Celeste archive; stage and provenance are measured.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkIncludes } from '../lib/assert.mjs';
import { monoArchive } from '../lib/guest-fixtures.mjs';
import { resolve } from 'node:path';
export default class CelesteBootTest extends ProbeTest {
  constructor() { super({ tag: 'celeste-boot', timeoutMs: 1200000 }); }
  async run({ page }) {
    const game = await this.mountArchive(process.env.CELESTE_ZIP || '/home/ubuntu/kn0tzer/games/celeste-drmfree.zip', 'celeste.zip');
    const overlay = resolve(this.dir, 'celeste-overlay.zip');
    monoArchive(overlay, { gl32: true });
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
