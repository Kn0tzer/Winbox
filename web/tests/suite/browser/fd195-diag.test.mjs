// Diagnostic probe for FD-195: with a simulated gamepad installed, trace
// the guest's PlugPlay/HID enumeration end to end on the 32-bit arm and
// record whether the FD-24 witness sees the controller. Unlike the
// pass/fail probes, this probe never throws on the broken state: it
// writes a verdict file so the session traces can be read offline.
// Its slot budget is deliberately large so it can wait out build
// relays that starve the standing probes of their start window.
import { ProbeTest } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';

function installSimulatedGamepad() {
  const buttons = [];
  for (let i = 0; i < 17; i++) buttons.push({ pressed: false, touched: false, value: 0 });
  const pad = {
    id: 'BoxedWine simulated gamepad (STANDARD GAMEPAD Vendor: 045e Product: 028e)',
    index: 0, connected: true, mapping: 'standard', timestamp: 0,
    buttons, axes: [0, 0, 0, 0],
  };
  globalThis.__bwPad = pad;
  navigator.getGamepads = () => [pad, null, null, null];
}

export default class Fd195DiagTest extends ProbeTest {
  constructor() { super({ tag: 'fd195-diag', timeoutMs: 3600000 }); }
  async run({ page }) {
    const verdict = { arch: 'x86' };
    try {
      await page.addInitScript(installSimulatedGamepad);
      await this.bootGuest(page, { arch: 'x86', winedbg: '+ntoskrnl,+plugplay,+setupapi,+winebus,+hid,+debugstr' });
      await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
      await page.waitForTimeout(30000);
      const exe = await guestFixture('fd188-hid-enum', 'x86');
      await this.stageFile(page, 'fd188-hid-enum.exe', exe);
      let from = await this.spawnGuest(page, 'fd188-hid-enum.exe', { arch: 'x86', env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /fd188-hid: DONE/, 180000, from);
      const hidLines = this.consoleLines.slice(from).filter(l => /fd188-hid:/i.test(l));
      const count = hidLines.find(l => /HID devices=/.test(l));
      verdict.hidDevices = count ? parseInt(count.match(/devices=(\d+)/)[1], 10) : -1;
      const svc = hidLines.find(l => /service WineBus state=/.test(l));
      verdict.winebusState = svc ? svc.match(/state=(\d+)/)[1] : '?';
      const wexe = await guestFixture('gamepad-witness', 'x86');
      await this.stageFile(page, 'gamepad-witness.exe', wexe);
      from = await this.spawnGuest(page, 'gamepad-witness.exe', { arch: 'x86', env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /gamepad-witness: READY/, 180000, from);
      // Give detection up to 60s; absence is a verdict, not a failure.
      const until = Date.now() + 60000;
      while (Date.now() < until) {
        const lines = this.consoleLines.slice(from);
        if (lines.some(l => /XINPUT slot=0 CONNECTED/.test(l)) &&
            lines.some(l => /DINPUT devices=/.test(l))) break;
        await page.waitForTimeout(2000);
      }
      const wlines = this.consoleLines.slice(from);
      verdict.xinputSlot0 = wlines.some(l => /XINPUT slot=0 CONNECTED/.test(l));
      const di = wlines.find(l => /DINPUT devices=/.test(l));
      verdict.dinputDevices = di ? parseInt(di.match(/devices=(\d+)/)[1], 10) : -1;
    } catch (e) {
      verdict.error = String(e).slice(0, 500);
    }
    verdict.failLines = [...new Set(this.consoleLines.filter(l => /BW64-FAIL/.test(l)))];
    await writeFile(join(this.dir, 'verdict.json'), JSON.stringify(verdict, null, 2) + '\n');
  }
}
