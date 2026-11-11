# =============================================================================
# MAX22200 driver: host example (any PC, no hardware)
# =============================================================================
# The driver against a simulated MAX22200 (the protocol model the host tests
# use, tests/mock_max22200.hpp) through the host port in this folder.
#
#   cmake -S examples/host -B build-example
#   cmake --build build-example && ./build-example/max22200_host_example
#
# The host tests also build this folder and run the example (CTest
# max22200_host_example), so it always compiles against the current headers.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(max22200_host_example LANGUAGES CXX)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. max22200)
endif()

# ISO C++20, also when this folder is built on its own (handbook KB-CPP20-001).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(max22200_host_example main.cpp)
target_link_libraries(max22200_host_example PRIVATE max22200::max22200)
target_include_directories(max22200_host_example PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../../tests)
target_compile_features(max22200_host_example PRIVATE cxx_std_20)
target_compile_options(max22200_host_example PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
  -pedantic-errors)
