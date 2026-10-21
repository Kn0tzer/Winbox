import test from 'node:test';
import assert from 'node:assert/strict';
import { createRecoveringBridge, recoverBridgeCall, reportBridgeFailure, bridgeRecoveryStats, setBridgeRecoveryEnabled } from '../../../bridge-recovery.mjs';

test('bridge calls recover from sync and async errors with safe defaults', async () => {
  const messages = [];
  const start = bridgeRecoveryStats().caught;
  const bridge = createRecoveringBridge({
    createBuffer() { throw new Error('bad descriptor'); },
    drawArrays() { throw new Error('bad draw'); },
    getShaderInfoLog() { throw new Error('bad shader'); },
    async readPixels() { throw new Error('bad readback'); },
  }, { name: 'test-gl', log: message => messages.push(message) });

  assert.equal(bridge.createBuffer(), 0);
  assert.equal(bridge.drawArrays(), undefined);
  assert.equal(bridge.getShaderInfoLog(), '');
  assert.equal(await bridge.readPixels(), undefined);
  assert.equal(bridge.bwRecoverStats().caught, start + 4);
  assert.equal(messages.length, 4);
  assert.equal(bridge.createBuffer(), 0);
  assert.equal(messages.length, 4); // Same bridge failure is logged once.
});

test('repeated bridge errors count each call and log once', () => {
  const messages = [];
  const log = message => messages.push(message);
  const before = bridgeRecoveryStats();
  reportBridgeFailure('test-repeat', new Error('same'), log);
  reportBridgeFailure('test-repeat', new Error('same'), log);
  assert.deepEqual(messages, ['bridge-recover: test-repeat: same']);
  assert.equal(bridgeRecoveryStats().caught, before.caught + 2);
});

test('wrapped standalone callback catches a rejected promise', async () => {
  const messages = [];
  const call = recoverBridgeCall('test-async', async () => { throw new Error('rejected'); }, 0, message => messages.push(message));
  assert.equal(await call(), 0);
  assert.equal(messages.length, 1);
});

test('recover=0 keeps bridge calls strict', () => {
  const bridge = createRecoveringBridge({ createBuffer() { throw new Error('strict'); } }, { name: 'test-strict', log() {} });
  setBridgeRecoveryEnabled(false);
  try {
    assert.throws(() => bridge.createBuffer(), /strict/);
  } finally {
    setBridgeRecoveryEnabled(true);
  }
});
