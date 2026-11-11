/**
 * @file gpio_read_misdeclared.cpp
 * @brief Must NOT compile: an optional GpioRead hook that cannot report a failed pin read is
 * rejected, not ignored (handbook KB-CPP20-007).
 *
 * EXPECT: GpioRead hook must be
 */
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"

class VoidReadPort : public max22200_test::MockPortBase<VoidReadPort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  void GpioRead(max22200::CtrlPin pin, max22200::GpioSignal& signal) noexcept {
    (void)pin;
    signal = max22200::GpioSignal::INACTIVE;
  }
};

int main() {
  VoidReadPort port;
  max22200::MAX22200<VoidReadPort> chip(port);
  return static_cast<int>(chip.ReadFaultPin().error);
}
