import test from 'node:test';
import assert from 'node:assert/strict';

for (const name of ['d3d10-comprehensive', 'd3d11-comprehensive', 'd3d12-comprehensive']) {
  test(`${name} records the architecture used by its guest boot`, async () => {
    const {default: Probe} = await import(`../browser/${name}.test.mjs`);
    const probe = new Probe();
    probe._server = {url: 'http://probe.invalid'};
    probe.waitForPageFn = async () => {};
    const staged = new Error('reached fixture staging');
    probe.stageFile = async () => { throw staged; };
    let bootUrl;
    const page = {goto: async url => { bootUrl = new URL(url); }};
    await assert.rejects(probe.run({page, server: probe._server}), error => error === staged);
    assert.deepEqual([...probe.testedArchitectures], ['x64']);
    assert.equal(bootUrl.searchParams.get('arch'), 'x64');
  });
}
