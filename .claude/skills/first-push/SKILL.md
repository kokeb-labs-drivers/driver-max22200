---
name: first-push
description: The gate before a new driver's first push to GitHub. Runs the local rehearsal of CI in the CI container, the independent review rounds and the evidence list, so the first CI run is green. Use when a new driver is built and about to be pushed for the first time.
---

# First push

A driver's first CI run costs about 20 jobs and the free runner pool queues them. Every wasted run delays every other
driver, so the first push happens only when everything a workstation can run has passed and an independent reviewer
found no blocker. Order matters: each step is cheaper than the one after it.

## 1. The rehearsal (you or the builder)

1. `tools/preflight.sh --container` (Docker; the first run builds the CI image, `tools/ci-container`). It runs the
   preflight and the coverage (MC/DC), ASan/UBSan and libFuzzer gates on Ubuntu 24.04 with the CI compilers. Required
   result: `0 failure(s)` and `0 gate failure(s)`. A skipped check is named in its last line: install the tool, do not
   push around it. Set `FUZZ_SECONDS` higher for a final run (CI fuzzes 60 s to 10 min per target).
2. The checks CI has and the container does not: the MISRA scan (CodeQL) and the ESP32 builds. Cover them by hand:
   run the `misra-review` skill (step 7 lists the patterns that failed on earlier drivers) and read
   `examples/esp32` against the pinned ESP-IDF version in `.github/workflows`.
3. `python3 tools/scaffold.py --check-drift .` reports nothing, and `python3 tools/check_standalone.py` passes: no
   product, board, project or personal name anywhere, copyright "The driver-<chip> authors".
4. `kokeb-docs hardware --check`, `figures`, `gen` and `check` leave no diff, and the strict docs build passes.
   The README shows the chip card, pinout, outline and block diagram (`docs/hardware.yml`).
5. No datasheet PDF in the tree: `git ls-files | grep -i '\.pdf$'` is empty; `docs/datasheets.yml` has vendor,
   document number, revision, date, URL and SHA-256.

## 2. The independent review (not the builder)

Reviewers read; they do not edit. Run each as a separate pass, in this order, and give each only the code and the
datasheet, not the builder's conclusions: `safety-reviewer`, `misra-review`, `hardware-lessons-review`,
`datasheet-conformance`, and a docs and diagram pass (every claim about the chip cites a datasheet section; every
figure agrees with `docs/hardware.yml`). A fixer then resolves every blocker and major finding; the reviewers check
the fixes again. Repeat until a round has no blocker. Earlier drivers needed two to four rounds. Doubt is moved to
the documented ambiguities, never dropped.

## 3. Evidence

Record in the PR body, or the first commit message if there is no PR: the template commit
(`.kokeb/template.yml`), the container preflight result, the coverage line / branch / condition numbers, the fuzz
seconds per target, the review rounds with the finding count of each, and what was not verified (always: hardware;
unless run: MISRA scan and ESP32 builds).

## 4. Push

One commit, `git commit -s`, message `<CHIP> driver, first version`; noreply identity, no attribution lines. Check
`git show --stat` is exactly the reviewed tree. Push to the new private repository. At most three repositories have
CI running at once. Then follow the `release` skill, section 5, for any red check.
