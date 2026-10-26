// web/tests/suite/unit/host-ui-paths.test.mjs — the low-severity native
// host/UI rows from the coverage register, pinned so the fixes cannot
// silently revert: quit is gated on a failed save, the empty file-list
// branch reports an error, settings error paths log, desktop link names
// are stripped of .lnk, a stuck download socket is bounded, and the audio
// backend selection documents its single-backend invariant.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = rel => readFileSync(new URL(rel, import.meta.url), 'utf8');

test('quit is cancelled when saving the current view fails', () => {
  const mainui = read('../../../../source/ui/mainui.cpp');
  const quit = mainui.match(/if \(event\.type == SDL_QUIT\) \{[\s\S]*?\n        \}/)?.[0];
  assert.ok(quit);
  assert.match(quit, /!currentView->saveChanges\(\)/);
  assert.match(quit, /kwarn_fmt\("uiLoop: quit cancelled/);
  assert.doesNotMatch(mainui, /:TODO:/);
  // the file-list branch that used to do nothing now reports the error
  assert.match(mainui, /\} else \{\s*\/\/ The file list download finished[\s\S]*?new OkDlg\(Msg::GENERIC_DLG_ERROR_TITLE, getTranslation\(Msg::ERROR_NO_FILE_LIST\)/);
});

test('options save location relies on the saveChanges gate, with the reason recorded', () => {
  const options = read('../../../../source/ui/controls/optionsView.cpp');
  assert.doesNotMatch(options, /:TODO:/);
  assert.match(options, /No focus-loss tracking is needed for this field/);
  assert.match(options, /saveChanges\(\)/);
});

test('settings error paths log instead of silently continuing', () => {
  const settings = read('../../../../source/ui/data/globalSettings.cpp');
  assert.doesNotMatch(settings, /:TODO: error msg/);
  assert.match(settings, /kwarn_fmt\("GlobalSettings: demo icon path has no directory part/);
  assert.match(settings, /kwarn_fmt\("FileSystemZip::getLocalFilePath: file path has no directory part/);
});

test('desktop link apps are named without the .lnk extension', () => {
  const container = read('../../../../source/ui/data/boxedContainer.cpp');
  assert.doesNotMatch(container, /parse link to get name/);
  assert.match(container, /name\.endsWith\("\.lnk", true\)\) \{\s*name\.remove\(\(int\)name\.length\(\) - 4\);/);
});

test('runOptions records why no dependency pass is needed', () => {
  const appFile = read('../../../../source/ui/data/appFile.cpp');
  assert.doesNotMatch(appFile, /find dependencies/);
  assert.match(appFile, /No dependency pass is needed here/);
});

test('a stuck download socket is bounded by a low-speed limit and cancel is explained', () => {
  const network = read('../../../../source/util/networkutils.cpp');
  assert.match(network, /CURLOPT_LOW_SPEED_LIMIT, 1L/);
  assert.match(network, /CURLOPT_LOW_SPEED_TIME, 120L/);
  const dlg = read('../../../../source/ui/controls/downloadDlg.cpp');
  assert.doesNotMatch(dlg, /what if the socket is stuck/);
  assert.match(dlg, /low-speed/);
  assert.match(dlg, /networkutils\.cpp/);
});

test('audio backend selection is a single-backend invariant, not a missing choice', () => {
  const audio = read('../../../../platform/sdl/knativeaudio.cpp');
  assert.doesNotMatch(audio, /:TODO:/);
  const create = audio.match(/std::shared_ptr<KNativeAudio> KNativeAudio::createNativeAudio\(\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(create);
  assert.match(create, /Exactly one backend is registered per platform/);
  assert.match(create, /availableAudio\.front\(\)/);
  // init() registers exactly one backend in this translation unit
  assert.equal((audio.match(/availableAudio\.push_back/g) || []).length, 1);
});
