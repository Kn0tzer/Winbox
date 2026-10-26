import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const source=readFileSync(new URL('../../../vkwebgpu.mjs',import.meta.url),'utf8');
// Regression: the shell-owned Vulkan presentation canvas overlays the runtime
// iframe inside #display. If it accepts pointer events, every Vulkan-tier
// game (Portal/DXVK, ...) renders in the shell but receives no mouse input:
// events target the overlay and never reach the runtime canvas the guest
// listens on. Probe evidence: test-results/portal-gameplay/run6 -- the
// runtime iframe document received 0 mouse events while the overlay canvas
// was the target of every mousemove/mousedown at the game point, and the
// Portal main menu ignored instant clicks, 1200ms and 2500ms hold clicks,
// and a keyboard battery. runtime.html already keeps its own #vkCanvas
// click-transparent (pointer-events:none); the shell canvas must be too.
test('page-tier presentation canvas is click-transparent',()=>{
 const fn=source.match(/function defaultCanvas\(\) \{([\s\S]*?)\n  \}/)?.[1]??'';
 assert.match(fn,/pointerEvents\s*=\s*'none'/,'defaultCanvas must set pointerEvents none so input falls through to the runtime iframe');
});
