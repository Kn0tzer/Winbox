// web/tests/suite/browser/d3d11-triangle.test.mjs — D3D11 triangle rendering test.
//
// VERIFIED WORKING (2026-10-08): LIT=8192, 14 gipa HITs for vkCmdCopyBuffer.
// This test encodes that verification as a permanent regression guard.
//
// What it tests:
//   - D3D11CreateDevice succeeds at FL 9_3
//   - vkCmdCopyBuffer is gipa-HIT (not MISS) — the LIT=0 root cause
//   - The triangle renders with LIT > 0 (non-zero pixels)
//
// If this test fails, the D3D11 pipeline is broken. Check:
//   1. gipa HITs for vkCmdCopyBuffer (shim registration)
//   2. BUFFER_DATA records in V2 frames (bridge emission)
//   3. Buffer cache key consistency (page lookup)

import { ProbeTest } from '../lib/probe-base.mjs';
import { check, checkGreaterThan } from '../lib/assert.mjs';

export default class D3D11TriangleTest extends ProbeTest {
  constructor() {
    super({ tag: 'd3d11-triangle', timeoutMs: 900000 });
  }

  async run(ctx) {
    const { page } = ctx;

    // Navigate to the D3D11 triangle probe page
    // The probe page runs tri11.exe and reports LIT via window.__lit
    await page.goto(`${ctx.server.url}/?test=d3d11-triangle`);

    // Wait for the test to complete (probe sets window.__done = true)
    await this.waitForPageFn(page,
      () => window.__done === true,
      600000
    );

    // Check 1: D3D11 device was created
    const deviceOk = await page.evaluate(() => window.__d3d11DeviceOk);
    check(deviceOk === true, 'D3D11CreateDevice must succeed');

    // Check 2: vkCmdCopyBuffer is gipa-HIT (not MISS)
    // This is the LIT=0 root cause — if MISS, vertex uploads don't happen
    const gipaHits = this.countConsole(/gipa HIT.*vkCmdCopyBuffer/);
    checkGreaterThan(gipaHits, 0,
      `vkCmdCopyBuffer must be gipa-HIT (found ${gipaHits} HITs). ` +
      `If 0, the shim registration is broken (see 75a12466).`);

    // Check 3: LIT > 0 (triangle actually rendered)
    const lit = await page.evaluate(() => window.__lit);
    checkGreaterThan(lit, 0,
      `D3D11 triangle must render non-zero pixels (LIT=${lit}). ` +
      `If 0, check: (1) BUFFER_DATA records in V2 frames, ` +
      `(2) buffer cache key consistency, (3) shader translation.`);
  }
}
