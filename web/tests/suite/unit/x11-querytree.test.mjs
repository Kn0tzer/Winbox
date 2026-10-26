// web/tests/suite/unit/x11-querytree.test.mjs — XQueryTree reports the root, the
// parent and every child of a window, bottom-most child first.
//
// source/x11/x11common.cpp:x11_QueryTree was a kpanic, so any client that walked the
// window tree stopped the emulator. The handler is compiled against a stub server and
// windows and run, so the test checks the real body.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const queryTree = extractSourceSlice(source, 'static void x11_QueryTree(CPU* cpu) {', '//int XChangeWindowAttributes(', { sourceName });

test('x11_QueryTree does not abort the emulator', () => {
  assert.doesNotMatch(queryTree, /kpanic\(/);
});

test('x11_QueryTree reports the root, the parent and the children bottom-most first', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-querytree-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
static U8 guest[1 << 16];
static U32 bump = 0x1000;
struct KMemory {
    void writed(U32 a, U32 v) { std::memcpy(guest + a, &v, 4); }
};
struct KProcess {
    U32 alloc(void*, U32 n) { U32 a = bump; bump += (n + 15) & ~15u; return a; }
};
struct KThread { KProcess* process; };
struct CPU {
    KMemory* memory;
    KThread* thread;
    U32 args[11];
    U32 eax;
    U32 peek32(int i) { return args[i]; }
};
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)
#define ARG5 cpu->peek32(5)
#define ARG6 cpu->peek32(6)
#define EAX (cpu->eax)
class XWindow;
using XWindowPtr = std::shared_ptr<XWindow>;
class XWindow {
public:
    U32 id;
    XWindowPtr parent;
    std::vector<XWindowPtr> children;
    XWindowPtr getParent() { return parent; }
    std::vector<XWindowPtr> getChildrenBottomToTop() { return children; }
};
struct XRoot { U32 id; };
struct XServer {
    std::vector<XWindowPtr> windows;
    std::shared_ptr<XRoot> root = std::make_shared<XRoot>(XRoot{0x123});
    static XServer* getServer() { static XServer s; return &s; }
    XWindowPtr getWindow(U32 id) { for (auto& w : windows) if (w->id == id) return w; return nullptr; }
    const std::shared_ptr<XRoot>& getRoot() { return root; }
};
${queryTree}
static U32 word(U32 a) { U32 v; std::memcpy(&v, guest + a, 4); return v; }
int main() {
    XServer* server = XServer::getServer();
    // window 0x200 is top level with two children; 0x201 is the bottom one and has child 0x203
    auto top = std::make_shared<XWindow>(); top->id = 0x200;
    auto bottom = std::make_shared<XWindow>(); bottom->id = 0x201; bottom->parent = top;
    auto upper = std::make_shared<XWindow>(); upper->id = 0x202; upper->parent = top;
    auto nested = std::make_shared<XWindow>(); nested->id = 0x203; nested->parent = bottom;
    top->children = {bottom, upper};
    bottom->children = {nested};
    server->windows = {top, bottom, upper, nested};
    KMemory mem;
    KProcess process;
    KThread thread{&process};
    CPU cpu{};
    cpu.memory = &mem;
    cpu.thread = &thread;
    // a top-level window has the root as its root and no parent
    {
        std::memset(guest, 0, sizeof(guest));
        cpu.args[2] = 0x200; cpu.args[3] = 16; cpu.args[4] = 20; cpu.args[5] = 24; cpu.args[6] = 28;
        x11_QueryTree(&cpu);
        assert(cpu.eax == 1);
        assert(word(16) == 0x123 && word(20) == 0);
        assert(word(28) == 2);
        U32 children = word(24);
        assert(children != 0);
        assert(word(children) == 0x201 && word(children + 4) == 0x202);
    }
    // a child reports its parent and its own children
    {
        std::memset(guest, 0, sizeof(guest));
        cpu.args[2] = 0x201; cpu.args[3] = 16; cpu.args[4] = 20; cpu.args[5] = 24; cpu.args[6] = 28;
        x11_QueryTree(&cpu);
        assert(cpu.eax == 1);
        assert(word(20) == 0x200);
        assert(word(28) == 1);
        assert(word(word(24)) == 0x203);
    }
    // a window without children reports none and no array
    {
        std::memset(guest, 0, sizeof(guest));
        cpu.args[2] = 0x203; cpu.args[3] = 16; cpu.args[4] = 20; cpu.args[5] = 24; cpu.args[6] = 28;
        x11_QueryTree(&cpu);
        assert(cpu.eax == 1);
        assert(word(20) == 0x201);
        assert(word(28) == 0 && word(24) == 0);
    }
    // an unknown window says so and writes nothing
    {
        std::memset(guest, 0, sizeof(guest));
        cpu.args[2] = 0x999; cpu.args[3] = 16; cpu.args[4] = 20; cpu.args[5] = 24; cpu.args[6] = 28;
        x11_QueryTree(&cpu);
        assert(cpu.eax == 0);
        assert(word(16) == 0 && word(28) == 0);
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
