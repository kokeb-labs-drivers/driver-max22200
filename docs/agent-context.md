# Agent context

What an agent needs before it touches this repository: the flow, the skill for each step, the exact local checks, the
file layout, the standing rules, the pitfalls that failed CI before, and when to stop. Read [AGENTS.md](../AGENTS.md)
for the repository itself. This page is managed by the template (`scaffold.py --check-drift --fix` ships it) and
`tools/check_skills.py` checks that every skill named here exists and that the files its commands name exist.

## 1. The flow and the skill for each step

Each step has an input, an output and an exit rule. Do not skip a step and do not start the next before the exit rule
holds.

| # | Step | Skill | Output | Exit rule |
| --- | --- | --- | --- | --- |
| 0 | Plan: new driver, extension or duplicate, and the API shape | [`plan-driver`](../.claude/skills/plan-driver/SKILL.md) | `placement.md` with a verdict | Verdict `NEW`; otherwise stop or extend |
| 1 | Scaffold | [`new-driver-from-template`](../.claude/skills/new-driver-from-template/SKILL.md) | A building driver with the example chip renamed | `verify` passes on the untouched scaffold |
| 2 | Build from the datasheet | [`datasheet-conformance`](../.claude/skills/datasheet-conformance/SKILL.md), [`add-driver-feature`](../.claude/skills/add-driver-feature/SKILL.md), [`add-port`](../.claude/skills/add-port/SKILL.md) | Register map, model, tests, driver, port, examples, docs | Host tests green; every datasheet topic recorded |
| 3 | Self-review | [`cpp20-profile`](../.claude/skills/cpp20-profile/SKILL.md), [`misra-review`](../.claude/skills/misra-review/SKILL.md), [`hardware-lessons-review`](../.claude/skills/hardware-lessons-review/SKILL.md) | Findings fixed | Every catalogue class in `recurring-defects.md` answered |
| 4 | Verify | [`verify`](../.claude/skills/verify/SKILL.md) | Local gate result | `tools/preflight.sh` ends `0 failure(s)` |
| 5 | Independent review | [`review-round`](../.claude/skills/review-round/SKILL.md) | A report per round | `NO BLOCKER` and no MAJOR |
| 6 | First-push gate | [`first-push`](../.claude/skills/first-push/SKILL.md) | Container rehearsal and the evidence list | `0 failure(s)` and `0 gate failure(s)` |
| 7 | Publish | [`publish-driver`](../.claude/skills/publish-driver/SKILL.md) | Public repository, one commit, green `main`, Pages | `CI gate / gate` green on `main`; the owner did the owner-only steps |
| 8 | Release candidate | [`release`](../.claude/skills/release/SKILL.md) | Tag `v0.1.0-rc.N` | Release workflow green; checksums verified |
| 9 | Hardware | [`hardware-validation`](../.claude/skills/hardware-validation/SKILL.md) | Level H0, H1 or H2 stated and evidenced | Badge, `driver.yml` and release text agree |
| 10 | After release | [`bump-pin`](../.claude/skills/bump-pin/SKILL.md) | A pin-bump pull request per consumer | Consumer CI green |

A change to an existing driver skips steps 0, 1 and 7: use `add-driver-feature` or `add-port`, then steps 3 to 5 and a
pull request (AGENTS.md, "Branches, commits and pull requests"). The agent
[`safety-reviewer`](../.claude/agents/safety-reviewer.md) runs the review skills read-only.

## 2. The local gate: commands and what passes

Run from the repository root. Set `VENV` (a venv with the docs theme, lizard, yamllint), `CI_TOOLS_DIR` (a
`kokeb-labs/ci-tools` checkout) and `CLANG_TIDY` (version 20) when the tools are not on `PATH`. A step the preflight
skips is named in its last line: install the tool; do not push around it.

