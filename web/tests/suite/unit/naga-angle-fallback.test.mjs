import test from 'node:test';
import assert from 'node:assert/strict';
import { installNagaCapture } from '../../../naga-rt.mjs';

test('unsupported GLSL versions still reach ANGLE unchanged and are recorded as fallback observations', async () => {
  const previous = {
    installed: globalThis.__bw64NagaInstalled,
    stats: globalThis.bwNagaStats,
    dump: globalThis.bwNagaDump,
    ring: globalThis.__bw64NagaRing,
    context: globalThis.WebGL2RenderingContext,
  };
  class FakeWebGL2 {
    shaderSource(shader, source) { this.last = { shader, source }; return 'ANGLE received'; }
  }
  globalThis.WebGL2RenderingContext = FakeWebGL2;
  globalThis.__bw64NagaInstalled = false;
  try {
    installNagaCapture();
    const gl = new FakeWebGL2();
    const shader = {};
    const unsupported = '#version 450\nvoid main(){gl_Position=vec4(0.0);}';
    assert.equal(gl.shaderSource(shader, unsupported), 'ANGLE received');
    assert.deepEqual(gl.last, { shader, source: unsupported });
    await new Promise(resolve => setTimeout(resolve, 0));
    const record = globalThis.__bw64NagaRing.find(item => item.type === 'unsupported');
    assert.equal(record.src, unsupported);
    assert.equal(record.reason, 'unsupported-version');
    assert.equal(globalThis.bwNagaStats().broken, 0);
  } finally {
    Object.assign(globalThis, {
      __bw64NagaInstalled: previous.installed,
      bwNagaStats: previous.stats,
      bwNagaDump: previous.dump,
      __bw64NagaRing: previous.ring,
      WebGL2RenderingContext: previous.context,
    });
  }
});
