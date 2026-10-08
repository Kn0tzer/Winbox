// web/tests/suite/browser/boot32-vkboot.test.mjs — 32-bit PE entry point test.
//
// CURRENT BLOCKER (as of 2026-10-08): No 32-bit PE has ever run its entry point.
// ^log: vkboot$ = 0 in all probes.
//
// What it tests:
//   - 32-bit Wine boots and spawns the test PE
//   - The PE's entry point executes (prints "vkboot" to the log)
//   - create_winstation is called (GUI initialization)
//
// This is the gate for ALL 32-bit work, including Portal.
// If this test fails, nothing 32-bit works.
//
// Diagnostic checklist (in order):
//   1. Does the 32-bit wineserver start? (socket isolation)
//   2. Do 32-bit DLLs load? (user32.dll, win32u.dll)
//   3. Does the PE spawn? (init_first_thread in +server log)
//   4. Does the entry point execute? (vkboot in console)
//   5. Does create_winstation get called?

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';

export default class Boot32VkbootTest extends ProbeTest {
  constructor() {
    super({ tag: 'boot32-vkboot', timeoutMs: 900000 });
  }

  async run(ctx) {
    const { page } = ctx;

    // Boot 32-bit Wine and run the vkboot test PE
    await page.goto(`${ctx.server.url}/?arch=x86&test=vkboot`);

    // Wait for boot to complete or timeout
    // The test PE prints "vkboot" via log: when its entry point runs
    try {
      await this.waitForPageFn(page,
        () => window.__boot32Done === true,
        600000
      );
    } catch (e) {
      // Timeout — check what we got anyway for diagnostics
    }

    // Check 1: 32-bit wineserver started (socket isolation working)
    const serverStarted = this.countConsole(/wineserver32.*started|server32.*listening/i);
    // (This is informational — the real checks are below)

    // Check 2: 32-bit DLLs loaded (in-process, should work)
    const user32Loaded = this.countConsole(/user32\.dll/i);
    checkGreaterThan(user32Loaded, 0,
      `32-bit user32.dll must load (found ${user32Loaded} mentions). ` +
      `If 0, the 32-bit Wine prefix or DLL loading is broken.`);

    // Check 3: THE GATE — did a 32-bit PE run its entry point?
    const vkbootCount = this.countConsole(/^log: vkboot$/m);
    checkGreaterThan(vkbootCount, 0,
      `32-bit PE entry point must execute (^log: vkboot$ found ${vkbootCount} times). ` +
      `If 0: (1) check __wineboot_event signaling, ` +
      `(2) check for page faults in ntdll during PE init, ` +
      `(3) verify 32-bit PE entry-point transfer in Wine loader.`);

    // Check 4: create_winstation called (GUI initialization)
    const winstationCount = this.countConsole(/create_winstation/i);
    checkGreaterThan(winstationCount, 0,
      `create_winstation must be called (found ${winstationCount}). ` +
      `Required for 32-bit GUI/desktop.`);
  }
}
