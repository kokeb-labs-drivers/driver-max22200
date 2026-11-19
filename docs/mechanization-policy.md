# Mechanization policy

Where each defect class of the [catalogue](recurring-defects.md) is caught, and how a class moves to a cheaper
catch. The goal: a defect is found by the cheapest check that can find it, and reviewers spend their time on
judgement, not on what a script can do.

## Decision rule

Catch a class at the first level that can decide it without judgement, in order of cost:

1. **Lint** (`tools/preflight.sh`, runs in seconds, on every builder's machine). Use it when the defect is a pattern in
   text or syntax: a forbidden include, a MISRA rule CodeQL reports, a missing `else`, a parameter name, a symbol
   named in docs that does not exist, a missing gitlink.
2. **Test, model or mutation** (host tests, the chip model's fault knobs, fuzz properties, compile-fail cases,
   mutants of a fix). Use it when the defect is behaviour: what the driver sends or returns when the chip or the bus
   misbehaves. A test is the right level when the model can be made to misbehave in exactly that way.
3. **Independent review question** (a reviewer who did not write the code). Use it only for what needs judgement:
   whether the datasheet was read correctly, whether a state machine is complete, whether the model is independent of
   the driver, whether a doc claim is true.

Every catalogue entry names its level under DETECTION. A class with no lint or test is a review class and costs
reviewer time every round; it is a candidate for promotion.

## Promotion

- A defect that reached a reviewer or the CI of a **second** driver is promoted one level up: review to test, test to
  lint where a pattern exists. The reviewer who finds the second occurrence writes the promotion item in the round's
  report; the template maintainer (the agent or person that owns the template branch) implements it before the next
  driver's first push, or records why it cannot be mechanised in the catalogue entry.
- A defect that reached a third driver after its promotion means the check is wrong: fix the check first, then
  look at the driver.
- Demotion never happens by silence: a check is removed only with a catalogue note saying what replaced it.

## What a lint may and may not do

- It reports `file:line`, the catalogue id, and the fix. It exits non-zero on a finding, in the preflight and the gate.
- It has **no false-positive tolerance**: before merging, it runs over every finished driver and the template; any
  finding on a driver that CI shows clean is a false positive, and the lint is not merged until it has none (narrow the
  pattern; do not add an ignore list per driver).
- It may be conservative the other way (miss cases): then the catalogue says `partial` and names the review question
  that covers the rest.
- It never edits files, never skips silently (a missing tool is reported as a skipped step in the final line, and the
  gate treats a skipped MISRA or ESP32 step as a failure), and has a selftest with one bad and one good case.
- It does not replace the CI scan: CI is the final word for MISRA.

## Recording a new finding

1. Append the finding to its catalogue entry (driver, review round or CI report file, finding id) and update the
   count and the index table; add a new entry only when no class fits.
2. Write the check at the level the rules above give: the lint, or the test pattern (model knob, mutant, fuzz property),
   or the reviewer's question in the skill.
3. Run it over the finished drivers (lint) or add the regression test that fails without the fix (test).
4. Add a `CHANGELOG.md` entry under `[Unreleased]` and the template change is released as a new template commit;
   drivers take it with `scaffold.py --check-drift --fix` (the skills and tools are managed files), which updates the
   `template` pin in `.kokeb/template.yml`. Mention the catalogue id in the commit subject.

## The pre-push gate

A driver is cleared for its first push only by `kokeb-tools/gate.sh <chip>` (the orchestrator's script, one run per
driver through the shared Docker lock). It writes a log and a verdict, and PASS needs all of:

- container preflight (`tools/preflight.sh --container`) with `0 failure(s)` and `0 gate failure(s)`, including the
  MISRA scan and the ESP32 builds; a skipped MISRA or ESP32 step is a FAIL;
- `handbook_check` reports 0 errors (this includes the library-subset include scan);
- `tools/check_standalone.py` passes;
- template drift: `scaffold.py --check-drift` reports nothing;
- no `.pdf` file is tracked;
- the `examples/esp32/scripts` gitlink (mode 160000) is present;
- the tree is staged as it will be committed.

No silent skips: every step prints a line, and a missing tool fails the gate.

## Roles

- **Builders** write code, models, tests and docs and run host tests and the quick preflight. They do not run the
  container gate, push, or create repositories.
- **The gate runner** is one serialized worker that runs `gate.sh` for one driver at a time and reports the verdict
  and the log; it does not edit the driver.
- **Reviewers** are read-only and independent. They are given the code, the model, the datasheet, the catalogue and
  the previous round's report, and **never the builder's conclusions** (the build report is read only in the
  claims-vs-code step). They run the [review-round](../.claude/skills/review-round/SKILL.md) protocol.
- **Fixers** change the driver, fix at the cause, add the test that fails without the fix, and answer each finding id.
- **The template maintainer** applies promotions and keeps the catalogue current.

## Backlog of promotions (state of the catalogue)

The classes whose target level is above their present level are listed in the catalogue index: RD-12 to RD-18 and
RD-23 (MISRA patterns, one `misra_lint` with a rule per id), RD-20 and RD-22 (ESP-IDF and compile-fail lints), RD-08
(`claims-symbols`), RD-07 (model standalone lint), RD-06, RD-01, RD-02, RD-03, RD-05, RD-10 and RD-25 (shared model
knobs and a template mutation runner), RD-21 (host start-up test of the ESP32 port).
