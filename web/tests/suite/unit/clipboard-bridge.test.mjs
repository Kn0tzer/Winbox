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

test('a throwing guest call is contained, reported once per direction, and leaves the event alone', () => {
  const listeners = new Map();
  const document = { addEventListener: (name, callback) => listeners.set(name, callback) };
  const module = { ccall: () => { throw Error('runtime gone'); } };
  connectRuntimeClipboard(document, () => module);
  const warnings = [];
  const originalWarn = console.warn;
  console.warn = (...args) => warnings.push(args.map(String).join(' '));
  let prevented = 0;
  try {
    const copyEvent = { clipboardData: { setData() { throw Error('must not write'); } }, preventDefault: () => { prevented++; } };
    listeners.get('copy')(copyEvent);
    listeners.get('copy')(copyEvent);
    listeners.get('paste')({ clipboardData: { getData: () => 'host text' }, preventDefault: () => { prevented++; } });
  } finally {
    console.warn = originalWarn;
  }
  assert.equal(prevented, 0);
  assert.equal(warnings.length, 2);
  assert.match(warnings[0], /BW64-FAIL clipboard-bridge op=get error=runtime gone/);
  assert.match(warnings[1], /BW64-FAIL clipboard-bridge op=set error=runtime gone/);
});
