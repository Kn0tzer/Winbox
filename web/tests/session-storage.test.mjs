import test from 'node:test';
import assert from 'node:assert/strict';
import { snapshotRuntime, readSaveBlobs } from '../session-storage.mjs';
test('stopping before restoration never opens or clears the saved-files database',async()=>{
 const original=globalThis.indexedDB;let opened=0;
 globalThis.indexedDB={open(){opened++;throw Error('Must not touch saves before restoration');}};
 try { await snapshotRuntime({dataset:{installation:'existing'},contentWindow:{bwFilesystemReady:false,bwRuntime:{fs:()=>({readdir:()=>[]})}}});assert.equal(opened,0); }
 finally {globalThis.indexedDB=original;}
});
test('oversized writable backup is rejected before any Blob data is read',async()=>{
 let reads=0;const blob={arrayBuffer(){reads++;throw Error('Must not allocate oversized saves');}};
 await assert.rejects(readSaveBlobs([{path:'save.dat',size:1024**3,blob}],'/root/home/username/'),/limit/);
 assert.equal(reads,0);
});
test('an incomplete filesystem traversal fails before replacing saved files',async()=>{
 const original=globalThis.indexedDB;let opened=0;
 globalThis.indexedDB={open(){opened++;throw Error('Must not clear a partial snapshot');}};
 try {await assert.rejects(snapshotRuntime({dataset:{installation:'existing'},contentWindow:{bwFilesystemReady:true,bwRuntime:{fs:()=>({readdir(){throw Error('Filesystem not readable');}})}}}),/not readable/);assert.equal(opened,0);}
 finally {globalThis.indexedDB=original;}
});
