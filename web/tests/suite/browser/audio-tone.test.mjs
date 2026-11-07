import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
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

// The dsptone guest fixture (tools/guest-fixtures/dsptone.c) is fully
// deterministic: a 440 Hz square wave at +/-12000/32768 (~0.36621) in
// 16-bit samples, identical on both channels, written for exactly six
// seconds at 44100 Hz. Frequency alone cannot see wrong sample VALUES
// (the FD-108 lesson: stored values were already wrong while every
// frequency-shaped check passed), so this measures the captured values
// themselves: level (peak and RMS), wave shape (RMS/peak ratio and duty
// cycle), channel balance, active duration, and the longest silent gap
// inside the tone (a dropout).
const TONE_PEAK = 12000 / 32768;
const TONE_SECONDS = 6;
const ACTIVE_THRESHOLD = 0.05;

function toneStats(samples, sampleRate) {
  let peak = 0;
  let first = -1;
  let last = -1;
  for (let i = 0; i < samples.length; i++) {
    const magnitude = Math.abs(samples[i]);
    if (magnitude > peak) peak = magnitude;
    if (magnitude > ACTIVE_THRESHOLD) {
      if (first < 0) first = i;
      last = i;
    }
  }
  if (first < 0) {
    return { captured: samples.length, peak, first, last, activeSeconds: 0,
      rms: 0, duty: 0, longestGapMs: 0 };
  }
  let sumSquares = 0;
  let positive = 0;
  let gap = 0;
  let longestGap = 0;
  for (let i = first; i <= last; i++) {
    const sample = samples[i];
    sumSquares += sample * sample;
    if (sample > 0) positive++;
    if (Math.abs(sample) <= ACTIVE_THRESHOLD) {
      gap++;
      if (gap > longestGap) longestGap = gap;
    } else {
      gap = 0;
    }
  }
  const activeFrames = last - first + 1;
  return {
    captured: samples.length,
    peak,
    first,
    last,
    activeSeconds: activeFrames / sampleRate,
    rms: Math.sqrt(sumSquares / activeFrames),
    duty: positive / activeFrames,
    longestGapMs: longestGap * 1000 / sampleRate,
  };
}

