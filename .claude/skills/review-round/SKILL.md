---
name: review-round
description: The protocol of one independent review round on a driver before its first push or release - who runs it, what the reviewer is given, mutant testing of the previous round's fixes, the claims-versus-code check, the report format and the stop rule. Use when organising or running a review round.
---

# Review round

A round is one independent reviewer, one fixer, one verdict. Rounds repeat until the stop rule holds. Earlier drivers
needed two to six rounds; the catalogue ([`docs/recurring-defects.md`](../../../docs/recurring-defects.md)) and the
lints exist to make that number small, and the policy ([`docs/mechanization-policy.md`](../../../docs/mechanization-policy.md))
says which findings move from review to test to lint.

## Who runs it

The orchestrator (or the person) starts the round, after the gate runner reported PASS for the current tree. The
reviewer did not write the driver, its model or its tests, and is read-only: it works in a scratch copy of the tree.
The fixer is a different worker from the reviewer. Reviewers never see the builder's conclusions.

## Inputs given to the reviewer

The tree, the datasheet, the catalogue, the template's skills, the previous round's report (round 2 and later) and
the gate verdict and log. Not given: the builder's self-review, or "what to expect". The build report and the docs
are inputs to the claims check only.

## Steps

1. **Run the mechanical results first** (preflight lines, gate log, CI lines) and do not re-discuss them.
2. **Verify the previous round's findings at the cause.** For each BLOCKER and MAJOR, read the fix and then build
   **mutants**: revert the fix, or negate, drop or change a constant in the guard it added, using a runner outside the
   tree (quote the test binary path: a space in a path once made every mutant look killed). A fix is accepted only if a
   unit test fails on its mutant; the fix must be at the cause, not at the symptom, and the fuzz invariant it touches
   is weakened to the true weaker invariant, never deleted. Record the mutant table: id, edit, killing test or SURVIVES.
3. **Fresh pass with the skills**: `cpp20-profile`, `misra-review`, `hardware-lessons-review`, `datasheet-conformance`
   in review mode, and the docs and diagram pass (each claim cites a datasheet section; figures agree with
   `docs/hardware.yml`). Spend effort on judgement: datasheet conformance, state-machine completeness, model
   independence (RD-28, RD-10, RD-07).
4. **Claims versus code.** List every behavioural claim in the build report, `docs/`, Doxygen, the contract and the
   coverage matrix ("never", "always", "returns", "each fix has a test", numbers of tests and rows). For each, cite the
   code line and the test that backs it, or raise a finding (RD-09, RD-08). Check numbers by running the tool.
5. **Probes.** For each finding, a scratch probe or test that reproduces the failing scenario, so the fixer has a
   reproduction; run sanitizers and the fuzz target on the fixed tree where the fix touched them.
6. **Report** (format below), and append the round's open items to the gaps ledger.

## Report format

Header: tree, datasheet, commands run, scratch location. Then sections in this order: verdict on the previous round's
findings (with the mutant table), findings, checks that came out clean (with evidence), not verified, counts.

- Severity: `BLOCKER`, `MAJOR`, `minor`, as defined in the `safety-reviewer` agent.
- One entry per finding: id (`R3-1`), severity, catalogue id, `file:line`, the failing scenario, the fix and the test
  that proves it.
- A list of **documented ambiguities** (each with its model knob) and of what only hardware can confirm.
- Counts line (`BLOCKER n, MAJOR n, minor n`) and, last, exactly one line: `NO BLOCKER` or `BLOCKERS REMAIN`.

## Stop rule

Stop when a round has **no BLOCKER and no MAJOR**, every previous finding is verified by a killed mutant, the claims
check is clean, and the gate verdict for the final tree is PASS. A MAJOR is never carried into the first push. Minors
may be taken before or after. If a round finds a defect of a class already in the catalogue, that is a promotion
item (see the policy), recorded in the report. If three rounds in a row find new MAJORs in the same area, stop
patching and ask for a redesign of that area. After the stop rule holds, `first-push` continues with the evidence list.
