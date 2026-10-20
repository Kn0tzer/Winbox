import { test } from 'node:test';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { runInNewContext } from 'node:vm';
import { check } from '../lib/assert.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const shimPath = join(repoRoot, 'project/emscripten/boxedwine-audio-callback-shim.js');

function makeAudioContext() {
  class FakeNode {
    constructor(bufferSize) {
      this.bufferSize = bufferSize;
      this.connected = false;
      this._onaudioprocess = null;
    }
    set onaudioprocess(handler) { this._onaudioprocess = handler; }
    get onaudioprocess() { return this._onaudioprocess; }
    connect() { this.connected = true; }
    fire(length, channels = 2) {
      const outputBuffer = {
        length,
        numberOfChannels: channels,
        channels: Array.from({ length: channels }, () => new Float32Array(length)),
        getChannelData(index) { return this.channels[index]; },
      };
      this._onaudioprocess?.({ outputBuffer });
      return outputBuffer;
    }
  }
  class FakeAudioContext {
    createScriptProcessor(bufferSize) { return new FakeNode(bufferSize); }
  }
  return { FakeAudioContext, FakeNode };
}

test('matching ScriptProcessor sizes keep SDL audio callbacks running', () => {
  const { FakeAudioContext } = makeAudioContext();
  const context = { AudioContext: FakeAudioContext };
  runInNewContext(readFileSync(shimPath, 'utf8'), { globalThis: context });
  const node = new context.AudioContext().createScriptProcessor(2048, 0, 2);
  let calls = 0;
  node.onaudioprocess = event => {
    calls++;
    event.outputBuffer.channels[0].fill(0.25);
  };
  const output = node.fire(2048);
  check(calls === 1, 'matching buffer size should call the SDL audio handler');
  check(output.channels[0][0] === 0.25, 'matching callback output should remain intact');
});

test('mismatched ScriptProcessor sizes are silent and never throw', () => {
  const { FakeAudioContext } = makeAudioContext();
  const context = { AudioContext: FakeAudioContext };
  runInNewContext(readFileSync(shimPath, 'utf8'), { globalThis: context });
  const node = new context.AudioContext().createScriptProcessor(2048, 0, 2);
  let calls = 0;
  node.onaudioprocess = () => { calls++; throw new Error('must not run for a mismatched buffer'); };
  let output;
  let thrown = null;
  try { output = node.fire(4096); } catch (error) { thrown = error; }
  check(thrown === null, `mismatched audio callback threw: ${thrown}`);
  check(calls === 0, 'mismatched output size should skip the SDL callback');
  check(output.channels.every(channel => channel.every(sample => sample === 0)),
    'mismatched output should be zero-filled');
});
