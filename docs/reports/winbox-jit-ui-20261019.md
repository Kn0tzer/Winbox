# Winbox JIT Settings deployment, 2026-10-19

The combined CPU JIT build is available at <https://kn0tzer.work.gd/wine>, which
redirects to `/winbox/`. Open the gear button, then choose **CPU JIT → On
(experimental)**. The preference defaults to Off, is saved in this browser, and
applies when you next start or restart a program. It controls both architectures.
Changing it leaves the active program running with its original engine.

The setting forwards `jit=0|1` through the shared launcher path used by demos,
uploaded programs, saved folders and Restart. Experimental JIT can be slower,
particularly for 32-bit programs; the Settings description states that limitation.
The x87 precision fallback and measured costs remain documented in the
[CPU validation report](winbox-jit-20261018.md).

The export now includes `frame-metrics.mjs`. Its omission previously stopped the
main application module from loading. Runtime release identifiers also include
the JS and WASM hashes. Main scripts, pthread scripts and the WASM payload use
that identifier in their request URLs, avoiding cached interpreter payloads after
deployment. The loader uses Emscripten 6.0.9's documented
[`Module.locateFile`](https://emscripten.org/docs/api_reference/module.html#Module.locateFile)
hook; pthreads inherit the versioned main-script URL.

The deployed executable source is commit `d7ee184e5`, with clean build provenance:

- WASM: `7b4ab076519bc0e152d057f58815953fc6496b23883f164ee1a3824cb290153e`.
- JS: `6592b2b5c0b8e9c81c4471b0038680e8dd1b13bdecf892f7f972d33df7779931`.
- Runtime URL release: `56b0753634e4`.

Validation before publication passed 133 repository unit groups, 104 legacy
tests, type checking and the pnpm dependency audit (zero reported advisories).
The fast runner skipped its one optional shader browser test. Browser UI checks
cover the default, persistence, both architectures, uploaded folders, switching
off, native focus, and 320/640/1920px layouts. Settings screenshots were inspected.
The real local launcher test executes generated code for x64 Notepad and an x86
Win32 fixture, then restarts the fixture with JIT disabled and observes zero
generated entries. Guest saves survive both restarts. Core runtime requests all
carry the release identifier.

The complete export replaced `/home/ubuntu/kn0tzer/winbox` through an atomic
directory exchange. No container restart was needed. The previous deployment
and deployment metadata are retained under
`/home/ubuntu/backups/winbox-jit-20261019-062827/`.
Public HTTPS checks confirm matching HTML, application modules, runtime loader,
JS, WASM and build metadata, plus the required COOP/COEP headers.

The same real launcher flow passed against public `/wine` over HTTPS, including
preference persistence, guest rendering, save preservation and versioned core
asset requests. It recorded the following generated-code entry counts:

| Live guest | JIT selection | Generated entries |
| --- | --- | ---: |
| x64 Notepad | On | 98,214,191 |
| Uploaded x86 Win32 fixture | On | 90,704,330 |
| x86 fixture after switching Off and restarting | Off | 0 |

The live browser reported no page errors. Its 286,296 lit guest pixels and
counter progression from 1 to 2 to 3 verify rendering and saves across restarts.
Raw results are in `test-results/launcher-redesign`,
`test-results/jit-settings-local`, `test-results/jit-settings-live`, and
`/tmp/winbox-jit-ui-20261019`.

Rollback is an atomic exchange of the live directory and the retained `winbox`
directory. Use it for a new launcher/runtime error or an asset-version mismatch,
then check that public build metadata again reports WASM `35e230d206ef…` and
that a program starts. Browser libraries and saves remain in browser storage;
this deployment has no database migration. Deployment metadata records the full
old and new build identities.
