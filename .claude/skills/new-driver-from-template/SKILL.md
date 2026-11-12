---
name: new-driver-from-template
description: Turn a repository created from driver-template into a real driver or library - run tools/setup.py, then fill in the register map, port, tests and docs. Use right after `gh repo create --template kokeb-labs/driver-template`.
---

# New driver from the template

The template's example chip is the fictional XMPL100. `docs/template.md` explains every file.

## 1. Scaffold

If `tools/setup.py` is gone, the setup already ran: start at step 2.

1. Branch `feat/initial-driver`.
2. Run `python3 tools/setup.py` in place. It infers the kind and name from the repository name (`driver-<chip>`
   is a driver, any other name a library), asks for the bus (`spi`, `i2c`, `uart`, `adc`, or `none` for a part
   with GPIO pins only) and a one-sentence description (and the namespace for a library), scaffolds, then builds
   and runs the host tests. Without a terminal pass the answers as flags:
   `python3 tools/setup.py --non-interactive --bus i2c --description "<one sentence>"`. Add
   `--datasheet <url>` to link the datasheet (only the link: never download or commit the PDF). `--org`,
   `--repo`, `--author` (default `The <repo> authors`) and `--license MIT|Apache-2.0` override what it reads
   from the git remote;
   `--platforms` picks the example platforms (`host,esp32` for a driver, `host` for a library).
3. Read the checklist it prints; it is also the PR description's to-do list.
4. Run the `verify` skill: the renamed example must still build (including the strict-dialect header checks),
   pass its source checks and publish before you change anything.
   Commit this state on its own, signed off (`git commit -s -m "Scaffold <CHIP> from driver-template"`).

## 2. Replace the example chip (drivers)

Use the `datasheet-conformance` skill for every step, one subsystem at a time:

1. `inc/<chip>_registers.hpp`: the real register map with datasheet section references.
2. `inc/<chip>_types.hpp`: the chip ID, timing constants, enums, `Config` with the real reset values,
   presets, `Validate()`. Keep the base `Error` set; add chip-specific values from 16 only when a call returns them.
3. `tests/mock_<chip>.hpp`: the register model of the real chip, then the tests, then the driver
   (`inc/<chip>.hpp`, `src/<chip>.ipp`).
4. The port (`inc/<chip>_<bus>_interface.hpp`): keep the standard hooks for the bus; add a hook only for a
   protocol that does not fit, and list it on the Porting page.
5. `examples/host/`: the host port over the new register model and the example program.
6. `examples/esp32/main/` (when scaffolded with `esp32`): the ESP-IDF port pins and the example. Keep the
   header self-check `<chip>_cxx20_check.cpp`: it is the only C++20 conformance check inside the ESP-IDF build.
7. Write the driver to the handbook's C++20 profile and the MISRA patterns from the first line (the
   `cpp20-profile` skill, and step 7 of the `misra-review` skill: typed literals for constant arguments,
   `default: break;` in enumeration switches, parenthesised operands, constant loop bounds, `EnumLast<E>` instead
   of a sentinel enumerator). Before the first PR run the `misra-review` and `hardware-lessons-review` skills, or
   the `safety-reviewer` agent, over the whole repository; the `MISRA` CI job then checks every translation unit.
8. **Moving a driver from an older repository.** Take the behaviour, never the history: rewrite each part into
   this layout and these conventions, and describe it as this chip's design with its datasheet section. Drop
   every reference to another product, board, project or driver repository, dated lab notes and old ownership
   lines; `tools/check_standalone.py` checks this. What a bench taught becomes a model test and a design rule, or
   a bench step marked **not verified on hardware**.

## 3. Docs and metadata

1. `docs/datasheets.yml`: it still holds the fictional XMPL100 placeholder (only `url` is set, by `--datasheet`).
   Fill in the real datasheet's vendor, number, revision, date, `url`, `pdf` (a stable direct link or `null`) and
   the `sha256` of the PDF you used (never commit the PDF), and one topic per section the driver uses (section,
   printed page, driver API, the docs pages that explain it). Every docs page a topic names keeps its
   `<!-- generated: datasheet-refs -->` box; remove the box from a page no topic names. A library has none of this.
2. `docs/hardware.yml`: it still holds the fictional XMPL100 (an SPI part). Replace it with the real chip: the
   package and every pin from the datasheet's pin configuration chapter; `package.outline` (the body in
   millimetres, the pitch and the height) from the package outline drawing; `package.variants` from the ordering
   information (every other package the part is sold in, with its part-number suffix; delete the list for a
   single package); `supply` from the electrical characteristics; the hooks from the port header, the blocks from
   the block diagram and one circuit from the application chapter. Cite section and page for each fact and leave
   out a number the datasheet does not give. The README's pinout, outline and block figures and its chip card
   (`<!-- generated: chip-card -->`) are drawn from this file, so the module view of the chip is never typed.
3. Every page in `docs/`: the chip card, pins, timing, power, errors; the README's paragraph, At a glance table,
   wiring and documentation table. Remove every statement about the XMPL100.
4. `kokeb-docs figures && kokeb-docs gen`, `mkdocs build -f _config/mkdocs.yml --strict`.
5. `CHANGELOG.md`: `## [Unreleased]` / `### Added` describing the first version. Headings are only `### Breaking`,
   `### Added`, `### Changed`, `### Deprecated`, `### Removed`, `### Fixed`, `### Security` (the `release` skill
   and CI check this); anything else is a bold lead-in under one of them.
6. The repository's About comes from its entry in `kokeb-labs/.github` `org/repos.json` (`docs/repo-setup.md`,
   step 2): the README's first sentence as the description, and the shared topics plus the bus, the device
   category, the vendor and the part.
7. Run `tools/preflight.sh` until it reports `0 failure(s)`, then push once and open the PR; give the owner
   `docs/repo-setup.md` for the org settings. Before merging a driver that switches loads, have an independent
   reviewer (the `safety-reviewer` agent, run by someone who did not write the code) check it against the
   datasheet: a model written by the same author shares the author's assumptions, so green tests are not enough.
8. After the merge, do not tag until `CI` on `main` is green; then follow `docs/repo-setup.md` section 6 (release
   candidate, public, Pages) and the `release` skill, section 5, when a check goes red for a reason outside the code.
