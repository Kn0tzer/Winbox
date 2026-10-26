// web/tests/suite/unit/readicons-pe32plus.test.mjs — PE32+ executables must
// have their icon resources read, not skipped.
//
// source/ui/utils/readIcons.cpp read the resource directory only for PE32
// (magic 0x010b); a PE32+ executable (0x020b, every 64-bit Windows binary)
// hit a kwarn("Icon 20b not implemented") and produced no icon. The
// resource directory layout is identical once the data-directory array
// offset is adjusted: PE32+ optional headers carry 112 bytes of standard
// fields before the directories, PE32 carries 96 (sizeof the PE32 struct).
// This is a source guard: readIcons is host-UI code driven by a host file
// and the launcher's icon cache, so it cannot run in this harness; the
// guard pins the shared branch and the offset selection so the PE32+ path
// cannot silently regress to a skip.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../../../../source/ui/utils/readIcons.cpp', import.meta.url), 'utf8');

test('readIcons handles PE32 and PE32+ in one resource-directory branch', () => {
  assert.match(source, /optionalHeader->Magic == 0x010b \|\| optionalHeader->Magic == 0x020b/);
  assert.doesNotMatch(source, /Icon 20b/);
  assert.doesNotMatch(source, /not implemented/);
});

test('readIcons selects the data-directory offset by PE format', () => {
  assert.match(source, /dataDirOffset = optionalHeader->Magic == 0x020b \? 112 : \(U32\)sizeof\(Pe32OptionalHeader\)/);
  assert.match(source, /dataDirOffset\+16/);
  assert.match(source, /dataDirOffset\+20/);
});
