import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../../../../', import.meta.url));

test('serialization totals are accumulated once after all command buffers', () => {
  const source = readFileSync(join(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');
  const fn = source.slice(source.indexOf('void serializeSubmitV2('), source.indexOf('// Physical device:', source.indexOf('void serializeSubmitV2(')));
  const addAt = fn.indexOf('pa.accBufData += vkprofBufT;');
  const lastFrameAt = fn.indexOf('g_frameNo++;');
  assert.ok(addAt > lastFrameAt, 'per-call cumulative counters must be added after the final command-buffer iteration');
  assert.equal((fn.match(/pa\.accSer2 \+=/g) || []).length, 1);
});

test('nested exclusive encode stages conserve elapsed wall, including repeated phases and disabled scopes', () => {
  const dir = mkdtempSync(join(tmpdir(), 'vk-encode-profile-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include "source/vulkan/vk_encode_profile.h"
#include <cassert>
struct Clock { static inline double t=0; static inline int reads=0; static double now(){++reads;return t;} };
int main(){
 using P=vkencode::Profile<Clock>;P p;
 {
  P::Scope total(p,vkencode::Submit,true); Clock::t=1;
  { P::Scope desc(p,vkencode::Descriptors,true); Clock::t=3;
    { P::Scope image(p,vkencode::Image,true);Clock::t=4;
      {P::Scope lookup(p,vkencode::ImageView,true);Clock::t=7;}
      Clock::t=9;
    }
    Clock::t=13;
  }
  Clock::t=17;
 }
 assert(p.exclusive[vkencode::Submit]==5);assert(p.exclusive[vkencode::Descriptors]==6);
 assert(p.exclusive[vkencode::Image]==3);assert(p.exclusive[vkencode::ImageView]==3);
 assert(p.inclusive[vkencode::Submit]==17);
 double sum=0;for(double v:p.exclusive)sum+=v;assert(sum==17);assert(p.rootMs==17);
 {P::Scope second(p,vkencode::Submit,true);Clock::t=20;}
 sum=0;for(double v:p.exclusive)sum+=v;assert(sum==20);assert(p.rootMs==20);assert(p.calls[vkencode::Submit]==2);
 auto reads=Clock::reads;{P::Scope disabled(p,vkencode::Image,false);Clock::t=90;}
 assert(Clock::reads==reads);assert(p.calls[vkencode::Image]==1);
 {P::Scope stopped(p,vkencode::Submit,true);Clock::t=91;stopped.stop();auto n=Clock::reads;stopped.stop();assert(Clock::reads==n);}
 assert(p.inclusive[vkencode::Submit]==21);assert(p.rootMs==21);assert(p.calls[vkencode::Submit]==3);
 p.reset();for(double v:p.exclusive)assert(v==0);for(auto v:p.calls)assert(v==0);
}
`);
    execFileSync('c++', ['-std=c++17', '-I', root, join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('profile report retains repeated-run counts/variance and rejects missing or nonconserving evidence', () => {
  const dir = mkdtempSync(join(tmpdir(), 'vk-profile-summary-'));
  try {
    const script = join(root, 'tools/bench/vk-profile-summary.py');
    const log = join(dir, 'sample.log');
    writeFileSync(log, [100, 200, 300].map((wall, frame) => `vk64: VKPROF frame=${frame} wall=${wall}ms submit=100.000ms imgdata=20.000ms guest=30.000ms imgC=4 imgB=0/0\nvk64: VKPROF2 frame=${frame} {"rootMs":100,"stages":{"serialize":{"exclusiveMs":80,"inclusiveMs":100,"calls":1},"image":{"exclusiveMs":20,"inclusiveMs":20,"calls":4}}}`).join('\n'));
    const result = JSON.parse(execFileSync('python3', [script, '--warmup', '1', log, log], { encoding: 'utf8' }));
    assert.equal(result.runs.length, 2); assert.equal(result.runs[0].observedFrames, 3); assert.equal(result.runs[0].steadyFrames, 2);
    assert.equal(result.runs[0].metrics.wall.mean, 250); assert.equal(result.runs[0].metrics.imgBHashed.mean, 0);
    assert.ok(result.runs[0].metrics.wall.sampleStddev > 70); assert.equal(result.runMeanVariance.wall.sampleStddev, 0);
    assert.equal(result.runs[0].detailedFrames, 2); assert.match(result.legacyGuestMeaning, /not an interpreter/);
    writeFileSync(log, 'no guest presents');
    assert.throws(() => execFileSync('python3', [script, '--warmup', '0', log], { stdio: 'pipe' }), /Command failed/);
    writeFileSync(log, 'vk64: VKPROF frame=0 wall=1ms\nvk64: VKPROF2 frame=0 {"rootMs":10,"stages":{"image":{"exclusiveMs":11,"inclusiveMs":11,"calls":1}}}');
    assert.throws(() => execFileSync('python3', [script, '--warmup', '0', log], { stdio: 'pipe' }), /Command failed/);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
