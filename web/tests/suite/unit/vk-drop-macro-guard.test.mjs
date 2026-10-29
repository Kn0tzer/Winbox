import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

// VK64_DROP_IF macro-class guard (register row: VK64_DROP_IF macro class,
// 2026-10-25). The macro used to wrap its action in do{}while(0); a
// continue/break action then binds to the macro's own do-while, not the
// caller's loop/switch, so 58 guard sites logged their drop and then fell
// through and used the value they meant to reject (row 380's fallback scan
// was the first proven instance). The macro is now an if/else{} statement:
// flow control binds to the caller, and the trailing else{} turns use as an
// unbraced if-arm into a compile error instead of a silent else re-bind.
// These tests pin (1) the define's shape, (2) its compiled semantics against
// the REAL define line extracted from the bridge, and (3) the exact census
// of guarded sites, so neither the define nor any site can quietly regress.

const root = new URL('../../../../', import.meta.url);
const bridge = readFileSync(new URL('source/vulkan/vk64bridge.cpp', root), 'utf8');

const DEFINE_RE = /^#define VK64_DROP_IF\(action, \.\.\.\) .*$/m;
const defineLine = (bridge.match(DEFINE_RE) || [])[0];

test('VK64_DROP_IF define is the flow-safe if/else{} form, defined once', () => {
  assert.ok(defineLine, 'VK64_DROP_IF define not found');
  assert.equal(defineLine,
    '#define VK64_DROP_IF(action, ...) if (__VA_ARGS__) { vk64DropLog(#__VA_ARGS__, __LINE__); action; } else {}');
  assert.equal((bridge.match(/^#define VK64_DROP_IF/gm) || []).length, 1);
  assert.doesNotMatch(bridge, /^#define .*while \(0\)/m,
    'no bridge macro may wrap its action in do{}while(0)');
});

// Extract every VK64_DROP_IF invocation outside comments/strings, as
// [action, condition] with whitespace canonicalised (the same view the
// preprocessor has). The #define line itself is excluded.
function dropSites(src) {
  const sites = [];
  let i = 0, lineStart = true;
  const n = src.length;
  while (i < n) {
    const c = src[i];
    if (c === '\n') { lineStart = true; i++; continue; }
    if (c === '/' && src[i + 1] === '/') { while (i < n && src[i] !== '\n') i++; continue; }
    if (c === '/' && src[i + 1] === '*') {
      i += 2;
      while (i + 1 < n && !(src[i] === '*' && src[i + 1] === '/')) i++;
      i += 2; continue;
    }
    if (c === '"' || c === "'") {
      const q = c; i++;
      while (i < n && src[i] !== q) { if (src[i] === '\\') i++; i++; }
      i++; continue;
    }
    if (src.startsWith('VK64_DROP_IF', i) && !/[\w]/.test(src[i - 1] || ' ')) {
      // skip the #define line: `#define VK64_DROP_IF(action, ...)`
      const lineBegin = src.lastIndexOf('\n', i) + 1;
      if (src.slice(lineBegin, i).includes('#define')) { i += 12; continue; }
      let j = i + 12;
      while (src[j] === ' ' || src[j] === '\t') j++;
      if (src[j] === '(') {
        let depth = 0, k = j;
        const args = []; let cur = '';
        while (k < n) {
          const ch = src[k];
          if (ch === '"' || ch === "'") {
            const q = ch; cur += ch; k++;
            while (k < n && src[k] !== q) { if (src[k] === '\\') { cur += src[k]; k++; } if (k < n) { cur += src[k]; k++; } }
            if (k < n) { cur += src[k]; k++; }
            continue;
          }
          if (ch === '(') { depth++; if (depth > 1) cur += ch; k++; continue; }
          if (ch === ')') { depth--; if (depth === 0) { k++; break; } cur += ch; k++; continue; }
          if (ch === ',' && depth === 1) { args.push(cur); cur = ''; k++; continue; }
          cur += ch; k++;
        }
        args.push(cur);
        const action = args[0].trim().replace(/\s+/g, ' ');
        const cond = args.slice(1).join(',').trim().replace(/\s+/g, ' ');
        sites.push([action, cond]);
        i = k; continue;
      }
    }
    if (c !== ' ' && c !== '\t') lineStart = false;
    i++;
  }
  return sites;
}

test('guarded-site census: 257 sites, flow-control sites exactly as audited', () => {
  const sites = dropSites(bridge);
  assert.equal(sites.length, 257, 'total VK64_DROP_IF call sites changed');
  const byAction = {};
  for (const [a] of sites) byAction[a] = (byAction[a] || 0) + 1;
  assert.deepEqual(byAction,
    { 'return 0': 162, 'return': 28, 'return nullptr': 1, 'continue': 63, 'break': 3 });
  // The 58 continue/break sites from the macro-class audit (action, condition)
  // with multiplicities (mirrored handlers share condition text). Any site
  // added, removed or reworded must update this pin and the register row.
  const pinned = {
    'break ||| first >= VK64_MAX_SETS': 1,
    'break ||| out.n >= 16': 2,
    'continue ||| !buf || !buf->mem': 1,
    'continue ||| !cb': 5,
    'continue ||| !heap': 2,
    'continue ||| !mem || !mem->va': 1,
    'continue ||| !sb || !cp.size': 1,
    'continue ||| !rb.w || !rb.h || rb.w > 4096 || rb.h > 4096': 1,
    'continue ||| !s': 2,
    'continue ||| !sec': 1,
    'continue ||| !secondary': 1,
    'continue ||| !si || !di': 1,
    'continue ||| !src || !dst': 2,
    'continue ||| !vb || k.vbOffsets[i] >= vb->size': 1,
    'continue ||| !waitForReadbackFlag(pend.flagPtr, 10000)': 1,
    'continue ||| dstBase >= memEnd': 1,
    'continue ||| dOff + pxBytes > dEndB': 2,
    'continue ||| dRel < dst->memOff || dRel >= dmem->size': 2,
    'continue ||| dRel < dstB->memOff || dRel >= dmem->size': 1,
    'continue ||| dxAbs >= dst->w': 3,
    'continue ||| dyAbs >= dst->h': 3,
    'continue ||| me.offset + me.size > si.dataSize || me.size > 8 || me.size == 0': 1,
    'continue ||| me.offset > si.dataSize || me.size > si.dataSize - me.offset': 1,
    'continue ||| me.size != 1 && me.size != 2 && me.size != 4 && me.size != 8': 2,
    'continue ||| r.srcOffset >= smem->size || r.dstOffset >= dmem->size': 1,
    'continue ||| sOff + pxBytes > sEndB': 2,
    'continue ||| sOff >= sEnd || dOff >= dEnd': 3,
    'continue ||| sOff >= sEndB || dOff >= dEndB': 2,
    'continue ||| sRel < src->memOff || sRel >= smem->size': 2,
    'continue ||| sRel < srcB->memOff || sRel >= smem->size': 1,
    'continue ||| slot >= VK64_MAX_VERT_BINDINGS': 1,
    'continue ||| srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0': 3,
    'continue ||| sx < 0 || (U32)sx >= src->w': 3,
    'continue ||| sy < 0 || (U32)sy >= src->h': 3,
    'continue ||| sy >= src->h || dy >= dst->h': 4,
    'continue ||| w <= 0': 2,
  };
  const actual = {};
  for (const [a, c] of sites) {
    if (a !== 'continue' && a !== 'break') continue;
    const key = `${a} ||| ${c}`;
    actual[key] = (actual[key] || 0) + 1;
  }
  assert.deepEqual(actual, pinned);
});

test('compiled semantics of the real VK64_DROP_IF define', () => {
  assert.ok(defineLine, 'VK64_DROP_IF define not found');
  const directory = mkdtempSync(join(tmpdir(), 'vk-drop-macro-'));
  try {
    writeFileSync(join(directory, 'check.cpp'), `
#include <string>
#include <vector>
static std::vector<std::string> g_log;
static void vk64DropLog(const char* cond, int line) { g_log.push_back(cond); (void)line; }
${defineLine}
static int failures = 0;
#define CHECK(x) do { if (!(x)) failures++; } while (0)
static int fnReturn(int x) {
    VK64_DROP_IF(return 42, x > 10);
    return 7;
}
int main() {
    std::vector<int> visited;
    for (int i = 0; i < 5; i++) {
        VK64_DROP_IF(continue, i == 2);
        visited.push_back(i);
    }
    CHECK(visited == std::vector<int>({0, 1, 3, 4}));       // continue skips the iteration
    int last = -1, iters = 0;
    for (int i = 0; i < 10; i++) {
        VK64_DROP_IF(break, i == 3);
        last = i; iters++;
    }
    CHECK(iters == 3 && last == 2);                          // break exits the loop
    int stage = 0;
    switch (1) {
        case 1: {
            VK64_DROP_IF(break, stage == 0);
            stage = 100;
        }
        case 2: stage = 200; break;
        default: break;
    }
    CHECK(stage == 0);                                       // break exits the switch, no fallthrough
    CHECK(fnReturn(20) == 42 && fnReturn(5) == 7);           // return action unchanged
    int evals = 0;
    auto cond = [&]() { evals++; return true; };
    for (int i = 0; i < 3; i++) { VK64_DROP_IF(continue, cond()); }
    CHECK(evals == 3);                                       // condition evaluated exactly once
    int inner = 0, outer = 0;
    for (int a = 0; a < 2; a++) {
        outer++;
        for (int b = 0; b < 3; b++) {
            VK64_DROP_IF(continue, b == 1);
            inner++;
        }
    }
    CHECK(outer == 2 && inner == 4);                         // continue binds to the inner loop
    int jumped = 0;
    for (int i = 0; i < 3; i++) { VK64_DROP_IF(goto done, i == 1); }
  done:
    jumped = 1;
    CHECK(jumped == 1);                                      // goto action unchanged
    // log hits: continue 1, break 1, switch 1, fnReturn(20) 1, cond-loop 3,
    // nested 2, goto 1 = 10, each with the stringised condition.
    CHECK(g_log.size() == 10);
    CHECK(g_log[0] == "i == 2");
    return failures;
}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Wextra', join(directory, 'check.cpp'), '-o', join(directory, 'check')]);
    execFileSync(join(directory, 'check'));

    // Misuse as an unbraced if-arm must be a COMPILE ERROR (the trailing
    // else{} leaves the caller's else with nothing to bind to), never a
    // silent re-bind. Under the old do{}while(0) define this compiled fine.
    writeFileSync(join(directory, 'misuse.cpp'), `
static void vk64DropLog(const char*, int) {}
${defineLine}
int f(int x) {
    if (x) VK64_DROP_IF(return 1, x > 0);
    else return 2;
    return 3;
}
int main() { return f(1); }
`);
    assert.throws(() =>
      execFileSync('c++', ['-std=c++17', join(directory, 'misuse.cpp'), '-o', join(directory, 'misuse')], { stdio: 'pipe' }),
      'unbraced if-arm use of VK64_DROP_IF must not compile');
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
