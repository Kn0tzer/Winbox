Provide JSON cases with instruction bytes, GPRs, EFLAGS, eight XMM/MMX values, and 64 memory bytes.
Run `python3 tools/cpu-diff32/gen.py cases.json --out-dir /tmp/winbox-finish/work-F1/batch --run-qemu --cpp source/test/cpu/testCpuDiff32Generated.cpp`.
The tool builds a static i686 ELF, runs it with `qemu-i386-static`, and emits the generated native tests.
Exclude host-dependent system instructions; QEMU user mode raises SIGILL for MONITOR/MWAIT, which have native no-op tests.
