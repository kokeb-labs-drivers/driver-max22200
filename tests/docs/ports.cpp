/**
 * @file ports.cpp
 * @brief The port with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <cstddef>
#include <cstdint>

#include "max22200.hpp"

bool platform_spi_init() noexcept;
bool platform_spi_transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept;
void platform_delay_us(uint32_t us) noexcept;
bool platform_gpio_write(int pin, bool high) noexcept;
bool platform_gpio_read(int pin, bool& high) noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:full-port]
class FullPort : public max22200::SpiInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_spi_init();
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return platform_spi_transfer(tx, rx, len);
  }
  bool GpioSet(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    const bool high = signal == max22200::GpioSignal::ACTIVE;
    if (pin == max22200::CtrlPin::Cmd) {
      return platform_gpio_write(kCmdGpio, high);
    }
    return pin == max22200::CtrlPin::Enable && platform_gpio_write(kEnableGpio, high);
  }
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

  // Optional: the FAULT pin, open drain and active low. ACTIVE means a fault, a low level.
  bool GpioRead(max22200::CtrlPin pin, max22200::GpioSignal& signal) noexcept {
    bool high = true;
    if (pin != max22200::CtrlPin::Fault || !platform_gpio_read(kFaultGpio, high)) {
      return false;
    }
    signal = high ? max22200::GpioSignal::INACTIVE : max22200::GpioSignal::ACTIVE;
    return true;
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(max22200::LogLevel level, const char* message) noexcept {
    if (level > max22200::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }

private:
  static constexpr int kCmdGpio = 5;
  static constexpr int kEnableGpio = 6;
  static constexpr int kFaultGpio = 7;
};
// --8<-- [end:full-port]

// --8<-- [start:fault-pin]
// In the FAULT interrupt's deferred handler (never in the ISR: it uses the bus).
max22200::Result<max22200::Faults> OnFaultPin(max22200::MAX22200<FullPort>& chip) {
  const auto pin = chip.ReadFaultPin(); // no SPI
  if (!pin || !pin.value) {
    return max22200::Result<max22200::Faults>::Failure(pin.error);
  }
  return chip.ReadFaults(); // which channel, which fault; the MAX22200A keeps the flags
}
// --8<-- [end:fault-pin]
