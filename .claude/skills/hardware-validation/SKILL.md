---
name: hardware-validation
description: The three hardware validation levels (H0 host model only, H1 bench rig exercised by an agent with logs, H2 verified by a hardware engineer), the evidence each needs, the bench-rig document generated from the driver's example, the driver.yml and README fields and badge, the needs-hardware and hardware-verified labels, and the wording for release candidates that are not hardware-verified. Use when a driver is published, when a bench run happens, or when a release candidate is written.
---

# Hardware validation

A driver is tested against a model of the chip that its author wrote from the datasheet. That proves the code does
what the author understood, not what the chip does. This skill keeps the claim about hardware exactly as strong as
its evidence, in one field, one badge, two labels and one document.

## 1. The levels

| Level | Name | Means | Who may declare it |
| --- | --- | --- | --- |
| `H0` | Host model only | Passes the host tests, fuzz and gates against the register model. Never run on a chip. | The author (default for every new driver) |
| `H1` | Bench exercised | A defined rig with the real chip ran the driver's example, an agent drove and observed it, and the logs and the rig document are kept. | The agent that ran it, after the evidence of section 2 exists |
| `H2` | Hardware verified | A hardware engineer reviewed the rig against the datasheet, saw or re-ran the steps, and signed off in the repository. | The engineer; an agent never sets `H2` |

A level never rises on opinion. It drops back to `H0` when a change to the protocol, timing or start-up code lands
after the run and the affected steps are not re-run; the next release states it.

## 2. Evidence for each level

