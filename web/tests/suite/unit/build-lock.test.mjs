import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, existsSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { spawnSync } from 'node:child_process';

const wrapper = resolve('tools/bw64-build.sh');

async function withLock(run) {
  const folder = mkdtempSync(join(tmpdir(), 'bw64-lock-test-'));
  const lock = join(folder, 'build.lock.d');
  const previous = process.env.BUILD_LOCK_DIR;
  process.env.BUILD_LOCK_DIR = lock;
  try {
    const module = await import(`${pathToFileURL(resolve('web/tests/build-lock.mjs'))}?test=${encodeURIComponent(folder)}`);
    await run({ folder, lock, module });
  } finally {
    if (previous === undefined) delete process.env.BUILD_LOCK_DIR;
    else process.env.BUILD_LOCK_DIR = previous;
    rmSync(folder, { recursive: true, force: true });
  }
}

test('an aged live builder remains locked in both readers and the build wrapper', async () => {
  await withLock(({ lock, module }) => {
    mkdirSync(lock);
    writeFileSync(join(lock, 'info'), `${process.pid}|1|test\n`);
    assert.equal(module.buildInProgress(), true);
    const check = spawnSync('bash', [wrapper, '--check'], { encoding: 'utf8' });
    assert.equal(check.status, 10, check.stderr);
    const build = spawnSync('bash', [wrapper, '--export-only'], {
      encoding: 'utf8', env: { ...process.env, BW64_BUILD_WAIT_SECONDS: '0' },
    });
    assert.equal(build.status, 1);
    assert.equal(existsSync(join(lock, 'info')), true);
  });
});

test('new and malformed lock records remain busy instead of being removed', async () => {
  await withLock(({ lock, module }) => {
    mkdirSync(lock);
    assert.equal(module.buildInProgress(), true);
    for (const record of ['invalid record\n', '999999999999999999999999|1|invalid PID\n']) {
      writeFileSync(join(lock, 'info'), record);
      assert.equal(module.buildInProgress(), true);
      assert.equal(spawnSync('bash', [wrapper, '--check']).status, 10);
    }
    assert.equal(existsSync(lock), true);
  });
});

test('a validated dead builder can be cleaned up', async () => {
  await withLock(({ lock, module }) => {
    mkdirSync(lock);
    writeFileSync(join(lock, 'info'), '2147483647|1|dead\n');
    assert.equal(spawnSync('bash', [wrapper, '--check']).status, 11);
    assert.equal(module.buildInProgress(), false);
    assert.equal(existsSync(lock), false);
  });
});

test('live and malformed probe slots reject a build before any asset mutation', async () => {
  await withLock(({ folder, lock }) => {
    const slots = join(folder, 'slots');
    mkdirSync(slots);
    for (const contents of [`${process.pid}\n`, `${process.pid}|20261013\n`]) {
      writeFileSync(join(slots, 'slot-0'), contents);
      const build = spawnSync('bash', [wrapper, '--export-only'], {
        encoding: 'utf8',
        env: { ...process.env, PROBE_SLOT_DIR: slots, BW64_BUILD_FORCE: '0', BW64_BUILD_WAIT_MIN: '0' },
      });
      assert.equal(build.status, 1);
      assert.match(build.stderr, /probe (activity|slot)/);
      assert.equal(existsSync(join(slots, 'slot-0')), true);
      assert.equal(existsSync(lock), false);
    }
  });
});
