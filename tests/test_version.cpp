/**
 * @file test_version.cpp
 * @brief The version getters follow the generated max22200_version.h, so they cannot drift from
 * CMake.
 *
 * Built twice by tests/CMakeLists.txt: against the real generated header (the getters must equal
 * the CMake project version, passed as EXPECTED_*), and against a probe header generated from the
 * same template with version 9.8.7 (a getter that returns a literal instead of reading the header
 * fails there).
 */
#include <cstdio>
#include <cstring>

#include "max22200_types.hpp"

static_assert(max22200::GetDriverVersionMajor() == EXPECTED_MAJOR,
              "major version differs from CMake");
static_assert(max22200::GetDriverVersionMinor() == EXPECTED_MINOR,
              "minor version differs from CMake");
static_assert(max22200::GetDriverVersionPatch() == EXPECTED_PATCH,
              "patch version differs from CMake");

int main() {
  if (std::strcmp(max22200::GetDriverVersion(), EXPECTED_STRING) != 0) {
    std::printf("version string %s, expected %s\n", max22200::GetDriverVersion(), EXPECTED_STRING);
    return 1;
  }
  std::printf("version %s: PASS\n", max22200::GetDriverVersion());
  return 0;
}
