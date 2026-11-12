// Shared Wine dependencies on the Wine 8 x64 stack: embedded HTML through Wine
// Gecko and native audio through OpenAL Soft. Neither payload is staged with
// the fixture; the runtime's shared archive must provide it. The Wine 9 x86
// console path is excluded until its Gecko/OpenAL message-loop block is fixed.
import { loadavg } from 'node:os';
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
export default class CommonDependenciesTest extends ProbeTest {
  // Total budget: boot (up to 600 s in the deploy band) + MSHTML wait (up to
  // 900 s, load-scaled) + OpenAL wait (up to 600 s, load-scaled) + audio
  // 30 s + staging overhead. 2400 s covers the worst case with margin; the
  // runner kills the test after timeoutMs + 60 s.
  constructor() { super({ tag: 'common-dependencies', timeoutMs: 2400000 }); }
  async run({ page }) {
    await page.addInitScript(() => {
      globalThis.__audioCapture = { contexts: [], callbacks: 0, samples: 0, nonZero: 0, peak: 0, worklets: 0 };
      const countBuffer = buffer => {
        const capture = globalThis.__audioCapture;
        capture.callbacks++;
        for (const sample of buffer.getChannelData(0)) {
          capture.samples++; capture.peak = Math.max(capture.peak, Math.abs(sample));
          if (Math.abs(sample) > 0.001) capture.nonZero++;
        }
      };
      const RealContext = globalThis.AudioContext;
      globalThis.AudioContext = class extends RealContext {
        constructor(...args) {
          super(...args); globalThis.__audioCapture.contexts.push(this);
          const create = this.createScriptProcessor.bind(this);
          // The bwPump bridge carries guest audio on an AudioWorkletNode
          // (kdspaudio.cpp), not on a ScriptProcessorNode; the un-wrapped
          // creator is kept so the worklet tap below can observe that path
          // (the same pattern as audio-tone.test.mjs).
          this.__bwTestCreateScriptProcessor = create;
          this.createScriptProcessor = (...args) => {
            const node = create(...args);
            let prototype = Object.getPrototypeOf(node), descriptor;
            while (prototype && !descriptor) { descriptor = Object.getOwnPropertyDescriptor(prototype, 'onaudioprocess'); prototype = Object.getPrototypeOf(prototype); }
            if (!descriptor?.set) throw new Error('Missing native audio event setter');
            let handler;
            Object.defineProperty(node, 'onaudioprocess', { configurable: true, get: () => handler,
              set: fn => { handler = fn; descriptor.set.call(node, fn && (event => {
                fn.call(node, event);
                countBuffer(event.outputBuffer);
              })); }
            });
            return node;
          };
        }
      };
      // Guest audio reaches the page destination through the bridge's
      // AudioWorkletNode (see above). Tap that node's output so the capture
      // counts the samples a listener would actually hear; without this the
      // capture only ever sees SDL's bypassed ScriptProcessor node, whose
      // output is silence by construction (FD-87).
      const RealWorkletNode = globalThis.AudioWorkletNode;
      if (RealWorkletNode) {
        globalThis.AudioWorkletNode = class extends RealWorkletNode {
          constructor(context, name, options) {
            super(context, name, options);
            globalThis.__audioCapture.worklets++;
            const channels = options.outputChannelCount?.[0] || options.channelCount || 2;
            const tap = context.__bwTestCreateScriptProcessor(1024, channels, 1);
            const silent = context.createGain();
            silent.gain.value = 0;
            tap.onaudioprocess = event => countBuffer(event.inputBuffer);
            this.connect(tap);
            tap.connect(silent);
            silent.connect(context.destination);
          }
        };
      }
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
    // FD-275: the Gecko startup stage grows with box load (~130 s at load ~9,
    // 230 s at load 12-14, over 360 s at load 9-16 while the guest keeps
    // progressing). A fixed wait keeps breaking as load rises (FD-273 raised
    // 180 s to 360 s and it broke again within a day), so the wait is scaled
    // with the 1-minute load at spawn: 60 s per load unit, floored at the
    // FD-273 360 s and capped at 900 s so the test budget still fits.
    // The chosen wait and load are logged so each run records its calibration.
    const [load1] = loadavg();
    const mshtmlWaitMs = Math.min(900000, Math.max(360000, Math.ceil(load1) * 60000));
    console.log(`common-dependencies: 1-min load ${load1.toFixed(2)}, MSHTML wait ${mshtmlWaitMs / 1000}s`);
    const mshtmlStarted = Date.now();
    await this.waitForLog(target, /MSHTML: RESULT 0/, mshtmlWaitMs, from);
    console.log(`common-dependencies: MSHTML stage took ${((Date.now() - mshtmlStarted) / 1000).toFixed(1)}s`);
    check(this.consoleLines.slice(from).some(line => /MSHTML: TITLE boxedwine gecko/.test(line)), `${arch} Gecko parsed the document title`);
    check(this.consoleLines.slice(from).some(line => /MSHTML: TEXT embedded html works/.test(line)), `${arch} Gecko exposed parsed body text`);
    await this.stageFile(target, 'openal-tone.exe', await guestFixture('openal-tone', arch));
    from = await this.spawnGuest(target, 'openal-tone.exe', { arch });
    // FD-275 (proof run2): the OpenAL startup stage also grows with box load
    // (~47 s at load 12-14, over 180 s at load ~21 while the guest keeps
    // progressing). Same load scaling as the MSHTML wait above, with a
    // smaller cap — the stage is lighter than Gecko startup.
    const [load1openal] = loadavg();
    const openalWaitMs = Math.min(600000, Math.max(180000, Math.ceil(load1openal) * 60000));
    console.log(`common-dependencies: 1-min load ${load1openal.toFixed(2)}, OpenAL wait ${openalWaitMs / 1000}s`);
    const openalStarted = Date.now();
    await this.waitForLog(target, /OPENAL: RESULT 0/, openalWaitMs, from);
    console.log(`common-dependencies: OpenAL stage took ${((Date.now() - openalStarted) / 1000).toFixed(1)}s`);
    const vendor = this.consoleLines.slice(from).find(line => /OPENAL: VENDOR /.test(line));
    check(Boolean(vendor), `${arch} OpenAL exposed a vendor string`);
    await this.waitForPageFn(target, () => globalThis.__audioCapture.nonZero > 0, 30000);
    const capture = await target.evaluate(() => ({ ...globalThis.__audioCapture, contexts: undefined }));
    checkGreaterThan(capture.callbacks, 0, 'browser audio pump executed for OpenAL output');
    checkGreaterThan(capture.nonZero, 0, 'OpenAL tone reached browser audio capture');
  }
}
