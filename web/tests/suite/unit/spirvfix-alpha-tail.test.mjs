import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {applySpirvFix,parseSpirv,serializeSpirv} from '../../../../tools/spirvfix/spirvfix.mjs';
const bytes=new Uint8Array(readFileSync(new URL('../fixtures/dxvk-alpha-test-tail.spv',import.meta.url)));
test('real DXVK alpha-test shader can discard before an ignored output clamp',()=>{
 const fixed=applySpirvFix(bytes);assert.ok(fixed.applied.includes('tail-demote-to-kill'));
 const m=parseSpirv(fixed.bytes);assert.ok(!m.insns.some(i=>i.op===5380));assert.ok(!m.insns.some(i=>i.op===17&&i.words[0]===5379));assert.ok(m.insns.some(i=>i.op===252));
 assert.ok(!applySpirvFix(fixed.bytes).applied.includes('tail-demote-to-kill'));
});
test('mid-shader demotes preserve helper execution with derivatives or non-output writes',()=>{
 for(const mutate of [m=>{const k=m.insns.findLastIndex(i=>i.op===62&&i.words[0]===191);m.insns.splice(k,0,{op:207,words:[10,800,345]});},m=>{m.insns.find(i=>i.op===59&&i.words[1]===191).words[2]=7;}]){
  const m=parseSpirv(bytes);mutate(m);const fixed=applySpirvFix(serializeSpirv(m));assert.ok(!fixed.applied.includes('tail-demote-to-kill'));assert.ok(fixed.applied.includes('lower-mid-shader-discard'));
  const out=parseSpirv(fixed.bytes);assert.ok(!out.insns.some(i=>i.op===5380));assert.ok(out.insns.some(i=>i.op===166),'entry return checks the delayed discard flag');assert.ok(out.insns.some(i=>i.op===252),'kill is delayed to an entry return');
 }
});
