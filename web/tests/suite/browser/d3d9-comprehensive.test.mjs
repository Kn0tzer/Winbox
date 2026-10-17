// D3D9 guest workloads: device creation, clear, textured quad, vertex shader,
// and a multi-draw scene. Both variants check compositor clear pixels.
//
// Uses existing test executables in tools/d3dtests/:
//   clear9.exe — basic device + clear
//   texquad9.exe — textured quad
//   vshade9.exe — vertex shader
//   scene9.exe — complex scene

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { readFile } from 'node:fs/promises';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { screenshotPixels } from '../lib/pixels.mjs';
import { writeFile } from 'node:fs/promises';

export default class D3D9ComprehensiveTest extends ProbeTest {
  constructor({ arch = 'x64' } = {}) {
    super({ tag: arch === 'x86' ? 'd3d9-32' : 'd3d9-comprehensive', timeoutMs: 1200000 });
    this.arch = arch;
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
    const loader = this.arch === 'x86' ? '/opt/wine-stable/bin/wine' : '/usr/lib/wine/wine64';
    const before = this.consoleLines.length;
    const spawn = await page.evaluate(({exe, loader}) => window.bwRuntime.call('bw64_spawn',
      ['string', 'string'],
      [`${loader}\nZ:\\home\\username\\${exe}`, 'BW64_VKTRACE=1\nBW64_FRAMELOG=1\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username']),
      {exe: exeName, loader});
    check(spawn > 0, `${tag}: guest spawn must be queued`);
    if (tag === 'clear9') {
      await this.waitForLog(page, /clear9: CAPTURE_READY/, 300000, before);
      await page.waitForTimeout(1000);
      const png = await page.locator('#vkCanvas').screenshot();
      const pixels = screenshotPixels(png);
      await writeFile(this.dir + '/clear.png', png);
      await writeFile(this.dir + '/clear-pixels.json', JSON.stringify(pixels, null, 2));
      await writeFile(this.dir + '/clear-renderer.json', JSON.stringify(await page.evaluate(() => globalThis.bwVkStats?.()), null, 2));
      await writeFile(this.dir + '/clear-frame.json', await page.evaluate(() => JSON.stringify(globalThis.__d3d9Frame, (_, value) => {
        if (value instanceof Map) return Object.fromEntries(value);
        if (ArrayBuffer.isView(value)) return Array.from(value);
        if (value instanceof ArrayBuffer) return Array.from(new Uint8Array(value));
        return value;
      })));
      checkGreaterThan(pixels.redDominant, 100000, `${this.arch} DXVK clear must reach the browser compositor with its final red color`);
    }
    return this.waitForLog(page, resultPattern, 300000, before);
  }

  async run(ctx) {
    const { page, server } = ctx;
    const logs = this.consoleLines;
    await page.addInitScript(() => {
      let renderer;
      Object.defineProperty(globalThis, 'bwVkRenderer2', { configurable: true, get: () => renderer, set(value) {
        renderer = value;
        const render = value.render.bind(value);
        value.render = (frame, options) => { globalThis.__d3d9Frame = frame; return render(frame, options); };
      }});
    });

    await this.bootGuest(page, { arch: this.arch, novideo: '0', gltrace: '0', framelog: '1' });
    await this.waitForPageFn(page, () => window.bwRuntime?.ready?.() === true, 600000);

    // Stage D3D9 test executables
    for (const exe of ['clear9.exe', 'texquad9.exe', 'vshade9.exe', 'scene9.exe']) {
      const file = await guestFixture('d3d9-' + exe.slice(0,-4), this.arch);
      await this.stageFile(page, exe, file);
    }
    await this.stageFile(page, 'checker.bmp', 'tools/d3dtests/checker.bmp');
    // Exercise DXVK through the actual Vulkan bridge.
    const bits = this.arch === 'x86' ? '32' : '64';
    await this.stageFile(page, 'd3d9.dll', `tools/dxvk/build.w${bits}/src/d3d9/d3d9.dll`);
    await this.stageFile(page, 'libvulkan.so.1', `tools/rootfs64/libvk${bits}/libvulkan.so.1`);

    // Runtime readiness is bounded above; each real guest RESULT below proves execution.


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
