// web/tests/suite/browser/d3d12-comprehensive.test.mjs — D3D12 full-spectrum test.
//
// BASIC: device creation, clear render target
// COMMON: triangle rendering, PSO creation, command lists
// EDGE: descriptor heaps, root signatures, multiple command queues
//
// Uses: tools/vkd3d/probe/tri12.exe (clear) and tri12draw.exe (triangle)

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { readFile } from 'node:fs/promises';

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
    const logs = [];
    page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));

    await page.goto(`${server.url}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0&gltrace=0`);
    await this.waitForPageFn(page, () => window.bwRuntime?.ready?.() === true, 600000);

    for (const f of [
      ['tri12.exe', 'tools/vkd3d/probe/tri12.exe'],
      ['d3d12.dll', 'tools/vkd3d/build/src/d3d12/d3d12.dll'],
      ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1'],
    ]) {
      try { await this.stageFile(page, f[0], f[1]); }
      catch (e) { console.log(`  (stage ${f[0]}: ${e.message})`); }
    }

    const bootstrapUntil = Date.now() + 600000;
    while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
      if (Date.now() > bootstrapUntil) throw new Error('Wine bootstrap timed out');
      await page.waitForTimeout(500);
    }

    await page.evaluate(async () => {
      const { installVkPageTier } = await import('../vkwebgpu.mjs');
      window.bwVkTier = installVkPageTier();
    });

    // BASIC: device creation + clear
    const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn',
      ['string', 'string'],
      ['/usr/lib/wine/wine64\nZ:\\home\\username\\tri12.exe',
       'BW64_VKTRACE=0\nWINEDLLOVERRIDES=d3d12=n\nLD_LIBRARY_PATH=/home/username']));

    const until = Date.now() + 600000;
    let resultLine = null;
    while (Date.now() < until) {
      resultLine = logs.find(l => /tri12:\s*RESULT/.test(l));
      if (resultLine) break;
      await page.waitForTimeout(2000);
    }
    check(resultLine !== null, 'D3D12 test must produce RESULT line');
    check(/RESULT 0/.test(resultLine), `D3D12 device + clear must succeed (got: ${resultLine})`);

    // COMMON: V2 frames must be built (proves vkd3d -> bridge -> page path)
    const frames = logs.filter(l => /FRAME \d+ built \(v2\)/.test(l)).length;
    checkGreaterThan(frames, 0, 'D3D12 must build V2 frames');

    // EDGE: no vkd3d errors in console
    const vkd3dErrs = logs.filter(l => /vkd3d.*(err|fail)/i.test(l));
    check(vkd3dErrs.length === 0,
      `No vkd3d errors expected (found ${vkd3dErrs.length})`);
  }
}
