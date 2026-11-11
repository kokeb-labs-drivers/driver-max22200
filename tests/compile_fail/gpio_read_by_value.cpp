/**
 * @file gpio_read_by_value.cpp
 * @brief Must NOT compile: a GpioRead hook that takes the signal by value can never report it, so
 * ReadFaultPin() would always say "no fault"; the driver rejects it (handbook KB-CPP20-007).
 *
 * EXPECT: GpioRead hook must be
 */
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"

class ByValuePort : public max22200_test::MockPortBase<ByValuePort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  bool GpioRead(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    (void)pin;
    return signal == max22200::GpioSignal::ACTIVE;
  }
};

int main() {
  ByValuePort port;
  max22200::MAX22200<ByValuePort> chip(port);
  return static_cast<int>(chip.ReadFaultPin().error);
}
