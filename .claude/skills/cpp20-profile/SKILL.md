---
name: cpp20-profile
description: Check C++ code against the handbook's C++20 profile (guidelines KB-CPP20-001 to 058) - which features to use, use with rules or never use - running the automated checks first and reviewing the rest. Use when writing or reviewing any C++ in inc/, src/, tests/ or examples/.
---

# C++20 profile check

The handbook's C++20 profile (Standards > C++20 profile, rules `KB-CPP20-*`) says which C++20 features the code
may use, the rules each comes with, and which it may not use. Deviation MD-CPP-001 covers exactly the allowed
set: anything else, anything C++20 deprecates and anything from C++23 or later is a defect, even when a compiler
accepts it.

## 1. Run what is automated

1. The strict-dialect build: configure and build the host tests (`cmake -S . -B build -DMAX22200_BUILD_TESTS=ON`,
   `cmake --build build`). Every target builds as ISO C++20 with `-pedantic-errors` and the C++20 deprecation
   warnings as errors, and the target `max22200_header_checks` compiles each public header alone, asserting that
   the dialect is C++20. Repeat with the other host compiler if you have it. The Cortex-M and ESP-IDF builds of
   the same checks run in CI (`ci-cross-build.yml`, and the ESP-IDF example's
   `max22200_cxx20_check.cpp` when the repository has one).
2. The handbook's source checks at the pinned commit (the `ref:` in `.github/workflows/ci-repo-checks.yml`):
   `curl -fsSL https://raw.githubusercontent.com/kokeb-labs/handbook/<ref>/checks/source_checks.py -o /tmp/sc.py`
   then `python3 /tmp/sc.py scan --repo .`. The `cpp20-profile/...` findings are forbidden features,
   `CMAKE_CXX_EXTENSIONS OFF` missing, an `Error` or `Result` type without `[[nodiscard]]`, a requires-expression
   inside `if constexpr`, and a header that includes the Kconfig header.
3. clang-tidy with `_config/.clang-tidy` on the changed files (see the `verify` skill); the profile relies on
   `bugprone-dangling-handle`, `bugprone-unchecked-optional-access`, `hicpp-signed-bitwise`,
   `cppcoreguidelines-misleading-capture-default-by-value` and `modernize-use-designated-initializers`.

A finding is fixed in the code. Never switch a check off or add a suppression to pass it.

## 2. Review the rest

For each changed construct, decide its verdict:

- **Use** (no extra rules): `enum class` with a fixed `uint8_t`-style base, `[[nodiscard]]`, `constexpr`,
  `static_assert`, `std::array`, `if`/`switch` with an initialiser, nested namespaces, standard concepts,
  fixed-extent `std::span`, `<bit>`, `std::in_range` and `std::cmp_*`, `[[maybe_unused]]`, defaulted special
  members, default member initialisers, range-`for` over named arrays and spans, digit separators.
- **Use with rules**: concepts (exact hook signatures, `detail::ExactHook`, `noexcept`), optional-hook detection
  (named `Has<Hook>` and `Declares<Hook>` concepts), `if constexpr` (each branch instantiated by a test),
  dynamic `std::span` (size checked before the first access, never stored), designated initialisers (every member
  defaulted), defaulted `operator==` (integral members only, as a hidden friend:
  `friend constexpr bool operator==(const T& lhs, const T& rhs) noexcept = default;`), lambdas (explicit
  captures, never stored), CRTP (port-independent code outside the class template), `consteval` (build-time
  construction, run-time `Validate()` kept; the `_u8`/`_u16`/`_u32` literals of `detail`), `constinit`,
  `std::bit_cast` (never for byte order), `using enum` (block scope), `std::optional` (never `.value()`),
  structured bindings (error tested first), the `std::ranges` algorithms (no views).
- **Forbid**: coroutines, modules, range views and `<ranges>`, `<format>` and `std::print`, `<=>` in drivers,
  `char8_t`, parenthesised aggregate initialisation, lambdas in unevaluated operands, generic lambdas in drivers,
  `[[fallthrough]]`, `[[no_unique_address]]` in drivers, `[[likely]]` without a measurement,
  `std::is_constant_evaluated`, `constexpr` allocation, `std::variant`/`any`/`function`/`expected` in drivers,
  feature-test macros, every C++23 feature (`std::to_underlying`: use `detail::ToUnderlying`).

Then go through the guidelines the change touches. The integer ones find the most defects: a shift count that is
not masked or checked in the same function (020), a `uint8_t` or `uint16_t` operand not widened to `uint32_t`
before a bit operation (021), a narrowing compound assignment (022), an integer cast to an enumeration without a
range check (025), a loop bound from configuration that is not validated against a named maximum (058), a
pointer and a length instead of a span (042).

