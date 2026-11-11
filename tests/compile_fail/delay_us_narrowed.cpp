/**
 * @file delay_us_narrowed.cpp
 * @brief Must NOT compile: a DelayUs hook with a narrower parameter would truncate the wake timeout
 * and tDIS, so the driver rejects it instead of using it (handbook KB-CPP20-007).
 *
 * EXPECT: DelayUs hook must be
 */
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"

class ShortDelayPort : public max22200_test::MockPortBase<ShortDelayPort> {
public:
  void DelayUs(uint16_t us) noexcept {
    (void)us;
  }
};

int main() {
  ShortDelayPort port;
  max22200::MAX22200<ShortDelayPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
