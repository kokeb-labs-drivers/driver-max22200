---
name: add-driver-feature
description: Add one chip feature end to end - register model, API, tests, docs, changelog. Use when asked to support a register, mode or command the driver does not cover yet.
---

# Add a driver feature

One feature per PR, branch `feat/<feature>`. Cite the datasheet section for every register, bit and timing.

1. **Read the datasheet section** and write down: registers and bits, reset values, timing, error conditions, what
   the chip does on invalid input. Note the section numbers.
2. **Model it first.** Extend `tests/mock_max22200.hpp` so the register model behaves like the datasheet: reset
   values, read-only bits, self-clearing bits, timing (through `Elapse()`), and a fault-injection field for each
   failure the feature can hit.
3. **Write the failing tests** in `tests/test_max22200.cpp`: the register traffic of the feature, a call before
   `EnsureInitialized()` (`NotInitialized`), a failing port hook on every path (`Bus`), argument checks
   (`InvalidArgument`, nothing sent), and each new error. Run them: they fail.
4. **Add the registers** to `inc/max22200_registers.hpp` and the types (enums, `Config` fields with unit suffixes
   and datasheet defaults, a `Status` field) to `inc/max22200_types.hpp`; extend `Validate()`.
5. **Add the API** to `inc/max22200.hpp` (PascalCase, `noexcept`, `Result<T>`, Doxygen with units, return values,
   errors and whether it does I/O) and implement it in `src/max22200.ipp` (private helpers camelBack). Follow the
   handbook's C++20 profile as you write: buffers as spans (fixed extent when the size is known), register fields
   through `detail::Field`, every shift count masked or checked, narrow operands widened to `uint32_t`, loops
   bounded by a validated maximum, pure encoders as free `constexpr` functions with a datasheet `static_assert`
   and a round trip, a new optional hook as named `Declares`/`Has` concepts. Run the `cpp20-profile` skill on the
   change, and `hardware-lessons-review` for anything that touches replies, retries, setters or start-up.
   A new chip-specific `Error` value takes the next free value from 16, gets a `ToString()` case and a scenario in
   `tests/test_errors.cpp`.
6. **Docs.** A compiled snippet in `tests/docs/api_tasks.cpp` and a section in `docs/api-guide.md`; update
   `docs/configuration.md`, `docs/contract.md` (errors table) and `docs/hardware.md` if they change. Run
   `kokeb-docs figures && kokeb-docs gen`.
7. **Changelog** under `## [Unreleased]` (`### Added`, or `### Breaking` with what callers change).
8. Run the `verify` skill (strict-dialect build, source checks, the review skills or the `safety-reviewer` agent)
   and open the PR with the bench steps for the feature and the review's findings, each fixed or explained.
