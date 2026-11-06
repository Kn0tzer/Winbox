# Task standards — how recurring work is done in this project

One written standard per recurring task, in this one directory. Each
standard describes the tools that actually exist today and the order
that actually works, so a worker who has never done the task can
follow it end to end and get the same result as an experienced one.

| Task | Standard |
| --- | --- |
| Starting a fresh working copy | [fresh-working-copy.md](fresh-working-copy.md) |
| Building (each kind of build) | [building.md](building.md) |
| Running checks, incl. the checking queue | [checks.md](checks.md) |
| Recording results and filing defects | [recording-and-defects.md](recording-and-defects.md) |
| Closing out a finished piece of work | [closeout.md](closeout.md) |
| Combining finished work into the main line | [combining.md](combining.md) |

Rules that govern the standards themselves:

- A standard is changed only by doing the task, finding where the
  written words and reality disagree, and fixing the words in the
  same piece of work. Every standard carries the date and the record
  of its last literal verification.
- If following a standard literally surfaces a product defect, the
  defect is filed on the register (`docs/defect-register.md`) and
  the standard is recorded as blocked at that step — the product is
  not fixed inside a standards change.
- Where a standard names a number (a suite total, a row count), the
  number is the standing figure from the last gate, quoted so a
  worker can spot drift. Totals grow as tests are added; the verdict
  is always the failure count, never the total.
- Nothing here overrides the register's own maintenance rule or the
  failure-logging standard (`docs/failure-logging-and-recovery.md`).

Last verified: 2026-11-05, by following every standard literally —
see `docs/reports/standards-verification-20261105.md`.
