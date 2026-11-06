# Standard: recording results and filing defects

Two artifacts make a piece of work real: its **record** and its
**register entries**. A lane's report does not count as filed until
both exist.

## 1. The lane record

- One file per piece of work: `docs/reports/<lane>-<subject>-<yyyymmdd>.md`,
  committed on the lane branch as part of the work (models: the
  records already in `docs/reports/`).
- It states, in plain terms:
  - the end state the work set out to reach, and whether it did;
  - what was actually done, commit by commit;
  - the proof: figures quoted from the runs' own preserved logs
    (suite totals, build identity, hashes), never paraphrased from
    another agent's summary;
  - what was **not** done — caveats, unrun checks, adjudications —
    stated as plainly as the successes;
  - the register changes the work made (entries filed, entries the
    work answers, provisional closure claims).
- Before reporting completion, re-read the branch tip, the record,
  and the register change from disk and quote them. A completion
  report whose hashes or figures cannot be re-read from disk is
  not a completion.

## 2. Filing a defect

The register, `docs/defect-register.md`, is the project's single
development queue. Its own preamble and maintenance rule govern;
this standard is the mechanics.

- **Where:** section A (Open), under the heading that fits
  (Product-facing first; follow the existing headings).
- **Number:** the next `FD-NN` after the highest number in the
  **integration branch's** register — not your branch's, which may
  be behind. The number is **provisional** until composition:
  parallel lanes can claim the same number, first-composed keeps
  it, and the merge lane renumbers the rest, preserving their text.
  Say "provisional" in your record when you file.
- **Form:**

      - **FD-NN — <title in plain words: what a user or a lane
        would see>.** *(Severity)*
        <what happens, how it was found, the evidence (record path,
        preserved log), and the closing terms — what a fix must
        show to close this entry.>

- **Severity** uses the register's own scale (Critical / High /
  Medium / Low), defined in its preamble.
- **When to file:** any defect you find but do not fix — including
  defects found while doing something else, and defects a check
  surfaces that you adjudicate as real. File before your report
  counts as filed. "Examined and deliberately not registered"
  outcomes go in the register's Appendix, with the reason.

## 3. Fixing a defect on a lane branch

- The section A entry **stays open**. A fix that exists only on a
  branch does not close anything.
- Add a row to section B's table:
  `| lane-<name> | FD-NN: <what the fix does> | <proof on the branch> |`.
- If your record claims the fix complete, claim a provisional
  closure number (`CL-NN`, next after the integration register's
  highest) **in your branch's register copy and your record**,
  marked provisional. It becomes real only when the merge lane
  moves the entry to section C at composition.

## 4. Closing an entry (merge lane only)

- At composition, the entry moves from section A to section C as:

      - **CL-NN — <title>.** <what was wrong>. Fixed in commit
        `<sha>` <and how>. Proof: <record path / preserved evidence>.

- Closing requires naming the **fixing commit and the proof**.
  A "not a defect" outcome closes as a CL entry marked
  NOT-A-DEFECT, with the evidence that disproved it.
- Entries are **never deleted** — not when fixed, not when wrong.

Verified literally: 2026-11-05 —
`docs/reports/standards-verification-20261105.md`.
