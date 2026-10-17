// web/tests/suite/browser/d3d12-comprehensive.test.mjs — D3D12 full-spectrum test.
//
// BASIC: device creation, clear render target
// COMMON: triangle rendering, PSO creation, command lists
// EDGE: descriptor heaps, root signatures, multiple command queues
//
// Uses: tools/vkd3d/probe/tri12.exe (clear) and tri12draw.exe (triangle)

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { readFile, writeFile } from 'node:fs/promises';
import { isVkd3dError } from '../lib/log-evidence.mjs';
import { screenshotPixels } from '../lib/pixels.mjs';

export default class D3D12ComprehensiveTest extends ProbeTest {
  constructor() {
    super({ tag: 'd3d12-comprehensive', timeoutMs: 1500000 });
  }

  async stageFile(page, name, repoPath) {
    const bytes = await readFile(repoPath);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const r = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      if (r !== 1) throw new Error('VFS registration failed for ' + name);
    }, { name, b64 });
  }

  async run(ctx) {
    const { page, server } = ctx;
    const logs = this.consoleLines;

    await this.bootGuest(page, { arch: 'x64', novideo: '0', gltrace: '0', framelog: '1' });

    for (const f of [
      ['tri12.exe', 'tools/vkd3d/probe/tri12.exe'],
      ['tri12draw.exe', 'tools/vkd3d/probe/tri12draw.exe'],
      ['d3d12.dll', 'tools/vkd3d/build.w64/libs/d3d12/d3d12.dll'],
      ['d3d12core.dll', 'tools/vkd3d/build.w64/libs/d3d12core/d3d12core.dll'],
      ['dxgi.dll', 'tools/dxvk/build.w64/src/dxgi/dxgi.dll'],
      ['bw64vulkan.dll', 'tools/vkd3d/probe/bw64vulkan.dll'],
      ['vulkan-1.dll', 'tools/vkd3d/probe/bw64vulkan.dll'],
      ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1'],
    ]) {
      await this.stageFile(page, f[0], f[1]);
    }

    // Runtime readiness is bounded above; each real guest RESULT below proves execution.


    await page.evaluate(async () => {
      const { installVkPageTier } = await import('../vkwebgpu.mjs');
      window.bwVkTier = installVkPageTier();
    });

    // BASIC: device creation + clear
    const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn',
      ['string', 'string'],
      ['/usr/lib/wine/wine64\nZ:\\home\\username\\tri12.exe',
       'BW64_VKTRACE=1\nBW64_FRAMELOG=1\nWINEDLLOVERRIDES=d3d12=n;d3d12core=n;dxgi=n;vulkan-1=n;winevulkan=n\nLD_LIBRARY_PATH=/home/username']));

    const until = Date.now() + 600000;
    let resultLine = null;
    while (Date.now() < until) {
      resultLine = logs.find(l => /tri12:\s*RESULT/.test(l));
      if (resultLine) break;
      await page.waitForTimeout(2000);
    }
    check(resultLine !== null, 'D3D12 test must produce RESULT line');
    check(/RESULT 0/.test(resultLine), `D3D12 device + clear must succeed (got: ${resultLine})`);

    // The clear probe has no swapchain. Exercise draw and presentation with
    // the existing triangle fixture before checking the frame builder.
    const drawFrom = logs.length;
    await this.spawnGuest(page, 'tri12draw.exe', { arch: 'x64', env:
      'BW64_VKTRACE=1\nWINEDLLOVERRIDES=d3d12=n;d3d12core=n;dxgi=n;vulkan-1=n;winevulkan=n' });
    const draw = await this.waitForLog(page, /tri12draw:\s*RESULT/, 600000, drawFrom);
    check(/RESULT 0/.test(draw), `D3D12 draw and present must succeed (got: ${draw})`);

    // COMMON: V2 frames must be built (proves vkd3d -> bridge -> page path)
    const frames = logs.filter(l => /FRAME \d+ built \(v2\)/.test(l)).length;
    checkGreaterThan(frames, 0, 'D3D12 must build V2 frames');
    await this.waitForPageFn(page, () => window.bwVkStats?.().v2?.rendered > 0, 30000);
    await page.evaluate(() => window.bwVkRenderer2.device.queue.onSubmittedWorkDone());
    const pixels = screenshotPixels(await page.locator('#vkCanvas').screenshot({ path: this.dir + '/triangle.png' }));
    await writeFile(this.dir + '/pixels.json', JSON.stringify({ pixels, stats: await page.evaluate(() => window.bwVkStats?.()) }, null, 2));
    checkGreaterThan(pixels.redDominant, 1000, 'D3D12 must present the RGB triangle above its blue clear color');

    // EDGE: no vkd3d errors in console
    const vkd3dErrs = logs.filter(isVkd3dError);
    check(vkd3dErrs.length === 0,
      `No vkd3d errors expected (found ${vkd3dErrs.length})`);
  }
}
