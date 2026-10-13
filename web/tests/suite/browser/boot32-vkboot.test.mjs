// Real 32-bit Wine/PE/GUI boot; match normalized guest output, not page globals.
import { ProbeTest } from '../lib/probe-base.mjs';
import { checkGreaterThan } from '../lib/assert.mjs';
export default class Boot32VkbootTest extends ProbeTest {
  constructor() { super({ tag: 'boot32-vkboot', timeoutMs: 900000 }); }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86', winedbg: '+loaddll,+server' });
    await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
    await this.waitForLog(page, /^log: vkboot(?:\s*$|[0-9a-f]{4}: )/i, 180000);
    checkGreaterThan(this.countConsole(/user32\.dll/i), 0, '32-bit user32.dll must load');
    checkGreaterThan(this.countConsole(/^log: vkboot(?:\s*$|[0-9a-f]{4}: )/i), 0, '32-bit PE entry point must execute');
    checkGreaterThan(this.countConsole(/create_winstation/i), 0, 'create_winstation must initialize GUI');
  }
}
