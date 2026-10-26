import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const makefile = fileURLToPath(new URL('../../../../project/linux/makefile', import.meta.url));
test('native make invalidates objects on compiler ABI flags, including reverting flags', () => {
  const dir = mkdtempSync(join(tmpdir(), 'bw64-native-flags-'));
  try {
    const source = join(dir, 'layout.cpp');
    const build = join(dir, 'build');
    const exe = join(build, 'layout');
    const obj = resolve(`${build}/${source}.o`);
    writeFileSync(source, '#include <cstdio>\nstruct CPU { char saved[ABI_WORDS]; void* memory; };\nint main() { std::printf("%zu\\n", sizeof(CPU)); }\n');
    const compile = words => {
      const r = spawnSync('make', ['-f', makefile, `BUILD_DIR=${build}`, `SRCS=${source}`,
        'SOFT_SOURCES=', 'LDFLAGS=', 'TARGET_EXEC=layout', `CPPFLAGS=-std=c++20 -DABI_WORDS=${words}`],
        { cwd: dir, encoding: 'utf8' });
      assert.equal(r.status, 0, r.stdout + r.stderr);
      const run = spawnSync(exe, [], { encoding: 'utf8' });
      assert.equal(run.status, 0, run.stderr);
      return Number(run.stdout.trim());
    };
    const first = compile(8);
    const initialTime = statSync(obj).mtimeNs ?? statSync(obj).mtimeMs;
    assert.equal(compile(8), first);
    assert.equal(statSync(obj).mtimeNs ?? statSync(obj).mtimeMs, initialTime,
      'unchanged flags should reuse the object');
    const switched = compile(104); // ARMv8 storedRegs adds 12 U64s = 96 bytes
    assert.equal(switched - first, 96, 'flag switch reused the old CPU layout');
    assert.equal(compile(8), first, 'returning to earlier flags reused the newer layout');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
