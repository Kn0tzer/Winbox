import { copyFile } from 'node:fs/promises';
import { join } from 'node:path';
import { ProbeTest } from '../lib/probe-base.mjs';

export default class Cpu64SelftestTest extends ProbeTest {
  constructor() {
    super({ tag: 'cpu64-selftest', timeoutMs: 180000 });
    this.guestArchitectures = ['x64'];
  }
  async run({ page }) {
    await copyFile(new URL('../../../../tools/guest-fixtures/cpu64-selftest.html', import.meta.url),
      join(this._root, '__fixtures/cpu64-selftest.html'));
    await page.goto(this._server.url + '/__fixtures/cpu64-selftest.html');
    const line = await this.waitForLog(page, /=== self-test summary: \d+ passed, \d+ failed ===/, 120000);
    const [, passed, failed] = line.match(/summary: (\d+) passed, (\d+) failed/);
    if (Number(passed) < 327 || Number(failed) !== 0) throw Error(line);
    this.testedArchitectures.add('x64');
  }
}
