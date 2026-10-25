import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('legacy XInput raw motion distinguishes pointer-lock deltas from recentered absolute coordinates', () => {
  const server = readFileSync(new URL('../../../../source/x11/xserver.cpp', import.meta.url), 'utf8');
  const input = readFileSync(new URL('../../../../platform/sdl/knativeinputSDL.cpp', import.meta.url), 'utf8');
  const helper = server.match(/struct X11RelativeMotion \{[\s\S]*?static X11RelativeMotion x11RelativeMotion\([\s\S]*?\n}/)?.[0];
  assert.ok(helper);
  assert.match(input, /SDL_GetRelativeMouseMode\(\) == SDL_TRUE/);
  assert.match(input, /relative \? e->motion\.xrel : e->motion\.x/);
  assert.match(input, /x = x \* 100 \/ scaleX/);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-legacy-relative-mouse-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using S32=int32_t;
${helper}
int main(){auto locked=x11RelativeMotion(-4,7,true,400,300);assert(locked.x==-4&&locked.y==7&&!locked.shouldWarp);auto absolute=x11RelativeMotion(405,292,false,400,300);assert(absolute.x==5&&absolute.y==-8&&absolute.shouldWarp&&absolute.centerX==400&&absolute.centerY==300);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
