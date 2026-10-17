import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { spawn } from 'node:child_process';
import { ProbeTest, terminalText, withTimeout } from '../lib/probe-base.mjs';

test('completed browser deadlines do not keep a Node process alive', async () => {
  const module = new URL('../lib/probe-base.mjs', import.meta.url).href;
  const result = await new Promise((resolve, reject) => {
    const child = spawn(process.execPath, ['--input-type=module', '-e',
      `import {withTimeout} from ${JSON.stringify(module)}; console.log(await withTimeout(Promise.resolve('done'),60000,'fixture'));`]);
    let stdout = '', stderr = '', completed = false;
    const fail = message => { child.kill('SIGKILL'); reject(new Error(message)); };
    // Loading Playwright's module graph can take several seconds under build
    // load. Measure the original three-second exit bound after completion.
    let timer = setTimeout(() => fail('fixture did not finish importing'), 20000);
    child.stdout.on('data', chunk => {
      stdout += chunk;
      if (!completed && stdout.includes('done\n')) {
        completed = true;
        clearTimeout(timer);
        timer = setTimeout(() => fail('completed deadline kept the child alive'), 3000);
      }
    });
    child.stderr.on('data', chunk => { stderr += chunk; });
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('close', code => {
      clearTimeout(timer);
      if (code !== 0) reject(new Error(stderr || `fixture exited ${code}`));
      else resolve(stdout.trim());
    });
  });
  assert.equal(result, 'done');
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
