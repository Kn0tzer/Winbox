import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// FD-195 regression test for the emulator's synthesized udev
// surface: runs the fd195-udevprobe ELF inside the guest, which
// drives the guest libudev (systemd's sd-device) exactly the way
// winebus's udev backend does. The emulator fixes this guards:
// synthesized virtual files must report regular-file type bits in
// st_mode (sd-device refuses to read a uevent file otherwise),
// synthesized sysfs symlinks must report S_IFLNK (sd-device only
// resolves the device path through them when they do), and the
// synthesized uevent content must terminate its DEVNAME line
// (sd-device's line parser only dispatches newline-terminated
// lines). Any of the three regressing makes the enumerated gamepad
// come back uninitialized with no devnode, and winebus silently
// drops it.
export default class Fd195UdevProbeTest extends ProbeTest {
  constructor() { super({ tag: 'fd195-udevprobe', timeoutMs: 3600000 }); }

  async run({ page, dir }) {
    const params = new URLSearchParams({
      p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1', arch: 'x86',
      gamepad: 'sim', ...extraUrlParams(),
    });
    await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    this.testedArchitectures.add('x86');
    await this.waitForPageFn(page, () => globalThis.bwRuntime?.call?.('bw64_session_ready', [], []) === 1, 600000);
    await this.waitForLog(page, /session: persistent pins spawned/, 60000);
    await page.waitForTimeout(5000);

    const elf = 'fd195-udevprobe-x86.elf';
    await this.stageFile(page, elf, await guestFixture('fd195-udevprobe', 'x86'));
    const from = this.consoleLines.length;
    const pid = await page.evaluate(name => globalThis.bwRuntime.call('bw64_spawn', ['string', 'string'],
      [`/home/username/${name}`, '']), elf);
    if (pid <= 0) throw new Error(`udevprobe spawn failed: ${pid}`);
    await this.waitForLog(page, /UDEVPROBE DONE/, 120000, from);
    const lines = this.consoleLines.slice(from).filter(l => l.includes('UDEVPROBE'));
    await writeFile(join(dir, 'udevprobe.json'), JSON.stringify(lines, null, 2));

    const joined = lines.join('\n');
    const expect = [
      /enumerate subsystem=input scan=0 entries: \[\/sys\/class\/input\/event5\] \(count=1\)/,
      /devnode=\/dev\/input\/event5 syspath=\/sys\/class\/input\/event5 subsystem=input initialized=1/,
      /prop ID_INPUT_JOYSTICK=1 DEVNAME=\/dev\/input\/event5/,
      /raw stat \/run\/udev\/data\/c13:69 -> 0 mode=100400 /,
      /raw stat \/sys\/class\/input\/event5\/uevent -> 0 mode=100400 /,
    ];
    for (const re of expect) {
      if (!re.test(joined)) throw new Error(`fd195-udevprobe: expected line missing: ${re} in:\n${joined}`);
    }
  }
}