export default class AudioToneTest extends ProbeTest {
  constructor() { super({ tag: 'audio-tone', timeoutMs: 1800000 }); }
  async run({ page }) {
    await page.addInitScript(() => {
      globalThis.__audioCapture = { contexts: [], channels: [], outputChannels: 0, worklets: 0,
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
      const RealWorkletNode = globalThis.AudioWorkletNode;
      if (RealWorkletNode) {
        globalThis.AudioWorkletNode = class extends RealWorkletNode {
          constructor(context, name, options) {
            super(context, name, options);
            globalThis.__audioCapture.worklets++;
            const channels = options.outputChannelCount?.[0] || options.channelCount || 2;
            const tap = context.__testCreateScriptProcessor(1024, channels, 1);
            const silent = context.createGain();
            silent.gain.value = 0;
            tap.onaudioprocess = event => capture(event.inputBuffer);
            this.connect(tap);
            tap.connect(silent);
            silent.connect(context.destination);
            globalThis.__audioCapture.outputChannels = channels;
          }
        };
      }
    });
    const params = new URLSearchParams({
      p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1', arch: 'x64', ...extraUrlParams(),
    });
    await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    await this.waitForPageFn(page, () => globalThis.bwRuntime?.call?.('bw64_session_ready', [], []) === 1, 600000);
    await this.waitForLog(page, /session: persistent pins spawned/, 60000);
    await page.locator('#canvas').click({ position: { x: 30, y: 30 } });
    await this.waitForPageFn(page, () => globalThis.__audioCapture.contexts.every(context => context.state === 'running')
      || globalThis.__audioCapture.contexts.length === 0, 10000);

    // Both architectures are captured (and their evidence written) before
    // any sample-value assertion runs, so one architecture's bad values
    // cannot hide the other's measurements.
    const measured = {};
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
      // The guest queues six seconds of tone and the device drains after
      // RESULT 0; wait until the capture stops growing so the measured
      // duration and completeness cover the whole tone, not a prefix.
      {
        const sampleRate = await page.evaluate(() => globalThis.__audioCapture.contexts[0]?.sampleRate || 48000);
        let lastLength = -1;
        for (let i = 0; i < 40; i++) {
          const length = await page.evaluate(() => globalThis.__audioCapture.channels[0]?.length || 0);
          if (length === lastLength && length > sampleRate * 5) break;
          lastLength = length;
          await page.waitForTimeout(1500);
        }
      }
      const result = await page.evaluate(() => ({
        sampleRate: globalThis.__audioCapture.contexts[0]?.sampleRate || 48000,
        outputChannels: globalThis.__audioCapture.outputChannels,
        worklets: globalThis.__audioCapture.worklets,
        crossOriginIsolated: globalThis.__audioCapture.crossOriginIsolated,
        channels: globalThis.__audioCapture.channels.map(samples => samples.slice()),
      }));
      const leftHz = frequency(result.channels[0], result.sampleRate);
      const rightHz = frequency(result.channels[1], result.sampleRate);
      const left = toneStats(result.channels[0], result.sampleRate);
      const right = toneStats(result.channels[1], result.sampleRate);
      const pairFrames = Math.min(left.last, right.last) - Math.max(left.first, right.first) + 1;
      let maxAbsDiff = 0;
      if (pairFrames > 0) {
        const offset = Math.max(left.first, right.first);
        for (let i = 0; i < pairFrames; i++) {
          const diff = Math.abs(result.channels[0][offset + i] - result.channels[1][offset + i]);
          if (diff > maxAbsDiff) maxAbsDiff = diff;
        }
      }
      const stats = {
        left, right,
        rmsRatioLR: right.rms > 0 ? left.rms / right.rms : 0,
        maxAbsDiffLR: maxAbsDiff,
        rmsPeakRatioLeft: left.peak > 0 ? left.rms / left.peak : 0,
        rmsPeakRatioRight: right.peak > 0 ? right.rms / right.peak : 0,
      };
      // Evidence is written before the assertions so a failing run still
      // leaves its measured values on disk for adjudication.
      await import('node:fs/promises').then(fs => fs.writeFile(`${this.dir}/audio-${arch}.json`, JSON.stringify({
        sampleRate: result.sampleRate, leftHz, rightHz, outputChannels: result.outputChannels,
        leftSamples: result.channels[0].length, rightSamples: result.channels[1].length,
        expected: { peak: TONE_PEAK, seconds: TONE_SECONDS },
        stats,
      }, null, 2)));
      check(result.outputChannels === 2, `${arch} DSP output must expose two channels, got ${result.outputChannels}`);
      check(result.crossOriginIsolated === true, 'Audio runtime must be cross-origin isolated');
      if (process.env.BW_EXPECT_AUDIO_WORKLET !== '0')
        check(result.worklets > 0, 'AudioWorkletNode must carry the shared audio ring');
      check(leftHz >= 418 && leftHz <= 462, `${arch} left channel frequency ${leftHz}Hz is outside 440Hz ±5%`);
      check(rightHz >= 418 && rightHz <= 462, `${arch} right channel frequency ${rightHz}Hz is outside 440Hz ±5%`);
      measured[arch] = { left, right, stats,
        channelCounts: [result.channels[0].length, result.channels[1].length] };
    }

    for (const arch of ['x64', 'x86']) {
      const { left, right, stats, channelCounts } = measured[arch];
      for (const [name, channel] of [['left', left], ['right', right]]) {
        check(channel.peak >= TONE_PEAK * 0.85 && channel.peak <= TONE_PEAK * 1.15,
          `${arch} ${name} channel peak ${channel.peak} is outside ±15% of the guest's ${TONE_PEAK}`);
        check(channel.rms >= TONE_PEAK * 0.8 && channel.rms <= TONE_PEAK * 1.15,
          `${arch} ${name} channel RMS ${channel.rms} does not match a square wave at the guest's level ${TONE_PEAK}`);
        check(channel.duty >= 0.46 && channel.duty <= 0.54,
          `${arch} ${name} channel duty cycle ${channel.duty} is not the guest's 50% square wave`);
        check(channel.activeSeconds >= TONE_SECONDS - 0.5 && channel.activeSeconds <= TONE_SECONDS + 0.5,
          `${arch} ${name} channel active duration ${channel.activeSeconds}s is not the guest's ${TONE_SECONDS}s of tone`);
        check(channel.longestGapMs <= 20,
          `${arch} ${name} channel has a ${channel.longestGapMs}ms silent gap inside the tone (dropout)`);
      }
      check(stats.rmsPeakRatioLeft >= 0.8 && stats.rmsPeakRatioRight >= 0.8,
        `${arch} RMS/peak ratios ${stats.rmsPeakRatioLeft}/${stats.rmsPeakRatioRight} are not a square wave's ~1.0 shape`);
      check(stats.rmsRatioLR >= 0.95 && stats.rmsRatioLR <= 1.05,
        `${arch} channel levels are unbalanced: left/right RMS ratio ${stats.rmsRatioLR}, guest writes identical channels`);
      check(stats.maxAbsDiffLR <= 0.05,
        `${arch} channels differ by up to ${stats.maxAbsDiffLR}; the guest writes identical samples to both`);
      check(Math.abs(channelCounts[0] - channelCounts[1]) <= 1024,
        `${arch} channel sample counts differ: ${channelCounts[0]} vs ${channelCounts[1]}`);
    }
  }
}
