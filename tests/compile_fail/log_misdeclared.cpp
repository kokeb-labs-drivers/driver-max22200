/**
 * @file log_misdeclared.cpp
 * @brief Must NOT compile: a Log hook with another signature is rejected, not silently ignored
 * (handbook KB-CPP20-007).
 *
 * EXPECT: Log hook must be
 */
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"

class OddLogPort : public max22200_test::MockPortBase<OddLogPort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  void Log(const char* message) noexcept {
    (void)message;
  }
};

int main() {
  OddLogPort port;
  max22200::MAX22200<OddLogPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
