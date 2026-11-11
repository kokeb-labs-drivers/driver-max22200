# AGENTS.md

driver-max22200: C++20 driver for the Analog Devices MAX22200 and MAX22200A octal 36 V, 1 A serial-controlled
solenoid and motor drivers. It follows the
[Kokeb Labs engineering standard](https://github.com/kokeb-labs/.github/blob/main/standards/README.md)
(agents: `standards/agents.md`, drivers: `standards/driver.md`).

## Layout

- `inc/`, `src/`: the driver. `inc/max22200.hpp` is the umbrella header, `src/max22200.ipp` the implementation,
  `inc/max22200_spi_interface.hpp` the port (with the required `GpioSet()` for CMD and ENABLE) and the frame
  layer, `inc/max22200_registers.hpp` the Command Register and register map, `inc/max22200_types.hpp` variants,
  errors and config, `inc/max22200_detail.hpp` the compile-time helpers (exact hook probes, typed register
  fields, enumeration checks).
- `tests/`: host tests over the chip model `tests/mock_max22200.hpp` and the mock ports
  `tests/mock_max22200_bus.hpp`, `tests/compile_fail/`, `tests/docs/` (every code block on the site),
  `tests/install_consumer/`, and the libFuzzer targets in `tests/fuzz/` (`MAX22200_BUILD_FUZZERS`, Clang only).
- `docs/`: the site pages (`docs/.nav.yml` orders them). `docs/datasheets.yml` records the datasheet the driver
  follows (revision, SHA-256) and every topic it takes from it; `kokeb-docs gen` builds the README card, the
  Datasheet map (`docs/datasheet.md`) and each page's box from it (`tools/docs/generators/datasheet.py`).
- `examples/<platform>/`: the only platform code, one self-contained folder per platform with its own port.
  `examples/host/` (simulated chip, any PC) always exists and runs in the host tests.
  `examples/esp32/`: the ESP-IDF example; the root `CMakeLists.txt` is the ESP-IDF component (`ESP_PLATFORM`).
- `_config/`: all tool configuration (clang-format, clang-tidy, `Doxyfile`, `mkdocs.yml`, `Kconfig`, linters).
  Pass the path (`--style=file:_config/.clang-format`, `--config-file=_config/.clang-tidy`,
  `mkdocs build -f _config/mkdocs.yml`); never add copies or symlinks at the root.
- `cmake/max22200_build_settings.cmake`: the only place the version is set; `driver.yml` repeats name and version,
  and CI checks they agree.
- `.kokeb/handbook.yml`: the handbook release this repository follows, its kind and assurance level, and any
  waivers (rule, reason, approver, expiry). CI checks the handbook rules against it and reports each finding by
  rule ID (`KB-CPP-003`, ...); fix the code, or add a dated waiver only with the maintainer's approval.
- The root holds only what GitHub or a tool needs there and what a reader looks for first: `README.md`, `LICENSE`,
  `CHANGELOG.md`, `CMakeLists.txt`, `AGENTS.md`, `CLAUDE.md`, `.gitignore`, `.gitmodules`, `driver.yml`.

## Reading and citing the datasheet

- The chip's datasheet is the only source of the driver's behaviour: Analog Devices 19-100531, revision 1,
  *MAX22200/MAX22200A 36V, 1A Octal Integrated Serial-Controlled Solenoid and Motor Driver*; one document covers
  both parts, which differ in how they clear the FAULT register. The vendor publishes it from the product page
  only (`pdf: null`). `docs/datasheets.yml` names the exact
  document (vendor, number, revision, date, `url`, `pdf`, the `sha256` of the PDF) and lists every topic the
  driver takes from it: the datasheet's section name, the printed page, the driver API that implements it and the
  docs pages that explain it. Link the vendor page; keep the PDF itself out of the repository.
- Cite the section next to the code that depends on it (`// Figure 6`), in the Doxygen comment of the
  API, and in the topic row, so code, docs and the Datasheet map agree. Run `kokeb-docs gen` after editing
  `docs/datasheets.yml`.
- Waits use the datasheet maximum (tEN, tWU, tDIS), pulses its minimum; `Config{}` holds the reset values;
  `Validate()` the documented ranges. Start-up follows the programming procedure of Figure 6; 32-bit writes go
  LSB byte first (Figure 10), reads come MSB byte first (Figure 11).
- Behaviour learned on a bench is written as the driver's design, justified by the datasheet section that
  explains it. Where the datasheet is silent, write the design decision and its reasoning, and give the bench
  step that confirms it (**not verified on hardware** until someone runs it). The repository describes this
  chip only: no other boards, products, projects or drivers, and no dated history (`tools/check_standalone.py`
  checks this).

## Architecture and layer rules

The driver is three layers: the public API (`inc/max22200.hpp`, implemented in `src/max22200.ipp`), the chip's
definitions and compile-time helpers (`inc/max22200_registers.hpp`, `inc/max22200_types.hpp`,
`inc/max22200_detail.hpp`), and the port the
application supplies (`inc/max22200_spi_interface.hpp`). `docs/architecture.md` draws them.

- A driver is MCU-agnostic and platform-free: no SDK, no RTOS, no lock, no heap, exceptions, RTTI, `std::function`,
  `std::string`, `printf` or static mutable state. It reaches the bus only through the port hooks.
- Everything is in `namespace max22200`; internals in `max22200::detail`. The class is `max22200::MAX22200`.
- The port base declares no hook; the driver is constrained by the `SpiPort` concept, which checks each hook's
  exact signature and `noexcept` (`detail::ExactHook`); `GpioSet()` is required, because CMD selects the Command
  Register and ENABLE wakes the chip. `DelayUs()` is required where the driver first waits; `GpioRead()` (the
  FAULT pin) and `Log()` are optional. They are detected by named `Declares*`/`Has*` concepts in
  `max22200::detail` only, never by an inline `requires` expression, and a hook that is declared but
  misdeclared is a compile error.
- Code follows the handbook's C++20 profile (KB-CPP20): ISO C++20 with no extensions, nothing from C++23, spans
  for buffers, masked shifts, `uint32_t` bit work, bounded loops, `[[nodiscard]]` error types.
- `Error` keeps the base set at values 0 to 9; chip-specific values start at 16 and each is returned somewhere.
- Every fallible call returns `Result<T>`; every port failure propagates. The constructor stores the configuration
  with no I/O; `EnsureInitialized()` is the only start-up call, and every I/O call returns `NotInitialized` before it
  has succeeded.
- A call sends only the traffic the datasheet needs. Any extra read-back or compare is asked for per call with a
  trailing `Verify verify = Verify::None` argument, its cost in the doc comment (handbook KB-ARCH-041). Start-up
  checks in `EnsureInitialized()` and datasheet-required steps stay unconditional.
- Public and protected methods PascalCase, private methods camelBack, members `snake_case_`, constants
  `kPascalCase`, units as suffixes. 100 columns (`_config/.clang-format`).
- Kconfig values are read only in `inc/max22200_kconfig.hpp`.

## Standards

- **Handbook rules** (`kokeb-labs/handbook`, rule IDs such as `KB-CPP-003`): CI checks them against
  `.kokeb/handbook.yml` and reports each finding by rule ID. Fix the code; a waiver (rule, reason, approver,
  expiry) needs the maintainer's approval.
- **C++20 profile** (KB-CPP20-001 to 058): the `cpp20-profile` skill lists which features to use, use with
  rules, or leave out, and runs the automated checks first.
- **MISRA C++:2023 matrix**: the `misra-review` skill reviews against the handbook's compliance matrix and cites
  guideline numbers only.
- **Hardware lessons** (reply validity, stuck and floating lines, resynchronisation, retries, ready waits, bus
  requirements, pin idle levels): the `hardware-lessons-review` skill checks each rule has a code path and a
  model test.

## Build, test and verify

```bash
cmake -S . -B build -DMAX22200_BUILD_TESTS=ON
cmake --build build --parallel && ctest --test-dir build --output-on-failure
```

The tests build as ISO C++20 (`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`) with `-fno-exceptions -fno-rtti
-Werror`, and the target `max22200_header_checks` compiles each public header alone. CI runs GCC and Clang; run
the compilers you have. The host example runs as the CTest `max22200_host_example`. ESP32 builds run in CI
(`esp32-examples-build-ci.yml`); the host build covers a change locally.

## Docs checks

```bash
python3 -m venv .venv && .venv/bin/pip install \
  "git+https://github.com/kokeb-labs/ci-tools@v1#subdirectory=packages/kokeb-mkdocs-theme"
.venv/bin/kokeb-docs figures && .venv/bin/kokeb-docs gen    # after changing headers or snippets
.venv/bin/kokeb-docs check && .venv/bin/mkdocs build -f _config/mkdocs.yml --strict -d /tmp/site
```

Code shown on the site lives in `tests/docs/` between `// --8<-- [start:x]` / `[end:x]` markers. Doxygen runs as
the CTest `max22200_doxygen` when `doxygen` is installed; every public symbol in `inc/` is documented.

The `verify` skill (`.claude/skills/verify/SKILL.md`) runs every check CI runs, in order: GCC and Clang host
tests, the strict-dialect header checks, the handbook source checks, `tools/check_standalone.py`, clang-format
20, clang-tidy 20, cppcheck, lizard, markdownlint and the docs build. Run it before every push.

## Branches, commits and pull requests

- Branch from `main` as `feat/`, `fix/`, `docs/`, `ci/` or `chore/<topic>`; one logical change per branch.
- Commit with a sign-off (`git commit -s`) and an imperative subject.
- Open a pull request with the template filled in, including **Bench status**. When every check is green,
  including `CI gate / gate`, the change is merged with a squash merge; the PR title becomes the commit subject
  on `main`. `main` changes only through merged pull requests and keeps its history (no force-push).

## Definition of done

A change is done when it is merged: one change; the standards followed or each deviation named; every check
passes locally (the `verify` skill); host tests for new behaviour including failure paths; docs, Doxygen and
`docs/datasheets.yml` updated with the datasheet sections; `CHANGELOG.md` under `## [Unreleased]`; CI green
including `CI gate / gate` (the handbook check, the Cortex-M build and the coverage, complexity and sanitizer
gates); anything hardware-dependent marked **not verified on hardware** with bench steps; squash-merged.

## Safety rules

- Every commit is signed off (DCO).
- No attribution lines in commits, pull requests or files: no `Co-Authored-By:` trailers, no "Generated with"
  footers, no tool banners.
- No secrets, credentials or vendor PDFs in the repository.
- No force-push to `main` and no history rewrites on it.
- Repository settings (rulesets, merge options, Pages) are the maintainers'; `docs/repo-setup.md` lists them.

## Skills

In `.claude/skills/`:

- `verify`: every check CI runs, before each push.
- `datasheet-conformance`: register maps, timing, reset values and errors from the datasheet, test first.
- `add-driver-feature`, `add-port`: a new driver capability; a new platform under `examples/`.
- `cpp20-profile`, `misra-review`, `hardware-lessons-review`: the reviews of the standards above.
- `release`, `bump-pin`: a version release; a submodule or workflow pin update.
- `new-driver-from-template`: turning a repository made from the template into a real driver.

`.claude/agents/safety-reviewer.md` runs the review skills with `datasheet-conformance` and reports findings by
severity for the author to act on.
