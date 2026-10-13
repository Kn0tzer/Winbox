// Inject the runtime's real relative input API; observe guest RAWINPUT deltas.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkEqual, checkGreaterThan } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
const rawEvents = lines => [...new Map(lines.map(l => l.match(/input-witness: RAW seq=(\d+) flags=(\d+) dx=(-?\d+) dy=(-?\d+)/)).filter(Boolean)
  .map(m => [m[1], { sequence: +m[1], flags: +m[2], dx: +m[3], dy: +m[4] }])).values()];
export default class InputMouseRelTest extends ProbeTest {
  constructor() { super({ tag: 'input-mouse-rel', timeoutMs: 900000 }); }
  async run({ page }) {
    for (const arch of ['x86', 'x64']) {
      if (arch === 'x64') { await page.close(); page = await this.newPage(); }
      const exe = await guestFixture('input-witness', arch);
      await this.bootGuest(page, { arch, winedbg: '+debugstr' });
      await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
      await this.stageFile(page, 'input-witness.exe', exe);
      const from = await this.spawnGuest(page, 'input-witness.exe', { arch, env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /input-witness: READY/, 180000, from);
      let before = this.consoleLines.length;
      const inject = (dx, dy) => page.evaluate(([x,y]) => globalThis.bwRuntime.call('bw64_xwire_mouse_rel', ['number','number'], [x,y]), [dx,dy]);
      await inject(100,50);
      await this.waitForLog(page, /input-witness: RAW .*dx=100 dy=50/, 30000, before);
      const events = rawEvents(this.consoleLines.slice(before));
      checkGreaterThan(events.length, 0, 'Relative mouse movement must generate guest events');
      check(events[0] !== undefined, 'Must have at least one relative mouse event');
      checkEqual(events[0].flags & 1, 0, 'Guest RAWINPUT must use relative coordinates');
      before = this.consoleLines.length;
      // Physical samples arrive over time. Keep the packet-count assertion,
      // and separately check an instantaneous burst's exact accumulated motion.
      for (let i=0;i<10;i++) { await inject(50,0); await page.waitForTimeout(20); }
      await page.waitForTimeout(1500);
      const rapid = rawEvents(this.consoleLines.slice(before));
      checkGreaterThan(rapid.length, 5, `${arch}: rapid relative movements must not be lost`);
      checkEqual(rapid.reduce((sum,e) => sum+e.dx,0), 500, 'Every rapid horizontal delta must reach the guest');
      // Start this independent case inside the desktop again. Otherwise the
      // second +500 burst crosses the screen edge and Wine clips the cursor.
      const resetFrom = this.consoleLines.length;
      await page.evaluate(() => globalThis.bwRuntime.call('bw64_xwire_mouse_move', ['number','number'], [100,50]));
      await this.waitForLog(page, /input-witness: RAW .*dx=-500 dy=0/, 30000, resetFrom);
      before = this.consoleLines.length;
      await page.evaluate(() => { for (let i=0;i<10;i++) globalThis.bwRuntime.call('bw64_xwire_mouse_rel', ['number','number'], [50,0]); });
      await page.waitForTimeout(1500);
      const burst = rawEvents(this.consoleLines.slice(before));
      checkEqual(burst.reduce((sum,e) => sum+e.dx,0), 500, 'Coalesced burst must preserve all ten deltas');
      checkEqual(burst.reduce((sum,e) => sum+e.dy,0), 0, 'Horizontal burst must not introduce vertical motion');
      before = this.consoleLines.length;
      await inject(-75,-25);
      await this.waitForLog(page, /input-witness: RAW .*dx=-75 dy=-25/, 30000, before);
      checkGreaterThan(rawEvents(this.consoleLines.slice(before)).length, 0, 'Negative relative movement must generate guest events');
    }
  }
}
