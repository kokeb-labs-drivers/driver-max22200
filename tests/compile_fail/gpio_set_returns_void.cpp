/**
 * @file gpio_set_returns_void.cpp
 * @brief Must NOT compile: a GpioSet hook that cannot report a failed pin write does not satisfy
 * the port concept (handbook KB-CPP20-006).
 *
 * EXPECT: GpioSet
 */
#include "max22200.hpp"
#include "mock_max22200.hpp"

class VoidGpioPort : public max22200_test::MockPortBase<VoidGpioPort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  void GpioSet(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    (void)pin;
    (void)signal;
  }
};

int main() {
  VoidGpioPort port;
  max22200::MAX22200<VoidGpioPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
