#!/usr/bin/env bash
# Every check CI runs that can run on a workstation, in one command, before every push (the `verify` skill).
# CI minutes are not for finding what this finds: push only when it ends with "0 failure(s)".
#
#   tools/preflight.sh            run everything that is installed; say what was skipped
#   tools/preflight.sh --quick    skip the second compiler, clang-tidy and the docs build
#
# Tools are found on PATH, or named by these variables:
#   CXX_GCC (g++), CXX_CLANG (clang++), CLANG_FORMAT, CLANG_TIDY (version 20, as CI), ARM_CXX (arm-none-eabi-g++),
#   VENV (a Python venv with lizard, yamllint, kokeb-docs and mkdocs; see AGENTS.md "Docs checks"),
#   CI_TOOLS_DIR (a kokeb-labs/ci-tools checkout, default ../ci-tools: handbook_check.py runs from it, and its include
#   subset is the one tools/misra_lint.py enforces; handbook_check needs PyYAML, which the VENV has).
# Left to CI: the ESP32 builds, the MISRA scan, and the coverage, sanitizer and fuzz gates.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root" || exit 2
quick=0; [ "${1:-}" = "--quick" ] && quick=1
if [ "${1:-}" = "--container" ]; then
  # The CI toolchain (Ubuntu 24.04, GCC 14, clang 18/20, cppcheck, lizard, gcovr, Arm GCC) in Docker: this script and the
  # Linux-only gates (coverage, sanitizers, libFuzzer) run inside it on this checkout. First run builds the image.
  img=kokeb-ci:local
  [ -n "$(docker images -q "$img")" ] || docker build -t "$img" tools/ci-container || exit 2
  docker run --rm -v "$root:/src" "$img" bash -c \
    'cp -a /src /tmp/w && cd /tmp/w && tools/preflight.sh; a=$?; tools/ci-container/gates.sh /tmp/w; b=$?; exit $((a|b))'
  exit $?
fi
prefix="$(grep -oE 'option\(([A-Z0-9_]+)_BUILD_TESTS' CMakeLists.txt | head -1 | sed 's/option(//; s/_BUILD_TESTS//')"
[ -n "$prefix" ] || { echo "no <NAME>_BUILD_TESTS option in CMakeLists.txt"; exit 2; }
work="$(mktemp -d "${TMPDIR:-/tmp}/preflight.XXXXXX")"
fails=(); skipped=()

tool() {  # tool <variable> <candidates...>: the first one found
  local var="$1"; shift
  if [ -n "${!var:-}" ]; then echo "${!var}"; return; fi
  local c; for c in "$@"; do command -v "$c" >/dev/null 2>&1 && { command -v "$c"; return; }; done
}
pytool() { if [ -n "${VENV:-}" ] && [ -x "$VENV/bin/$1" ]; then echo "$VENV/bin/$1"; else command -v "$1"; fi; }
step() {
  local name="$1"; shift
  if "$@" >"$work/$name.log" 2>&1; then echo "PASS $name"
  else echo "FAIL $name (log: $work/$name.log)"; tail -20 "$work/$name.log" | sed 's/^/     /'; fails+=("$name"); fi
}
skip() { echo "SKIP $1 ($2)"; skipped+=("$1"); }

GCC="$(tool CXX_GCC g++-15 g++-14 g++-13 g++)"
CLANG="$(tool CXX_CLANG clang++-20 clang++)"
FMT="$(tool CLANG_FORMAT clang-format-20 /opt/homebrew/opt/llvm@20/bin/clang-format clang-format)"
TIDY="$(tool CLANG_TIDY clang-tidy-20 /opt/homebrew/opt/llvm@20/bin/clang-tidy clang-tidy)"
ARM="$(tool ARM_CXX arm-none-eabi-g++)"
SYSROOT=(); command -v xcrun >/dev/null 2>&1 && SYSROOT=(--extra-arg=-isysroot "--extra-arg=$(xcrun --show-sdk-path)")

