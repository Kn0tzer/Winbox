import { mkdir, writeFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { extraUrlParams, ProbeTest, withTimeout } from './suite/lib/probe-base.mjs';

class GamepadE2ETest extends ProbeTest {
  constructor() {
    super({ tag: 'gamepad-e2e', timeoutMs: 1200000 });
    this.dir = 'test-results/gamepad-e2e';
  }

  async bootGamepadRuntime(page, arch) {
    this.testedArchitectures.add(arch);
    const params = new URLSearchParams({
      p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1', arch,
      winedbg: '+plugplay,+hid,+xinput',
      ...extraUrlParams(),
    });
    await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    // The generic boot helper also waits for the desktop pin's window log,
    // which is unavailable in this headless probe. The test waits separately
    // for the persistent service pins before launching its guest fixture.
    await this.waitForPageFn(page, () =>
      globalThis.bwRuntime?.call?.('bw64_session_ready', [], []) === 1, 600000);
  }

  async run({ page }) {
    const results = {};
    let activePage = page;
    for (const arch of ['x64', 'x86']) {
      if (arch === 'x86') {
        await activePage.close();
        activePage = await this.newPage();
      }
      await activePage.addInitScript(() => {
        const buttons = Array.from({ length: 17 }, () => ({ pressed: false, value: 0, touched: false }));
        const pad = {
          id: 'Xbox 360 Controller (XInput STANDARD GAMEPAD)',
          index: 0,
          connected: true,
          mapping: 'standard',
          axes: [0, 0, 0, 0],
          buttons,
          timestamp: performance.now(),
        };
        Object.defineProperty(navigator, 'getGamepads', {
          configurable: true,
          value: () => [pad],
        });
        const dispatchConnected = () => {
          const event = new Event('gamepadconnected');
          Object.defineProperty(event, 'gamepad', { value: pad });
          window.dispatchEvent(event);
        };
        window.__gamepadTest = {
          pad,
          dispatchConnected,
          setAxis(index, value) {
            pad.axes[index] = value;
            pad.timestamp = performance.now();
          },
          setButton(index, pressed, value) {
            pad.buttons[index] = { pressed, value, touched: pressed || value > 0 };
            pad.timestamp = performance.now();
          },
        };
      });

      await this.bootGamepadRuntime(activePage, arch);
      await this.waitForLog(activePage,
        /session: persistent pins spawned; Wine owns service initialization/, 120000);
      // The pin-spawn message only says the services were queued. Wait until
      // Wine's one-shot service and PnP initialization processes have exited
      // before starting XInput, so its first query cannot race bus setup.
      await this.waitForLog(activePage, /CPU64: exit syscall, status=0.*services\.exe/, 120000);
      await this.waitForLog(activePage, /CPU64: exit syscall, status=0.*winedevice\.exe/, 120000);
      const runtimeFrame = await activePage.evaluate(() => ({
        path: location.pathname,
        ready: window.bwRuntime?.call?.('bw64_session_ready', [], []) === 1,
        gamepadId: navigator.getGamepads()[0]?.id,
      }));
      if (!runtimeFrame.path.endsWith('/runtime/index.html') || !runtimeFrame.ready ||
          runtimeFrame.gamepadId !== 'Xbox 360 Controller (XInput STANDARD GAMEPAD)') {
        throw new Error(`${arch} fake gamepad was not installed in the active runtime frame: ${JSON.stringify(runtimeFrame)}`);
      }
      const exe = `xinputprobe-${arch}.exe`;
      await this.stageFile(activePage, exe, resolve(`tools/guest-fixtures/${exe}`));
      await activePage.evaluate(() => window.__gamepadTest.dispatchConnected());
      await this.waitForLog(activePage, /bw64_gamepad_update: first snapshot connected=1/, 15000);
      const from = await this.spawnGuest(activePage, exe, { arch, env: 'WINEDEBUG=+plugplay,+hid,+xinput' });
      await this.waitForLog(activePage, /idx=0 rc=0\b/, 90000, from);

      await activePage.waitForTimeout(5000);
      await activePage.evaluate(() => window.__gamepadTest.setButton(0, true, 1));
      await activePage.waitForTimeout(1000);
      await activePage.evaluate(() => window.__gamepadTest.setButton(0, false, 0));
      await activePage.evaluate(() => window.__gamepadTest.setAxis(0, 1.0));
      await activePage.waitForTimeout(1000);
      await activePage.evaluate(() => window.__gamepadTest.setAxis(0, 0));
      await activePage.evaluate(() => window.__gamepadTest.setButton(7, true, 1));
      await activePage.waitForTimeout(1000);
      await activePage.evaluate(() => window.__gamepadTest.setButton(7, false, 0));
      await activePage.waitForTimeout(250);

      const lines = this.consoleLines.slice(from)
        .map(line => line.slice(line.indexOf('t=')))
        .filter(line => /^t=\d+ idx=\d+ rc=\d+ buttons=[0-9a-f]+ lx=-?\d+ ly=-?\d+ lt=\d+ rt=\d+$/i.test(line));
      const states = lines.map(line => {
        const match = line.match(/^t=(\d+) idx=(\d+) rc=(\d+) buttons=([0-9a-f]+) lx=(-?\d+) ly=(-?\d+) lt=(\d+) rt=(\d+)$/i);
        return {
          t: Number(match[1]), idx: Number(match[2]), rc: Number(match[3]),
          buttons: Number.parseInt(match[4], 16), lx: Number(match[5]), ly: Number(match[6]),
          lt: Number(match[7]), rt: Number(match[8]),
        };
      });
      const connected = states.some(state => state.idx === 0 && state.rc === 0);
      const aPressed = states.some(state => state.idx === 0 && (state.buttons & 0x1000) !== 0);
      const leftStick = states.some(state => state.idx === 0 && state.lx > 30000);
      const rightTrigger = states.some(state => state.idx === 0 && state.rt === 255);
      results[arch] = { connected, aPressed, leftStick, rightTrigger, states };
      await writeFile(join(this.dir, `${arch}.txt`), lines.join('\n') + (lines.length ? '\n' : ''));
      if (!connected || !aPressed || !leftStick || !rightTrigger) {
        throw new Error(`${arch} gamepad evidence incomplete: ${JSON.stringify(results[arch])}`);
      }
    }
    await mkdir(this.dir, { recursive: true });
    await writeFile(join(this.dir, 'summary.json'), JSON.stringify(results, null, 2) + '\n');
    console.log(JSON.stringify({ pass: true, architectures: Object.keys(results), outDir: this.dir }));
  }
}

const test = new GamepadE2ETest();
await test.setup();
let page;
try {
  page = await test.newPage();
  await test.run({ page });
} finally {
  if (page) await withTimeout(page.close(), 10000, 'gamepad page close').catch(() => {});
  await test.teardown();
}
