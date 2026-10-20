import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// Genuine PE64 under Wine64: the 32-bit Chromium benchmark cannot cover this.
export default class Jit64Test extends ProbeTest {
  constructor() { super({ tag: process.env.TAG || 'jit64', timeoutMs: 600000 }); this.guestArchitectures = ['x64']; }
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
    const results = [];
    const modes = (process.env.BW_TEST_JIT64_MODES || '0,1').split(',');
    if (modes.some(mode => !['0', '1'].includes(mode))) throw Error('Invalid BW_TEST_JIT64_MODES');
    for (const jit of modes) {
      if (jit === '1') { await page.close(); page = await this.newPage(); }
      const bootFrom = this.consoleLines.length;
      await this.bootGuest(page, { arch: 'x64', jit, winedbg: '+debugstr' });
      await this.waitForLog(page, /session: persistent pins spawned; Wine owns service initialization/, 120000, bootFrom);
      await this.waitForLog(page, /CPU64: exit_group syscall, status=0 .*wineboot\.exe--init/, 120000, bootFrom);
      await this.stageFile(page, 'jitbench64.exe', await guestFixture('jitbench64-pe', 'x64'));
      const from = await this.spawnGuest(page, 'jitbench64.exe', { arch: 'x64', env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /BW_JITBENCH64 sample=3/, 120000, from);
      const reported = this.consoleLines.slice(from).flatMap(line => {
        const match = line.match(/BW_JITBENCH64 sample=(\d+) ms=([\d.]+) checksum=([a-f0-9]+) passes=(\d+)/);
        return match ? [{ sample: Number(match[1]), ms: Number(match[2]), checksum: match[3], passes: Number(match[4]) }] : [];
      });
      // syscall64 tees fd=2 chunks and completed lines; debugstr can appear twice.
      const unique = new Map();
      for (const sample of reported) {
        if (unique.has(sample.sample)) assert.deepEqual(unique.get(sample.sample), sample);
        unique.set(sample.sample, sample);
      }
      const samples = [...unique.values()].sort((a, b) => a.sample - b.sample);
      assert.equal(samples.length, 4, 'cold and three warm samples completed');
      for (const sample of samples) {
        assert.equal(sample.checksum, '8b527b4986bdaccf', 'PE64 output matches independent ELF64 reference');
        assert.ok(sample.ms > 0, 'QPC reports elapsed guest workload time');
      }
      const stats = await page.evaluate(() => JSON.parse(Module.ccall('bw64_jit_stats_json_text', 'string', [], [])));
      assert.equal(Boolean(stats.cpu64.enabled), jit === '1');
      if (jit === '1') {
        assert.ok(stats.cpu64.blocksEntered > 0 && stats.cpu64.insnsEmitted > 0, 'generated code actually executed');
        assert.equal(stats.cpu64.instantiateFailed, 0);
      }
      const warmSamples = samples.filter(sample => sample.sample >= 1 && sample.sample <= 3).map(sample => sample.ms);
      assert.equal(warmSamples.length, 3, 'samples 1-3 are the three warm runs');
      const warmMs = warmSamples.sort((a, b) => a - b)[1];
      results.push({ jit, warmMs, samples, stats });
      const resultFor = mode => results.find(result => result.jit === mode);
      const jit0 = resultFor('0');
      const jit1 = resultFor('1');
      const output = {
        build: this.buildInfo,
        jit0: jit0 ? { warmMs: jit0.warmMs, cpu64: jit0.stats.cpu64 } : null,
        jit1: jit1 ? { warmMs: jit1.warmMs, cpu64: jit1.stats.cpu64 } : null,
        speedup: jit0 && jit1 ? jit0.warmMs / jit1.warmMs : null,
        results,
      };
      await writeFile(join(this.dir, 'benchmark.json'), JSON.stringify(output, null, 2));
      if (process.env.BW_TEST_JIT64_OUT) {
        const out = process.env.BW_TEST_JIT64_OUT;
        await mkdir(dirname(out), { recursive: true });
        await writeFile(out, JSON.stringify(output, null, 2));
      }
    }
  }
}