build() {  # build <compiler> <dir>
  cmake -S . -B "$2" -DCMAKE_BUILD_TYPE=Debug "-D${prefix}_BUILD_TESTS=ON" "-DCMAKE_CXX_COMPILER=$1" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build "$2" --parallel && ctest --test-dir "$2" --output-on-failure
}
# 1. Host build and tests (strict dialect, header checks, compile-fail tests, docs snippets, examples, Doxygen).
first=""
if [ -n "$GCC" ]; then step gcc-build-test build "$GCC" "$work/gcc"; first="$work/gcc"; else skip gcc "no g++"; fi
if [ "$quick" = 0 ] && [ -n "$CLANG" ]; then step clang-build-test build "$CLANG" "$work/clang"; first="$work/clang"
elif [ -z "$CLANG" ]; then skip clang "no clang++"; fi
# 2. Cortex-M: every test source compiled for the cross target (no link), as ru-cross-build does.
if [ -n "$ARM" ]; then
  cross() {
    cmake -S . -B "$work/arm" -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
      "-DCMAKE_CXX_COMPILER=$ARM" "-DCMAKE_CXX_FLAGS=-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16" \
      "-D${prefix}_BUILD_TESTS=ON" && cmake --build "$work/arm" --parallel --target "$(echo "$prefix" | tr A-Z a-z)_optimised_build"
  }
  step cortex-m cross
else skip cortex-m "no arm-none-eabi-g++; CI builds it"; fi

# 3. The handbook's source checks, at the commit CI pins.
ref="$(grep -oE 'ref: [0-9a-f]{40}' .github/workflows/ci-repo-checks.yml | head -1 | cut -d' ' -f2)"
handbook() {
  if [ -n "${HANDBOOK_DIR:-}" ]; then git -C "$HANDBOOK_DIR" show "$ref:checks/source_checks.py" >"$work/sc.py"
  else curl -fsSL "https://raw.githubusercontent.com/kokeb-labs/handbook/$ref/checks/source_checks.py" -o "$work/sc.py"; fi &&
    python3 "$work/sc.py" scan --repo .
}
step handbook-source-checks handbook
step standalone python3 tools/check_standalone.py
# The handbook's repository checks (handbook_check.py of ci-tools, as the repository-checks job runs them): the only
# skip is a missing checkout or PyYAML, and it is named.
CI_TOOLS="${CI_TOOLS_DIR:-$root/../ci-tools}"
HB_PY=python3; [ -n "${VENV:-}" ] && [ -x "$VENV/bin/python3" ] && HB_PY="$VENV/bin/python3"
if [ ! -f "$CI_TOOLS/tools/handbook_check/handbook_check.py" ]; then
  skip handbook-check "no ci-tools checkout at $CI_TOOLS; set CI_TOOLS_DIR"
elif ! "$HB_PY" -c 'import yaml' >/dev/null 2>&1; then
  skip handbook-check "no PyYAML for $HB_PY; set VENV"
else
  hbcheck() { "$HB_PY" "$CI_TOOLS/tools/handbook_check/handbook_check.py" --repo . | tee "$work/hb.out" &&
    grep -q '^0 error(s)' "$work/hb.out"; }
  step handbook-check hbcheck
fi
# MISRA C++:2023 defect classes the CI scan reported on earlier drivers (tools/misra_lint.py): not skippable.
step misra-lint-selftest python3 tools/misra_lint.py --selftest
step misra-lint python3 tools/misra_lint.py --quiet --root . --ci-tools "$CI_TOOLS"
# Unspecified evaluation order in a test or fuzz check (RD-34): a call must not take a driver operation and a read of the
# state it changes as separate arguments (GCC on x86-64 reads the arguments right to left; no other rehearsal sees it).
step eval-order-selftest python3 -I tools/eval_order_check.py --selftest
step eval-order python3 -I tools/eval_order_check.py --quiet --root .
# The agent context: every skill is linked from docs/agent-context.md, has front matter and names files that exist; one
# source per header and SDK code only in src/boundary/; the hardware level, badge and evidence agree.
step skills-selftest python3 -I tools/check_skills.py --selftest
step skills python3 -I tools/check_skills.py --quiet
step layout-selftest python3 -I tools/check_layout.py --selftest
step layout python3 -I tools/check_layout.py --quiet
step hardware-selftest python3 -I tools/check_hardware.py --selftest
step hardware python3 -I tools/check_hardware.py --quiet
# The C++ standard is declared once in driver.yml (`language:`) and agrees with the CMake files, clang-format, the
# -std flags, the workflows and the README (docs/language-standard.md).
step language-selftest python3 -I tools/check_language.py --selftest
step language python3 -I tools/check_language.py --quiet
# The first-push gitlink: after `git add -A`, examples/esp32/scripts is a gitlink (mode 160000), not a missing or
# empty folder (checked on a scratch copy of the index, the working tree's index is left alone).
gitlink() {
  [ -d examples/esp32 ] || { echo "no examples/esp32: nothing to check"; return 0; }
  local idx="$work/gitlink.index"
  cp "$(git rev-parse --git-path index)" "$idx" 2>/dev/null || : >"$idx"
  GIT_INDEX_FILE="$idx" git add -A >/dev/null 2>&1
  if GIT_INDEX_FILE="$idx" git ls-files -s -- examples/esp32/scripts | grep -q '^160000 '; then return 0; fi
  echo "examples/esp32/scripts is not a gitlink (mode 160000) after git add -A: the first commit would lack the ESP-IDF"
  echo "scripts submodule. Run: git submodule add https://github.com/kokeb-labs/espidf-tools.git examples/esp32/scripts"
  return 1
}
step gitlink gitlink

