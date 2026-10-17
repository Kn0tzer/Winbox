import test from 'node:test';
import assert from 'node:assert/strict';
import { startupMessage } from '../startup.mjs';
test('recent logs reassure while Running is waiting for its first visible frame',()=>{
 assert.equal(startupMessage('Running...',1000,false,2000),'Not frozen, still loading');
});
test('stale logs never claim the program is active',()=>{
 assert.equal(startupMessage('Running...',1000,false,4000),'Running...');
});
test('the message clears after a visible frame and preserves useful progress',()=>{
 assert.equal(startupMessage('Running...',1000,true,2000),'Running...');
 assert.equal(startupMessage('Downloading runtime: 25%',1000,false,2000),'Downloading runtime: 25%');
});
