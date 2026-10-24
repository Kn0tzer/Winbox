// Register evidence (docs/known-gaps/vulkan.md "web replay drops"): every command the page replay
// cannot honour is counted per reason in snapshot().dropReasons, bumps stats.unsupported/skippedDraws
// where it changes output, and is logged through console.warn ("v2 dropped <reason> xN").
import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

const shaders = () => new Map([['vs', {code: new Uint8Array(4)}], ['fs', {code: new Uint8Array(4)}]]);
const frame = passes => ({frameNo: 1, width: 4, height: 4, shaders: shaders(), images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [], passes});
const att = (format = 37) => ({format, isDepth: 0, samples: 1, loadOp: 1, storeOp: 0});
const pass = (ops, attachments = [att()]) => ({w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments, ops});
const draw = {op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0};

async function replay(passes) {
  const gpu = createValidationDevice();
  const warns = [];
  const warn = console.warn; console.warn = (...a) => warns.push(a.join(' '));
  try {
    const r = await createVkRenderer2(gpu.canvas, {translate: async () => 'fn main() {}'});
    await r.render(frame(passes));
    const snap = r.snapshot();
    r.destroy();
    return {snap, warns};
  } finally { console.warn = warn; gpu.restore(); }
}

test('unknown replay op is counted, logged and flagged unsupported', async () => {
  const {snap, warns} = await replay([pass([{op: 'noSuchOp'}])]);
  assert.equal(snap.dropReasons['unknown-replay-op'], 1);
  assert.ok(snap.unsupported >= 1);
  assert.ok(warns.some(w => /v2 dropped unknown-replay-op x1/.test(w)), warns.join('\n'));
});

test('unsupported colour attachment format is counted, logged and flagged unsupported', async () => {
  const {snap, warns} = await replay([pass([], [att(0x7ffffff0)])]);
  assert.equal(snap.dropReasons['pass-color-format-unsupported'], 1);
  assert.ok(snap.unsupported >= 1);
  assert.ok(warns.some(w => /v2 dropped pass-color-format-unsupported x1/.test(w)));
});

test('unsupported vertex format skips the draw loudly', async () => {
  const pipe = {pipeId: 'badvf', topology: 3, vb: [{binding: 0, stride: 16, inputRate: 0}], va: [{location: 0, binding: 0, format: 0x7ffffff0, offset: 0}], vsHash: 'vs', fsHash: 'fs'};
  const {snap, warns} = await replay([pass([{op: 'bindPipe', pipe}, draw])]);
  assert.equal(snap.dropReasons['vertex-format'], 1);
  assert.ok(snap.skippedDraws >= 1);
  assert.ok(warns.some(w => /v2 dropped vertex-format x1/.test(w)));
});

test('drop log is rate limited to powers of two after the first four', async () => {
  const ops = Array.from({length: 20}, () => ({op: 'noSuchOp'}));
  const {snap, warns} = await replay([pass(ops)]);
  assert.equal(snap.dropReasons['unknown-replay-op'], 20);
  const logged = warns.filter(w => /v2 dropped unknown-replay-op/.test(w)).map(w => +/x(\d+)/.exec(w)[1]);
  assert.deepEqual(logged, [1, 2, 3, 4, 8, 16]);
});
