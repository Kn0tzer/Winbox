// Attach to the page owned by a live game probe without creating a new context.
import { chromium } from '@playwright/test';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';

export async function connectProbe(directory) {
  const browser = await chromium.connect((await readFile(join(directory, '.browser.ws'), 'utf8')).trim());
  const root = await browser.newBrowserCDPSession();
  const targets = await root.send('Target.getTargets');
  const page = targets.targetInfos.find(t => t.type === 'page' && t.url.includes('/runtime/index.html'));
  if (!page) { await root.detach(); await browser.close(); throw Error('Probe page not found'); }
  const { sessionId } = await root.send('Target.attachToTarget', { targetId: page.targetId, flatten: false });
  const pending = new Map();
  let next = 0;
  root.on('Target.receivedMessageFromTarget', event => {
    if (event.sessionId !== sessionId) return;
    const result = JSON.parse(event.message);
    if (!result.id || !pending.has(result.id)) return;
    const { resolve, reject, timeout } = pending.get(result.id);
    pending.delete(result.id); clearTimeout(timeout);
    if (result.error) reject(Error(result.error.message)); else resolve(result.result);
  });
  function send(method, params = {}) {
    const id = ++next;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => { pending.delete(id); reject(Error(`CDP timeout: ${method}`)); }, 60000);
      pending.set(id, { resolve, reject, timeout });
      root.send('Target.sendMessageToTarget', { sessionId, message: JSON.stringify({ id, method, params }) })
        .catch(error => { clearTimeout(timeout); pending.delete(id); reject(error); });
    });
  }
  return {
    send,
    async evaluate(expression) {
      const reply = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
      if (reply.exceptionDetails) throw Error(reply.exceptionDetails.exception?.description || reply.exceptionDetails.text);
      return reply.result.value;
    },
    async close() {
      for (const item of pending.values()) { clearTimeout(item.timeout); item.reject(Error('Probe detached')); }
      pending.clear();
      await root.send('Target.detachFromTarget', { sessionId }).catch(() => {});
      await root.detach(); await browser.close();
    },
  };
}
