/**
 * @file test_harness.hpp
 * @brief A dependency-free test registry and check macros for the host tests.
 *
 * Built with -fno-exceptions -fno-rtti like the firmware, so it uses plain function pointers and a
 * fixed table.
 */
#ifndef MAX22200_TEST_HARNESS_HPP
#define MAX22200_TEST_HARNESS_HPP

#include <cstdio>

namespace ut {

using TestFn = void (*)();

struct TestCase {
  const char* name;
  TestFn fn;
};

inline constexpr int kMaxTests = 256;

struct Registry {
  TestCase tests[kMaxTests]{};
  int count = 0;
  int failures = 0;
};

inline Registry& GetRegistry() {
  static Registry registry{};
  return registry;
}

struct Registrar {
  Registrar(const char* name, TestFn fn) {
    Registry& r = GetRegistry();
    if (r.count < kMaxTests) {
      r.tests[r.count++] = {name, fn};
    }
  }
};

inline void ReportFailure(const char* file, int line, const char* what) {
  ++GetRegistry().failures;
  std::printf("    FAIL %s:%d: %s\n", file, line, what);
}

inline void ReportFailureValues(const char* file, int line, const char* what, long long a,
                                long long b) {
  ++GetRegistry().failures;
  std::printf("    FAIL %s:%d: %s (%lld vs %lld)\n", file, line, what, a, b);
}

} // namespace ut

#define UT_CONCAT2(a, b) a##b
#define UT_CONCAT(a, b) UT_CONCAT2(a, b)

/// Define a test case.
#define TEST_CASE(name)                                                                            \
  static void UT_CONCAT(ut_fn_, __LINE__)();                                                       \
  static const ::ut::Registrar UT_CONCAT(ut_reg_, __LINE__)(name, &UT_CONCAT(ut_fn_, __LINE__));   \
  static void UT_CONCAT(ut_fn_, __LINE__)()

/// Non-fatal check.
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    const bool ut_ok = static_cast<bool>(cond); /* one name, so `a || b` is not rewritten */       \
    if (!ut_ok) {                                                                                  \
      ::ut::ReportFailure(__FILE__, __LINE__, #cond);                                              \
    }                                                                                              \
  } while (0)

/// Non-fatal equality check of two integral or enum values.
#define CHECK_EQ(a, b)                                                                             \
  do {                                                                                             \
    const auto ut_a = (a);                                                                         \
    const auto ut_b = (b);                                                                         \
    if (!(ut_a == ut_b)) {                                                                         \
      ::ut::ReportFailureValues(__FILE__, __LINE__, #a " == " #b, static_cast<long long>(ut_a),    \
                                static_cast<long long>(ut_b));                                     \
    }                                                                                              \
  } while (0)

/// Fatal check: stop the current test case.
#define REQUIRE(cond)                                                                              \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      ::ut::ReportFailure(__FILE__, __LINE__, #cond);                                              \
      return;                                                                                      \
    }                                                                                              \
  } while (0)

#endif // MAX22200_TEST_HARNESS_HPP
