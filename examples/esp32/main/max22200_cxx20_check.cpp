/**
 * @file max22200_cxx20_check.cpp
 * @brief The ESP-IDF header self-check: the public headers compiled as ISO C++20.
 * @copyright Copyright (c) 2026 The driver-max22200 authors. SPDX-License-Identifier: MIT
 *
 * ESP-IDF builds C++ in a newer GNU dialect by default (gnu++23 or gnu++26), which would accept a
 * C++23 feature or a GNU extension in a header. main/CMakeLists.txt compiles this file alone with
 * -std=c++20 -pedantic-errors and the ESP-IDF toolchain of the target, and the assertion below
 * proves the flag took effect (handbook KB-CPP20-001, check strict-dialect). It adds no code.
 */
#include "max22200.hpp"
#include "max22200_kconfig.hpp"

static_assert(__cplusplus == 202002L, "the ESP-IDF header self-check must build as -std=c++20");
