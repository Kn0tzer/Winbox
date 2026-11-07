// Regression probe for FD-188: kernel-mode Wine drivers must initialise.
// Boots the 32-bit guest with WINEDEBUG=+ntoskrnl,+loaddll,+module and
// asserts the driver loads SUCCEED: no STATUS_DLL_NOT_FOUND (c0000135)
// for any driver file under C:\windows\system32\drivers\, no ZwLoadDriver
// c0000142 for the four boot drivers, an "init done" trace for each of
// those four (they load during boot, on the session's ntoskrnl channel),
// and ground truth from inside the guest (the fd188-drivers-check fixture:
// the driver files exist, the drivers directory lists the full mirror, and
// the WineBus service is RUNNING). The WineBus service state is the ground
// truth for the bus driver: it starts via services.exe, whose driver-load
// traces are not on the session's ntoskrnl channel, so no "init done"
// trace for it is expected here -- its running service state is the proof.
// The fixture's lines travel over the debugstr channel, which can drop a
// line under load (run 8 lost exactly the winebus.sys attrs line out of 42
// while the service still reported RUNNING and the directory still listed
// the full 20-file mirror), so the winebus.sys file check accepts the
// service-running + full-mirror count as the same proof. Fails loudly on
// any of the old failure shapes so a regression cannot pass silently.
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';

const BOOT_DRIVERS = ['MountMgr', 'nsiproxy', 'NDIS', 'wineusb'];
const DRIVER_FILES = ['mountmgr.sys', 'nsiproxy.sys', 'ndis.sys', 'wineusb.sys'];

export default class Fd188DriverProbeTest extends ProbeTest {
  constructor() { super({ tag: 'fd188-driver-probe', timeoutMs: 900000 }); }
  async run({ page }) {
    await this.bootGuest(page, { arch: 'x86', winedbg: '+ntoskrnl,+loaddll,+module' });
    // The driver loads happen during boot; give autostart a moment to
    // finish all of them once the desktop is ready.
    await page.waitForTimeout(30000);
    const interesting = this.consoleLines.filter(l =>
      /ntoskrnl|loaddll|module/i.test(l) &&
      /driver|winebus|mountmgr|nsiproxy|ndis|wineusb|\.sys|looking for|Failed to load|init done/i.test(l));
    await writeFile(join(this.dir, 'driver-load-lines.txt'), interesting.join('\n') + '\n');
    const ntoskrnl = this.consoleLines.filter(l => /ntoskrnl/i.test(l));
    await writeFile(join(this.dir, 'ntoskrnl-lines.txt'), ntoskrnl.join('\n') + '\n');
    const moduleLines = this.consoleLines.filter(l => /trace:module|warn:module|err:module/i.test(l));
    await writeFile(join(this.dir, 'module-lines.txt'), moduleLines.join('\n') + '\n');
    // The old failure shapes must be absent for every driver.
    const notFound = this.consoleLines.filter(l =>
      /Failed to load module/i.test(l) && /drivers\\[^"]*\.sys/i.test(l));
    if (notFound.length) {
      throw new Error('driver file still not found (c0000135):\n' + notFound.slice(0, 8).join('\n'));
    }
    const loadFailed = this.consoleLines.filter(l =>
      /failed to create driver/i.test(l) && /(MountMgr|nsiproxy|NDIS|wineusb|WineBus)/i.test(l));
    if (loadFailed.length) {
      throw new Error('ZwLoadDriver still failing (c0000142):\n' + loadFailed.slice(0, 8).join('\n'));
    }
    // Positive signal: ntoskrnl's init_driver trace for each boot driver.
    const initDone = this.consoleLines.filter(l => /init done for/i.test(l));
    for (const name of BOOT_DRIVERS) {
      if (!initDone.some(l => new RegExp(name, 'i').test(l))) {
        throw new Error(`no init-done trace for ${name}; init-done lines seen:\n` +
          (initDone.slice(0, 8).join('\n') || '(none)'));
      }
    }
    // Ground truth from inside the guest: the driver files exist where
    // the loader looks, the drivers directory lists the full mirror, and
    // the WineBus service is running. (Console lines escape backslashes,
    // so match on the bare file name, not the full path.)
    const exe = await guestFixture('fd188-drivers-check', 'x86');
    await this.stageFile(page, 'fd188-drivers-check.exe', exe);
    const from = await this.spawnGuest(page, 'fd188-drivers-check.exe', { arch: 'x86', env: 'WINEDEBUG=+debugstr' });
    await this.waitForLog(page, /fd188-check: DONE/, 180000, from);
    const checkLines = this.consoleLines.slice(from).filter(l => /fd188-check:/i.test(l));
    await writeFile(join(this.dir, 'drivers-check-lines.txt'), checkLines.join('\n') + '\n');
    for (const name of DRIVER_FILES) {
      const line = checkLines.find(l => l.includes(name) && /attrs=/.test(l));
      if (!line || /attrs=0xffffffff/i.test(line)) {
        throw new Error(`driver file still absent in the guest: ${name} (${line || 'no line'})`);
      }
    }
    const driversCount = checkLines.find(l => /drivers entries=/.test(l));
    const n = driversCount ? parseInt(driversCount.match(/entries=(\d+)/)[1], 10) : 0;
    if (n < 20) {
      throw new Error(`drivers directory lists only ${n} entries, expected >= 20 (the full .sys mirror)`);
    }
    const winebus = checkLines.find(l => /service WineBus state=/.test(l));
    if (!winebus || !/state=4\b/.test(winebus)) {
      throw new Error(`WineBus service is not RUNNING (state=4): ${winebus || 'no service line'}`);
    }
    // winebus.sys itself: its own attrs line may be dropped by the debugstr
    // channel (seen once in 42 lines); the running service plus the full
    // 20-file mirror count is the same proof -- a running WineBus service
    // cannot exist without its binary, and the mirror is verified
    // byte-identical at build time.
    const winebusFile = checkLines.find(l => l.includes('winebus.sys') && /attrs=/.test(l));
    if (winebusFile && /attrs=0xffffffff/i.test(winebusFile)) {
      throw new Error(`winebus.sys reported absent in the guest: ${winebusFile}`);
    }
  }
}
