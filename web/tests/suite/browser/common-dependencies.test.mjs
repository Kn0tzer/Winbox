// Shared Wine dependencies on the Wine 8 x64 stack: embedded HTML through Wine
// Gecko and native audio through OpenAL Soft. Neither payload is staged with
// the fixture; the runtime's shared archive must provide it. The Wine 9 x86
// console path is excluded until its Gecko/OpenAL message-loop block is fixed.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
export default class CommonDependenciesTest extends ProbeTest {
  constructor() { super({ tag: 'common-dependencies', timeoutMs: 1200000 }); }
  async run({ page }) {
    await page.addInitScript(() => {
      globalThis.__audioCapture = { contexts: [], callbacks: 0, samples: 0, nonZero: 0, peak: 0 };
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
                for (const sample of event.outputBuffer.getChannelData(0)) {
                  capture.samples++; capture.peak = Math.max(capture.peak, Math.abs(sample));
                  if (Math.abs(sample) > 0.001) capture.nonZero++;
                }
              })); }
            });
            return node;
          };
        }
      };
    });
    // The Wine 8 x64 stack is the one Chromium uses. The Wine 9 x86 stack
    // stages the same x86 payloads, but this console harness blocks inside
    // mshtml's CoCreateInstance while Gecko starts, and OpenAL x86 stops in
    // device enumeration; both need a GUI message loop. Those x86 payloads are
    // pinned and staged (see dependencies.json), while end-to-end guest proof
    // runs on x64 until the Wine 9 console path is fixed.
    const arch = 'x64';
    const target = page;
    await this.bootGuest(target, {});
    await target.locator('#canvas').click({ position: { x: 24, y: 24 } });
    await this.stageFile(target, 'mshtml-document.exe', await guestFixture('mshtml-document', arch));
    let from = await this.spawnGuest(target, 'mshtml-document.exe', { arch });
    await this.waitForLog(target, /MSHTML: RESULT 0/, 180000, from);
    check(this.consoleLines.slice(from).some(line => /MSHTML: TITLE boxedwine gecko/.test(line)), `${arch} Gecko parsed the document title`);
    check(this.consoleLines.slice(from).some(line => /MSHTML: TEXT embedded html works/.test(line)), `${arch} Gecko exposed parsed body text`);
    await this.stageFile(target, 'openal-tone.exe', await guestFixture('openal-tone', arch));
    from = await this.spawnGuest(target, 'openal-tone.exe', { arch });
    await this.waitForLog(target, /OPENAL: RESULT 0/, 180000, from);
    const vendor = this.consoleLines.slice(from).find(line => /OPENAL: VENDOR /.test(line));
    check(Boolean(vendor), `${arch} OpenAL exposed a vendor string`);
    await this.waitForPageFn(target, () => globalThis.__audioCapture.nonZero > 0, 30000);
    const capture = await target.evaluate(() => ({ ...globalThis.__audioCapture, contexts: undefined }));
    checkGreaterThan(capture.callbacks, 0, 'browser audio pump executed for OpenAL output');
    checkGreaterThan(capture.nonZero, 0, 'OpenAL tone reached browser audio capture');
  }
}
