import {ProbeTest} from '../lib/probe-base.mjs';
import {guestFixture, canvasPixels} from '../lib/guest-fixtures.mjs';
import {check, checkEqual, checkGreaterThan} from '../lib/assert.mjs';
import {writeFile} from 'node:fs/promises';

export default class Gl32FramebufferTest extends ProbeTest {
  constructor() { super({tag: 'gl32-framebuffer', timeoutMs: 600000}); }
  async run({page}) {
    await this.bootGuest(page, {arch: 'x86'});
    await this.stageFile(page, 'gl-framebuffer.exe', await guestFixture('gl-framebuffer', 'x86'));
    await this.stageFile(page, 'libGL.so.1', 'tools/rootfs64/libgl32/libGL.so.1');
    const from = await this.spawnGuest(page, 'gl-framebuffer.exe', {arch: 'x86', env: 'WINEDEBUG=+debugstr'});
    const ready = await this.waitForLog(page, /gl-framebuffer: (?:CAPTURE_READY|RESULT 1)/, 120000, from);
    await page.screenshot({path: this.dir + '/framebuffer.png'});
    check(/CAPTURE_READY/.test(ready), `Offscreen framebuffer contract must succeed (${ready})`);
    const pixels = await canvasPixels(page);
    await writeFile(this.dir + '/pixels.json', JSON.stringify(pixels,null,2));
    checkEqual(pixels.width,320,'A small offscreen viewport must not resize the window');
    checkEqual(pixels.height,240,'The window must retain its drawable height');
    checkGreaterThan(pixels.redDominant,4000,'The offscreen red rectangle must blit into the window backbuffer');
    const green = await page.evaluate(() => {
      const source = document.querySelector('#canvas'), copy = document.createElement('canvas');
      copy.width = source.width; copy.height = source.height;
      const context = copy.getContext('2d'); context.drawImage(source,0,0);
      const bytes = context.getImageData(0,0,copy.width,copy.height).data;
      let green = 0;
      for (let i=0;i<bytes.length;i+=4) if(bytes[i+1]>200 && bytes[i]<30 && bytes[i+2]<30) green++;
      return green;
    });
    checkGreaterThan(green,60000,'The original default framebuffer must survive offscreen rendering');
    const result = await this.waitForLog(page,/gl-framebuffer: RESULT [01]/,30000,from);
    check(/RESULT 0/.test(result),'The guest framebuffer fixture must finish successfully');
  }
}
