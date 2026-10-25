import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('Java TCP pipe can set and read TCP_NODELAY on a guest accepted socket', () => {
  const source = readFileSync(join(repoRoot, 'source/kernel/knativesocket.cpp'), 'utf8');
  const setStart = source.indexOf('U32 KNativeSocketObject::setsockopt(');
  const setEnd = source.indexOf('    KMemory* memory = thread->memory;', setStart);
  const getStart = source.indexOf('U32 KNativeSocketObject::getsockopt(');
  const getEnd = source.indexOf('native_getsockopt:', getStart);
  const dir = mkdtempSync(join(tmpdir(), 'bw-tcp-options-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <sys/socket.h>
using U32=uint32_t;
constexpr U32 K_SOL_SOCKET=1,K_IPPROTO_IP=0,K_IPPROTO_TCP=6,K_TCP_NODELAY=1,K_ENOPROTOOPT=92,K_EINVAL=22;
constexpr U32 K_SO_ERROR=4,K_SO_TYPE=3,K_SO_PROTOCOL=38,K_SO_RCVBUF=8,K_SO_SNDBUF=7,K_SO_BROADCAST=6,K_SO_REUSEADDR=2,K_SO_ACCEPTCONN=30;
struct KMemory{U32 option=0,length=4;U32 readd(U32 a){return a==4?length:option;}void writed(U32 a,U32 v){(a==4?length:option)=v;}};
struct KThread{KMemory* memory;};using KFileDescriptorPtr=int;
struct KNativeSocketObject{bool loopbackDatagram=false,loopbackStream=true,loopbackStreamConnected=true,loopbackReuseAddress=false,loopbackNoDelay=false,connected=true,listening=false;int nativeSocket=-1;U32 error=0,loopbackPort=1234,type=1,protocol=6,recvLen=4096,sendLen=4096;
 U32 setsockopt(KThread*,const KFileDescriptorPtr&,U32,U32,U32,U32);U32 getsockopt(KThread*,const KFileDescriptorPtr&,U32,U32,U32,U32);
};
${extractSourceRange(source, setStart, setEnd)} return -K_ENOPROTOOPT; }
${extractSourceRange(source, getStart, getEnd)} native_getsockopt: return -K_ENOPROTOOPT; }
int main(){KMemory m;KThread t{&m};KNativeSocketObject accepted;
 m.option=1;assert(accepted.setsockopt(&t,0,K_IPPROTO_TCP,K_TCP_NODELAY,0,4)==0);
 m.option=0;assert(accepted.getsockopt(&t,0,K_IPPROTO_TCP,K_TCP_NODELAY,0,4)==0&&m.option==1&&m.length==4);
 m.option=0;assert(accepted.setsockopt(&t,0,K_IPPROTO_TCP,K_TCP_NODELAY,0,4)==0);
 m.option=99;assert(accepted.getsockopt(&t,0,K_IPPROTO_TCP,K_TCP_NODELAY,0,4)==0&&m.option==0);
}
`);
    const build = spawnSync('c++', ['-std=c++20', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8', timeout: 3000 });
    assert.equal(run.status, 0, run.error?.message || run.stderr);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
