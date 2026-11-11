---
name: verify
description: Run locally every check CI runs (strict-dialect host build and tests, the handbook's source checks, the stand-alone check, format, tidy, cppcheck, complexity, markdownlint, docs) and the review skills. Use before every push and before opening a PR.
---

# Verify

Run from the repository root. Tool configuration is in `_config/`: pass its path, never copy it to the root.
Stop at the first failure, fix the cause, start again. Never weaken a check to pass.

**`tools/preflight.sh` runs steps 1 to 10 in one command** and ends with `0 failure(s)` or the list of failing
steps and their logs; it says what it skipped because a tool is missing (set `VENV`, `CXX_GCC`, `CXX_CLANG`,
`CLANG_FORMAT`, `CLANG_TIDY`, `ARM_CXX` or `HANDBOOK_DIR` to point at yours). Push only when it passes: CI minutes
are for what a workstation cannot run (the ESP32 builds, the MISRA scan, the coverage, sanitizer and fuzz gates),
not for finding a format or lint error. Fix everything it reports and push once, not once per finding. The steps
below say what each check does and how to run it alone.

1. Configure and build the host tests, doc snippets, compile-fail tests and the strict-dialect header checks:
   `cmake -S . -B build -DMAX22200_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`.
   Every target builds as ISO C++20 (`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`), and the target
   `max22200_header_checks` compiles each public header alone and asserts the dialect (handbook KB-CPP20-001).
2. Run them: `ctest --test-dir build --output-on-failure`. This includes the version-copy tests, the install
   consumer, the host example (`examples/host`) and, when `doxygen` is installed, Doxygen with warnings as errors.
3. If both GCC and Clang are installed, repeat 1 and 2 with `-DCMAKE_CXX_COMPILER=g++` and `clang++` in separate
   build directories.
4. The handbook's source checks (include guards, bus requirements, raw access, the C++20 profile), at the commit
   CI pins (the `ref:` of the handbook checkout in `.github/workflows/ci-repo-checks.yml`):
   `curl -fsSL https://raw.githubusercontent.com/kokeb-labs/handbook/<ref>/checks/source_checks.py -o /tmp/sc.py`
   and `python3 /tmp/sc.py scan --repo .`. Every finding fails CI.
   Then the stand-alone check: `python3 tools/check_standalone.py`. It reports another project's name (a board,
   a test project, another driver repository), a dated history line, and a copyright holder other than
   `The <repo> authors`. Rewrite the sentence as this chip's behaviour with its datasheet section; a step still
   to check on hardware stays a bench procedure without a date.
5. Format the changed C++ files: `clang-format --style=file:_config/.clang-format -i <files>`, then
   `git diff --exit-code` shows nothing new.
6. Tidy the changed files in `inc/` and `src/` when clang-tidy is installed:
   `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` then
   `clang-tidy -p build --config-file=_config/.clang-tidy <files>` on the changed `.cpp` files and the tests
   that include the changed headers; fix findings in changed lines.
7. cppcheck and complexity, as CI runs them (when installed):
   `cppcheck --std=c++20 --language=c++ --enable=warning,style,performance,portability,information --force
   --inline-suppr -I inc inc src` reports nothing, and `lizard -l cpp -m -w inc src` stays within the limits of the
   repository's level (handbook ADR-0020).
8. Markdown:
   `npx markdownlint-cli2 --config _config/.markdownlint.json "**/*.md" "#build*" "#examples/esp32/scripts"`
   (skip if Node is not available and say so in the PR).
9. Docs (in a venv with the docs theme, see AGENTS.md):
   `kokeb-docs figures && kokeb-docs gen`, then
   `kokeb-docs check && mkdocs build -f _config/mkdocs.yml --strict -d /tmp/site`.
   Commit any figure or generated region that changed. CI builds the docs without submodules; if
   `examples/esp32/scripts` is checked out, run `git submodule deinit -f examples/esp32/scripts` first (its
   README has figure markers of its own), and `git submodule update --init` afterwards.
10. In the template repository only: `python3 tools/scaffold.py --selftest` scaffolds every kind into temporary
   directories, checks that each scaffolded root holds only the allowed files, no template name, no other
   project's name and the portable copyright (the stand-alone check with `--strict`), and runs steps 1, 2 and 9
   on each.
11. Review the change: the `cpp20-profile`, `misra-review` and (drivers) `hardware-lessons-review` skills, or the
    `safety-reviewer` agent (`.claude/agents/safety-reviewer.md`), which runs them with `datasheet-conformance`
    and reports without editing. Fix what they find, or list it in the PR with the reason.
12. Delete the build directories (`build*/`, `/tmp/site`) when done.

Report what ran and what could not run locally (ESP32 builds always run in CI only).

On macOS: Apple's linker cannot link libFuzzer targets from Homebrew LLVM (add `-Wl,-ld_classic`, or run the fuzz
entry point over random inputs under ASan and UBSan instead), and local branch coverage differs from CI's GCC
on Linux; take the coverage gate's number from CI.
