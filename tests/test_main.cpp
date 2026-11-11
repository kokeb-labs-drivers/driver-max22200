/**
 * @file test_main.cpp
 * @brief Runs every registered host test; the exit code is non-zero when one fails.
 */
#include "test_harness.hpp"

int main() {
  ::ut::Registry& r = ::ut::GetRegistry();
  int failed_cases = 0;
  for (int i = 0; i < r.count; ++i) {
    const int before = r.failures;
    std::printf("[ RUN  ] %s\n", r.tests[i].name);
    r.tests[i].fn();
    const bool ok = r.failures == before;
    std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", r.tests[i].name);
    if (!ok) {
      ++failed_cases;
    }
  }
  std::printf("%d test cases, %d failed\n", r.count, failed_cases);
  return failed_cases == 0 && r.count > 0 ? 0 : 1;
}
