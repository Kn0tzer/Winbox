import { ProbeTest } from '../lib/probe-base.mjs';
import { check } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

function frequency(samples, sampleRate) {
  let activeStart = -1;
  let lastSign = 0;
  let quietFrames = 0;
  let activeFrames = 0;
  let crossings = 0;
  for (let i = 0; i < samples.length; i++) {
    const sample = samples[i];
    if (Math.abs(sample) > 0.0005) {
      const sign = sample < 0 ? -1 : 1;
      if (activeStart < 0 || quietFrames > 8) {
        if (activeStart >= 0) activeFrames += i - quietFrames - activeStart;
        activeStart = i;
        lastSign = sign;
      } else if (sign !== lastSign) {
        crossings++;
        lastSign = sign;
      }
      quietFrames = 0;
    } else if (activeStart >= 0) {
      quietFrames++;
      if (quietFrames > 8) {
        activeFrames += i - quietFrames - activeStart;
        activeStart = -1;
        lastSign = 0;
        quietFrames = 0;
      }
    }
  }
  if (activeStart >= 0) activeFrames += samples.length - quietFrames - activeStart;
  return activeFrames ? crossings * sampleRate / (2 * activeFrames) : 0;
}

export default class AudioToneTest extends ProbeTest {
  constructor() { super({ tag: 'audio-tone', timeoutMs: 1800000 }); }
  async run({ page }) {
    await page.addInitScript(() => {
      globalThis.__audioCapture = { contexts: [], channels: [], outputChannels: 0,
        crossOriginIsolated: globalThis.crossOriginIsolated };
      const capture = buffer => {
        const count = buffer.numberOfChannels;
        globalThis.__audioCapture.outputChannels = count;
        while (globalThis.__audioCapture.channels.length < count) globalThis.__audioCapture.channels.push([]);
        for (let channel = 0; channel < count; channel++)
          globalThis.__audioCapture.channels[channel].push(...buffer.getChannelData(channel));
      };
      const RealContext = globalThis.AudioContext;
      globalThis.AudioContext = class extends RealContext {
        constructor(...args) {
          super(...args);
          globalThis.__audioCapture.contexts.push(this);
          const create = this.createScriptProcessor.bind(this);
          this.__testCreateScriptProcessor = create;
          this.createScriptProcessor = (...args) => {
            const node = create(...args);
            const channels = args[2] || 1;
            let proto = Object.getPrototypeOf(node);
            let descriptor;
            while (proto && !descriptor) {
              descriptor = Object.getOwnPropertyDescriptor(proto, 'onaudioprocess');
              proto = Object.getPrototypeOf(proto);
            }
            let handler;
            Object.defineProperty(node, 'onaudioprocess', { configurable: true,
              get: () => handler,
              set: fn => {
                handler = fn;
                descriptor.set.call(node, fn && (event => { fn.call(node, event); capture(event.outputBuffer); }));
              },
            });
            globalThis.__audioCapture.outputChannels = channels;
            return node;
          };
        }
      };

    });
    await this.bootGuest(page, { arch: 'x64' });
    await page.locator('#canvas').click({ position: { x: 30, y: 30 } });
    await this.waitForPageFn(page, () => globalThis.__audioCapture.contexts.every(context => context.state === 'running')
      || globalThis.__audioCapture.contexts.length === 0, 10000);

    for (const arch of ['x64', 'x86']) {
      const exe = `dsptone-${arch}.elf`;
      await this.stageFile(page, exe, await guestFixture('dsptone-stereo', arch));
      await page.evaluate(() => {
        globalThis.__audioCapture.channels = [];
        globalThis.__audioCapture.outputChannels = 0;
      });
      this.testedArchitectures.add(arch);
      const from = this.consoleLines.length;
      const pid = await page.evaluate(exe => globalThis.bwRuntime.call('bw64_spawn', ['string', 'string'],
        [`/home/username/${exe}`, ''] ), exe);
      if (pid <= 0) throw new Error(`${arch} DSP tone guest spawn failed: ${pid}`);
      await this.waitForLog(page, /dsptone: WRITESTART rate=\d+ channels=2/, 30000, from);
      await this.waitForLog(page, /dsptone: RESULT 0/, 30000, from);
      await this.waitForPageFn(page, () => globalThis.__audioCapture.channels.length >= 2
        && globalThis.__audioCapture.channels[0].length > (globalThis.__audioCapture.contexts[0]?.sampleRate || 48000) * 2, 15000);
      const result = await page.evaluate(() => ({
        sampleRate: globalThis.__audioCapture.contexts[0]?.sampleRate || 48000,
        outputChannels: globalThis.__audioCapture.outputChannels,
        crossOriginIsolated: globalThis.__audioCapture.crossOriginIsolated,
        channels: globalThis.__audioCapture.channels.map(samples => samples.slice()),
      }));
      const leftHz = frequency(result.channels[0], result.sampleRate);
      const rightHz = frequency(result.channels[1], result.sampleRate);
      check(result.outputChannels === 2, `${arch} DSP output must expose two channels, got ${result.outputChannels}`);
      check(result.crossOriginIsolated === true, 'Audio runtime must be cross-origin isolated');
      check(leftHz >= 418 && leftHz <= 462, `${arch} left channel frequency ${leftHz}Hz is outside 440Hz ±5%`);
      check(rightHz >= 418 && rightHz <= 462, `${arch} right channel frequency ${rightHz}Hz is outside 440Hz ±5%`);
      await import('node:fs/promises').then(fs => fs.writeFile(`${this.dir}/audio-${arch}.json`, JSON.stringify({
        sampleRate: result.sampleRate, leftHz, rightHz, outputChannels: result.outputChannels,
        leftSamples: result.channels[0].length, rightSamples: result.channels[1].length,
      }, null, 2)));
    }
  }
}
