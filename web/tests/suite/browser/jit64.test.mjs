import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// Genuine PE64 under Wine64: the 32-bit Chromium benchmark cannot cover this.
export default class Jit64Test extends ProbeTest {
  constructor() { super({ tag: 'jit64', timeoutMs: 600000 }); this.guestArchitectures = ['x64']; }
  async run({ page }) {
    const results = [];
    for (const jit of ['0', '1']) {
      if (jit === '1') { await page.close(); page = await this.newPage(); }
      await this.bootGuest(page, { arch: 'x64', jit, winedbg: '+debugstr' });
      await this.stageFile(page, 'jitbench64.exe', await guestFixture('jitbench64-pe', 'x64'));
      const from = await this.spawnGuest(page, 'jitbench64.exe', { arch: 'x64', env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /BW_JITBENCH64.*"sample":3/, 120000, from);
      const samples = this.consoleLines.slice(from).flatMap(line => {
        const match = line.match(/BW_JITBENCH64 (\{.*\})/);
        return match ? [JSON.parse(match[1])] : [];
      });
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
      results.push({ jit, samples, stats });
    }
    await writeFile(join(this.dir, 'benchmark.json'), JSON.stringify({ build: this.buildInfo, results }, null, 2));
  }
}
