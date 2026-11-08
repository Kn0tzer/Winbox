import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// Before/after bench for the scalar SSE move + packed FP logic fast
// paths (J64_SSE_MOVSCALAR / J64_SSE_LOGIC128). Modeled on
// incprobe-bench.test.mjs: waits for the Wine prefix initialization
// (wineboot --init) to EXIT before spawning the fixture, because
// spawning into a session whose prefix init is still running wedges
// the fixture's wineserver handshake at a high rate. Each mode boots a
// fresh session; per-mode cpu64 stats (blocksEntered, fallbacks) are
// recorded alongside the samples so the fast-path coverage change is
// visible in the same run as the timing. The fixture prints a battery
// checksum plus per-sample checksums for two timing loops (scalar
// moves, packed logic); every checksum must agree across samples and
// engine modes.
export default class SseprobeBenchTest extends ProbeTest {
  constructor() { super({ tag: process.env.TAG || 'sseprobe-bench', timeoutMs: 3600000 }); this.guestArchitectures = ['x64']; }
  async bootGuest(page, parameters = {}) {
    this.testedArchitectures.add(parameters.arch || 'x64');
    const params = new URLSearchParams({ p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1', ...parameters, ...extraUrlParams() });
    await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    await page.waitForFunction(() => {
      const module = globalThis.Module;
      return typeof module?._bw64_session_ready === 'function' && module._bw64_session_ready() === 1;
    }, null, { timeout: 600000 });
    await page.evaluate(() => {
      const module = globalThis.Module;
      const runtime = globalThis.bwRuntime;
      if (!runtime || typeof module?._bw64_session_ready !== 'function' || typeof module?.ccall !== 'function') {
        throw new Error('CPU64 session exports are unavailable');
      }
      runtime.ready = () => globalThis.Module._bw64_session_ready() === 1;
      runtime.call = (name, argTypes, args) => globalThis.Module.ccall(name, 'number', argTypes, args);
    });
  }
  async run({ page }) {
    const modes = (process.env.BW_SSEPROBE_MODES || '0,1,1,0').split(',');
    if (modes.some(mode => !['0', '1'].includes(mode))) throw Error('Invalid BW_SSEPROBE_MODES');
    const results = [];
    let first = true;
    for (const jit of modes) {
      if (!first) { await page.close(); page = await this.newPage(); }
      first = false;
      const bootFrom = this.consoleLines.length;
      await this.bootGuest(page, { arch: 'x64', jit, jitwarm: '0', winedbg: '+debugstr' });
      await this.waitForLog(page, /session: persistent pins spawned; Wine owns service initialization/, 120000, bootFrom);
      await this.waitForLog(page, /CPU64: exit_group syscall, status=0 .*wineboot\.exe --init/, 120000, bootFrom);
      await this.stageFile(page, 'sseprobe64.exe', await guestFixture('sseprobe64-pe', 'x64'));
      const from = await this.spawnGuest(page, 'sseprobe64.exe', { arch: 'x64', env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /BW_SSEPROBE64 sample=3 loop=logic /, 1500000, from);
      const lines = this.consoleLines.slice(from);
      const batteryMatches = lines.flatMap(line => {
        const match = line.match(/BW_SSEPROBE64 battery checksum=([a-f0-9]+)/);
        return match ? [match[1]] : [];
      });
      assert.ok(batteryMatches.length >= 1, 'battery checksum reported');
      const batteryChecksum = batteryMatches[0];
      const reported = lines.flatMap(line => {
        const match = line.match(/BW_SSEPROBE64 sample=(\d+) loop=(scalar|logic) ms=([\d.]+) checksum=([a-f0-9]+) passes=(\d+)/);
        return match ? [{ sample: Number(match[1]), loop: match[2], ms: Number(match[3]), checksum: match[4], passes: Number(match[5]) }] : [];
      });
      const loops = {};
      for (const loop of ['scalar', 'logic']) {
        const unique = new Map();
        for (const sample of reported.filter(s => s.loop === loop)) {
          if (unique.has(sample.sample)) assert.deepEqual(unique.get(sample.sample), sample);
          unique.set(sample.sample, sample);
        }
        const samples = [...unique.values()].sort((a, b) => a.sample - b.sample);
        assert.equal(samples.length, 4, `${loop}: cold and three warm samples completed`);
        const checksums = new Set(samples.map(s => s.checksum));
        assert.equal(checksums.size, 1, `${loop}: checksum identical across all samples in a mode`);
        const warmSamples = samples.filter(s => s.sample >= 1 && s.sample <= 3).map(s => s.ms).sort((a, b) => a - b);
        loops[loop] = { warmMedianMs: warmSamples[1], samples, checksum: samples[0].checksum };
      }
      // Stats from the runtime's own METRICS console lines, as in
      // incprobe-bench (the jit-stats JSON truncates on a full Wine
      // boot's reject census).
      const metricsLines = lines.filter(l => l.includes('bw64: METRICS '));
      assert.ok(metricsLines.length > 0, 'a METRICS line was captured for this boot');
      const metrics = JSON.parse(metricsLines[metricsLines.length - 1].split('bw64: METRICS ')[1]);
      const cpu64 = metrics.jit.cpu64;
      const engineUnavailable = cpu64.available === false || cpu64.available === 0 || cpu64.engine === 'unavailable';
      let engineUnavailableReason;
      if (engineUnavailable) {
        engineUnavailableReason = cpu64.reason || `engine '${cpu64.engine}' unavailable in this build`;
        assert.equal(cpu64.blocksEntered ?? 0, 0, 'unavailable engine reports no generated code entered');
      } else {
        assert.equal(Boolean(cpu64.enabled), jit === '1');
      }
      results.push({ jit, batteryChecksum, loops, cpu64, ...(engineUnavailableReason ? { engineUnavailable: engineUnavailableReason } : {}) });
    }
    const allBattery = new Set(results.map(r => r.batteryChecksum));
    assert.equal(allBattery.size, 1, 'battery checksum identical across engine modes (fast path must not change results)');
    for (const loop of ['scalar', 'logic']) {
      const allLoop = new Set(results.map(r => r.loops[loop].checksum));
      assert.equal(allLoop.size, 1, `${loop}: checksum identical across engine modes`);
    }
    const output = { build: this.buildInfo, results };
    await writeFile(join(this.dir, 'sseprobe-bench.json'), JSON.stringify(output, null, 2));
    if (process.env.BW_SSEPROBE_OUT) {
      const out = process.env.BW_SSEPROBE_OUT;
      await mkdir(dirname(out), { recursive: true });
      await writeFile(out, JSON.stringify(output, null, 2));
    }
  }
}
