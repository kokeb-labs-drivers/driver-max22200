#!/usr/bin/env bash
# Every check CI runs that can run on a workstation, in one command, before every push (the `verify` skill).
# CI minutes are not for finding what this finds: push only when it ends with "0 failure(s)".
#
#   tools/preflight.sh            run everything that is installed; say what was skipped
#   tools/preflight.sh --quick    skip the second compiler, clang-tidy and the docs build
#
# Tools are found on PATH, or named by these variables:
#   CXX_GCC (g++), CXX_CLANG (clang++), CLANG_FORMAT, CLANG_TIDY (version 20, as CI), ARM_CXX (arm-none-eabi-g++),
#   VENV (a Python venv with lizard, yamllint, kokeb-docs and mkdocs; see AGENTS.md "Docs checks").
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

# 4. Format, tidy, cppcheck, complexity.
if [ -n "$FMT" ]; then
  fmt() { git ls-files '*.hpp' '*.ipp' '*.cpp' '*.h' | grep -v '^examples/esp32/scripts' |
    xargs "$FMT" --style=file:_config/.clang-format --dry-run --Werror; }
  step clang-format fmt
else skip clang-format "no clang-format"; fi
if [ "$quick" = 0 ] && [ -n "$TIDY" ] && [ -n "$first" ]; then
  tidy() {  # every unit of the host build and every header alone (CI lints each file); fail on a file CI lints
    local units=() f
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
    [ "$linked" = 0 ] || rm -f .clang-tidy
    sed -E 's#/inc/\.\./src/#/src/#g; s#/tests/\.\./(inc|src)/#/\1/#g' "$work/tidy.raw" >"$work/tidy.out"
    grep -E "^$root/.*(warning|error):" "$work/tidy.out" |
      grep -vE "^$root/(build|docs|_site|examples/esp32|tools/scaffold|tests/compile_fail)/" >"$work/tidy.findings"
    if [ -s "$work/tidy.findings" ]; then cat "$work/tidy.findings"; return 1; fi
    # clang-tidy itself failing (a file it could not open or parse) is a failure too, not a clean run.
    ! grep -qE "^Error while processing|error: no such file|LLVM ERROR" "$work/tidy.out"
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
