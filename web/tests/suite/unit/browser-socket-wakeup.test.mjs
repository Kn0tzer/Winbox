import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('browser native socket monitor uses a working local wakeup pipe', () => {
  const source = readFileSync(join(repoRoot, 'source/kernel/knativesocket.cpp'), 'utf8');
  const functions = extractSourceRange(source, source.indexOf('void startNativeSocketsThread()'), source.indexOf('void addWaitingNativeSocket('));
  assert.ok(functions.startsWith('void startNativeSocketsThread()'));
  const dir = mkdtempSync(join(tmpdir(), 'bw64-socket-wakeup-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <sys/select.h>
using S32=int32_t;
#define __EMSCRIPTEN__ 1
#define BOXEDWINE_MULTI_THREADED 1
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m)
#define B(s) s
#define kpanic(s) throw std::runtime_error(s)
static S32 nativeSocketPipe[2];
static bool checkWaitingNativeSocketsThreadDone;
static int checkWaitingNativeSocketsThreadMutex;
static int checkWaitingNativeSockets_thread(void*){return 0;}
static void setNativeBlocking(int fd,bool blocking){assert(!blocking);fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);}
struct Platform {static int nativeSocketPair(S32*){errno=ENOSYS;return -1;}};
struct KNativeThread {
 static KNativeThread* createAndStartThread(int(*)(void*),const char*,void*){static KNativeThread thread;return &thread;}
 int wait(){char value=9;assert(read(nativeSocketPipe[0],&value,1)==1);assert(value==0);return 0;}
};
static KNativeThread* checkWaitingNativeSocketsThread;
#ifdef BOXEDWINE_MULTI_THREADED
${functions}
int main(){
 startNativeSocketsThread();
 assert(nativeSocketPipe[0]>2 && nativeSocketPipe[1]>2 && "unimplemented socketpair must not monitor stdin");
 struct stat info{};assert(fstat(nativeSocketPipe[0],&info)==0 && S_ISFIFO(info.st_mode));
 assert(write(nativeSocketPipe[1],"x",1)==1);char byte=0;assert(read(nativeSocketPipe[0],&byte,1)==1 && byte=='x');
 const int previous=nativeSocketPipe[0];
 stopNativeSocketsThread();
 assert(checkWaitingNativeSocketsThread==nullptr && !checkWaitingNativeSocketsThreadDone);
 assert(fcntl(previous,F_GETFD)==-1 && errno==EBADF);
 startNativeSocketsThread();stopNativeSocketsThread();
 std::vector<int> extra;int highest=-1;
 while(highest<FD_SETSIZE){highest=open("/dev/null",O_RDONLY);assert(highest>=0);extra.push_back(highest);}
 bool rejected=false;try{startNativeSocketsThread();}catch(const std::runtime_error&){rejected=true;}
 assert(rejected && checkWaitingNativeSocketsThread==nullptr);
 assert(nativeSocketPipe[0]==-1 && nativeSocketPipe[1]==-1);
 for(int fd:extra)close(fd);
}
`);
    const build = spawnSync('c++', ['-std=c++20', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 3000 });
    assert.equal(run.status, 0, run.stderr);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