# 4. Format, tidy, cppcheck, complexity.
if [ -n "$FMT" ]; then
  fmt() { git ls-files '*.hpp' '*.ipp' '*.cpp' '*.h' | grep -v '^examples/esp32/scripts' |
    xargs "$FMT" --style=file:_config/.clang-format --dry-run --Werror; }
  step clang-format fmt
else skip clang-format "no clang-format"; fi
if [ "$quick" = 0 ] && [ -n "$TIDY" ] && [ -n "$first" ]; then
  tidy() {  # every unit of the host build and every header alone (CI lints each file); fail on a file CI lints
    local units=() f t0=$SECONDS
    while IFS= read -r f; do units+=("$f"); done < <(
      python3 -c 'import json,sys; print("\n".join(sorted({e["file"] for e in json.load(open(sys.argv[1]))})))' \
        "$first/compile_commands.json" | grep -E '/(tests|examples/host)/' | grep -v '/tests/compile_fail/'
      # Headers alone; an .ipp is only ever compiled through the header that includes it.
      git ls-files '*.hpp' '*.h' | grep -vE '^(build|docs|_site|examples/esp32|tools/scaffold|tests/compile_fail)/' |
        sed "s#^#$root/#")
    [ "${#units[@]}" -gt 0 ] || { echo "no files to lint"; return 1; }
    # As CI does: _config/.clang-tidy linked at the root, so each folder's own .clang-tidy (tests/, examples/host/)
    # still applies on top of it (InheritParentConfig); --config-file would replace them. The link goes again after.
    local linked=0
    [ -e .clang-tidy ] || { ln -s _config/.clang-tidy .clang-tidy && linked=1; }
    "$TIDY" -p "$first" --quiet "${SYSROOT[@]}" "${units[@]}" >"$work/tidy.raw" 2>&1
    # tests/install_consumer is built by its own CMake project (not in the compilation database) and CI lints it like
    # every other C++ file: lint it here against the source tree's headers and the generated version header.
    if [ -f tests/install_consumer/main.cpp ]; then
      local gen; gen="$(find "$first" -maxdepth 2 -type d -name '*_generated' | head -1)"
      "$TIDY" --quiet "${SYSROOT[@]}" "$root/tests/install_consumer/main.cpp" -- -std=c++20 -fno-exceptions -fno-rtti \
        "-I$root/inc" ${gen:+"-I$gen"} >>"$work/tidy.raw" 2>&1
    fi
    [ "$linked" = 0 ] || rm -f .clang-tidy
    sed -E 's#/inc/\.\./src/#/src/#g; s#/tests/\.\./(inc|src)/#/\1/#g' "$work/tidy.raw" >"$work/tidy.out"
    grep -E "^$root/.*(warning|error):" "$work/tidy.out" |
      grep -vE "^$root/(build|docs|_site|examples/esp32|tools/scaffold|tests/compile_fail)/" >"$work/tidy.findings"
    if [ -s "$work/tidy.findings" ]; then cat "$work/tidy.findings"; return 1; fi
    # clang-tidy itself failing (a file it could not open or parse) is a failure too, not a clean run.
    ! grep -qE "^Error while processing|error: no such file|LLVM ERROR" "$work/tidy.out" || return 1
    # CI stops the C++ lint job at 20 minutes: a third of that on this machine is a warning CI will time out.
    [ $((SECONDS - t0)) -le 400 ] || { echo "clang-tidy took $((SECONDS - t0)) s (limit 400 s): fewer or smaller test units"; return 1; }
  }
  step clang-tidy tidy
