/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>

#include "max22200.hpp"

namespace {

// A port that answers every read with 0: enough to instantiate the whole driver template.
class NullPort : public max22200::SpiInterface<NullPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    (void)tx;
    for (std::size_t i = 0; rx != nullptr && i < len; ++i) {
      rx[i] = 0;
    }
    return true;
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

} // namespace

int main() {
  NullPort port;
  max22200::MAX22200<NullPort> chip(port);
  // SDO reads 0 (no command echo), so EnsureInitialized() must fail with DeviceIdMismatch: proof
  // the template compiled and ran.
  const auto r = chip.EnsureInitialized();
  return (r.error == max22200::Error::DeviceIdMismatch && max22200::GetDriverVersion()[0] != '\0')
             ? 0
             : 1;
}
