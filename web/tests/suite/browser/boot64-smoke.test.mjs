// Real 64-bit Wine boot smoke; the booted wine64 must actually EXECUTE,
// not merely load. Regression guard for the x64 browser-start failure in
// which the ELF loader pre-applied PT_GNU_RELRO read-only and, once guest
// page-permission enforcement existed (a835b2a65), the guest ld.so's own
// first relocation write into the main executable's PT_DYNAMIC page
// faulted — the session came up but zero guest code ever ran and no
// window ever mapped. This test therefore asserts guest-execution
// evidence from the console stream itself (the guest-side wineserver
// execve is the first unambiguous sign the guest loader survived),
// never a readiness flag.
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
export default class Boot64SmokeTest extends ProbeTest {
  constructor() { super({ tag: 'boot64-smoke', timeoutMs: 900000 }); }
  async run({ page }) {
    this.testedArchitectures.add('x64');
    const params = new URLSearchParams({
      p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1',
      arch: 'x64', winedbg: '+loaddll,+server', ...extraUrlParams(),
    });
    await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    // The wineserver execve proves the guest loader survived and the
    // booted wine64 is running guest code.
    await this.waitForLog(page, /sys_execve64: pid=/, 300000);
    // Then the session must actually initialize: 64-bit DLLs load into
    // the booted cmd, its entry point runs (the echo prints), and the
    // window station comes up.
    await this.waitForLog(page, /user32\.dll/i, 240000);
    await this.waitForLog(page, /^log: vkboot(?:\s*$|[0-9a-f]{4}: )/i, 240000);
    await this.waitForLog(page, /create_winstation/i, 240000);
  }
}
