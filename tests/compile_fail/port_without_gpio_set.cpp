/**
 * @file port_without_gpio_set.cpp
 * @brief Must NOT compile: CMD selects the Command Register, so the GpioSet hook is required.
 *
 * EXPECT: GpioSet
 */
#include <cstddef>
#include <cstdint>

#include "max22200.hpp"

class NoGpioPort : public max22200::SpiInterface<NoGpioPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    (void)tx;
    (void)rx;
    (void)len;
    return true;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

int main() {
  NoGpioPort port;
  max22200::MAX22200<NoGpioPort> chip(port);
  return static_cast<int>(chip.EnsureInitialized().error);
}