| Guideline | Category | How it is checked here |
| --- | --- | --- |
| KB-CPP20-001 Strict ISO C++20, with a conformance build on every target compiler | required | source check `cpp20-profile/cmake-extensions`; `forbidden-patterns/cmake-cxx-standard`; compiler `-pedantic-errors`; `strict-dialect` |
| KB-CPP20-002 Only profile features; nothing from C++23 or later | required | source check `cpp20-profile/forbidden-features`; `strict-dialect`; `include-allowlist/driver`; `include-allowlist/target`; review: A C++20 feature not in the profile joins it and MD-CPP-001 in one pull request, or is not used. |
| KB-CPP20-003 Nothing C++20 deprecates | required | compiler `-Wdeprecated`; compiler `-Wdeprecated-enum-enum-conversion`; compiler `-Wdeprecated-enum-float-conversion`; clang-tidy `cppcoreguidelines-misleading-capture-default-by-value` |
| KB-CPP20-004 No feature-test macros to choose behaviour | advisory | source check `cpp20-profile/forbidden-features` |
| KB-CPP20-005 Every template parameter is constrained by a named concept | required | review (CodeQL query planned); review: Every new template parameter names its concept. |
| KB-CPP20-006 Port concepts check the exact hook signature and `noexcept` | required | `compile-fail-tests`; review (CodeQL query planned); review: Each port concept matches every hook with `detail::ExactHook` and states `noexcept` on it. |
| KB-CPP20-007 Optional hooks use named detection concepts; a misdeclared hook is an error | required | source check `cpp20-profile/inline-requires`; `compile-fail-tests` |
| KB-CPP20-008 One dependent-false helper | advisory | review: Discarded branches use `detail::kAlwaysFalse`. |
| KB-CPP20-009 Code that does not depend on the port lives outside the class template | advisory | review (CodeQL query planned); review: A static member that never names the port moves out. |
| KB-CPP20-010 Abbreviated function templates are constrained and stay out of the public API | advisory | review (CodeQL query planned); review: No `auto` parameter in public driver API. |
| KB-CPP20-011 Concepts carry meaning; overloads are not chosen by subsumption | advisory | review: No two overloads differ only in one being more constrained. |
| KB-CPP20-012 Pure computations are `constexpr` and proved with `static_assert` | required | review (CodeQL query planned); review: Every new pure function has a datasheet `static_assert`; encode and decode pairs a round trip. |
| KB-CPP20-013 `consteval` only for build-time construction | advisory | review: The run-time `Validate()` call stays in the start-up path. |
| KB-CPP20-014 `constinit` on every mutable static | required | clang-tidy `cppcoreguidelines-avoid-non-const-global-variables`; review (CodeQL query planned); review: Every mutable static is `constinit`. |
| KB-CPP20-015 `if constexpr` only on compile-time facts, each branch instantiated by a test | required | `coverage-floor/level`; review: The tests have a minimal and a full port, so both branches compile. |
| KB-CPP20-016 No `std::is_constant_evaluated` | advisory | source check `cpp20-profile/forbidden-features` |
| KB-CPP20-017 Header-scope variables are `constexpr` | required | clang-tidy `cppcoreguidelines-avoid-non-const-global-variables`; review (CodeQL query planned) |
| KB-CPP20-018 Configuration-dependent inline code: one configuration per program | required | source check `cpp20-profile/kconfig-leaf`; review: The Kconfig header's comment states one configuration per program. |
| KB-CPP20-019 No allocation and no virtual functions in constant evaluation | required | `forbidden-patterns/heap`; `include-allowlist/driver` |
| KB-CPP20-020 Every shift count is provably below the operand width | mandatory | clang-tidy `clang-analyzer-core.BitwiseShift`; `sanitizers`; review (CodeQL query planned); review: Every run-time shift count is masked or checked in the same function. |
| KB-CPP20-021 Bit work in `uint32_t`; no promoted narrow operands; no platform-width types | required | clang-tidy `hicpp-signed-bitwise`; review (CodeQL query planned); review: Narrow operands are widened to `uint32_t` before bit operations; no `UL` widths. |
| KB-CPP20-022 Narrowing is explicit, compound assignment included | required | compiler `-Wconversion`; clang-tidy `bugprone-narrowing-conversions`; review (CodeQL query planned); review: No `uint8_t \|= unsigned` style compound assignment. |
| KB-CPP20-023 Mixed-sign comparison through `std::cmp_less` and its siblings | advisory | compiler `-Wsign-compare` |
| KB-CPP20-024 Signed reinterpretation of wire values through a named helper | required | clang-tidy `cppcoreguidelines-pro-type-union-access`; clang-tidy `cppcoreguidelines-pro-type-reinterpret-cast` |
| KB-CPP20-025 Integer-to-enum conversion only for proven enumerators | mandatory | review (CodeQL query planned); review: Each integer-to-enum cast is preceded by a range check or proof. |
| KB-CPP20-026 One enumeration utility layer | advisory | review: Enumeration range checks use `detail::IsEnumerator`. |
| KB-CPP20-027 `using enum` only at block scope | required | review (CodeQL query planned); review: `using enum` only inside function bodies. |
| KB-CPP20-028 `<bit>` for counting and rotation instead of hand-written loops | advisory | review: Bit counting uses `<bit>`. |
| KB-CPP20-029 `std::bit_cast` between equal-size values, never for byte order | required | clang-tidy `bugprone-bitwise-pointer-cast`; review (CodeQL query planned); review: No `bit_cast` from a byte array. |
| KB-CPP20-030 Strong types for units and easily swapped parameters | advisory | review: Adjacent same-typed parameters with different units use strong types. |
| KB-CPP20-031 Designated initialisers for configuration; every member has a default | required | clang-tidy `modernize-use-designated-initializers`; clang-tidy `cppcoreguidelines-pro-type-member-init` |
| KB-CPP20-032 No parenthesised aggregate initialisation | required | review (CodeQL query planned); review: Aggregates use braces. |
| KB-CPP20-033 Defaulted `operator==` only on exactly comparable members | required | review (CodeQL query planned); review: No defaulted `==` on a type with a float or pointer member. |
| KB-CPP20-034 No three-way comparison in drivers | required | source check `cpp20-profile/forbidden-features`; `include-allowlist/driver` |
| KB-CPP20-035 Structured bindings test the error before the value | advisory | review (CodeQL query planned); review: The error binding is tested first. |
| KB-CPP20-036 Class-type template arguments are structural and integral | advisory | review: Class NTTPs have integral members only. |
| KB-CPP20-037 `[[nodiscard]]` on every error-carrying type | required | source check `cpp20-profile/nodiscard-error`; compiler `-Wunused-result`; clang-tidy `bugprone-unused-return-value` |
| KB-CPP20-038 `[[maybe_unused]]` instead of `(void)` for conditionally used names | advisory | compiler `-Wunused-parameter` |
| KB-CPP20-039 No `[[fallthrough]]` | required | source check `cpp20-profile/forbidden-features`; compiler `-Wimplicit-fallthrough` |
| KB-CPP20-040 `[[likely]]` and `[[unlikely]]` only with a measurement | advisory | source check `cpp20-profile/forbidden-features` |
| KB-CPP20-041 No `[[no_unique_address]]` in drivers | required | source check `cpp20-profile/forbidden-features` |
| KB-CPP20-042 Caller buffers are spans, with a fixed extent when the size is known | required | review (CodeQL query planned); review: No pointer-and-length parameter outside port hooks. |
| KB-CPP20-043 A dynamic span is checked before it is indexed, wherever it is indexed | required | `library-hardening`; `sanitizers`; review (CodeQL query planned); review: Every dynamic span is size-checked before its first access. |
| KB-CPP20-044 Views and references never outlive what they refer to | mandatory | clang-tidy `bugprone-dangling-handle`; review (CodeQL query planned); review: No span member; reference accessors are `&`-qualified. |
| KB-CPP20-045 Lambdas: explicit captures, local use, no state | required | clang-tidy `cppcoreguidelines-misleading-capture-default-by-value`; review (CodeQL query planned); review: Every lambda capture is explicit. |
| KB-CPP20-046 No lambdas in unevaluated operands or at namespace scope in headers | required | review (CodeQL query planned); review: No lambda in an unevaluated operand. |
| KB-CPP20-047 No generic lambdas in drivers | advisory | review (CodeQL query planned); review: No generic lambda in driver code. |
| KB-CPP20-048 `std::optional` is read only when engaged, never through `.value()` | mandatory | clang-tidy `bugprone-unchecked-optional-access`; clang-tidy `bugprone-optional-value-conversion`; review: No `.value()` on an optional. |
| KB-CPP20-049 No `variant`, `any`, `function` or `expected` in drivers | required | source check `cpp20-profile/forbidden-features`; `include-allowlist/driver` |
| KB-CPP20-050 Range algorithms with rules; no views in target code | required | source check `cpp20-profile/forbidden-features`; `include-allowlist/driver` |
| KB-CPP20-051 No `<format>`, `std::format` or `std::print` | required | source check `cpp20-profile/forbidden-features` |
| KB-CPP20-052 No `std::source_location` in drivers | required | source check `cpp20-profile/forbidden-features`; `include-allowlist/driver` |
| KB-CPP20-053 No `char8_t` and no `u8` literals in target code | required | source check `cpp20-profile/forbidden-features` |
| KB-CPP20-054 `<numbers>` for mathematical constants | advisory | clang-tidy `modernize-use-std-numbers` |
| KB-CPP20-055 `std::atomic_ref` only for lock-free sharing with an interrupt | advisory | review: A `static_assert` of `is_always_lock_free` sits next to each `atomic_ref`. |
| KB-CPP20-056 No coroutines | required | source check `cpp20-profile/forbidden-features`; clang-tidy `cppcoreguidelines-avoid-capturing-lambda-coroutines` |
| KB-CPP20-057 No modules | required | source check `cpp20-profile/forbidden-features` |
| KB-CPP20-058 Loops are bounded by a validated constant | required | clang-tidy `bugprone-too-small-loop-variable`; review (CodeQL query planned); review: Every loop bound from configuration or input is validated against a named maximum. |

## 3. Report

List each finding as `KB-CPP20-NNN`, category, `file:line`, what is wrong and the compliant form, mandatory and
required first. Say which automated checks ran and which only run in CI. A feature the profile does not list is
not used until the profile and MD-CPP-001 are changed in the handbook first.
