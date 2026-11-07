// Diagnostic probe for FD-188/FD-24: with a simulated gamepad installed,
// what does the guest's PlugPlay/HID layer actually enumerate? Boots the
// 32-bit guest with the winebus/plugplay/setupapi channels on, captures
// the WINEBUS device creation + function-driver install traces, and runs
// the fd188-hid-enum guest fixture (SetupDi HID enumeration + the
// C:\windows\inf\ listing + the WineBus service state). Reports the HID
// device count; fails if the guest sees no HID device at all, since the
// FD-24 witness needs at least the gamepad to be visible.
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
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

export default class Fd188HidEnumTest extends ProbeTest {
  constructor() { super({ tag: 'fd188-hid-enum', timeoutMs: 900000 }); }
  async run({ page }) {
    await page.addInitScript(installSimulatedGamepad);
    await this.bootGuest(page, { arch: 'x86', winedbg: '+ntoskrnl,+plugplay,+setupapi,+winebus,+debugstr' });
    await page.waitForTimeout(30000);
    const interesting = this.consoleLines.filter(l =>
      /ntoskrnl|plugplay|setupapi|winebus|winehid/i.test(l) &&
      /driver|WINEBUS|winebus|winehid|HID|install|enumerate|Failed|Unable|init done/i.test(l));
    await writeFile(join(this.dir, 'hid-enum-lines.txt'), interesting.join('\n') + '\n');
    const exe = await guestFixture('fd188-hid-enum', 'x86');
    await this.stageFile(page, 'fd188-hid-enum.exe', exe);
    const from = await this.spawnGuest(page, 'fd188-hid-enum.exe', { arch: 'x86', env: 'WINEDEBUG=+debugstr' });
    await this.waitForLog(page, /fd188-hid: DONE/, 180000, from);
    const hidLines = this.consoleLines.slice(from).filter(l => /fd188-hid:/i.test(l));
    await writeFile(join(this.dir, 'hid-enum-fixture-lines.txt'), hidLines.join('\n') + '\n');
    const count = hidLines.find(l => /HID devices=/.test(l));
    const n = count ? parseInt(count.match(/devices=(\d+)/)[1], 10) : -1;
    if (n < 1) {
      throw new Error(`guest sees no HID device at all (HID devices=${n}); see hid-enum-lines.txt for the winebus/plugplay traces`);
    }
  }
}
