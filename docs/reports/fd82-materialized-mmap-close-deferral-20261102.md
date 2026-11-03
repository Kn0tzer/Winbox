# FD-82 — 64-bit materialized file mappings join the close-event deferral (2026-11-02)

Lane `lane-fd82`, off integ `cc1d89ab3`. One Low entry, filed by the
FD-79/80 lane as the stated remainder of FD-79: the demand-paged
path's close-event deferral (CL-38) did not extend to the 64-bit
guest memory path. The host contract was already measured by the
FD-79/80 lane's reference programs (any live mapping of a
description — shared or private, read or write — defers
IN_CLOSE_WRITE / IN_CLOSE_NOWRITE to the last unmap), so this lane
re-measured nothing on the host; it convicted the emulator side and
wired the same contract into the materialized path.

## What the emulator did (measured, RED)

A 64-bit guest's file mappings are made by `sys_mmap64_file`, which
materializes them inside `KMemory64`: MAP_PRIVATE mappings are an
anonymous mapping plus a copy of the file bytes; MAP_SHARED mappings
adopt process-global registry pages keyed by node id. Neither
materialization recorded which open description the mapping belonged
to, and nothing on the path called the FD-79 accounting
(`FsOpenNode::noteMappedPagesAdded/Released`), so at fd close
`signalCloseEvent` always saw a live count of zero and the event was
delivered immediately, mapping or no mapping. Unlike the
demand-paged path's original failure (the event was *lost*, pinned
behind the KFile), here it is simply *early* — the materialized
mapping holds no KFile reference, so close proceeds at once. The RED
test below fails on the base in exactly that shape: the event fires
at fd close in every deferral case and never arrives at the
mapping's death.

## Fix

The contract is now tracked on the 64-bit path the same way FD-79
tracks it on the demand-paged path — per-description accounting,
with the memory layer reporting births and deaths:

- `KMemory64` gains a per-address-space table of its live
  file-backed mappings: start page -> page count + the `KFile`,
  pinned for the mapping's lifetime exactly as the demand-paged
  path's `MappedFile` pins it (a description can be closed and its
  last descriptor dropped while the mapping is live; the pinned
  KFile keeps the `FsOpenNode` alive to receive the accounting).
  Entries are ordered and non-overlapping and cover every address —
  the existing `ranges` map cannot serve here, as it only tracks
  the mmap region (>= 0x700000000) and carries no description.
- Births: `sys_mmap64_file` calls `KMemory64::noteFileMapping`
  once a mapping is established, for both materializations
  (after `mmapSharedFile` succeeds; in the private branch after the
  reservation/map, before the content copy — the mapping exists
  from that point whatever the copy does; see FD-92).
  `noteFileMapping` counts the birth *before* releasing whatever
  the new mapping replaced, so a same-description replacement
  never dips the live total through zero mid-operation.
- Deaths are reported before pages are invalidated or replaced,
  mirroring `KMemory::noteMappedFilePagesDying`'s placement:
  from `munmap` (the guest-visible death; KMemory64's munmap
  retains page buffers for Wine's lazy re-reads, which is a
  backing-store detail below the mapping contract), from
  overwrite inside `mmapAnonymousFixed` and `mmapSharedFile`,
  from `mremap`, from fork, and from `~KMemory64`.
- `mremap`: a shrink goes through `munmap`, which trims the entry.
  A grow-in-place or a move re-notes the resulting range when the
  whole source range is one file mapping
  (`fileMappingCoverLocked`); the move notes the destination
  *before* the source is unmapped, and the MREMAP_DONTUNMAP variant
  releases the source entry explicitly (its retained range is
  anonymous). The zero-length shared-page duplicate path notes the
  alias as a second live mapping of the description.
- Fork (`cloneFrom`): the child inherits the table without
  re-counting births — the description's count already includes
  those pages once, and either twin's deaths are reported against
  it (the release side saturates at zero), which is FD-79's stated
  fork model. MADV_DONTFORK pages are detached in the child's table
  *without* releasing: the parent's pages are still live.
- Address-space destruction (`~KMemory64`, reached at exec
  replacement and at teardown) releases every remaining entry, so
  a still-pending event is delivered at the mappings' true end
  rather than left to the open node's destructor backstop.

## Boundaries (stated exactly)

- The fork corner inherits FD-79's stated boundary: a description
  shared with another process is signalled by the first process to
  drop its last descriptor and its pages are counted across the
  sharers, so the event can arrive early; it is never lost.
- Mappings the emulator's own loader makes (not through a guest
  descriptor) carry no description and are outside the contract,
  as on the demand-paged path.
- `FsZipOpenNode` never generated close events and still does
  not; untouched. SysV shared memory is a different object;
  untouched.
- FD-82's filed text said the demand-paged path "is exact after
  CL-36"; the deferral closure is CL-38. Corrected in the FD-82
  entry and in CL-45.

## Proof

`testCloseWriteDeferral64` (x64 native suite; registered in both
test lists behind the same `BOXEDWINE_GUEST_X64` guard as
`testPreadv64Syscalls` — the materialized path exists only in the
64-bit build, so the main suite has no 64-bit path to drive; the
shared contract's main-suite guard remains
`testCloseWriteDeferral`). The test drives the real 64-bit syscall
dispatch (mmap 9 / munmap 11 / close 3 through `ksyscall64`) — the
materialized path itself is the thing under test — over seven
cases: shared-writable deferral, private-read NOWRITE deferral,
two mappings of one description, unmap-before-close, plain close,
overwrite by an anonymous MAP_FIXED mapping, and a partial unmap
of a two-page mapping.

- RED on the base (`test-results/fd82/red-closewrite64-x64.log`):
  1 test FAILED, ten assertions — the close event fired at fd
  close in every deferral case (A64, B64, C64, F64, G64) and never
  arrived at the mapping's death; the unmap-before-close and
  plain-close guards passed, so the close signalling itself was
  never in doubt — only the deferral was missing, as filed.
- GREEN with the fix (`green-closewrite64-x64.log`): the test
  passes. Full native suites, serial: main 668/668, 0 FAILED
  (263 s; the base count — this lane adds no main-suite test) and
  x64 70/70, 0 FAILED (69 baseline + this test), which also
  covers the existing KMemory64 consumers of the touched paths
  (mremap, madvise, sysinfo, self-test shared mappings).
- JS unit suite: 348 passed / 0 failed — the standing baseline;
  no harness or silent-failure adjustments were needed.
- Browser build on the fixed tree (clean staging only:
  `bootstrap.py --local` from this base's pinned
  runtime-assets-20261101 archive, "Restored and verified 13
  runtime assets", `--check` PASS): "Browser export verified" /
  BUILD COMPLETE, wasm `551ec728`, support archive
  `c4dbe35986a5` unchanged. (The FD-65 lane's 20261102 archive
  belongs to that branch's re-pin and is not this base's
  manifest.)

## Filed by this lane

- FD-92 (Low, provisional number): `sys_mmap64_file`'s private
  branch can return an error after the mapping has been
  established (the content read that follows the map fails; the
  pages stay mapped and the range stays reserved), where a failed
  host mmap leaves nothing mapped. Noticed while placing FD-82's
  birth accounting; left as found rather than changed inside an
  accounting fix.

## Register changes on this branch

FD-82 marked FIXED in place with a pointer to CL-45 and the
CL-36 -> CL-38 correction; FD-92 filed open; CL-45 filed
provisionally in the closure list (CL-40..CL-44 are held
provisionally by parallel branches; the composing merge verifies
by audit and renumbers if needed); a Section B row for this lane.
