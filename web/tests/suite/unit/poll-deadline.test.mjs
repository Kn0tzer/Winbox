import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('real poll loop preserves deadlines across spurious wakes and consecutive sleeps', () => {
  const src = readFileSync(process.env.BW64_POLL_SOURCE || new URL('../../../../source/kernel/kpoll.cpp', import.meta.url), 'utf8');
  const sliceFunction = (needle) => {
    const start = src.indexOf(needle);
    if (start < 0) throw new Error(`Missing function: ${needle}`);
    let end = src.indexOf('{', start), depth = 1;
    for (++end; depth && end < src.length; ++end) { if (src[end] === '{') ++depth; if (src[end] === '}') --depth; }
    return src.slice(start, end);
  };
  // internal_poll is now a thin wrapper over internal_poll_impl, which carries
  // the deadline arithmetic; compile both so the real loop is exercised.
  const impl = sliceFunction('S32 internal_poll_impl(');
  const wrapper = sliceFunction('S32 internal_poll(');
  const dir = mkdtempSync(join(tmpdir(), 'bw64-poll-deadline-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <atomic>
using U32=uint32_t;using S32=int32_t;
constexpr int K_POLLIN=1,K_POLLPRI=2,K_POLLOUT=4,K_POLLERR=8,K_POLLHUP=16,K_POLLNVAL=32,KTYPE_UNIX_SOCKET=3,K_EINTR=4;
struct KSystem {static U32 now;static U32 getMilliesSinceStart(){return now;}};U32 KSystem::now=1000;
std::vector<U32> wakes;size_t wake=0;
void waitTimeout(U32 ms){KSystem::now+=wake<wakes.size()?std::min(ms,wakes[wake++]):ms;}
#define BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c) ((void)0)
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) ((void)0)
#define BOXEDWINE_CONDITION_WAIT_TIMEOUT(c,ms) waitTimeout(ms)
#define BOXEDWINE_CONDITION_WAIT(c) assert(false)
#define BOXEDWINE_CONDITION_WAIT_SIGNALABLE(c) assert(false)
#define klog_fmt(...) ((void)0)
struct KObject {virtual ~KObject()=default;int type=0;void waitForEvents(int,int){}bool isOpen(){return true;}bool isPriorityReadReady(){return false;}bool isReadReady(){return false;}bool isWriteReady(){return false;}};
struct KUnixSocketObject:KObject {long debugRecvUsed(){return 0;}long debugMsgsSize(){return 0;}bool debugInClosed(){return false;}long debugPendingTotal(){return 0;}long debugPendingLive(){return 0;}};
struct KFileDescriptor {std::shared_ptr<KObject> kobject;};
struct Process {int id=1,fdsMutex=0;KFileDescriptor* getFileDescriptor_nolock(int){return nullptr;}};
struct KThread {bool inSignal=false,interrupted=false;U32 condStartWaitTime=0;int pollCond=0;Process* process;};
struct KPollData {int fd=0;U32 events=0,revents=0;};
void clearPollData(KThread*,KPollData*,U32){}
${impl}
${wrapper}
int main(){
 Process process;KThread thread;thread.process=&process;
 for(int i=0;i<4;i++){U32 before=KSystem::now;assert(internal_poll(&thread,nullptr,0,100)==0);assert(KSystem::now-before==100);assert(thread.condStartWaitTime==0);}
 wakes={30,30};wake=0;U32 before=KSystem::now;
 assert(internal_poll(&thread,nullptr,0,100)==0);assert(KSystem::now-before==100);assert(thread.condStartWaitTime==0);
 thread.condStartWaitTime=KSystem::now-100;assert(internal_poll(&thread,nullptr,0,100)==0);assert(thread.condStartWaitTime==0);
 thread.condStartWaitTime=KSystem::now-5;assert(internal_poll(&thread,nullptr,0,0)==0);assert(thread.condStartWaitTime==0);
 thread.interrupted=true;assert(internal_poll(&thread,nullptr,0,100)==-K_EINTR);assert(thread.condStartWaitTime==0);
 KSystem::now=0xfffffff0u;wakes={30,30};wake=0;before=KSystem::now;
 assert(internal_poll(&thread,nullptr,0,100)==0);assert(U32(KSystem::now-before)==100);
}
`);
    execFileSync('c++', ['-std=c++17', '-fsanitize=address,undefined', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync('bash', ['-c', 'ulimit -c 0; exec "$1"', 'poll-deadline-check', join(dir, 'check')], { timeout: 2000 });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
