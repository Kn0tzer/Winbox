// web/tests/suite/unit/x11-gc-setters.test.mjs — the GC setters the legacy X11
// path used to abort on: XCreateGC with a valuemask, XSetDashes, XSetArcMode
// and XSetFillStyle.
//
// source/x11/x11common.cpp had a kpanic in each of these. The handlers are
// compiled against a stub server, GC and guest memory and run, so the test
// checks the real bodies: the valuemask is applied, the dash offset and first
// dash are stored, and the arc mode and fill style are stored.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const createGC = extractSourceSlice(source, 'static void x11_CreateGC(CPU* cpu) {', '// int XSetDashes(', { sourceName });
const setDashes = extractSourceSlice(source, '// int XSetDashes(', '// int XDrawLine(Display* display, Drawable d, GC gc, int x1', { sourceName });
const setArcMode = extractSourceSlice(source, '// int XSetArcMode(', 'static void x11_FillArc(CPU* cpu) {', { sourceName });
const setFillStyle = extractSourceSlice(source, '// int XSetFillStyle(', '// int XFree(void* data)', { sourceName });

test('the GC setters do not abort the emulator', () => {
  for (const block of [createGC, setDashes, setArcMode, setFillStyle]) {
    assert.doesNotMatch(block, /kpanic\(/);
  }
});

test('the GC setters store their values and report errors', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-gc-setters-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
typedef int32_t S32;
typedef int8_t S8;
typedef uint32_t U32;
typedef uint8_t U8;
enum { Success = 0, BadValue = 2, BadDrawable = 9, BadGC = 13 };
// the emulator stops on kpanic; the stub stops the test the same way
[[noreturn, maybe_unused]] static void kpanic(const char* msg) { std::fprintf(stderr, "kpanic: %s\\n", msg); std::exit(3); }
[[maybe_unused]] static void kwarn(const char*) {}
[[maybe_unused]] static void klog(const char*) {}
struct BString {
    BString& operator+=(const char*) { return *this; }
    template <class T> void append(T, int) {}
    const char* c_str() const { return ""; }
};
// guest memory: a few bytes at known offsets
struct KMemory {
    U8 mem[256];
    U32 readd(U32 a) { U32 v; std::memcpy(&v, mem + a, 4); return v; }
    U8 readb(U32 a) { return mem[a]; }
};
struct CPU { KMemory* memory; U32 args[11]; U32 eax; U32 peek32(int i) { return args[i]; } };
#define ARG1 cpu->peek32(1)
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)
#define ARG5 cpu->peek32(5)
#define EAX (cpu->eax)
struct DisplayData { U32 displayId = 0; };
#define DisplayDataPtr std::shared_ptr<DisplayData>
struct XGCValues {
    S32 function = 0, foreground = 0, fill_style = 0, arc_mode = 0, dash_offset = 0;
    S8 dashes = 4;
    void read(KMemory* memory, U32 address) {
        function = (S32)memory->readd(address);
        foreground = (S32)memory->readd(address + 8);
    }
};
class XDrawable {};
#define XDrawablePtr std::shared_ptr<XDrawable>
class XGC {
public:
    explicit XGC(U32 i) : id(i) {}
    const U32 id;
    XGCValues values;
    std::vector<U8> dash_list;
    U32 lastMask = 0;
    S32 lastForeground = 0;
    void updateValues(U32 mask, XGCValues* v) { lastMask = mask; lastForeground = v->foreground; }
};
#define XGCPtr std::shared_ptr<XGC>
struct XServer {
    bool traceGC = false;
    std::map<U32, XGCPtr> gcs;
    static XServer* getServer() { static XServer s; return &s; }
    XDrawablePtr getDrawable(U32 id) { return id == 1 ? std::make_shared<XDrawable>() : nullptr; }
    XGCPtr createGC(const XDrawablePtr&) { XGCPtr g = std::make_shared<XGC>(0x77); gcs[0x77] = g; return g; }
    XGCPtr getGC(U32 id) { auto it = gcs.find(id); return it == gcs.end() ? nullptr : it->second; }
    DisplayDataPtr getDisplayDataByAddressOfDisplay(KMemory*, U32) { return std::make_shared<DisplayData>(); }
};
${createGC}
${setDashes}
${setArcMode}
${setFillStyle}
int main() {
    KMemory mem;
    CPU cpu{};
    cpu.memory = &mem;
    XServer* server = XServer::getServer();

    // XCreateGC with a valuemask applies the masked fields only
    {
        std::memset(mem.mem, 0, sizeof(mem.mem));
        U32 foreground = 0xabcdef;
        std::memcpy(mem.mem + 64 + 8, &foreground, 4);
        cpu.args[1] = 0; cpu.args[2] = 1; cpu.args[3] = 1 << 2; cpu.args[4] = 64;
        x11_CreateGC(&cpu);
        assert(cpu.eax == 0x77);
        assert(server->getGC(0x77)->lastMask == (1U << 2));
        assert(server->getGC(0x77)->lastForeground == (S32)0xabcdef);
    }
    // XCreateGC with no valuemask does not read the values
    {
        cpu.args[2] = 1; cpu.args[3] = 0; cpu.args[4] = 0;
        x11_CreateGC(&cpu);
        assert(server->getGC(0x77)->lastMask == 0);
    }

    // XSetDashes stores all dash lengths and the offset; zero lengths and empty lists are BadValue
    {
        XGCPtr gc = server->createGC(nullptr);
        cpu.args[2] = 0x77; cpu.args[3] = 2; cpu.args[4] = 100; cpu.args[5] = 0;
        x11_SetDashes(&cpu);
        assert(cpu.eax == BadValue);
        assert(gc->values.dash_offset == 0);
        mem.mem[100] = 2; mem.mem[101] = 3; mem.mem[102] = 4;
        cpu.args[5] = 3;
        x11_SetDashes(&cpu);
        assert(cpu.eax == Success);
        assert(gc->values.dash_offset == 2 && gc->values.dashes == 2);
        assert((gc->dash_list == std::vector<U8>{2, 3, 4}));
        mem.mem[100] = 0;
        x11_SetDashes(&cpu);
        assert(cpu.eax == BadValue && gc->dash_list == std::vector<U8>({2, 3, 4}));
        cpu.args[2] = 0x99;
        x11_SetDashes(&cpu);
        assert(cpu.eax == BadGC);
    }

    // XSetArcMode and XSetFillStyle store their value and report a missing GC
    {
        XGCPtr gc = server->createGC(nullptr);
        cpu.args[2] = 0x77; cpu.args[3] = 0;
        x11_SetArcMode(&cpu);
        assert(cpu.eax == Success && gc->values.arc_mode == 0);
        cpu.args[3] = 1;
        x11_SetFillStyle(&cpu);
        assert(cpu.eax == Success && gc->values.fill_style == 1);
        cpu.args[2] = 0x99;
        x11_SetArcMode(&cpu);
        assert(cpu.eax == BadGC);
        x11_SetFillStyle(&cpu);
        assert(cpu.eax == BadGC);
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