**H0.** The CI run, the coverage numbers and the fuzz seconds of the `first-push` evidence list; the model's
assumptions listed as documented ambiguities (`docs/hardware.md` and the contract page); bench steps written for
every behaviour only hardware can confirm (the pull-request template's "Bench status").

**H1** (everything in H0, plus):

- `docs/bench-rig.md` in the format of section 3, naming the MCU family, the rig's wiring, supply, bus settings and
  the firmware (driver version and commit id, SDK and compiler versions).
- The run log of each step: command, expected result, observed result, `pass`, `fail` or `skipped` with the reason.
  Raw logs are attached to the release candidate as an asset (`bench-logs-vX.Y.Z-rc.N.txt`); the repository holds the
  summary table, not the logs.
- Every datasheet behaviour the model assumed that the run contradicted: a model test that reproduces the real
  behaviour, a fix, and a changelog line. A run with no discrepancy says so.
- The faults that were injected on the rig where it is safe (a disconnected line, a wrong supply within the
  datasheet's limits, a held reset) and what the driver returned. Anything that could damage the chip or the
  rig is not injected; it stays a documented model test.
- A run is identified by driver version and commit id. Never by date or by the name of a person, board or product
  (`tools/check_standalone.py` rejects dated history and other names); describe the rig by what it is: "an
  ESP32-class development board, 3.3 V, SPI at 1 MHz".

**H2** (everything in H1, plus):

- A sign-off in a pull request or issue of the repository by the engineer: which steps they re-ran or witnessed,
  that the wiring document matches the datasheet's recommended circuit, and any step they refused. The repository
  records the role ("hardware engineer review") and the version, not the person's name.
- The `hardware-verified` label on that pull request or issue.
- A second look at the failure paths that matter for safety (switch-off, over-current, watchdog), at the rig if
  the driver can drive an actuator.

## 3. The rig document (`docs/bench-rig.md`)

Generate it from the example the rig runs (`examples/esp32/main/` or the platform's folder) and `docs/hardware.yml`:
the pins in the example's port are the rig's wiring. Do not type a pin from memory. Sections, in this order, all
required at H1 and H2 (the checker looks for the headings):

```markdown
# Bench rig

## Purpose
One sentence: which driver version this rig validates and at which level (H1 or H2).

## Equipment
| Item | Role | Setting |
| --- | --- | --- |
(MCU family board, the chip on its breakout or test board, supply, analyser or meter if used. Roles, not product names.)

## Wiring
| Chip pin | Function | MCU pin | Notes |
| --- | --- | --- | --- |
(one row per connected pin, from the example's port and hardware.yml; pull-ups, level shifters, series parts)

## Bus settings
Mode, clock, address or baud rate, as the example configures them, each with the datasheet section.

## Firmware
Example name, driver version and commit id, SDK and version, build command, flash command.

## Steps and results
| # | Step | Expected (datasheet section) | Observed | Result |
| --- | --- | --- | --- | --- |
(each bench step from `docs/hardware.md`, the pull-request bench steps and the contract page; pass, fail or skipped)

## Discrepancies with the model
Each difference found, the model test and fix that followed, or "none".

## Not covered
What this rig cannot show (temperature range, long-term drift, EMC, other bus speeds, other silicon revisions).
```

## 4. Fields, badge and labels

`driver.yml` (the manifest also read by the catalogue; extra keys are allowed):

```yaml
hardware:
  level: H0          # H0, H1 or H2
  rig: null          # H1/H2: docs/bench-rig.md
  evidence: null     # H1/H2: the release asset with the logs, e.g. bench-logs-v0.1.0-rc.2.txt
  reviewed: null     # H2: "hardware engineer review", then the release it applies to, e.g. v0.1.0-rc.3
```

The same file carries the driver's safety class, which says what the chip path can do to the physical world:

```yaml
safety:
  class: sensing     # passive, sensing or actuating
```

- `passive`: nothing a safety decision rests on (memory, display, communication, status LEDs).
- `sensing`: a measurement a safety decision may rest on, so a wrong value is the hazard.
- `actuating`: it can move, energise, heat or switch something, so a wrong command is the hazard. Changes to such a
  driver take the `safety` label and a human review (KB-AGT-013).

Pick the highest that applies to any function of the chip the driver exposes. It is the driver's own class; the
product that uses the driver classifies itself (KB-ARCH-030). The organisation page shows it next to the level.

README, directly under the other badges (the badge text and colour follow the level):

```markdown
[![Hardware: H0](https://img.shields.io/badge/hardware-H0%20host%20model-lightgrey)](docs/testing.md#on-hardware)
[![Hardware: H1](https://img.shields.io/badge/hardware-H1%20bench%20exercised-yellow)](docs/testing.md#on-hardware)
[![Hardware: H2](https://img.shields.io/badge/hardware-H2%20verified-brightgreen)](docs/testing.md#on-hardware)
```

Keep only the line for the current level. The README's **Status** row says the same thing in words (section 5).
`python3 tools/check_hardware.py` (part of the preflight) checks that the level and the safety class are valid, that the
badge matches it, and that H1 and H2 name an existing rig document with the required headings. Release the badge change
in the same pull request as the evidence.

Labels (`.github/labels.yml`; `kokeb_org.py --apply` creates them):

- `needs-hardware`: on any pull request or issue whose behaviour only a bench can confirm, and on the tracking issue of
  a release candidate while the level is `H0`. Remove it when an H1 run covered that behaviour.
- `hardware-verified`: only on a pull request or issue the engineer signed off (H2). The comment says which rig
  document and which version.

## 5. Wording for releases that are not hardware-verified

Use these texts in the release body (first lines), the README **Status** row and the changelog entry. State the level,
then what was and was not done. Never write "tested" without "against a model" at H0.

- **H0:** "Release candidate vX.Y.Z-rc.N. Hardware level H0: tested against a model of the chip written from the
  datasheet; **not hardware-verified**. It has not been run on a chip. The bench steps are in `docs/hardware.md`. Use it
  for evaluation and report differences you find on a bench."
- **H1:** "Hardware level H1: the example ran on a bench rig (`docs/bench-rig.md`, logs attached) and the steps marked
  pass behaved as the datasheet states. **Not independently hardware-verified** (no hardware engineer review yet).
  Discrepancies found and fixed: <list or none>."
- **H2:** "Hardware level H2: verified on the rig of `docs/bench-rig.md` by a hardware engineer review of vX.Y.Z-rc.N.
  Not covered: <the rig's Not covered section>."

A final `vX.Y.Z` after only H0 says in its first line that it is **not hardware-verified**; whether that is allowed for
a given driver is the owner's decision (`release` skill, section 1).

## 6. Run an H1 bench (agent)

1. The rig exists and is described: if no human wired one, stop at H0 and say what is needed (section 3 is the
   request). Never guess a wiring.
2. Build the example at the commit of the candidate (`add-port` and the ESP-IDF version pinned in
   `examples/esp32/app_config.yml`), flash, capture the serial log to a file, run each step of section 3.
3. Safety first: for a driver that drives an actuator or a high voltage, do not enable it on a rig that is not
   explicitly built for it; a step that could damage the chip or the rig is `skipped` with the reason.
4. Write `docs/bench-rig.md`, attach the log, add the model tests for discrepancies, update `driver.yml`, the badge and
   the Status row, and open the pull request with the `needs-hardware` label removed only for the covered steps.
5. Stop and report on a result that contradicts the datasheet: do not change the model to make it pass; the
   datasheet reading or the silicon is the question (`datasheet-conformance` skill).
