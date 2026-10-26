import test from 'node:test';
import assert from 'node:assert/strict';
import { connectRuntimeClipboard } from '../../../clipboard-bridge.mjs';

test('browser copy reads guest X clipboard and paste stages host text for guest selections', () => {
  const listeners = new Map();
  const document = { addEventListener: (name, callback) => listeners.set(name, callback) };
  const calls = [];
  const module = { ccall: (...args) => {
    calls.push(args);
    return args[0] === 'bw64_clipboard_get' ? 'guest copied text' : undefined;
  } };
  connectRuntimeClipboard(document, () => module);
  let prevented = false;
  const copyData = { values: {}, setData: (type, value) => { copyData.values[type] = value; } };
  listeners.get('copy')({ clipboardData: copyData, preventDefault: () => { prevented = true; } });
  assert.equal(prevented, true);
  assert.equal(copyData.values['text/plain'], 'guest copied text');
  assert.deepEqual(calls[0], ['bw64_clipboard_get', 'string', [], []]);

  prevented = false;
  const pasteData = { getData: type => type === 'text/plain' ? 'host pasted text' : '' };
  listeners.get('paste')({ clipboardData: pasteData, preventDefault: () => { prevented = true; } });
  assert.equal(prevented, true);
  assert.deepEqual(calls[1], ['bw64_clipboard_set', null, ['string'], ['host pasted text']]);
});

test('clipboard bridge leaves events untouched when no runtime module or clipboard text exists', () => {
  const listeners = new Map();
  const document = { addEventListener: (name, callback) => listeners.set(name, callback) };
  connectRuntimeClipboard(document, () => null);
  let prevented = false;
  listeners.get('copy')({ clipboardData: { setData() { throw Error('unexpected write'); } }, preventDefault: () => { prevented = true; } });
  listeners.get('paste')({ clipboardData: { getData: () => '' }, preventDefault: () => { prevented = true; } });
  assert.equal(prevented, false);
});
