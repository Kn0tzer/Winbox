# System and Wine diagnostics

All 25 former repository-root `.mjs` probes are preserved here. Run them as
`node web/tests/diagnostics/<original-filename>.mjs`; their environment
controls and filenames are unchanged. Each resolves the checkout from its
own location and changes to the repository root before using relative
runtime/result paths. Imports no longer depend on `/home/ubuntu/boxedwine64`.

| Purpose | Preserved entrypoints |
| --- | --- |
| Unix sockets | `bind32-probe`, `bind64-probe`, `bindconnect-probe` |
| Filesystem/mmap | `checktmp-probe`, `fsnode-verify-probe`, `mmap-probe` |
| Wineboot and 32/64-bit startup | `b64-probe`, `vkboot32-probe`, `vkboot32-probe2`, `vkboot-minimal-probe`, `vkboot-persist-probe`, `wb-probe`, `wb-probe2`, `wb-probe3`, `wbdiag-probe`, `wbdiag2-probe`, `wbdiag4-probe`, `wineboot-probe`, `probe-tmp5` |
| Desktop, dispatcher and exception diagnosis | `desktoptest-seh-probe`, `dispatcher-test-probe`, `dt3diag-probe`, `verify-sh-fix` |
| Wineserver processes/counters | `wineserver-measure-probe`, `wscount-hex-probe` |

Names above omit `.mjs`. Before migration, `probe-tmp5.mjs` was byte-identical
to the retained `../wine10-clean-boot.mjs` and `../wine10-clean-boot-tmp.mjs`.
Its diagnostic entrypoint is retained too.

These are historical experiments, not an automatically passing regression
suite. Some select old runtime modes or use legacy probe-lock conventions.
Follow the build/probe protocol and inspect each experiment before running.
Useful output belongs in ignored `test-results/`; tests, fixtures and scripts
belong in maintained directories. See the [test catalog](../CATALOG.md) for
current runnable gates and coverage limits.