| Command | Passes when |
| --- | --- |
| `tools/preflight.sh` | Last line `== 0 failure(s)` and no skip you can fix |
| `tools/preflight.sh --container` | `0 failure(s)` and `0 gate failure(s)` (coverage, ASan/UBSan, fuzz in the CI image) |
| `python3 tools/scaffold.py --check-drift .` | `no template drift` (or each difference is listed under `local` with a reason) |
| `python3 tools/check_standalone.py` | Exit 0: no other project's name, no dated history, the right copyright holder |
| `python3 -I tools/check_skills.py` | Every skill named in this page exists, has front matter, and names existing files |
| `python3 -I tools/check_layout.py` | One source per header, SDK code only in `src/boundary/` |
| `python3 -I tools/check_hardware.py` | `driver.yml` hardware level, README badge and rig document agree |
| `python3 -I tools/check_language.py` | `driver.yml` `language:` block and every place that repeats the C++ standard agree (`--fix` rewrites them) |
| `python3 tools/misra_lint.py --root .` | No error; fix warnings in files you touch |
| `kokeb-docs figures && kokeb-docs gen && kokeb-docs check` | No diff afterwards (`git diff --exit-code docs README.md`) |
| `mkdocs build -f _config/mkdocs.yml --strict -d /tmp/site` | Exit 0, no warning |
| `git ls-files \| grep -i '\.pdf$'` | No output |
| `git add -A && git ls-files -s examples/esp32/scripts` | Mode `160000` (a gitlink), if `examples/esp32` exists |

CI-only checks, which a workstation cannot run: the MISRA scan and the ESP32 builds. Cover them by reading the
`misra-review` skill and building `examples/esp32` with the ESP-IDF version in `examples/esp32/app_config.yml` (the
scripts are in `examples/esp32/scripts`, [Platforms](platforms.md)) when ESP-IDF is installed. Delete the build
directories afterwards; never run `docker system prune` or any other prune on a shared machine. Run one container job
at a time and `cmake --build ... --parallel 3` at most when other agents are working.

## 3. File layout

- **One source per header, same stem.** `inc/<stem>.hpp` has `src/<stem>.cpp` (or `.ipp` for a header-only template
  driver). A header that declares only constants, types, details, registers, the Kconfig defaults or the port contract
  (`_types`, `_detail`, `_registers`, `_kconfig`, `_interface`) needs no source; any other header without one is
  listed in `tools/layout.yml` with the reason. No combined file (`a_and_b.cpp`): split it by header.
- **SDK-touching code** (ESP-IDF, an RTOS, a vendor HAL) is never in `inc/` or in `src/`'s logic: it lives one file
  per SDK component in `src/boundary/<component>.cpp`, named after the component, so the directory carries the
  layering rule. A driver normally has none: it reaches hardware only through the port hooks, and the SDK appears
  only in `examples/<platform>/`. The only generated SDK header read by the driver is `sdkconfig.h`, in
  `inc/<name>_kconfig.hpp`.
- `examples/<platform>/` is the only platform code; `examples/host/` always exists. Tests do not include SDK headers.
- Tool configuration is in `_config/` only: no copies or symlinks at the root. The root holds only the files
  AGENTS.md lists. Never commit `build*/`, a venv, a PDF, logs or a scratch file.
- `tools/layout.yml` (optional) lists the exceptions: `header_only`, `internal`, `boundary_extra`, each with a reason
  that `tools/check_layout.py` also keeps current.

## 4. Naming

Namespace and package are the part number in lower case; files `<name>.hpp`, `<name>_<topic>.hpp`; public methods
PascalCase, private camelBack, members `snake_case_`, constants `kPascalCase`, units as suffixes (`TimeoutMs`,
`VoltageMv`); include guards `<NAME>_<FILE_STEM>_<EXT>`; enumerations `enum class` with `EnumLast`, never a sentinel
value; chip-specific `Error` values from 16. The repository is `driver-<part>`; the copyright holder is
`The driver-<part> authors`. Full rules: AGENTS.md and the handbook.

## 5. Docs and diagrams

- Every claim about the chip cites a datasheet section and page; `docs/datasheets.yml` records vendor, number,
  revision, date, URL, SHA-256 and one topic per section used. The PDF itself is never committed.
- `docs/hardware.yml` is the one description of the chip (package, pins, supply, hooks, blocks, a circuit, the README's
  overview, capabilities and frames). The pinout, outline, block diagram, wiring and frame figures, the chip card and
  the coverage table are generated from it and the headers: never draw or type them by hand.
