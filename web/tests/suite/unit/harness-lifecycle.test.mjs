import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { ProbeTest, terminalText, withTimeout } from '../lib/probe-base.mjs';

test('completed browser deadlines do not keep a Node process alive', async () => {
  const module = new URL('../lib/probe-base.mjs', import.meta.url).href;
  const result = await promisify(execFile)(process.execPath, ['--input-type=module', '-e',
    `import {withTimeout} from ${JSON.stringify(module)}; console.log(await withTimeout(Promise.resolve('done'),60000,'fixture'));`], { timeout: 3000 });
  assert.equal(result.stdout.trim(), 'done');
});

test('a hung operation reports the original deadline and a specific failure', async () => {
  await assert.rejects(withTimeout(new Promise(() => {}), 20, 'guest boot'), /guest boot timed out after 20ms/);
});

test('Wine terminal escapes match guest sentinels while the raw log remains intact', async () => {
  const probe = new ProbeTest({ tag: 'log-control' });
  const page = new EventEmitter();
  probe._browser = { newPage: async () => page };
  await probe.newPage();
  const raw = '\x1b[?25lvkboot\x1b[?25h';
  page.emit('console', { type: () => 'log', text: () => raw });
  assert.equal(probe.countConsole(/^log: vkboot$/), 1);
  assert.equal(probe.rawConsoleLines[0], 'log: ' + raw);
  assert.equal(terminalText('log: hello\x1b[1Cworld'), 'log: hello world');
  page.emit('console', { type: () => 'log', text: () => 'fixture path /vkboot/ is not an executed PE' });
  assert.equal(probe.countConsole(/^log: vkboot$/), 1);
});