else skip clang-tidy "quick, or no clang-tidy"; fi
if command -v cppcheck >/dev/null; then
  cpp() { cppcheck --std=c++20 --language=c++ --enable=warning,style,performance,portability --force --inline-suppr \
    --suppress=missingIncludeSystem --suppress=normalCheckLevelMaxBranches --error-exitcode=1 -q -I inc inc src; }
  step cppcheck cpp
else skip cppcheck "no cppcheck"; fi
LIZARD="$(pytool lizard)"
if [ -n "$LIZARD" ]; then
  # The same files ru-host-quality measures, .ipp included (given a directory, lizard skips extensions it does not
  # know), at the high level's limits (handbook ADR-0020).
  liz() { find inc src -type f \( -name '*.[ch]' -o -name '*.[ch]pp' -o -name '*.cc' -o -name '*.hh' -o -name '*.cxx' \
    -o -name '*.hxx' -o -name '*.ipp' -o -name '*.tpp' -o -name '*.inl' \) -print0 | sort -z |
    xargs -0 "$LIZARD" -l cpp -m -w -C 15 -L 60 -a 6; }
  step lizard liz
else skip lizard "no lizard"; fi

# 5. Markdown, YAML, workflows.
if command -v npx >/dev/null; then
  md() { npx -y markdownlint-cli2@0.18.1 --config _config/.markdownlint.json "**/*.md" "#build*" "#examples/esp32/scripts" "#node_modules"; }
  step markdownlint md
else skip markdownlint "no npx"; fi
YAMLLINT="$(pytool yamllint)"
if [ -n "$YAMLLINT" ]; then step yamllint "$YAMLLINT" -c _config/.yamllint .github docs driver.yml; else skip yamllint "no yamllint"; fi
if command -v actionlint >/dev/null; then step actionlint actionlint .github/workflows/*.yml; else skip actionlint "no actionlint"; fi

# 6. Docs: figures and generated regions current, strict site build.
KD="$(pytool kokeb-docs)"; MK="$(pytool mkdocs)"
if [ "$quick" = 0 ] && [ -n "$KD" ] && [ -n "$MK" ]; then
  docs() { "$KD" figures && "$KD" gen && "$KD" check && git diff --exit-code -- docs README.md &&
    "$MK" build -f _config/mkdocs.yml --strict -d "$work/site"; }
  step docs docs
else skip docs "quick, or no kokeb-docs/mkdocs (VENV)"; fi

# 7. Versions agree; changelog headings are the allowed set.
ver() {
  local v; v=$(grep -E "${prefix}_VERSION_(MAJOR|MINOR|PATCH) " cmake/*_build_settings.cmake | grep -oE '[0-9]+\)' | tr -d ')' | paste -sd. -)
  grep -qE "version: \"?$v\"?" driver.yml || { echo "driver.yml version is not $v"; return 1; }
  grep -qE "^PROJECT_NUMBER *= *$v" _config/Doxyfile || { echo "Doxyfile PROJECT_NUMBER is not $v"; return 1; }
  ! grep -E '^### ' CHANGELOG.md | grep -vE '^### (Breaking|Added|Changed|Deprecated|Removed|Fixed|Security)$'
}
step versions ver
[ -f tools/scaffold.py ] && [ "$quick" = 0 ] && step scaffold-selftest python3 tools/scaffold.py --selftest

rm -rf "$work/gcc" "$work/clang" "$work/arm" "$work/site"
echo "== ${#fails[@]} failure(s)${fails[*]:+: ${fails[*]}}${skipped[*]:+; skipped: ${skipped[*]}}; CI only: ESP32, MISRA, coverage/sanitizer/fuzz gates"
[ ${#fails[@]} -eq 0 ]
