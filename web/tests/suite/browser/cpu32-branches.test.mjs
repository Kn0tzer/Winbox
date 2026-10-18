import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

export default class Cpu32BranchesTest extends ProbeTest {
  constructor() {
    super({ tag: 'cpu32-branches', timeoutMs: 180000 });
    this.guestArchitectures = ['x86'];
  }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86' });
    await this.stageFile(page, 'cpu32-branches.elf', await guestFixture('cpu32-branches', 'x86'));
    const from = this.consoleLines.length;
    const accepted = await page.evaluate(() => globalThis.bwRuntime.call('bw64_spawn', ['string', 'string'],
      ['/home/username/cpu32-branches.elf\n--spin', '']));
    if (accepted !== 1) throw Error('Branch fixture spawn was not queued');
    await this.waitForLog(page, /cpu32-branches: FLAGS LOOP CALLS RESULT 0/, 15000, from);
    const lines = this.consoleLines.slice(from);
    const launch = lines.findIndex(line => line.includes('wine64session: spawning "/home/username/cpu32-branches.elf"'));
    const spawned = launch < 0 ? null : lines.slice(launch + 1).find(line => /wine64session: spawned pid=\d+/.test(line));
    if (!spawned) throw Error('Branch fixture PID was not reported');
    const pid = Number(spawned.match(/pid=(\d+)/)[1]);
    await page.waitForTimeout(250);
    const alive = await page.evaluate(pid => globalThis.bwRuntime.call('bw64_pid_alive', ['number'], [pid]), pid);
    if (alive !== 1) throw Error('Branch loop exited before termination');
    const queued = await page.evaluate(pid => globalThis.bwRuntime.call('bw64_kill', ['number'], [pid]), pid);
    if (queued !== 1) throw Error('Branch loop termination was not queued');
    await page.waitForFunction(pid => globalThis.bwRuntime.call('bw64_pid_alive', ['number'], [pid]) === 0,
      pid, { timeout: 10000 });
  }
}