- The README and `docs/index.md` lead with the chip (summary, hero figure, capabilities, uses, inside the chip, pins and
  wiring), then the quick start and the driver coverage; the layering figure is on the architecture page only.
- Code shown on the site lives in `tests/docs/` between `--8<--` markers and is compiled. Every public symbol has a
  Doxygen comment. `CHANGELOG.md` has `## [Unreleased]` with the allowed headings only.
- A claim of behaviour in docs, comments or the build report names the code and the test that back it (RD-09).
  Anything only a bench can confirm is marked **not verified on hardware** with bench steps, and the level is stated
  (`hardware-validation` skill).

## 6. Standing rules

- **Stand-alone.** The repository describes its own part and nothing else: no product, board, project, person or
  other repository by name, no lab history, no dates outside release headings, datasheet revisions and waiver
  expiries. Behaviour learned on a bench is written as the design with its datasheet section.
- **Copyright** is `The <repo> authors` in `LICENSE`, every source header, `driver.yml` and the docs footer. Do not
  put a company or a person there.
- **No attribution.** No `Co-Authored-By:` trailers, "Generated with" footers or tool names in commits, pull
  requests or files. Commits are signed off (`git commit -s`) with the owner's GitHub noreply address; no personal
  email address appears anywhere, and security reports go through GitHub advisories (`.github/SECURITY.md`).
- **No secrets** (tokens, keys, credentials, personal addresses) and **no vendor PDFs or other licensed documents**
  anywhere in the tree, the history, a pull request or a log you attach.
- **Datasheets** are fetched only from the vendor's public page into a scratch folder; never bypass a login or a 403,
  never fetch the same blocked URL again; record the gap and stop that item.
- **Owner-only actions:** making a repository public, rulesets and security settings, deleting or recreating a
  repository, force-pushing, the final release tag. `publish-driver` lists them. Ask; do not assume.
- **No force-push to `main`** and no history rewrite of a published branch. Never move or delete a tag.
- **Open source references** are read for ideas about API shape; no code, table or text is copied from a source
  whose licence you have not recorded, and none from an unlicensed or copyleft one.
- **Conserve CI.** Push only when the local gate passed; push the fixes together; re-run only infrastructure failures
  (`release` skill, section 5).
- **The datasheet is the source of behaviour.** When it is silent or contradicts another source, write the design
  decision and its reason, make the model knob switchable, and list the bench step. Do not change the model to match a
  bench result without reading the datasheet again.

## 7. Known pitfalls and the fix

