import { ProbeTest } from '../lib/probe-base.mjs';
import { checkGreaterThan } from '../lib/assert.mjs';
import { canvasPixels, guestFixture } from '../lib/guest-fixtures.mjs';

export default class Gl32WebGpuClearTest extends ProbeTest {
  constructor() { super({ tag: 'gl32-webgpu-clear', timeoutMs: 600000 }); }

  async run({ page }) {
    const fixture = await guestFixture('trigl', 'x86');
    await this.bootGuest(page, { arch: 'x86', glbackend: 'webgpu' });
    await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
    await this.stageFile(page, 'trigl32.exe', fixture);
    await this.stageFile(page, 'libGL.so.1', 'tools/rootfs64/libgl32/libGL.so.1');
    const from = await this.spawnGuest(page, 'trigl32.exe', { arch: 'x86', env: 'WINEDEBUG=+debugstr' });
    await this.waitForLog(page, /trigl: RESULT [01]/, 300000, from);
    const pixels = await canvasPixels(page);
    checkGreaterThan(pixels.lit, 0, `WebGPU GL clear must reach the presentation canvas (LIT=${pixels.lit})`);
    checkGreaterThan(pixels.varied, 100, 'The orange clear color must be visible in the presented frame');
    const blue = await page.evaluate(() => {
      const source = document.querySelector('#canvas'), copy = document.createElement('canvas');
      copy.width = source.width; copy.height = source.height;
      const context = copy.getContext('2d'); context.drawImage(source, 0, 0);
      const bytes = context.getImageData(0, 0, copy.width, copy.height).data;
      let count = 0;
      for (let i = 0; i < bytes.length; i += 4) {
        if (bytes[i] < 50 && bytes[i + 1] < 50 && bytes[i + 2] > 60) count++;
      }
      return count;
    });
    checkGreaterThan(blue, 1000, `The guest's blue clear pixels must reach the canvas (BLUE=${blue})`);
    await page.screenshot({ path: this.dir + '/gl-webgpu-clear.png' });
  }
}
