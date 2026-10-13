import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// wineserver-measure-probe.mjs — Measure wineserver protocol overhead.
import { chromium } from '@playwright/test';
import { serve } from '../../../tools/browser/serve.mjs';
import { mkdir, writeFile } from 'node:fs/promises';
import { chmod } from 'node:fs/promises';

const TAG = process.env.TAG || 'ws-measure';
await mkdir('test-results', { recursive: true }).catch(() => {});
try { await chmod('test-results', 0o777); } catch {}

const server = await serve('dist');
const url = `http://127.0.0.1:${server.address().port}/?test=d3d11-triangle`;

const browser = await chromium.launch({
    executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1234/chrome-linux/chrome',
    args: ['--use-gl=swiftshader', '--no-sandbox']
});
const page = await browser.newPage();

const logs = [];
page.on('console', m => {
    const text = m.text();
    logs.push(`[console.${m.type()}] ${text}`);
    if (text.includes('WINESERVER_STATS')) {
        console.log(`>>> ${text}`);
    }
});
page.on('pageerror', e => logs.push(`[pageerror] ${e.message}`));

console.log(`Navigating to ${url}`);
await page.goto(url, { timeout: 120000 });

console.log('Waiting for test completion (5 min)...');
try {
    await page.waitForFunction(() => window.__done === true, { timeout: 300000 });
    console.log('Test completed');
} catch (e) {
    console.log('Timeout, proceeding anyway');
}

console.log('Dumping wineserver stats...');
try {
    const result = await page.evaluate(() => {
        const M = window.Module;
        if (!M || typeof M.ccall !== 'function') {
            return 'ERROR: window.Module.ccall not available';
        }
        try {
            M.ccall('bw64_wineserver_stats', null, [], []);
            return 'OK: called bw64_wineserver_stats';
        } catch (e) {
            return 'ERROR: ' + e.message;
        }
    });
    console.log(`Evaluate result: ${result}`);
    await new Promise(r => setTimeout(r, 2000));
} catch (e) {
    console.log(`Error: ${e.message}`);
}

try {
    await writeFile(`test-results/${TAG}-console.log`, logs.join('\n'));
    console.log(`Logs saved`);
} catch (e) {
    console.log(`Could not save logs: ${e.message}`);
}

const statsLines = logs.filter(l => l.includes('WINESERVER_STATS'));
console.log('\n=== WINESERVER STATS ===');
for (const line of statsLines) {
    console.log(line);
}
if (statsLines.length === 0) {
    console.log('(no WINESERVER_STATS lines found in console)');
}

await browser.close();
server.close();
console.log('Done');
