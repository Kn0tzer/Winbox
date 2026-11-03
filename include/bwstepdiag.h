#pragma once

// FD-127 step-ring diagnostics.
//
// The pipe2 freeze (FD-127) sits inside a host-side path whose progress
// could not be observed through the browser console: console delivery
// itself transits the main thread, so a missing log line cannot
// distinguish "never reached" from "reached but not delivered". The
// step ring is the drop-proof alternative: call sites record numbered
// steps (with the calling process/thread ids) into a fixed in-memory
// ring with plain atomic stores -- no locks, no logging -- and the
// guest thread dump (bw64_dump_guest_threads, which runs on the main
// thread and prints through the try-locked log) emits the ring's tail
// on every dump. Steps recorded by a thread that then blocks inside
// the instrumented path therefore show up in the next dump regardless
// of console delivery.
//
// All call sites are gated on BW_THREAD_DUMP=1 (bwStepDiagEnabled),
// the same switch as the thread dump, so production runs record
// nothing. Step numbers:
//   1  pipe2/pipe handler enter (syscall64)
//   2  ksocketpairFds enter, before the fd-table lock
//   3  ksocketpairFds fd-table lock found busy
//   4  ksocketpairFds fd-table lock acquired (before its log line)
//   5  ksocketpairFds lock log line returned
//   6  ksocketpairFds about to create the first socket
//   7  ksocket enter
//   8  ksocket AF_UNIX object constructed
//   9  ksocket AF_UNIX descriptor allocated (about to return)
//   10 ksocketpairFds first socket created
//   11 ksocketpairFds about to create the second socket
//   12 ksocketpairFds second socket created
//   13 ksocketpairFds pair wired, before the flag fixups
//   14 ksocketpairFds flag fixups done
//   15 ksocketpairFds about to return success
//   16 pipe2 handler: pair creation returned
//   17 pipe2 handler: fd writeback done
//   19 KProcess::allocFileDescriptor enter (before the fd-table lock)
//   21 KProcess::allocFileDescriptor lock acquired
//   20 KProcess::allocFileDescriptor about to return

bool bwStepDiagEnabled();
void bwStepNote(unsigned int step, unsigned int pid, unsigned int tid);
void bwStepDumpTail();
