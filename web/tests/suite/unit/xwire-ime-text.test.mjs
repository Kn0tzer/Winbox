// IME committed-text channel — xwire-ime-text.test.mjs
//
// The channel that closes the coverage.md IME row: a browser IME composes
// in a hidden textarea on the launcher page; each finished composition is
// forwarded as committed text through the bw64_inject_text WASM export ->
// xwireInjectText (strict UTF-8 decode, one XWire EvText per code point)
// -> XWireConnection::deliverInputEvents, which expands each code point
// into a KeyPress/KeyRelease pair: static-US-keymap characters use their
// keycode (Shift state for shifted keysyms); anything else goes through a
// scratch keycode (248) whose keysym is rebound to the character's Unicode
// keysym, preceded on the wire by MappingNotify so clients refresh their
// cached keymap before the press lands.
//
// Form: node:test, like every other file in this suite (an earlier draft
// exported a harness-style test(t) that nothing in the runner consumes —
// it passed vacuously; the sabotage checks in the lane report caught it).
// The planner/binding helpers and the UTF-8 decoder are sliced verbatim
// out of the production sources, compiled with -Wall -Werror and run;
// the launcher's composition capture is extracted and driven in a vm
// with fake DOM nodes. A real OS-IME commit end to end needs a browser +
// OS IME (integ gate), which no unit test can stand up — what is pinned
// here is that every layer exists, is wired, and behaves: CJK code
// points route to the scratch binding with the correct Unicode keysym,
// malformed UTF-8 is skipped byte-wise, and empty / cancelled
// compositions are no-ops, not errors. The launcher subtests share one
// fake page and run in order (their call-count expectations accumulate).

