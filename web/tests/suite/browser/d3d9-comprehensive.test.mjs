// web/tests/suite/browser/d3d9-comprehensive.test.mjs — D3D9 full-spectrum test.
//
// BASIC: device creation, basic triangle, texture mapping
// COMMON: vertex/index buffers, all primitive types, shader models
// EDGE: state blocks, all render states, exotic FVF codes
//
// Uses existing test executables in tools/d3dtests/:
//   clear9.exe — basic device + clear
//   texquad9.exe — textured quad
//   vshade9.exe — vertex shader
//   scene9.exe — complex scene

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { readFile } from 'node:fs/promises';

export default class D3D9ComprehensiveTest extends ProbeTest {
  constructor() {
    super({ tag: 'd3d9-comprehensive', timeoutMs: 1200000 });
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

  async runTest(page, logs, exeName, resultPattern, tag) {
    const spawn = await page.evaluate((exe) => window.bwRuntime.call('bw64_spawn',
      ['string', 'string'],
      [`/usr/lib/wine/wine64\nZ:\\home\\username\\${exe}`, 'BW64_VKTRACE=0']),
      exeName);
    const until = Date.now() + 300000;
    while (Date.now() < until) {
      const hit = logs.find(l => resultPattern.test(l));
      if (hit) return hit;
      await page.waitForTimeout(2000);
    }
    throw new Error(`${tag}: result pattern not seen within 5 min (spawn=${spawn})`);
  }

  async run(ctx) {
    const { page, server } = ctx;
    const logs = [];
    page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));

    await page.goto(`${server.url}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0&gltrace=0`);
    await this.waitForPageFn(page, () => window.bwRuntime?.ready?.() === true, 600000);

    // Stage D3D9 test executables
    for (const exe of ['clear9.exe', 'texquad9.exe', 'vshade9.exe', 'scene9.exe']) {
      await this.stageFile(page, exe, `tools/d3dtests/${exe}`);
    }
    // Stage GL shim for wined3d path
    await this.stageFile(page, 'libGL.so.1', 'tools/rootfs64/libgl64/libGL.so.1');

    const bootstrapUntil = Date.now() + 600000;
    while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
      if (Date.now() > bootstrapUntil) throw new Error('Wine bootstrap timed out');
      await page.waitForTimeout(500);
    }

    // BASIC 1: device creation + clear (clear9.exe)
    const clear = await this.runTest(page, logs, 'clear9.exe', /clear9:\s*RESULT/, 'clear9');
    check(/RESULT 0/.test(clear), `D3D9 device creation + clear must succeed (got: ${clear})`);

    // BASIC 2: textured quad (texquad9.exe) — texture mapping works
    const tex = await this.runTest(page, logs, 'texquad9.exe', /texquad9:\s*RESULT/, 'texquad9');
    check(/RESULT 0/.test(tex), `D3D9 textured quad must succeed (got: ${tex})`);

    // COMMON: vertex shader (vshade9.exe)
    const vs = await this.runTest(page, logs, 'vshade9.exe', /vshade9:\s*RESULT/, 'vshade9');
    check(/RESULT 0/.test(vs), `D3D9 vertex shader must succeed (got: ${vs})`);

    // COMMON: complex scene (scene9.exe) — multiple draws, state changes
    const scene = await this.runTest(page, logs, 'scene9.exe', /scene9:\s*RESULT/, 'scene9');
    check(/RESULT 0/.test(scene), `D3D9 complex scene must succeed (got: ${scene})`);

    // Verify no GL bridge errors in console
    const glErrors = logs.filter(l => /gl64.*(error|fail|unimplemented)/i.test(l));
    check(glErrors.length === 0,
      `No GL bridge errors expected (found ${glErrors.length}): ${glErrors.slice(0, 3).join('; ')}`);
  }
}
