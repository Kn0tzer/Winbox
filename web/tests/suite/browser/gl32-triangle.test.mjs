// Execute the i386 WGL fixture and measure the real browser canvas.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { guestFixture, canvasPixels } from '../lib/guest-fixtures.mjs';
export default class Gl32TriangleTest extends ProbeTest {
  constructor() { super({ tag: 'gl32-triangle', timeoutMs: 900000 }); }
  async run({ page }) {
    const fixture = await guestFixture('trigl', 'x86');
    await this.bootGuest(page, { arch: 'x86' });
    await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
    await this.stageFile(page, 'trigl32.exe', fixture);
    await this.stageFile(page, 'libGL.so.1', 'tools/rootfs64/libgl32/libGL.so.1');
    const from = await this.spawnGuest(page, 'trigl32.exe', { arch: 'x86', env: 'WINEDEBUG=+debugstr' });
    const line = await this.waitForLog(page, /trigl: RESULT [01]/, 300000, from);
    check(/RESULT 0/.test(line), `32-bit GL fixture must succeed (${line})`);
    const pixels = await canvasPixels(page);
    checkGreaterThan(pixels.lit, 0, `32-bit OpenGL triangle must render non-zero pixels (LIT=${pixels.lit})`);
    checkGreaterThan(pixels.varied, 100, 'GL triangle must contain colored pixels, not only window chrome');
    await page.screenshot({ path: this.dir + '/triangle.png' });
  }
}