| Pitfall | Fix |
| --- | --- |
| The C++ lint job hits its 20-minute limit when the test sources are in the compilation database twice (the unit tests and their optimised copy) or the clang-analyzer runs over every test | The preflight times its tidy step and fails over 400 s (a third of the limit). Fix: put the optimised copy (`<name>_optimised_build`) behind a CMake option that the lint workflow turns off, disable `clang-analyzer-*` for `tests/` in `tests/.clang-tidy` (the analyzer still covers the driver through `examples/host`), and keep the suite in few, small translation units |
| The MISRA scan builds with an unbounded `make -j` and loses the runner on a large tree | Give the scan its own small target (headers alone, snippets, examples, fuzz) and bound the job count (`CMAKE_BUILD_PARALLEL_LEVEL=2`) |
| Local clang-tidy, clang-format or CMake differ from CI | Use clang-tidy 20 and clang-format 20 (`CLANG_TIDY`, `CLANG_FORMAT`); other versions report different findings and a green local run proves nothing |
| ESP-IDF differs between versions (v5.5 is pinned; v6 changes peripheral drivers and error codes, for example an SPI polling transfer accepts only an infinite wait, a bus NACK maps to another error code) | Build `examples/esp32` with the version in `app_config.yml`; use only APIs present in it; do not add an IDF version column without the owner; list every `REQUIRES` component the example includes |
| MISRA findings on vendor enumerations, `esp_err_t` values and C naming from an SDK | Keep SDK types out of the driver; convert at the edge in `src/boundary/<component>.cpp`; use `enum class` and fixed-width types in the public API |
| A stale deviation marker (a `MISRA deviation` comment, or a `tools/layout.yml` / waiver entry) outlives the code it excused | Remove the marker in the same commit as the code; the checks fail on an entry that names nothing |
| `_u8`/`_u16`/`_u32` literal operators raise rule 6.9.2 (they take `unsigned long long`) | Follow the `misra-review` skill's current pattern for typed constants; do not add new literal operators; ask when unsure |
| An unused named parameter (rule 0.2.2), a discarded `std::copy` result (0.1.2), char arithmetic (7.0.3), differing parameter names (13.3.3), an `else`-less chain (9.4.1) | Unnamed parameter or `static_cast<void>`; `static_cast<void>(std::copy(...))`; compare characters with `std::char_traits`; same names in header and definition; final `else`. `tools/misra_lint.py` finds most |
| Indirect recursion (8.2.10) between small helpers | Draw the call graph of retry and recovery paths; make one function the only caller of the other |
| `.gitignore` drops a file the build needs (`*.bin` silently removes fuzz corpus inputs) | Check what git will push, not the working tree: `git status --ignored`, and `git add -A && git ls-files tests/fuzz` |
| The host-example CI step runs `./build-example-<cc>/<name>_host_example`; a driver with other example binary names exits 127 after all tests pass | Keep the single example target name, or change the workflow input and the preflight together |
| Host builds on macOS cannot see type-width bugs (`%d` with `int32_t`, `PRId64` under strict C++20 on newlib, `int main(int32_t, ...)`) | Compile the sources for a newlib target (`-fsyntax-only`); cast to `long long` for `%lld`; use `uint32_t` and `main(int, char**)` |
| Unspecified evaluation order in a check (GCC on x86-64 evaluates arguments right to left) | Compute the driver result into a local before reading the state it changes (RD-34); `tools/eval_order_check.py` |
| A clock comparison with half-range logic stops working after 2^31 ms | Use unsigned elapsed subtraction (`now - start >= limit`); fuzz with the clock near wrap |
| Container build killed by out-of-memory (`Killed signal terminated program cc1plus`) | Pass `CMAKE_BUILD_PARALLEL_LEVEL=4` or lower to the container; never run two container jobs at once |
| macOS libFuzzer hangs in symbolization and leaves processes behind | Set `ASAN_OPTIONS=symbolize=0 UBSAN_OPTIONS=symbolize=0` and `-print_funcs=0`; kill orphans afterwards |
| `scaffold.py --check-drift` fails on `.kokeb/template.yml` | Keep each value on one line; a long description may continue with a trailing `\`. Never hand-edit `template:` |
| A tag was pushed to the wrong commit | Do not move or delete it (a ruleset forbids it); cut the next `-rc.N` and say why |
| Docs figures or generated regions differ between machines | Run `kokeb-docs figures && kokeb-docs gen` with the pinned theme version, commit the result, never edit generated regions |

## 8. When to stop and report instead of improvising

Stop, write down what you saw, what you tried and what you need, and hand it back when:

- the placement verdict is not `NEW`, or `plan-driver` ends `BLOCKED`;
- the datasheet cannot be obtained without a login, a bypassed 403 or a licence you cannot confirm, or two datasheet
  revisions disagree on something the driver depends on;
- a check fails twice for the same reason and you do not understand the cause, or a CI failure is outside the
  repository (a shared workflow, a runner, the quota) and is not an infrastructure failure the `release` skill lists;
- a fix needs a waiver, a change to a managed file (anything `--check-drift` ships), a new ESP-IDF version, or a
  change to `kokeb-labs/ci-tools`, the handbook or the shared catalogue;
- an action is owner-only (section 6), irreversible, or touches anything outside this repository;
- a safety question arises (the chip can switch a load, heat, move or hold a high voltage) that the datasheet does not
  answer, or a bench result contradicts the datasheet;
- you find a secret, a vendor PDF, a person's or another project's name already in the tree or the history.

An honest "not verified" in the report is worth more than a green check obtained by weakening a test, a lint or a
rule. Never do that.
