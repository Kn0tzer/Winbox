import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('partial guest result lines stay intact when native diagnostics arrive between writes', () => {
  const source = readFileSync(new URL('../../../../source/kernel/devs/devtty.cpp', import.meta.url), 'utf8');
  const start = source.indexOf('U32 DevTTY::writeNative(');
  assert.ok(start >= 0);
  let end = source.indexOf('{', start), depth = 1;
  for (end++; depth && end < source.length; end++) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
  }
  const implementation = source.slice(start, end);
  const include = new URL('../../../../include/guestloglines.h', import.meta.url).pathname;
  const dir = mkdtempSync(join(tmpdir(), 'bw-console-lines-'));
  try {
    writeFileSync(join(dir, 'test.cpp'), `
#include <cassert>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <cstdio>
using U32 = uint32_t; using U8 = uint8_t;
#if __has_include("${include}")
#include "${include}"
#define HAS_GUEST_LINES
#endif
struct BString : std::string {
 using std::string::string;
 static BString copy(const char* data, U32 size) { return BString(data,size); }
 bool contains(const char* value) const { return find(value) != npos; }
};
std::string output;
unsigned rawWrites = 0;
long fakeWrite(int,const void* bytes,unsigned long size) { output.append((const char*)bytes,size); return size; }
#define write fakeWrite
struct File { bool isOpen() { return false; } void write(char*,U32) {} };
struct KSystem { static File logFile; static std::function<void(BString)> watchTTY; static bool ttyPrepend; };
File KSystem::logFile; std::function<void(BString)> KSystem::watchTTY; bool KSystem::ttyPrepend;
struct Process { U32 id = 42; BString name = "fixture"; };
struct KThread { Process* process; U32 id=111; static KThread* currentThread() { static Process p; static KThread t{&p}; return &t; } };
void internal_log(BString text,FILE*) { fakeWrite(1,text.data(),text.size()); }
struct DevTTY {
#ifdef HAS_GUEST_LINES
 GuestLogLines guestLogLines;
#endif
 U32 writeNative(U8*,U32);
};
${implementation}
int main() {
 DevTTY tty; KSystem::watchTTY=[](BString){++rawWrites;};
 std::string first="scene10: RESU", last="LT 0\\n";
 assert(tty.writeNative((U8*)first.data(),first.size()) == first.size());
 fakeWrite(1,"FRAME 240 built\\n",16);
 assert(tty.writeNative((U8*)last.data(),last.size()) == last.size());
 if (output.find("scene10: RESULT 0\\n") == std::string::npos) return 7;
 assert(output.find("FRAME 240 built\\n") != std::string::npos);
 assert(rawWrites == 2);
 // Two threads of one guest process can write partial lines to the same TTY.
 // An async cache message must not split the main thread's RESULT marker.
 first="tri12: RESU"; last="LT 0\\n";
 tty.writeNative((U8*)first.data(),first.size());
 KThread::currentThread()->id=222;
 std::string other="other-thread: INFO cache miss\\n";
 tty.writeNative((U8*)other.data(),other.size());
 KThread::currentThread()->id=111;
 tty.writeNative((U8*)last.data(),last.size());
 if (output.find("tri12: RESULT 0\\n") == std::string::npos) return 8;
 assert(output.find("other-thread: INFO cache miss\\n") != std::string::npos);
}
`);
    execFileSync('c++', ['-std=c++17', '-D__EMSCRIPTEN__', '-pthread', join(dir, 'test.cpp'), '-o', join(dir, 'test')]);
    execFileSync(join(dir, 'test'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
