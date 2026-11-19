---
name: misra-review
description: Review a change against the handbook's MISRA C++:2023 compliance matrix, guideline by guideline, citing numbers only, with the patterns that failed CI on earlier drivers as a checklist. Use on every pull request that changes C++ in inc/, src/, tests/ or examples/, and before a release at the high or safety level.
---

# MISRA C++:2023 review

The handbook's compliance matrix is the reference, never the standard's text. Cite guideline numbers only
(`Rule 7.0.5`, `Dir 0.3.1`); never quote or paraphrase the standard, never paste it into code, commits, pull
requests or comments, and never upload the licensed copy. The catalogue ids (RD-nn) are in
[`docs/recurring-defects.md`](../../../docs/recurring-defects.md); the rule for where each is caught is in
[`docs/mechanization-policy.md`](../../../docs/mechanization-policy.md).

## Already mechanical: do not redo by eye

Run the preflight and read its lines; a finding there is fixed, not discussed. The patterns below are lint classes
(`misra_lint`, planned in the template's mechanical-lints work; until it lands, run the grep given). CI's CodeQL scan is
the final word, and it scans `tests/`, models, fuzz and consumers as well (RD-23).

| Id | Rule | Pattern that failed CI | Local grep or check |
| --- | --- | --- | --- |
| RD-12 | 7.0.3 | `c - '0'`, `'A' + n`, `c == 'x'`, `static_cast<char>(byte)` | `grep -nE "'.'" inc src tests examples` next to an operator |
| RD-13 | 8.0.1 | `a == b && c < d` unparenthesised, also in `static_assert` | parenthesis-depth scan of `inc src tests` |
| RD-14 | 9.4.1 | `if ... else if` with no final `else`, in tests and models too | every `else if` chain ends in `else` |
| RD-15 | 0.1.2 | a non-void call as a statement | call result used or the call returns `void` |
| RD-16 | 13.3.3 | parameter names differ between `inc/*.hpp` and `src/*.ipp` | compare each declaration and definition |
| RD-17 | 7.0.6 | a bare integer constant passed to a narrower parameter | `_u8`/`_u16`/`_u32` literals at the call site |
| RD-18 | 6.9.2, 15.1.4, 0.0.2 | `unsigned long long` literal-operator parameter, constructor with default member initialisers, constant-hook port conditions | known advisories: do not report them again; record or accept per the owner decision |
| RD-11 | KB-CPP-012 | `#include <numeric>`, `<string_view>` outside the driver subset | handbook_check include scan |

## Review steps (judgement)

1. **Pin.** The handbook commit is the `ref:` in `.github/workflows/ci-repo-checks.yml`; the matrix is
   `compliance/misra-cpp-2023.yml` and the deviations `compliance/misra-cpp-2023-deviations.yml` at that commit. The
   repository's level is in `.kokeb/handbook.yml`.
2. **Scope.** `git diff --name-only origin/main...HEAD -- inc src tests examples`, then `git diff -U0` per file. Review
   what changed and what the change makes reachable.
3. **Map constructs to guidelines** (a cast, shift, loop, switch, lambda, template, buffer access, initialisation).
   Follow each matrix entry: a tool entry without `partial: true` decides it (confirm the job passed); a `partial`
   entry: run the tool and do its `review` step; a `review` entry: check against its `how` text and say what you
   checked; `not-applicable`: confirm the excluded construct is absent. Apply `our_category`, never a weaker one.
4. **C++20 constructs** go through deviation MD-CPP-001 and the `cpp20-profile` skill; a forbidden or C++23 feature is
   a violation of Rule 4.1.1 with no deviation.
5. **The patterns the code already follows** (new code follows them too):
   - enum switches list every case and end `default: break;` (Rule 9.4.2); no `kCount` sentinel, use
     `detail::EnumLast<E>`;
   - narrow operands meet `U` constants and widen to `uint32_t` before bit operations (Rule 7.0.5);
   - integer constant arguments have the parameter's type (Rule 7.0.6), noting `uint32_t` is `unsigned long` on
     Cortex-M; bring literals in with `using detail::operator""_u32;` inside the function;
   - a non-constant loop bound is a `const` local (Rule 9.5.1); `std::fill`, not `std::fill_n` (Rule 0.1.2);
   - a defaulted `operator==` is a hidden friend (Rule 16.6.1); the trailing `#include "../src/<chip>.ipp"` is
     deviated by MD-CPP-009 on the line before it (Rule 19.0.3);
   - a member template on a local of a class-template member is written `x.template first<2>()`, or the CodeQL front end
     stops extracting the function.
6. **Deviations** follow the handbook: project-wide `MD-CPP-NNN` only inside its `scope`; a local one is
   `// NOLINTNEXTLINE(<check>): <reason>` plus, at high and safety levels, an entry in `docs/deviations.yml`. A
   mandatory guideline is never deviated. Propose a deviation in the report; never add one.
7. **CI report** (`misra-report`: `misra-summary.md`, `misra-findings.csv`): violations fail the job; *to review*
   findings carry anomaly ids from the tool validation (known misfires in class-template code: 7.11.3, 7.0.2, 0.1.1,
   7.0.5, 8.2.2, 18.4.1). Fix real ones, do not contort code for false ones; report a new misfire shape to
   ci-tools with a minimal reproducer.

## Report

Use the format of the review rounds. Findings are `BLOCKER` (a mandatory guideline broken, or a failing scan), `MAJOR`
(a required guideline broken), `minor` (an advisory one), each with `file:line`, the guideline number, the failing
construct and the fix or the deviation to propose. Then list: guidelines decided by a tool (with the job), guidelines
you reviewed by hand (with the step), tools you could not run, and documented ambiguities. End with exactly one line:
`NO BLOCKER` or `BLOCKERS REMAIN`.
