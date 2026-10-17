import test from 'node:test';
import assert from 'node:assert/strict';
import {isVkd3dError} from '../lib/log-evidence.mjs';
test('vkd3d cold-cache information is distinct from runtime errors',()=>{
 assert.equal(isVkd3dError('log: 1791909676.259:0028:008c:info:vkd3d-proton:vkd3d_pipeline_library_disk_cache_initial_setup: Failed to map read-only cache: vkd3d-proton.cache.'),false);
 assert.equal(isVkd3dError('log: [guest fd=2 pid=14 wine64] 1791909676.259:0028:008c:info:vkd3d-proton:vkd3d_pipeline_library_disk_cache_initial_setup: Failed to map read-only cache: vkd3d-proton.cache.'),false);
 assert.equal(isVkd3dError('log: 1791907962.159:0028:0090:err:vkd3d-proton:vkd3d_wait_for_gpu_timeline_semaphore: Failed to wait for Vulkan timeline semaphore, vr -8.'),true);
 assert.equal(isVkd3dError('log: [guest fd=2 pid=14 wine64] 1791907962.159:0028:0090:err:vkd3d-proton:allocation failed.'),true);
});
