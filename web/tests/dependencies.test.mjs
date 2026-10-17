import test from 'node:test';
import assert from 'node:assert/strict';
import { dependencySpec, loadDependencies } from '../dependencies.mjs';

const sha256 = 'a'.repeat(64);
const manifest = { version: 1, archive: `support-${sha256}.zip`, bytes: 123456, sha256 };
test('shared runtime uses an absolute, content-addressed range archive', () => {
  assert.equal(dependencySpec(manifest, 'https://example.test/winbox/runtime/'),
    `bw64url:123456;https://example.test/winbox/runtime/support-${sha256}.zip|123456`);
});
test('dependency loading checks availability, exact size and range support without downloading the archive', async () => {
  const metadata = () => Response.json(manifest);
  const archive = () => new Response(null, { headers: { 'content-length': String(manifest.bytes), 'accept-ranges': 'bytes' } });
  const fetcher = async (url, options) => options.method === 'HEAD' ? archive() : metadata();
  const base = new URL('https://example.test/winbox/runtime/dependencies.json?v=build');
  assert.equal(await loadDependencies(base, fetcher), dependencySpec(manifest, base));
  for (const response of [new Response(null, { status: 404 }), new Response(null, { headers: { 'content-length': '123', 'accept-ranges': 'bytes' } }),
    new Response(null, { headers: { 'content-length': String(manifest.bytes) } })]) {
    await assert.rejects(loadDependencies(base, async (url, options) => options.method === 'HEAD' ? response : metadata()), /dependencies could not be loaded/);
  }
  await assert.rejects(loadDependencies(base, async () => new Response(null, { status: 503 })), /dependencies could not be loaded/);
});
test('rejects incomplete or redirected dependency manifests before guest startup', () => {
  for (const bad of [{}, { ...manifest, version: 2 }, { ...manifest, bytes: 0 }, { ...manifest, bytes: 2.5 },
    { ...manifest, archive: '../support.zip' }, { ...manifest, archive: 'https://evil.test/support.zip' },
    { ...manifest, sha256: 'b'.repeat(64) }]) assert.throws(() => dependencySpec(bad, 'https://example.test/runtime/'), /dependencies/i);
});
