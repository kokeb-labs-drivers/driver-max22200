/**
 * @file hook_narrowed_parameter.cpp
 * @brief Must NOT compile: a Transfer hook with a narrower length would truncate every transfer, so
 * the port concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: SpiPort
 */
#include <cstddef>
#include <cstdint>

#include "max22200.hpp"

class NarrowPort : public max22200::SpiInterface<NarrowPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) noexcept {
    (void)tx;
    (void)rx;
    return len > 0U;
  }
  bool GpioSet(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    (void)pin;
    (void)signal;
    return true;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

int main() {
  NarrowPort port;
  max22200::MAX22200<NarrowPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
