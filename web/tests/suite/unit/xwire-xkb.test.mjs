import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('XKB client maps use canonical wire types and expose physical US key symbols',()=>{
 const source=readFileSync(join(root,'source/x11wire/xwireconnection.cpp'),'utf8');
 assert.ok(source.includes('extName == "XKEYBOARD"'));
 const symbols=extractSourceRange(source, source.indexOf('struct KeyPair'),source.indexOf('// ---------------------------------------------------------------------------',source.indexOf('struct KeyPair')));
 const dir=mkdtempSync(join(tmpdir(),'xwire-xkb-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`#include <cassert>\n#include <X11/Xlib.h>\n#include <X11/extensions/XKB.h>\n#include <X11/extensions/XKBproto.h>\n#include "${join(root,'source/x11wire/xwirekeyboard.h')}"\n${symbols}\nauto reply(const uint8_t* p,unsigned n){return xwireXkbReply(p,n,0x1234,[](unsigned key){auto s=keysymForKeycode(key);return std::array<uint32_t,2>{s.lo,s.hi};});}\nint main(){uint8_t use[8]={151,0,2,0,1,0,0,0};auto u=reply(use,8);auto* ur=(xkbUseExtensionReply*)u.data();assert(ur->supported && ur->serverMajor==1 && ur->sequenceNumber==0x1234);use[4]=2;assert(!reply(use,8)[1]);uint8_t req[28]={151,8,7,0,0,1,7,0};auto r=reply(req,28);auto* h=(xkbGetMapReply*)r.data();assert(h->length*4+32==r.size() && h->minKeyCode==8 && h->maxKeyCode==255 && h->present==7 && h->nTypes==4 && h->totalTypes==4 && h->nKeySyms==248 && h->totalSyms==496);size_t p=sizeof(*h);for(int i=0;i<4;i++){auto* t=(xkbKeyTypeWireDesc*)(r.data()+p);assert(t->numLevels==(i==0?1:2));assert(t->nMapEntries==(i==0?0:i==2?3:1));p+=sizeof(*t)+t->nMapEntries*sizeof(xkbKTMapEntryWireDesc);}for(int key=8;key<=255;key++){auto* map=(xkbSymMapWireDesc*)(r.data()+p);assert(map->groupInfo==1 && map->width==2 && map->nSyms==2);uint32_t lo,hi;memcpy(&lo,r.data()+p+8,4);memcpy(&hi,r.data()+p+12,4);if(key==49)assert(lo=='\x60' && hi=='~' && map->ktIndex[0]==1);if(key==38)assert(lo=='a' && hi=='A' && map->ktIndex[0]==2);if(key==36)assert(lo==0xff0d);p+=16;}assert(h->totalModMapKeys==5 && h->nModMapKeys==248);assert(r[p]==50 && r[p+1]==1);p+=10;assert((p+3)/4*4==r.size());req[6]=0;req[8]=2;req[12]=49;req[13]=1;r=reply(req,28);h=(xkbGetMapReply*)r.data();assert(h->nKeySyms==1 && h->firstKeySym==49 && h->totalSyms==2 && r.size()==56);assert(reply(req,4).empty());uint8_t select[8]={151,1,2};assert(reply(select,8).empty());}\n`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
