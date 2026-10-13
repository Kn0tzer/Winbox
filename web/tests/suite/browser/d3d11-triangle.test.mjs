// Real D3D11 FL9_3 staging readback: device, upload and non-zero triangle pixels.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
export default class D3D11TriangleTest extends ProbeTest {
  constructor() { super({ tag: 'd3d11-triangle', timeoutMs: 900000 }); }
  async run({ page }) {
    await this.bootGuest(page);
    await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
    for (const [name, path] of [
      ['tri11_93.exe', 'tools/d3dtests/tri11_93.exe'],
      ['d3d11.dll', 'tools/dxvk/build.w64/src/d3d11/d3d11.dll'],
      ['dxgi.dll', 'tools/dxvk/build.w64/src/dxgi/dxgi.dll'],
      ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1'],
    ]) await this.stageFile(page, name, path);
    const from = await this.spawnGuest(page, 'tri11_93.exe', { env: 'WINEDLLOVERRIDES=d3d11=n;dxgi=n\nBW64_VKTRACE=1\nBW64_FRAMELOG=1' });
    const result = await this.waitForLog(page, /tri1193: RESULT [01]/, 600000, from);
    check(/RESULT 0/.test(result), `D3D11 fixture must succeed (${result})`);
    check(this.consoleLines.some(l => /tri1193: D3D11CreateDevice OK/.test(l)), 'D3D11CreateDevice must succeed');
    checkGreaterThan(this.countConsole(/gipa HIT.*vkCmdCopyBuffer/), 0, 'vkCmdCopyBuffer must be gipa-HIT');
    const litLine = this.consoleLines.find(l => /tri1193: LIT=/.test(l));
    checkGreaterThan(Number(litLine?.match(/LIT=(\d+)/)?.[1]), 0, `D3D11 triangle must render non-zero pixels (${litLine})`);
    await page.screenshot({ path: this.dir + '/triangle.png' });
  }
}
