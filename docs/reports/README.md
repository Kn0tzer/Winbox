# Historical runtime reports

These 25 Markdown reports were preserved from `test-results/` during the
2026-10-13 maintenance. Their relative subdirectories and findings are
unchanged. Dates, hashes, passing operations and failures describe the
runtime measured by each report, not a fresh run of the current checkout.

- [D3D conformance matrix](d3d-conformance-MATRIX.md): recorded D3D9–12
  API/pixel buckets, consumed by the static suite D3D guard.
- [Bridge coverage](bridge-coverage/README.md): Vulkan/OpenGL matrices and
  bug notes.
- [CPU differential report](cpu-differential/REPORT.md): instruction forms,
  oracle comparisons and timing buckets.
- [Timing method](frametime-profile-pong2/METHOD.md) and
  [ranked measurements](frametime-profile-pong2/RANKED.md).
- [Wine32 report](wine32-final/report.md), standalone Baldi/GL reports,
  and the preserved D3D9 and maintenance inventories.

Raw logs, screenshots and captured chunks referenced by these reports are
archived under `/home/ubuntu/backups/boxedwine64-maintenance-20261013/`.
Restore the relevant capture directory before using replay/analysis tools.
Fresh probes write ignored `test-results/`; generated reports there can be
reviewed and explicitly promoted here with their date/runtime provenance.
Regression source recovered from the same output directory now lives in
`tools/regression/` and `testdata/`.
