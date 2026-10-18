import assert from 'node:assert/strict';
import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

export default class Cpu32JitGrowthTest extends ProbeTest {
  constructor() { super({ tag: 'cpu32-jit-growth', timeoutMs: 180000 }); this.guestArchitectures = ['x86']; }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86', jit: '1', jitstats: '1' });
    await this.stageFile(page, 'growth.elf', await guestFixture('cpu32-jit-growth', 'x86'));
    const from = this.consoleLines.length;
    const queued = await page.evaluate(() => bwRuntime.call('bw64_spawn', ['string', 'string'], ['/home/username/growth.elf', '']));
    assert.ok(queued > 0);
    const line = await this.waitForLog(page, /cpu32-jit-growth: (RESULT|FAIL)/, 15000, from);
    assert.match(line, /RESULT 0/);
    const stats = await page.evaluate(() => JSON.parse(Module.ccall('bw64_jit_stats_json_text', 'string', [], [])));
    assert.equal(stats.cpu32.retiredCounting, true);
    assert.ok(stats.cpu32.blocksEntered > 0 && stats.cpu32.instructionsRetired > 0,
      'integer/SSE code still executes in generated modules alongside 80-bit x87 fallback');
  }
}
