// Actual guest /dev/dsp bytes -> SDL audio -> browser output PCM.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
export default class AudioToneTest extends ProbeTest {
  constructor() { super({ tag: 'audio-tone', timeoutMs: 900000 }); }
  async run({ page }) {
    await page.addInitScript(() => {
      globalThis.__audioCapture = { contexts: [], callbacks: 0, samples: 0, nonZero: 0, squared: 0, peak: 0 };
      const RealContext = globalThis.AudioContext;
      globalThis.AudioContext = class extends RealContext {
        constructor(...args) {
          super(...args); globalThis.__audioCapture.contexts.push(this);
          const create = this.createScriptProcessor.bind(this);
          this.createScriptProcessor = (...args) => {
            const node = create(...args);
            let prototype = Object.getPrototypeOf(node), descriptor;
            while (prototype && !descriptor) { descriptor = Object.getOwnPropertyDescriptor(prototype, 'onaudioprocess'); prototype = Object.getPrototypeOf(prototype); }
            if (!descriptor?.set) throw new Error('Missing native audio event setter');
            let handler;
            Object.defineProperty(node, 'onaudioprocess', { configurable: true, get: () => handler,
              set: fn => { handler = fn; descriptor.set.call(node, fn && (event => {
                fn.call(node, event);
                const capture = globalThis.__audioCapture;
                capture.callbacks++;
                const pcm = event.outputBuffer.getChannelData(0);
                for (const sample of pcm) {
                  capture.samples++; capture.squared += sample * sample;
                  capture.peak = Math.max(capture.peak, Math.abs(sample));
                  if (Math.abs(sample) > 0.001) capture.nonZero++;
                }
              })); }
            });
            return node;
          };
        }
      };
    });
    await this.bootGuest(page);
    await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
    await this.stageFile(page, 'dsptone.exe', await guestFixture('dsptone', 'x64'));
    await page.evaluate(() => document.addEventListener('click', () => { for (const c of globalThis.__audioCapture.contexts) c.resume(); }));
    const from = await this.spawnGuest(page, 'dsptone.exe');
    await this.waitForLog(page, /dsptone: WRITESTART/, 180000, from);
    await page.locator('#canvas').click({ position: { x: 30, y: 30 } });
    await this.waitForLog(page, /dsptone: RESULT [01]/, 60000, from);
    await this.waitForPageFn(page, () => globalThis.__audioCapture.nonZero > 0, 30000);
    const capture = await page.evaluate(() => ({ ...globalThis.__audioCapture, contexts: undefined }));
    checkGreaterThan(capture.callbacks, 0, 'SDL browser audio pump must execute');
    checkGreaterThan(capture.nonZero, 0, 'Captured audio must contain non-zero guest tone bytes');
    checkGreaterThan(Math.sqrt(capture.squared / capture.samples), 0.005, 'Captured guest tone must have audible RMS');
    check(this.consoleLines.some(l => /dsptone: RESULT 0/.test(l)), 'Every guest write must succeed');
    await import('node:fs/promises').then(fs => fs.writeFile(this.dir + '/audio.json', JSON.stringify(capture, null, 2)));
  }
}
