import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
test('real X11 pointer handlers preserve exclusive ownership and release on unmap/destroy', () => {
  const src = readFileSync(new URL('../../../../source/x11wire/xwireconnection.cpp', import.meta.url), 'utf8');
  const handlers = ['X_GrabPointer','X_UngrabPointer','X_UnmapWindow','X_DestroyWindow'].map(name => {
    const body = src.match(new RegExp(`case ${name}: \\{[\\s\\S]*?\\n        \\}`))?.[0];
    assert.ok(body, name); return body;
  }).join('\n');
  const dir = mkdtempSync(join(tmpdir(),'bw64-pointer-grab-'));
  try {
    writeFileSync(join(dir,'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <map>
enum{X_GrabPointer=26,X_UngrabPointer=27,X_UnmapWindow=10,X_DestroyWindow=4};
struct Window{bool mapped=false,isRoot=false;};
struct XWireServer{std::mutex regMutex;std::map<uint32_t,Window> windows;uint32_t presentWindow=0,focusWindow=0,pointerGrabClient=0,pointerGrabWindow=0,pointerConfineWindow=0;void schedulePresent(){}static XWireServer& instance(){static XWireServer s;return s;}};
uint8_t reply[32];void writeToClient(const uint8_t* p,size_t n){assert(n==32);memcpy(reply,p,n);}
uint32_t rd32(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return v;}
void updateWmState(uint32_t,uint32_t){}void ensureWindow(){}
void dispatch(int op,uint32_t clientIdBase,uint32_t window,uint32_t confine=0){uint8_t bytes[24]={};memcpy(bytes+4,&window,4);memcpy(bytes+12,&confine,4);const uint8_t* req=bytes;uint16_t sequence=0x1234;switch(op){${handlers}}}
int main(){
 auto& s=XWireServer::instance();s.windows[0x260]={true,true};s.windows[0x400001]={true,false};s.windows[0x600001]={false,false};
 dispatch(X_GrabPointer,0x400000,0x600001);assert(reply[1]==3 && s.pointerGrabClient==0);
 dispatch(X_GrabPointer,0x400000,0x400001);assert(reply[0]==1 && reply[1]==0 && reply[2]==0x34 && reply[3]==0x12 && s.pointerGrabClient==0x400000);
 assert(s.pointerConfineWindow==0);
 dispatch(X_GrabPointer,0x400000,0x400001,0x400001);assert(reply[1]==0 && s.pointerConfineWindow==0x400001);
 dispatch(X_GrabPointer,0x600000,0x260);assert(reply[1]==1 && s.pointerGrabClient==0x400000);
 dispatch(X_UngrabPointer,0x600000,0);assert(s.pointerGrabClient==0x400000);
 dispatch(X_GrabPointer,0x400000,0x260);assert(reply[1]==0 && s.pointerGrabWindow==0x260);
 dispatch(X_UngrabPointer,0x400000,0);assert(s.pointerGrabClient==0 && s.pointerGrabWindow==0 && s.pointerConfineWindow==0);
 dispatch(X_GrabPointer,0x400000,0x400001,0x400001);dispatch(X_UnmapWindow,0x400000,0x400001);assert(s.pointerGrabClient==0 && s.pointerConfineWindow==0);
 s.windows[0x400001].mapped=true;dispatch(X_GrabPointer,0x400000,0x400001,0x400001);dispatch(X_DestroyWindow,0x400000,0x400001);assert(s.pointerGrabClient==0 && s.pointerConfineWindow==0 && !s.windows.count(0x400001));
}
`);
    execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);
    execFileSync(join(dir,'check'));
  } finally {rmSync(dir,{recursive:true,force:true});}
});

test('confined grabs request SDL relative input on the main thread and release it without blocking', () => {
  const src = readFileSync(new URL('../../../../source/x11wire/xwirepresentSDL.cpp', import.meta.url), 'utf8');
  const helper = src.match(/static void syncXWirePointerCapture\(\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(helper);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-pointer-capture-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <mutex>
struct XWireServer{std::mutex regMutex;unsigned pointerGrabClient=0,pointerConfineWindow=0;static XWireServer& instance(){static XWireServer s;return s;}};
enum{SDL_FALSE=0,SDL_TRUE=1};
int relative=0,calls=0;bool focused=true;
int SDL_GetRelativeMouseMode(){return relative;}
void* SDL_GetMouseFocus(){return focused ? (void*)1 : nullptr;}
int SDL_SetRelativeMouseMode(int mode){++calls;relative=mode;return 0;}
${helper}
int main(){
 auto& s=XWireServer::instance();
 s.pointerGrabClient=1;syncXWirePointerCapture();assert(calls==0 && !relative);
 s.pointerConfineWindow=2;focused=false;syncXWirePointerCapture();assert(calls==0);
 focused=true;syncXWirePointerCapture();assert(calls==1 && relative);
 syncXWirePointerCapture();assert(calls==1);
 s.pointerGrabClient=0;syncXWirePointerCapture();assert(calls==2 && !relative);
 s.pointerGrabClient=1;s.regMutex.lock();syncXWirePointerCapture();s.regMutex.unlock();assert(calls==2);
 syncXWirePointerCapture();assert(calls==3 && relative);
 s.pointerConfineWindow=0;syncXWirePointerCapture();assert(calls==4 && !relative);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, {recursive:true, force:true}); }
});
