import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { runInNewContext } from 'node:vm';

test('browser TCP is opt-in while guest UDP loopback and local IPC remain available', () => {
  const source = readFileSync('source/kernel/ksocket.cpp', 'utf8');
  const start = source.indexOf('U32 ksocket(');
  const end = source.indexOf('#define IS_NOT_SOCKET', start);
  assert.ok(start >= 0 && end > start);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-network-gate-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
using U32=uint32_t;
#define __EMSCRIPTEN__ 1
#define BOXEDWINE_GUEST_X64 1
constexpr U32 K_AF_UNIX=1,K_AF_INET=2,K_AF_NETLINK=16,K_AF_INET6=10;
constexpr U32 K_SOCK_STREAM=1,K_SOCK_DGRAM=2,K_EAFNOSUPPORT=97,K_O_RDWR=2;
struct Object {virtual ~Object()=default;};
struct KUnixSocketObject:Object {KUnixSocketObject(U32,U32,U32){}};
struct KNetLinkObject:Object {KNetLinkObject(U32,U32,U32){}};
struct KNativeSocketObject:Object {U32 error=0;KNativeSocketObject(U32,U32,U32){}};
struct Descriptor {U32 handle;};
using KFileDescriptorPtr=std::shared_ptr<Descriptor>;
struct Process {U32 next=3;KFileDescriptorPtr allocFileDescriptor(std::shared_ptr<Object>,U32,U32,int,U32){return std::make_shared<Descriptor>(Descriptor{next++});}};
struct KThread {std::shared_ptr<Process> process=std::make_shared<Process>();static KThread* currentThread(){static KThread thread;return &thread;}};
${source.slice(start,end)}
int main(int argc,char** argv){
 assert(argc==2);
 const bool enabled=!strcmp(argv[1],"1");
 if(!strcmp(argv[1],"unset"))unsetenv("BW64_NETWORK_RELAY");else setenv("BW64_NETWORK_RELAY",argv[1],1);
 const U32 denied=static_cast<U32>(-K_EAFNOSUPPORT);
 assert(ksocket(K_AF_UNIX,K_SOCK_STREAM,0)!=denied);
 assert(ksocket(K_AF_NETLINK,K_SOCK_DGRAM,0)!=denied);
 const U32 tcp=ksocket(K_AF_INET,K_SOCK_STREAM,6);
 assert((tcp!=denied)==enabled && "exact network opt-in must control IPv4 TCP");
 assert((ksocket(K_AF_INET,K_SOCK_STREAM,0)!=denied)==enabled);
 assert(ksocket(K_AF_INET,K_SOCK_DGRAM,0)!=denied && "IPv4 UDP loopback works without the relay");
 assert(ksocket(K_AF_INET,K_SOCK_DGRAM,17)!=denied && "IPv4 UDP loopback works without the relay");
 assert(ksocket(K_AF_INET,K_SOCK_DGRAM,6)==denied);
 assert(ksocket(K_AF_INET,K_SOCK_STREAM,1)==denied);
 assert(ksocket(K_AF_INET6,K_SOCK_STREAM,6)==denied);
 assert(ksocket(17,K_SOCK_STREAM,0)==denied);
}
`);
    const build = spawnSync('c++', ['-std=c++20', join(dir, 'check.cpp'), '-o', join(dir, 'check')],
      { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    for (const mode of ['unset', '0', 'true', '1']) {
      const run = spawnSync(join(dir, 'check'), [mode], { encoding: 'utf8', timeout: 5000 });
      assert.equal(run.status, 0, `${mode}: ${run.stdout}${run.stderr}`);
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
});


test('relay initialization uses one credential-free same-origin endpoint', () => {
  const source = readFileSync('project/emscripten/wine64-launcher.js', 'utf8');
  const start = source.indexOf('    function configureNetworkRelay(');
  const end = source.indexOf('    // Resolve the FS object', start);
  assert.ok(start >= 0 && end > start);
  const configure = runInNewContext(source.slice(start, end) + '\nconfigureNetworkRelay', { URL });
  for (const [href, expected] of [
    ['http://127.0.0.1:8000/runtime/index.html?network=1', 'ws://127.0.0.1:8000/runtime/__bw64_tcp'],
    ['https://user:password@example.test/nested/runtime/index.html?relay=bad#fragment', 'wss://example.test/nested/runtime/__bw64_tcp'],
  ]) {
    const on = () => {};
    const module = { websocket: { url: 'wss://unrelated.invalid/', on } }, env = {};
    configure(module, env, true, href);
    assert.equal(module.websocket.url, expected);
    assert.equal(module.websocket.subprotocol, 'binary');
    assert.equal(module.websocket.on, on);
    assert.equal(env.BW64_NETWORK_RELAY, '1');
    configure(module, env, true, href);
    assert.equal(module.websocket.url, expected, 'repeated setup must converge');
  }
  for (const enabled of [false, undefined, '1', 'true', 1]) {
    const module = {}, env = { BW64_NETWORK_RELAY: '1' };
    configure(module, env, enabled, 'https://example.test/runtime/index.html');
    assert.equal(env.BW64_NETWORK_RELAY, undefined);
    assert.equal(module.websocket, undefined);
  }
  for (const href of ['file:///runtime/index.html', 'data:text/html,test', 'invalid']) {
    const env = { BW64_NETWORK_RELAY: '1' };
    assert.throws(() => configure({}, env, true, href));
    assert.equal(env.BW64_NETWORK_RELAY, undefined, 'invalid setup must fail closed');
  }
});


test('network query parsing disables malformed and nonexact values', () => {
  const source = readFileSync('project/emscripten/wine64-launcher.js', 'utf8');
  const call = source.match(/configureNetworkRelay\(liveModule, ENV, (.*), window.location.href\);/);
  assert.ok(call, 'exercise the actual initialization expression');
  for (const [search, expected] of [
    ['?network=1', true], ['?network=%31', true], ['?network=%', false],
    ['?network=%FF', false], ['?network=true', false], ['?network=0', false], ['', false],
  ]) {
    assert.equal(runInNewContext(call[1], { URLSearchParams, window: { location: { search } } }), expected, search);
  }
});
