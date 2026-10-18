import test from 'node:test';
import assert from 'node:assert/strict';
import { startupProgress, startupFailure } from '../startup.mjs';
test('startup describes the stage before the first frame', () => {
  const progress = startupProgress('Running...', 1000, false, 2000, 0);
  assert.match(progress.detail, /first frame/i);
  assert.equal(progress.activity, '');
});
test('logs describe activity without claiming that background output proves launch health', () => {
  assert.equal(startupProgress('Running...', 15000, false, 15000, 0).activity, '');
  assert.match(startupProgress('Running...', 16000, false, 16001, 0).activity, /logs.*not.*confirm/i);
  assert.doesNotMatch(startupProgress('Running...', 12000, false, 16001, 0).activity, /Not frozen/);
  assert.match(startupProgress('Running...', 12000, false, 16001, 0).activity, /logs.*4 seconds/i);
});
test('long silent startup is possible and is not a timeout failure', () => {
  const progress = startupProgress('Running...', 1000, false, 241000, 0);
  assert.match(progress.activity, /quiet.*loading/i);
  assert.doesNotMatch(progress.activity, /frozen|failed|restart\./i);
});
test('visible frames clear startup reassurance and downloads retain progress', () => {
  assert.equal(startupProgress('Running...', 16000, true, 16001, 0).activity, '');
  assert.equal(startupProgress('Downloading runtime: 25%', 16000, false, 16001, 0).detail, 'Downloading runtime: 25%');
  assert.match(startupProgress('Loading rootfs 70%', null, false, 5000, 0).title, /Windows files/);
});
test('terminal target loader errors fail even when Wine services continue logging', () => {
  const fatal = '0024:err:module:LdrInitializeThunk Importing dlls for L"Z:\\home\\username\\apps\\Game.exe" failed, status c0000135';
  assert.match(startupFailure(fatal, 'nested/Game.exe'), /required DLL/);
  assert.equal(startupFailure(fatal, 'Other.exe'), null);
  assert.equal(startupFailure('fixme:service:scmdatabase_autostart_services Auto-start service failed to start', 'Game.exe'), null);
  assert.equal(startupFailure('Game.exe: compiling shaders', 'Game.exe'), null);
  assert.match(startupFailure('Boot failed: download failed'), /download failed/);
});
