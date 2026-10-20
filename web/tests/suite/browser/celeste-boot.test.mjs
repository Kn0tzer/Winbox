// Boot the actual DRM-free Celeste archive; stage and provenance are measured.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkIncludes, checkGreaterThan } from '../lib/assert.mjs';
import { canvasPixels } from '../lib/guest-fixtures.mjs';
import { writeFile } from 'node:fs/promises';
export default class CelesteBootTest extends ProbeTest {
  constructor() { super({ tag: 'celeste-boot', timeoutMs: 1200000, probeKind: 'heavy' }); }
  async run({ page }) {
    const game = await this.mountArchive(process.env.CELESTE_ZIP || '/home/ubuntu/kn0tzer/games/celeste-drmfree.zip', 'celeste.zip');
    await this.bootGuest(page, { arch: 'x86', gamezip: game.url, gamezipsize: game.bytes,
      p: 'Celeste.exe', exe: 'Celeste.exe', args: '[]', session: '0' });
    await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
    const stage = await this.waitForLog(page, /GAME DISPLAYED/, 900000);
    check(stage.length > 0, 'Actual Celeste must complete loading and display its game window');
    checkIncludes(this.consoleLines.join('\n'), 'wine', '32-bit Wine must execute Celeste');
    check(!!this.buildInfo.wasm_sha256, 'BUILD_INFO wasm hash must match actual runtime');
    // GAME DISPLAYED precedes the first draw. A black mapped window previously
    // passed this check, so inspect the actual guest canvas as well.
    let pixels;
    try {
      const deadline = Date.now() + 60000;
      do {
        pixels = await canvasPixels(page);
        if (pixels.lit > 10000 && pixels.varied > 1000) break;
        await page.waitForTimeout(500);
      } while (Date.now() < deadline);
    } finally {
      await page.screenshot({ path: this.dir + '/celeste-stage.png' });
      await writeFile(this.dir + '/pixels.json', JSON.stringify(pixels || null, null, 2));
    }
    checkGreaterThan(pixels.lit, 10000, 'Celeste must present a visible game frame');
    checkGreaterThan(pixels.varied, 1000, 'Celeste must present colored content beyond window chrome');
  }
}
