// Inject the runtime's real relative input API; observe guest RAWINPUT deltas.
import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkEqual, checkGreaterThan } from '../lib/assert.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';
import { rawEvents, rawEventAfter } from '../lib/raw-input-evidence.mjs';
export default class InputMouseRelTest extends ProbeTest {
  constructor() { super({ tag: 'input-mouse-rel', timeoutMs: 900000 }); }
  async run({ page }) {
    for (const arch of this.guestArchitectures) {
      if (arch === 'x64') { await page.close(); page = await this.newPage(); }
      const exe = await guestFixture('input-witness', arch);
      await this.bootGuest(page, { arch, winedbg: '+debugstr' });
      await this.waitForPageFn(page, () => globalThis.bwRuntime.ready(), 600000);
      await this.stageFile(page, 'input-witness.exe', exe);
      const from = await this.spawnGuest(page, 'input-witness.exe', { arch, env: 'WINEDEBUG=+debugstr' });
      await this.waitForLog(page, /input-witness: READY/, 180000, from);
      const inject = (dx, dy) => page.evaluate(([x,y]) => globalThis.bwRuntime.call('bw64_xwire_mouse_rel', ['number','number'], [x,y]), [dx,dy]);
      let sequence = 0;
      const waitForPacket = async (dx, dy) => {
        const line = await this.waitForLog(page, rawEventAfter(sequence, dx, dy), 30000, from);
        const event = rawEvents([line])[0];
        sequence = event.sequence;
        return event;
      };
      await inject(100,50);
      const initial = await waitForPacket(100,50);
      checkEqual(initial.flags & 1, 0, 'Guest RAWINPUT must use relative coordinates');
      // Require a new guest packet for every separate sample. A late duplicate
      // debug line cannot acknowledge the following injection.
      const rapid = [];
      for (let i=0;i<10;i++) {
        await inject(50,0);
        rapid.push(await waitForPacket(50,0));
      }
      checkGreaterThan(rapid.length, 5, `${arch}: rapid relative movements must not be lost`);
      checkEqual(rapid.reduce((sum,e) => sum+e.dx,0), 500, 'Every rapid horizontal delta must reach the guest');
      // Reverse the measured motion before the independent burst so it stays
      // inside the desktop, regardless of the initial host cursor position.
      await inject(-500,0);
      await waitForPacket(-500,0);
      const burstStart = sequence;
      await page.evaluate(() => { for (let i=0;i<10;i++) globalThis.bwRuntime.call('bw64_xwire_mouse_rel', ['number','number'], [50,0]); });
      const burstEvents = () => rawEvents(this.consoleLines.slice(from)).filter(event => event.sequence > burstStart);
      const until = Date.now() + 30000;
      while (Date.now() < until && burstEvents().reduce((sum, event) => sum + event.dx, 0) < 500) await page.waitForTimeout(100);
      const burst = burstEvents();
      checkEqual(burst.reduce((sum,e) => sum+e.dx,0), 500, 'Coalesced burst must preserve all ten deltas');
      checkEqual(burst.reduce((sum,e) => sum+e.dy,0), 0, 'Horizontal burst must not introduce vertical motion');
      sequence = Math.max(sequence, ...burst.map(event => event.sequence));
      await inject(-75,-25);
      const negative = await waitForPacket(-75,-25);
      check(negative.sequence > burstStart, 'Negative relative movement must generate a new guest event');
    }
  }
}
