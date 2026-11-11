# =============================================================================
# Fuzz targets (docs/testing.md, "Fuzz tests")
# =============================================================================
# Each tests/fuzz/fuzz_<name>.cpp defines LLVMFuzzerTestOneInput() and is built twice:
#
#   fuzz_replay_<name>   any compiler; a CTest that replays tests/fuzz/corpus/fuzz_<name>/
#   fuzz_<name>          Clang with -DMAX22200_BUILD_FUZZERS=ON: libFuzzer with ASan and UBSan
#
#   cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DMAX22200_BUILD_TESTS=ON -DMAX22200_BUILD_FUZZERS=ON
#   cmake --build build-fuzz --target fuzz_max22200
#   ./build-fuzz/tests/fuzz/fuzz_max22200 -max_total_time=60 tests/fuzz/corpus/fuzz_max22200
# =============================================================================

file(GLOB MAX22200_FUZZ_TARGETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_*.cpp)
foreach(src ${MAX22200_FUZZ_TARGETS})
  get_filename_component(name ${src} NAME_WE)

  add_executable(fuzz_replay_${name} ${src} replay_main.cpp)
  max22200_test_target(fuzz_replay_${name})
  target_include_directories(fuzz_replay_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  add_test(NAME max22200_${name}_corpus
    COMMAND fuzz_replay_${name} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})

  if(MAX22200_BUILD_FUZZERS)
    add_executable(${name} ${src})
    max22200_test_target(${name})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -g -O1)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
endforeach()
