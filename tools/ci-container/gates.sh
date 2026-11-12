#!/usr/bin/env bash
# The CI gates that need Linux: coverage (GCC 14 + gcovr, MC/DC), ASan+UBSan tests, libFuzzer. Run inside the CI container
# (`tools/preflight.sh --container` calls it). FUZZ_SECONDS per target (default 20; CI uses 60/120/600 by level).
set -uo pipefail
cd "${1:-/src}" || exit 2
prefix="$(grep -oE 'option\(([A-Z0-9_]+)_BUILD_TESTS' CMakeLists.txt | head -1 | sed 's/option(//; s/_BUILD_TESTS//')"
fz="$(grep -oE "${prefix}_BUILD_FUZZERS" CMakeLists.txt | head -1)"
out=/tmp/gates; rm -rf "$out"; mkdir -p "$out"; fails=()
run() { local n="$1"; shift; if "$@" >"$out/$n.log" 2>&1; then echo "PASS $n"; else echo "FAIL $n"; tail -25 "$out/$n.log" | sed 's/^/     /'; fails+=("$n"); fi; }
cov() {
  local f="--coverage -O0 -g -fcondition-coverage"
  cmake -S . -B "$out/cov" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
    -DCMAKE_CXX_FLAGS="$f" -DCMAKE_EXE_LINKER_FLAGS=--coverage "-D${prefix}_BUILD_TESTS=ON" &&
  cmake --build "$out/cov" --parallel && ctest --test-dir "$out/cov" --output-on-failure --parallel "$(nproc)" || return 1
  mapfile -t d < <(find "$out/cov" -name '*.gcda' -printf '%h\n' | sort -u)
  gcovr --root . --gcov-executable gcov-14 --filter '^inc/' --filter '^src/' --exclude-unreachable-branches \
    --exclude-throw-branches --merge-lines --txt "$out/coverage.txt" "${d[@]}" && tail -8 "$out/coverage.txt"
}
san() {
  local f="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -O1 -g"
  cmake -S . -B "$out/san" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_CXX_FLAGS="$f" \
    -DCMAKE_EXE_LINKER_FLAGS="$f" "-D${prefix}_BUILD_TESTS=ON" && cmake --build "$out/san" --parallel &&
  ctest --test-dir "$out/san" --output-on-failure --parallel "$(nproc)"
}
fuzz() {
  cmake -S . -B "$out/fz" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18 "-D${prefix}_BUILD_TESTS=ON" \
    "-D${fz}=ON" && cmake --build "$out/fz" --parallel || return 1
  local t n; while IFS= read -r t; do n="$(basename "$t")"; mkdir -p "$out/corpus/$n"
    "$t" -max_total_time="${FUZZ_SECONDS:-20}" -artifact_prefix="$out/crash-$n-" "$out/corpus/$n" "tests/fuzz/corpus/$n" || return 1
  done < <(find "$out/fz" -type f -perm -u+x -name 'fuzz_*' ! -name 'fuzz_replay_*' | sort)
}
run coverage cov; [ -f "$out/coverage.txt" ] && grep -E "^(TOTAL|lines|branches|conditions)" "$out/coverage.txt"; run sanitizers san
if [ -n "$fz" ]; then run fuzz fuzz; fi
echo "== ${#fails[@]} gate failure(s)${fails[*]:+: ${fails[*]}} (logs in the container: $out)"
[ ${#fails[@]} -eq 0 ]
