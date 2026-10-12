import test from 'node:test';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('Wine 10 saved Windows stacks require a valid TEB, mapped frame and stack bounds',()=>{
 const dir=mkdtempSync(join(tmpdir(),'wine32-stack-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`
#include <cassert>
#include <array>
#include <cstring>
#include "${join(root,'include/wine32stack.h')}"
struct Memory {
 std::array<unsigned char,65536> bytes{}; bool frameMapped=true;
 bool canRead(uint32_t addr,uint32_t size){return addr<=bytes.size() && size<=bytes.size()-addr && (frameMapped || addr!=0x2000);}
 uint32_t readd(uint32_t addr){assert(canRead(addr,4));uint32_t v;memcpy(&v,bytes.data()+addr,4);return v;}
 void put(uint32_t addr,uint32_t v){memcpy(bytes.data()+addr,&v,4);}
};
int main(){
 Memory m; Wine32SavedStack s{};
 m.put(0x418,0x400);m.put(0x404,0x4000);m.put(0x408,0x2800);m.put(0x5f8,0x2000);
 m.put(0x2008,0x1234);m.put(0x200c,0x3000);m.put(0x2034,0x3200);
 assert(readWine32SavedStack(m,0x400,s));assert(s.eip==0x1234 && s.esp==0x3000 && s.ebp==0x3200 && s.stackBase==0x4000);
 assert(!readWine32SavedStack(m,0,s));assert(!readWine32SavedStack(m,0xfffffff0,s));
 m.put(0x418,0);assert(!readWine32SavedStack(m,0x400,s));m.put(0x418,0x400);
 m.put(0x5f8,0);assert(!readWine32SavedStack(m,0x400,s));m.put(0x5f8,0xfffffff0);assert(!readWine32SavedStack(m,0x400,s));m.put(0x5f8,0x2000);
 m.frameMapped=false;assert(!readWine32SavedStack(m,0x400,s));m.frameMapped=true;
 m.put(0x200c,0x2000);assert(!readWine32SavedStack(m,0x400,s));m.put(0x200c,0x4000);assert(!readWine32SavedStack(m,0x400,s));m.put(0x200c,0x3000);
 m.put(0x2008,0);assert(!readWine32SavedStack(m,0x400,s));
}
`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
