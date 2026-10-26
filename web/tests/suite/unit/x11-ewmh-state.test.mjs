import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('EWMH state requests implement add, remove, toggle, and deduplicate atoms', () => {
  const src = readFileSync(new URL('../../../../source/x11/xwindow.cpp', import.meta.url), 'utf8');
  const helper = src.match(/static std::vector<U32> x11ApplyNetWmState\([\s\S]*?\n}/)?.[0];
  assert.ok(helper);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-ewmh-state-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
using U32=uint32_t;
${helper}
int main(){U32 add[]={101,113};
 auto a=x11ApplyNetWmState({},1,add,2);assert((a==std::vector<U32>{101,113}));
 auto b=x11ApplyNetWmState(a,1,add,2);assert(b==a);
 auto c=x11ApplyNetWmState(a,2,add,1);assert((c==std::vector<U32>{113}));
 U32 remove[]={113};auto d=x11ApplyNetWmState(c,0,remove,1);assert(d.empty());}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('fullscreen EWMH transitions restore window geometry and advertise supported atoms', () => {
  const window = readFileSync(new URL('../../../../source/x11/xwindow.cpp', import.meta.url), 'utf8');
  const server = readFileSync(new URL('../../../../source/x11/xserver.cpp', import.meta.url), 'utf8');
  assert.match(window, /restoreRect\.x = left; restoreRect\.y = top;[\s\S]*?isFullScreen = true;[\s\S]*?moveResize\(0, 0, root->width\(\), root->height\(\)\)/);
  assert.match(window, /isFullScreen = false;[\s\S]*?moveResize\(restoreRect\.x, restoreRect\.y, restoreRect\.width, restoreRect\.height\)/);
  assert.match(server, /const U32 supported\[\] = \{_NET_WM_STATE, _NET_WM_STATE_FULLSCREEN, _NET_WM_STATE_ABOVE,[\s\S]*?_NET_ACTIVE_WINDOW, _NET_WM_NAME, _NET_WM_ICON\}/);
});

test('XConfigureWindow sibling requests reorder children and reject invalid sibling windows', () => {
  const src = readFileSync(new URL('../../../../source/x11/xwindow.cpp', import.meta.url), 'utf8');
  const configure = src.match(/int XWindow::configure\(U32 mask, XWindowChanges\* changes\) \{[\s\S]*?\n}/)?.[0];
  assert.ok(configure);
  assert.doesNotMatch(configure, /CWSibling[\s\S]*?kpanic/);
  assert.match(configure, /if \(changes->sibling && !sibling\) return BadWindow/);
  assert.match(configure, /sibling->parent != parent/);
  assert.match(configure, /zchildren\.insert\(parent->zchildren\.begin\(\) \+ position, self\)/);
});