import test from "node:test";
import assert from "node:assert/strict";
import { readFile, writeFile, mkdtemp, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import vm from "node:vm";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
const run = promisify(execFile);

const here = path.dirname(new URL(import.meta.url).pathname);
const repoRoot = path.resolve(here, "..", "..", "..", "..");
const read = (p) => readFile(path.join(repoRoot, p), "utf8");

// Slice a top-level C++ construct: from the line containing `marker` up to
// (and including) the first following line that is exactly "}". The sliced
// functions are written so no body line is exactly "}".
function sliceFn(src, marker) {
  const lines = src.split("\n");
  const start = lines.findIndex((l) => l.includes(marker));
  assert(start >= 0, "marker not found: " + marker);
  for (let i = start + 1; i < lines.length; i++) {
    if (lines[i] === "}") return lines.slice(start, i + 1).join("\n");
  }
  throw new Error("unterminated slice: " + marker);
}

// Slice from the line containing `startMarker` up to (excluding) the line
// containing `endMarker`.
function sliceBetween(src, startMarker, endMarker) {
  const lines = src.split("\n");
  const start = lines.findIndex((l) => l.includes(startMarker));
  assert(start >= 0, "start marker not found: " + startMarker);
  const end = lines.findIndex((l, i) => i > start && l.includes(endMarker));
  assert(end > start, "end marker not found: " + endMarker);
  return lines.slice(start, end).join("\n");
}

test("IME committed-text channel: launcher -> bw64_inject_text -> EvText -> X key events", async (t) => {
  const conn = await read("source/x11wire/xwireconnection.cpp");
  const present = await read("source/x11wire/xwirepresentSDL.cpp");
  const presentH = await read("source/x11wire/xwirepresent.h");
  const session = await read("source/sdl/emscripten/wine64session.cpp");
  const launcher = await read("project/emscripten/wine64-launcher.js");

  // ---- Layer pins: every hop of the channel exists and is wired. ----
  await t.test("present.h declares EvText + xwireInjectText", () => {
    assert(presentH.includes("EvText"), "EvText missing from XWireInputEvent::Type");
    assert(presentH.includes("void xwireInjectText(const char* utf8);"), "xwireInjectText declaration missing");
  });
  await t.test("presentSDL implements xwireInjectText over EvText", () => {
    assert(present.includes("void xwireInjectText(const char* utf8)"), "xwireInjectText definition missing");
    assert(present.includes("ie.type = XWireInputEvent::EvText;"), "injector does not queue EvText");
    assert(present.includes("e2.type == XWireInputEvent::EvText"), "sink injectEvent does not special-case EvText");
  });
  await t.test("session exports bw64_inject_text (KEEPALIVE) into xwireInjectText", () => {
    assert(session.includes("EMSCRIPTEN_KEEPALIVE void bw64_inject_text(const char* utf8Text)"), "bw64_inject_text export missing");
    assert(session.includes("xwireInjectText(utf8Text);"), "export does not call xwireInjectText");
  });
  await t.test("connection plans EvText via keycodeForCodepoint + scratch binding + MappingNotify", () => {
    assert(conn.includes("ev.type == XWireInputEvent::EvText"), "deliverInputEvents ignores EvText");
    assert(conn.includes("static int keycodeForCodepoint(uint32_t cp, bool& shift)"), "planner missing");
    assert(conn.includes("static const int kImeScratchKeycode = 248;"), "scratch keycode missing");
    assert(conn.includes("bindImeScratchKeysym(unicodeKeysymForCodepoint(cp))"), "scratch bind missing");
    assert(conn.includes("sendPendingImeMappingNotify();"), "MappingNotify announce missing");
    assert(conn.includes("e[0] = 34;"), "MappingNotify event code missing");
    assert(conn.includes("if (kc == kImeScratchKeycode)"), "keysymForKeycode does not serve the scratch binding");
  });
  await t.test("launcher installs the capture and calls it before boot", () => {
    assert(launcher.includes("function bw64InstallImeCapture()"), "bw64InstallImeCapture missing");
    assert(launcher.includes("bw64InstallImeCapture();"), "capture never installed");
    assert(launcher.includes('callExport("bw64_inject_text", ["string"], [text])'), "commit does not reach bw64_inject_text");
  });

  // ---- Compiled behaviour: planner, scratch binding, UTF-8 decoder. ----
  const dir = await mkdtemp(path.join(os.tmpdir(), "bw64-ime-"));
  try {
    const keyPair = conn.split("\n").find((l) => l.includes("struct KeyPair { uint32_t lo, hi; };"));
    assert(keyPair, "KeyPair struct line not found");
    const imeState = sliceBetween(conn, "static const int kImeScratchKeycode", "static KeyPair keysymForKeycode");
    const keysymFn = sliceFn(conn, "static KeyPair keysymForKeycode(int kc) {");
    const plannerFn = sliceFn(conn, "static int keycodeForCodepoint(uint32_t cp, bool& shift) {");
    const decoderFn = sliceFn(present, "static int xwireUtf8DecodeOne(const unsigned char* s, size_t n, uint32_t& cp) {");
    const cpp = [
      "#include <cstdint>",
      "#include <cstddef>",
      "#include <cstdio>",
      "#include <cstring>",
      "#include <atomic>",
      keyPair,
      imeState,
      keysymFn,
      plannerFn,
      decoderFn,
      `
int main() {
    bool sh = true;
    // Static US keymap: unshifted and shifted forms resolve in place.
    if (keycodeForCodepoint('a', sh) != 38 || sh) return 1;
    if (keycodeForCodepoint('A', sh) != 38 || !sh) return 2;
    if (keycodeForCodepoint('5', sh) != 14 || sh) return 3;
    if (keycodeForCodepoint('%', sh) != 14 || !sh) return 4;
    if (keycodeForCodepoint(' ', sh) != 65 || sh) return 5;
    if (keycodeForCodepoint('z', sh) != 52 || sh) return 6;
    if (keycodeForCodepoint('~', sh) != 49 || !sh) return 7;
    if (keycodeForCodepoint('"', sh) != 48 || !sh) return 8;
    // Commit control characters map to their editing keys.
    if (keycodeForCodepoint('\\n', sh) != 36 || sh) return 9;
    if (keycodeForCodepoint('\\t', sh) != 23 || sh) return 10;
    // Unmapped characters (the CJK point of IME) plan to -1: the caller
    // falls back to the scratch binding.
    if (keycodeForCodepoint(0x3042, sh) != -1) return 11;   // U+3042 HIRAGANA A
    if (keycodeForCodepoint(0x4E2D, sh) != -1) return 12;   // U+4E2D CJK
    // Unicode keysym rule: Latin-1 is identity, above is 0x01000000|cp.
    if (unicodeKeysymForCodepoint(0xE9) != 0xE9) return 13;
    if (unicodeKeysymForCodepoint(0x3042) != 0x01003042) return 14;
    if (unicodeKeysymForCodepoint(0x1F600) != 0x0101F600) return 15;
    // E-acute is also unmapped on the US layout, so it too uses scratch.
    if (keycodeForCodepoint(0xE9, sh) != -1) return 16;
    // Scratch binding: bind, read back through the keymap, generation bumps.
    uint64_t g0 = g_imeKeymapGeneration.load();
    bindImeScratchKeysym(unicodeKeysymForCodepoint(0x3042));
    KeyPair kp = keysymForKeycode(kImeScratchKeycode);
    if (kp.lo != 0x01003042 || kp.hi != 0x01003042) return 17;
    if (g_imeKeymapGeneration.load() != g0 + 1) return 18;
    // The planner never returns the scratch keycode itself, bound or not.
    if (keycodeForCodepoint(0x3042, sh) != -1) return 19;
    // Rebinding replaces the keysym (next character of a mixed commit).
    bindImeScratchKeysym(unicodeKeysymForCodepoint(0x6587));
    kp = keysymForKeycode(kImeScratchKeycode);
    if (kp.lo != 0x01006587 || kp.hi != 0x01006587) return 20;
    // UTF-8 decoder: valid sequences of every length.
    uint32_t cp = 0;
    if (xwireUtf8DecodeOne((const unsigned char*)"a", 1, cp) != 1 || cp != 'a') return 30;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xC3\\xA9", 2, cp) != 2 || cp != 0xE9) return 31;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xE3\\x81\\x82", 3, cp) != 3 || cp != 0x3042) return 32;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xE2\\x82\\xAC", 3, cp) != 3 || cp != 0x20AC) return 33;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xF0\\x9F\\x98\\x80", 4, cp) != 4 || cp != 0x1F600) return 34;
    // Malformed input decodes to 0 (caller skips one byte): never a crash,
    // never a phantom character.
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xFF", 1, cp) != 0) return 35;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\x80", 1, cp) != 0) return 36;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xC0\\x80", 2, cp) != 0) return 37;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xED\\xA0\\x80", 3, cp) != 0) return 38;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xE3\\x81", 2, cp) != 0) return 39;
    if (xwireUtf8DecodeOne((const unsigned char*)"\\xF4\\x90\\x80\\x80", 4, cp) != 0) return 40;
    if (xwireUtf8DecodeOne((const unsigned char*)"", 0, cp) != 0) return 41;
    // Ordered walk over a mixed commit's bytes ("a" + U+6587 + U+1F600,
    // the exact UTF-8 a JS string produces): the decoder must yield the
    // code points in order, astral character as ONE scalar.
    {
        const unsigned char s[] = {'a', 0xE6, 0x96, 0x87, 0xF0, 0x9F, 0x98, 0x80};
        const uint32_t want[3] = {0x61, 0x6587, 0x1F600};
        size_t i = 0; int k = 0;
        while (i < sizeof(s)) {
            uint32_t c2 = 0;
            int len = xwireUtf8DecodeOne(s + i, sizeof(s) - i, c2);
            if (len == 0) { i++; continue; }
            i += (size_t)len;
            if (k >= 3 || c2 != want[k]) return 42;
            k++;
        }
        if (k != 3) return 43;
    }
    return 0;
}
`,
    ].join("\n");
    const srcPath = path.join(dir, "ime_check.cpp");
    const binPath = path.join(dir, "ime_check");
    await writeFile(srcPath, cpp);
    await run("c++", ["-std=c++17", "-Wall", "-Werror", srcPath, "-o", binPath]);
    await t.test("compiled planner + scratch binding + UTF-8 decoder behave", async () => {
      try {
        await run(binPath, []);
      } catch (e) {
        throw new Error("ime_check exited " + (e.code ?? "?") + " — the code names the failed case in this file's compiled main()");
      }
    });
  } finally {
    await rm(dir, { recursive: true, force: true });
  }

  // ---- Behaviour: launcher composition capture wiring. ----
  const fnMatch = launcher.match(/function bw64InstallImeCapture\(\) \{[\s\S]*?\n    \}/);
  assert(fnMatch, "could not extract bw64InstallImeCapture");
  const calls = [];
  const taHandlers = {};
  const canvasHandlers = {};
  const fakeTa = {
    id: "", value: "", tabIndex: 0, style: {}, attrs: {}, focused: false,
    setAttribute(k, v) { this.attrs[k] = v; },
    addEventListener(type, fn) { (taHandlers[type] ||= []).push(fn); },
    focus() { this.focused = true; },
  };
  const fakeParent = { children: [], appendChild(el) { this.children.push(el); } };
  const fakeCanvas = {
    parentNode: fakeParent,
    addEventListener(type, fn) { (canvasHandlers[type] ||= []).push(fn); },
  };
  const ctx = {
    bw64ImeCaptureInstalled: false,
    param: () => null,
    callExport: (n, argTypes, args) => { calls.push([n, argTypes, args]); return 0; },
    document: {
      getElementById: (id) => (id === "canvas" ? fakeCanvas : null),
      createElement: (tag) => { assert(tag === "textarea", "capture must create a textarea"); return fakeTa; },
      body: { appendChild() { throw new Error("canvas parent must be used"); } },
    },
  };
  vm.createContext(ctx);
  vm.runInContext(fnMatch[0], ctx);
  vm.runInContext("bw64InstallImeCapture()", ctx);
  const fire = (handlers, type, ev) => { for (const fn of handlers[type] || []) fn(ev || {}); };

  await t.test("capture installs one hidden textarea beside the canvas", () => {
    assert(fakeParent.children.length === 1 && fakeParent.children[0] === fakeTa, "textarea not appended to canvas parent");
    assert(fakeTa.id === "bw64-ime-input", "unexpected textarea id: " + fakeTa.id);
    assert(fakeTa.tabIndex === -1, "textarea must stay out of the tab order");
  });
  await t.test("canvas pointer press focuses the composition target", () => {
    fire(canvasHandlers, "pointerdown");
    assert(fakeTa.focused, "textarea was not focused on canvas pointerdown");
  });
  await t.test("a finished CJK composition commits exactly once via bw64_inject_text", () => {
    fire(taHandlers, "compositionstart");
    fakeTa.value = "あ";
    fire(taHandlers, "compositionend", { data: "あ" });
    assert(calls.length === 1, "expected exactly 1 export call, got " + calls.length);
    assert(calls[0][0] === "bw64_inject_text", "wrong export: " + calls[0][0]);
    assert(JSON.stringify(calls[0][1]) === '["string"]', "wrong arg types");
    assert(calls[0][2][0] === "あ", "wrong committed text: " + calls[0][2][0]);
    assert(fakeTa.value === "", "textarea must be cleared after commit");
  });
  await t.test("compositionend without data falls back to the textarea value", () => {
    fire(taHandlers, "compositionstart");
    fakeTa.value = "文";
    fire(taHandlers, "compositionend", {});
    assert(calls.length === 2 && calls[1][2][0] === "文", "value fallback commit missing");
    assert(fakeTa.value === "", "textarea must be cleared after fallback commit");
  });
  await t.test("astral commit (JS surrogate pair) forwards the exact string, UTF-8 as decoded guest-side", () => {
    fire(taHandlers, "compositionstart");
    fakeTa.value = "\u6587\uD83D\uDE00";
    fire(taHandlers, "compositionend", { data: "\u6587\uD83D\uDE00" });
    assert(calls.length === 3, "expected a third export call, got " + calls.length);
    assert(calls[2][2][0] === "\u6587\uD83D\uDE00", "astral commit string altered in transit");
    // The ccall "string" conversion emits UTF-8 (TextEncoder semantics):
    // the pair must combine into ONE 4-byte sequence for U+1F600, and a
    // lone surrogate becomes U+FFFD — never two surrogate halves, which
    // the guest decoder rejects byte-wise.
    const enc = new TextEncoder();
    assert(JSON.stringify([...enc.encode("\uD83D\uDE00")]) === "[240,159,152,128]", "astral pair must encode as one 4-byte UTF-8 sequence");
    assert(JSON.stringify([...enc.encode("\uD800")]) === "[239,191,189]", "lone surrogate must encode as U+FFFD");
    assert(fakeTa.value === "", "textarea must be cleared after astral commit");
  });
  await t.test("cancelled / empty composition is a no-op, not an error", () => {
    fire(taHandlers, "compositionstart");
    fakeTa.value = "";
    fire(taHandlers, "compositionend", { data: "" });
    assert(calls.length === 3, "empty commit must not call the export");
  });
  await t.test("non-composition input is discarded, never forwarded (no double typing)", () => {
    fakeTa.value = "x";
    fire(taHandlers, "input");
    assert(fakeTa.value === "", "plain input must be discarded");
    assert(calls.length === 3, "plain input must not reach the export");
  });
});
