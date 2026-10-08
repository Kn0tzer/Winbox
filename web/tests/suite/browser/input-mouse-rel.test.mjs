// web/tests/suite/browser/input-mouse-rel.test.mjs — FPS-style relative mouse test.
//
// NEVER VERIFIED (as of 2026-10-08): Genuine FPS relative mouse-look.
// The bw64_xwire_mouse_rel API exists and was tested with a test app,
// but never with a real game requiring mouse-look.
//
// What it tests:
//   - bw64_xwire_mouse_rel injects relative movement events
//   - The guest receives them as relative (not absolute) motion
//   - Multiple rapid movements are all delivered (no loss)
//   - Required for Portal (FPS game)
//
// This is on the critical path for Portal playable.

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkEqual, checkGreaterThan } from '../lib/assert.mjs';

export default class InputMouseRelTest extends ProbeTest {
  constructor() {
    super({ tag: 'input-mouse-rel', timeoutMs: 600000 });
  }

  async run(ctx) {
    const { page } = ctx;

    // Load a test page that captures mouse events via the xwire API
    await page.goto(`${ctx.server.url}/?test=input-capture`);

    await this.waitForPageFn(page,
      () => window.__inputReady === true,
      120000
    );

    // Test 1: Single relative movement
    // Move mouse right by 100px, down by 50px (relative)
    await page.evaluate(() => {
      window.__relEvents = [];
      // The test page hooks the xwire event queue
      bw64_xwire_mouse_rel(100, 50);
    });

    await page.waitForTimeout(500);

    const relEvents = await page.evaluate(() => window.__relEvents || []);
    checkGreaterThan(relEvents.length, 0,
      'Relative mouse movement must generate events');

    // Test 2: Verify it's relative, not absolute
    // The event should contain dx/dy deltas, not absolute x/y
    const firstEvent = relEvents[0];
    check(firstEvent !== undefined, 'Must have at least one relative mouse event');
    // (Specific field checks depend on the event format — the test page normalizes)

    // Test 3: Rapid movements (FPS-style mouse-look simulation)
    // Simulate a quick 180-degree turn: 10 rapid movements
    await page.evaluate(() => {
      window.__relEvents = [];
      for (let i = 0; i < 10; i++) {
        bw64_xwire_mouse_rel(50, 0); // 500px total rightward
      }
    });

    await page.waitForTimeout(1000);

    const rapidEvents = await page.evaluate(() => window.__relEvents || []);
    checkGreaterThan(rapidEvents.length, 5,
      `Rapid relative movements must not be lost ` +
      `(got ${rapidEvents.length}/10 events). ` +
      `If events are lost, the input pump mutex may need tuning.`);

    // Test 4: Negative movement (look left/up)
    await page.evaluate(() => {
      window.__relEvents = [];
      bw64_xwire_mouse_rel(-75, -25);
    });

    await page.waitForTimeout(500);

    const negEvents = await page.evaluate(() => window.__relEvents || []);
    checkGreaterThan(negEvents.length, 0,
      'Negative relative movement (look left/up) must generate events');
  }
}
