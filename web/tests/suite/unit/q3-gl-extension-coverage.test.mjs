import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const shim = readFileSync(join(repoRoot, 'tools/rootfs64/libgl64/libgl64.c'), 'utf8');

function advertisedExtensions() {
  const legacy = shim.match(/static const GLubyte\* exts\s*=\s*\(const GLubyte\*\)\s*"([^"]+)"/);
  assert.ok(legacy, 'legacy GL_EXTENSIONS string must exist');
  const array = shim.match(/static const char\* const g_extList\[\]\s*=\s*\{([\s\S]*?)\n\};/);
  assert.ok(array, 'indexed GL extension list must exist');
  const arrayBody = array[1].replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
  return {
    legacy: legacy[1].split(/\s+/).filter(Boolean),
    indexed: [...arrayBody.matchAll(/"(GL_[A-Za-z0-9_]+)"/g)].map(match => match[1]),
  };
}

function getProcNames() {
  const entry = shim.match(/static const struct procEntry g_procs\[\]\s*=\s*\{([\s\S]*?)\n\};/);
  assert.ok(entry, 'GL proc table must exist');
  return new Set([...entry[1].matchAll(/E\((gl\w+)\)/g)].map(match => match[1]));
}

function functionBody(name) {
  const start = new RegExp(`\\bAPI\\s+[^\\n{;]*\\b${name}\\s*\\([^\\n]*\\)\\s*\\{`).exec(shim);
  assert.ok(start, `${name} must have a guest wrapper definition`);
  const bodyStart = start.index + start[0].length;
  let depth = 1;
  for (let i = bodyStart; i < shim.length; i++) {
    if (shim[i] === '{') depth++;
    else if (shim[i] === '}' && --depth === 0) return shim.slice(bodyStart, i);
  }
  assert.fail(`${name} wrapper has an unterminated body`);
}

function assertCoreTrapWrapper(name) {
  assert.match(functionBody(name), new RegExp(`gl64_trap\\(GL64_fn_${name}\\s*,`),
    `${name} must reach its dedicated bridge trap, not a fallback no-op`);
}

test('Q3-visible extension strings agree in legacy and indexed GL queries', () => {
  const { legacy, indexed } = advertisedExtensions();
  assert.deepEqual(indexed, legacy, 'glGetString and glGetStringi must expose the same ordered set');
  assert.equal(new Set(legacy).size, legacy.length, 'extension list must not contain duplicate tokens');
});

test('every Q3 extension-init path enabled by our GL string has real proc wrappers', () => {
  const { legacy } = advertisedExtensions();
  const advertised = new Set(legacy);
  const procs = getProcNames();
  const enabledQ3ProcSets = {
    GL_ARB_multitexture: [
      ['glMultiTexCoord2fARB', 'trap'],
      ['glActiveTextureARB', 'core-alias:glActiveTexture'],
      ['glClientActiveTextureARB', 'trap'],
    ],
    GL_ARB_vertex_buffer_object: [
      ['glGenBuffersARB', 'core-alias:glGenBuffers'],
      ['glDeleteBuffersARB', 'core-alias:glDeleteBuffers'],
      ['glBindBufferARB', 'core-alias:glBindBuffer'],
      ['glBufferDataARB', 'core-alias:glBufferData'],
    ],
  };

  for (const [extension, entries] of Object.entries(enabledQ3ProcSets)) {
    if (!advertised.has(extension)) continue;
    for (const [name, implementation] of entries) {
      assert.ok(procs.has(name), `${extension} is advertised, so ${name} must resolve to a real wrapper`);
      if (implementation === 'trap') assertCoreTrapWrapper(name);
      else {
        const target = implementation.slice('core-alias:'.length);
        assert.match(functionBody(name), new RegExp(`\\b${target}\\s*\\(`),
          `${name} must forward to ${target}`);
        assertCoreTrapWrapper(target);
      }
    }
  }
});

test('Q3 features with stubbed or missing entry points stay out of both extension strings', () => {
  const { legacy, indexed } = advertisedExtensions();
  const absentForQ3 = [
    'GL_EXT_compiled_vertex_array',
    'GL_EXT_texture_edge_clamp', 'GL_SGIS_texture_edge_clamp', 'GL_ARB_texture_border_clamp',
    'GL_EXT_texture_env_add', 'GL_ARB_texture_env_combine', 'GL_EXT_texture_env_combine',
    'GL_EXT_texture_filter_anisotropic',
    'GL_ARB_texture_compression', 'GL_EXT_texture_compression_s3tc', 'GL_S3_s3tc',
    'GL_ARB_vertex_program', 'GL_ARB_fragment_program',
    'GL_EXT_framebuffer_object', 'GL_EXT_framebuffer_blit',
  ];
  for (const extension of absentForQ3) {
    assert.ok(!legacy.includes(extension), `${extension} must not be advertised to Q3`);
    assert.ok(!indexed.includes(extension), `${extension} must not be advertised by glGetStringi`);
  }
});
