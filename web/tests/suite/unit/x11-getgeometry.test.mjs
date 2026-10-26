// web/tests/suite/unit/x11-getgeometry.test.mjs — XGetGeometry reports the
// drawable's size, depth, position and border instead of aborting the emulator.
//
// source/x11/x11common.cpp:x11_GetGeometry was a kpanic, so any client that
// asked for a drawable's geometry stopped the emulator. The handler is compiled
// against a stub server, drawables and guest memory and run, so the test checks
// the real body.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const getGeometry = extractSourceSlice(source, 'static void x11_GetGeometry(CPU* cpu) {', '// Status XInternAtoms(', { sourceName });

test('x11_GetGeometry does not abort the emulator', () => {
  assert.doesNotMatch(getGeometry, /kpanic\(/);
});

test('x11_GetGeometry reports the drawable geometry', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-getgeometry-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
enum { Success = 0, BadDrawable = 9 };
// the emulator stops on kpanic; the stub stops the test the same way
[[noreturn, maybe_unused]] static void kpanic(const char* msg) { std::fprintf(stderr, "kpanic: %s\\n", msg); std::exit(3); }
// guest memory: writed stores 4 bytes at an offset
static U8 guest[256];
struct KMemory {
    void writed(U32 a, U32 v) { std::memcpy(guest + a, &v, 4); }
};
struct CPU { KMemory* memory; U32 args[11]; U32 eax; U32 peek32(int i) { return args[i]; } };
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)
#define ARG5 cpu->peek32(5)
#define ARG6 cpu->peek32(6)
#define ARG7 cpu->peek32(7)
#define ARG8 cpu->peek32(8)
#define ARG9 cpu->peek32(9)
#define EAX (cpu->eax)
#define XWindowPtr std::shared_ptr<XWindow>
#define XDrawablePtr std::shared_ptr<XDrawable>
class XDrawable {
public:
    XDrawable(U32 w, U32 h, U32 d, bool win) : isWindow(win), w_(w), h_(h), depth_(d) {}
    virtual ~XDrawable() {}
    const bool isWindow;
    U32 width() { return w_; }
    U32 height() { return h_; }
    U32 getDepth() { return depth_; }
private:
    U32 w_, h_, depth_;
};
class XWindow : public XDrawable {
public:
    XWindow(U32 w, U32 h, U32 d, S32 x, S32 y, U32 b) : XDrawable(w, h, d, true), left(x), top(y), border(b) {}
    void getPlacement(S32& x, S32& y, U32& borderWidth) { x = left; y = top; borderWidth = border; }
private:
    S32 left, top;
    U32 border;
};
struct XRoot { U32 id; };
struct XServer {
    XDrawablePtr drawable;
    std::shared_ptr<XRoot> root = std::make_shared<XRoot>(XRoot{0x123});
    static XServer* getServer() { static XServer s; return &s; }
    XDrawablePtr getDrawable(U32) { return drawable; }
    const std::shared_ptr<XRoot>& getRoot() { return root; }
};
${getGeometry}
int main() {
    KMemory mem;
    CPU cpu{};
    cpu.memory = &mem;
    auto read = [](U32 a) { U32 v; std::memcpy(&v, guest + a, 4); return v; };
    XServer* server = XServer::getServer();
    // a window reports its position in its parent, its border and its size and depth
    {
        std::memset(guest, 0, sizeof(guest));
        server->drawable = std::make_shared<XWindow>(40, 30, 24, 5, 7, 2);
        cpu.args[2] = 1; cpu.args[3] = 16; cpu.args[4] = 20; cpu.args[5] = 24; cpu.args[6] = 28; cpu.args[7] = 32; cpu.args[8] = 36; cpu.args[9] = 40;
        x11_GetGeometry(&cpu);
        assert(cpu.eax == 1);
        assert(read(16) == 0x123);
        assert((S32)read(20) == 5 && (S32)read(24) == 7);
        assert(read(28) == 40 && read(32) == 30 && read(36) == 2 && read(40) == 24);
    }
    // a pixmap has no position or border
    {
        std::memset(guest, 0, sizeof(guest));
        server->drawable = std::make_shared<XDrawable>(16, 8, 8, false);
        x11_GetGeometry(&cpu);
        assert(cpu.eax == 1);
        assert(read(20) == 0 && read(24) == 0 && read(36) == 0);
        assert(read(28) == 16 && read(32) == 8 && read(40) == 8);
    }
    // an unknown drawable writes nothing and says so
    {
        std::memset(guest, 0, sizeof(guest));
        server->drawable = nullptr;
        x11_GetGeometry(&cpu);
        assert(cpu.eax == BadDrawable);
        assert(read(16) == 0 && read(28) == 0);
    }
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
