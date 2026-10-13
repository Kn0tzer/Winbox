// web/tests/suite/browser/d3d10-comprehensive.test.mjs — D3D10 full-spectrum test.
//
// BASIC: device creation at FL 10_0, offscreen triangle
// COMMON: input layouts, constant buffers, sampler states
// EDGE: swapchain presentation (was blocked by naga, fixed by R5/R6)
//
// Uses: d3d10dev.exe (device), tri10.exe (triangle), scene10.exe (scene),
//       tri10swap.exe / tri10swap93.exe (swapchain)

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { readFile } from 'node:fs/promises';

export default class D3D10ComprehensiveTest extends ProbeTest {
  constructor() {
    super({ tag: 'd3d10-comprehensive', timeoutMs: 1200000 });
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

  async runTest(page, logs, exeName, resultPattern, tag, env = '') {
    const spawn = await page.evaluate(([exe, e]) => window.bwRuntime.call('bw64_spawn',
      ['string', 'string'],
      [`/usr/lib/wine/wine64\nZ:\\home\\username\\${exe}`,
       `BW64_VKTRACE=1\nBW64_FRAMELOG=1\nWINEDLLOVERRIDES=d3d10core=n;d3d11=n;dxgi=n\nLD_LIBRARY_PATH=/home/username\n${e}`]),
      [exeName, env]);
    check(spawn > 0, `${tag}: guest spawn must be queued`);
    return this.waitForLog(page, resultPattern, 300000);
  }

  async run(ctx) {
    const { page, server } = ctx;
    const logs = this.consoleLines;

    await page.goto(`${server.url}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0&gltrace=0&framelog=1`);
    await this.waitForPageFn(page, () => window.bwRuntime?.ready?.() === true, 600000);

    for (const f of [
      ['d3d10dev.exe', 'tools/d3dtests/d3d10dev.exe'],
      ['tri10.exe', 'tools/d3dtests/tri10.exe'],
      ['scene10.exe', 'tools/d3dtests/scene10.exe'],
      ['tri10swap93.exe', 'tools/d3dtests/tri10swap93.exe'],
      ['d3d10core.dll', 'tools/dxvk/build.w64/src/d3d10/d3d10core.dll'],
      ['d3d11.dll', 'tools/dxvk/build.w64/src/d3d11/d3d11.dll'],
      ['dxgi.dll', 'tools/dxvk/build.w64/src/dxgi/dxgi.dll'],
      ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1'],
    ]) await this.stageFile(page, f[0], f[1]);

    // Runtime readiness is bounded above; each real guest RESULT below proves execution.


    // BASIC 1: D3D10 device creation
    const dev = await this.runTest(page, logs, 'd3d10dev.exe', /d3d10dev:\s*RESULT/, 'd3d10dev');
    check(/RESULT 0/.test(dev), `D3D10CreateDevice must succeed (got: ${dev})`);

    // BASIC 2: offscreen triangle
    const tri = await this.runTest(page, logs, 'tri10.exe', /tri10:\s*RESULT/, 'tri10');
    check(/RESULT 0/.test(tri), `D3D10 triangle must succeed (got: ${tri})`);

    // COMMON: complex scene
    const scene = await this.runTest(page, logs, 'scene10.exe', /scene10:\s*RESULT/, 'scene10');
    check(/RESULT 0/.test(scene), `D3D10 scene must succeed (got: ${scene})`);

    // EDGE: swapchain presentation (naga R5/R6 regression — must be re-verified)
    const swap = await this.runTest(page, logs, 'tri10swap93.exe', /tri10swap.*RESULT/, 'tri10swap93');
    check(/RESULT 0/.test(swap),
      `D3D10 swapchain must present (got: ${swap}). ` +
      `If FAIL, the spirvfix R5/R6 present-shader fix regressed.`);

    // Verify zero naga translation errors
    const nagaErrs = logs.filter(l => /naga.*(error|cannot translate|Unsupported)/i.test(l));
    check(nagaErrs.length === 0,
      `Zero naga translation errors expected (found ${nagaErrs.length})`);
  }
}
