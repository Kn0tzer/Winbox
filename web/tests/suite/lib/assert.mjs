// web/tests/suite/lib/assert.mjs — Programmatic PASS/FAIL assertion helpers.
//
// Every test in the suite MUST use these (or node:assert/strict directly) so
// that failures print the specific assertion that failed, not just "FAIL".
//
// Usage:
//   import { check, checkEqual, checkGreaterThan } from '../lib/assert.mjs';
//
//   check(condition, 'vertex buffer had 4MB of data');
//   checkEqual(actual, expected, 'LIT pixel count');
//   checkGreaterThan(lit, 0, 'D3D11 triangle rendered pixels');
//
// Each helper throws an Error with a clear message on failure. The runner
// catches these and reports per-test PASS/FAIL.

export function check(cond, message) {
  if (!cond) {
    throw new Error(`ASSERT FAILED: ${message}`);
  }
}

export function checkEqual(actual, expected, message) {
  if (actual !== expected) {
    throw new Error(
      `ASSERT FAILED: ${message}\n  expected: ${JSON.stringify(expected)}\n  actual:   ${JSON.stringify(actual)}`
    );
  }
}

export function checkNotEqual(actual, unexpected, message) {
  if (actual === unexpected) {
    throw new Error(
      `ASSERT FAILED: ${message}\n  value should not equal: ${JSON.stringify(unexpected)}`
    );
  }
}

export function checkGreaterThan(actual, threshold, message) {
  if (!(actual > threshold)) {
    throw new Error(
      `ASSERT FAILED: ${message}\n  expected > ${threshold}, got: ${actual}`
    );
  }
}

export function checkGreaterThanOrEqual(actual, threshold, message) {
  if (!(actual >= threshold)) {
    throw new Error(
      `ASSERT FAILED: ${message}\n  expected >= ${threshold}, got: ${actual}`
    );
  }
}

export function checkLessThan(actual, threshold, message) {
  if (!(actual < threshold)) {
    throw new Error(
      `ASSERT FAILED: ${message}\n  expected < ${threshold}, got: ${actual}`
    );
  }
}

export function checkIncludes(haystack, needle, message) {
  const found = typeof haystack === 'string'
    ? haystack.includes(needle)
    : Array.isArray(haystack) ? haystack.includes(needle) : false;
  if (!found) {
    throw new Error(
      `ASSERT FAILED: ${message}\n  expected to find: ${JSON.stringify(needle)}`
    );
  }
}

export function checkMatches(str, regex, message) {
  if (!regex.test(str)) {
    throw new Error(
      `ASSERT FAILED: ${message}\n  expected to match: ${regex}\n  string was: ${str.slice(0, 500)}`
    );
  }
}
