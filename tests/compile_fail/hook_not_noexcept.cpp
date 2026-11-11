/**
 * @file hook_not_noexcept.cpp
 * @brief Must NOT compile: a Transfer hook that is not noexcept breaks the port contract, so the
 * port concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: SpiPort
 */
#include <cstddef>
#include <cstdint>

#include "max22200.hpp"

class ThrowingPort : public max22200::SpiInterface<ThrowingPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) {
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
  ThrowingPort port;
  max22200::MAX22200<ThrowingPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
