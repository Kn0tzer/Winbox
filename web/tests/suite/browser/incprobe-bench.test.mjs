import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// Before/after bench for the FF/FE INC/DEC fast path (J64_INCDEC).
// Modeled on jit64.test.mjs: unlike jitbench.test.mjs it waits for the
// Wine prefix initialization (wineboot --init) to EXIT before spawning
// the fixture. Spawning into a session whose prefix init is still
// running wedges the fixture's wineserver handshake at a high rate
// (observed repeatedly 2026-11-05: the fixture's wine64 blocks in a
// socket read with empty queues and never reaches main). Each mode
// boots a fresh session; per-mode cpu64 stats (blocksEntered,
// fallbacks, rejectTop) are recorded alongside the samples so the
// fast-path coverage change is visible in the same run as the timing.
export default class IncprobeBenchTest extends ProbeTest {
  constructor() { super({ tag: process.env.TAG || 'incprobe-bench', timeoutMs: 3600000 }); this.guestArchitectures = ['x64']; }
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
    const modes = (process.env.BW_INCPROBE_MODES || '0,1,1,0').split(',');
    if (modes.some(mode => !['0', '1'].includes(mode))) throw Error('Invalid BW_INCPROBE_MODES');
    const results = [];
    let first = true;
    for (const jit of modes) {
      if (!first) { await page.close(); page = await this.newPage(); }
      first = false;
      const bootFrom = this.consoleLines.length;
      await this.bootGuest(page, { arch: 'x64', jit, jitwarm: '0', winedbg: '+debugstr' });
      await this.waitForLog(page, /session: persistent pins spawned; Wine owns service initialization/, 120000, bootFrom);
      await this.waitForLog(page, /CPU64: exit_group syscall, status=0 .*wineboot\.exe --init/, 120000, bootFrom);
      await this.stageFile(page, 'incprobe64.exe', await guestFixture('incprobe64-pe', 'x64'));
      const from = await this.spawnGuest(page, 'incprobe64.exe', { arch: 'x64', env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /BW_INCPROBE64 sample=3 /, 1500000, from);
      const reported = this.consoleLines.slice(from).flatMap(line => {
        const match = line.match(/BW_INCPROBE64 sample=(\d+) ms=([\d.]+) checksum=([a-f0-9]+) passes=(\d+)/);
        return match ? [{ sample: Number(match[1]), ms: Number(match[2]), checksum: match[3], passes: Number(match[4]) }] : [];
      });
      const unique = new Map();
      for (const sample of reported) {
        if (unique.has(sample.sample)) assert.deepEqual(unique.get(sample.sample), sample);
        unique.set(sample.sample, sample);
      }
      const samples = [...unique.values()].sort((a, b) => a.sample - b.sample);
      assert.equal(samples.length, 4, 'cold and three warm samples completed');
      const checksums = new Set(samples.map(s => s.checksum));
      assert.equal(checksums.size, 1, 'checksum identical across all samples in a mode');
      // Stats come from the runtime's own METRICS console lines (the
      // metrics panel's periodic dump): bw64_jit_stats_json_text()
      // renders into a 3072-byte scratch buffer that a full Wine
      // boot's reject census overflows (truncated, unparseable), and
      // this build does not export _malloc for the buffer variant.
      // The METRICS line carries the same cpu64 counters.
      const metricsLines = this.consoleLines.slice(from).filter(l => l.includes('bw64: METRICS '));
      assert.ok(metricsLines.length > 0, 'a METRICS line was captured for this boot');
      const metrics = JSON.parse(metricsLines[metricsLines.length - 1].split('bw64: METRICS ')[1]);
      const stats = { cpu64: metrics.jit.cpu64 };
      const cpu64 = stats.cpu64;
      // The METRICS line reports availability as a boolean; the
      // jit-stats JSON used by jitbench reports it as 0/1 — accept
      // both shapes, plus the explicit 'unavailable' engine name.
      const engineUnavailable = cpu64.available === false || cpu64.available === 0 || cpu64.engine === 'unavailable';
      let engineUnavailableReason;
      if (engineUnavailable) {
        // This build does not run the 64-bit JIT engine (the standard
        // build has none compiled in: supported yet available:false,
        // engine 'off'/'interp'). No mode can make it generate code
        // here; what this build must guarantee instead is that no
        // generated code is reported entered, while the checksum
        // stability asserted above covers the interpreter path this
        // build does serve. On a build whose metrics report the engine
        // available, the full latch assertion in the else branch
        // applies — same handling as jitbench.test.mjs.
        engineUnavailableReason = cpu64.reason || `engine '${cpu64.engine}' unavailable in this build`;
        assert.equal(cpu64.blocksEntered ?? 0, 0, 'unavailable engine reports no generated code entered');
      } else {
        assert.equal(Boolean(cpu64.enabled), jit === '1');
      }
      const warmSamples = samples.filter(s => s.sample >= 1 && s.sample <= 3).map(s => s.ms).sort((a, b) => a - b);
      results.push({ jit, warmMedianMs: warmSamples[1], samples, checksum: samples[0].checksum, cpu64, ...(engineUnavailableReason ? { engineUnavailable: engineUnavailableReason } : {}) });
    }
    const allChecksums = new Set(results.map(r => r.checksum));
    assert.equal(allChecksums.size, 1, 'checksum identical across engine modes (fast path must not change results)');
    const output = { build: this.buildInfo, results };
    await writeFile(join(this.dir, 'incprobe-bench.json'), JSON.stringify(output, null, 2));
    if (process.env.BW_INCPROBE_OUT) {
      const out = process.env.BW_INCPROBE_OUT;
      await mkdir(dirname(out), { recursive: true });
      await writeFile(out, JSON.stringify(output, null, 2));
    }
  }
}
