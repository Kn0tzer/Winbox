import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const source=readFileSync(new URL('../../../app.mjs',import.meta.url),'utf8');
// Regression: the first-frame overlay stayed forever on every Wine program that
// draws into the runtime iframe's own canvas (Notepad, Doom, ...) because the
// frame probe only looked at the launcher-owned #vkCanvas / #gpuCanvas.
test('first-frame probe also samples the runtime iframe canvas',()=>{
 const call=source.match(/visibleFrame\(([^;]*?),\s*run\.probe\)/)?.[1]??'';
 assert.match(call,/\$\('vkCanvas'\)/,'must probe the Vulkan page canvas');
 assert.match(call,/run\.canvas/,'must probe the launcher GPU canvas');
 assert.match(call,/run\.frame\.contentWindow\?\.document\.getElementById\('canvas'\)/,'must probe the runtime iframe canvas');
});
