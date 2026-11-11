/**
 * @file my_port.hpp
 * @brief The SPI port shown on the Quickstart and Porting pages, compiled by the host build.
 *
 * Every platform call goes through the `platform_*` functions declared below; a real port replaces
 * them with its SDK (ESP-IDF, STM32 HAL, Zephyr, Linux spidev).
 */
#ifndef MAX22200_MY_PORT_HPP
#define MAX22200_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "max22200_spi_interface.hpp"

// Your SDK's calls. Declared only: the docs build checks that the port compiles.
bool platform_spi_init() noexcept; // mode 0, MSB first, <= 10 MHz; CMD high and ENABLE low
                                   // first, then both as outputs
bool platform_spi_transfer(const uint8_t* tx, uint8_t* rx,
                           std::size_t len) noexcept; // CSB low for all len bytes
bool platform_gpio_write(int pin, bool high) noexcept;
void platform_delay_us(uint32_t us) noexcept; // busy-wait or sleep

// --8<-- [start:port]
class MyPort : public max22200::SpiInterface<MyPort> {
public:
  // The bus settings platform_spi_init() uses, checked against the driver's requirements.
  static constexpr uint32_t kClockHz = 1'000'000;
  static_assert(kClockHz <= max22200::kMaxClockHz, "above the MAX22200's tCLK limit");
  static_assert(max22200::kSpiMode == 0 && max22200::kMsbFirst, "set the bus to mode 0, MSB first");

  // Bring the bus up once, CMD at its idle level high and ENABLE low before either pin becomes an
  // output; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_spi_init();
    }
    return ready_;
  }

  // One transfer: 1 byte (command or 8-bit data) or 4 bytes (32-bit data), CSB low throughout.
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return platform_spi_transfer(tx, rx, len);
  }

  // CMD and ENABLE are both active high. Return false when the pin could not be driven.
  bool GpioSet(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    const bool high = signal == max22200::GpioSignal::ACTIVE;
    switch (pin) {
    case max22200::CtrlPin::Cmd:
      return platform_gpio_write(kCmdGpio, high);
    case max22200::CtrlPin::Enable:
      return platform_gpio_write(kEnableGpio, high); // tied high on the board? return true
    case max22200::CtrlPin::Fault:                   // an input: never driven
    default:
      break;
    }
    return false;
  }

  // Wait at least `us` microseconds: tEN (0.5 ms) and tWU (2.5 ms) at start-up.
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

private:
  static constexpr int kCmdGpio = 5;
  static constexpr int kEnableGpio = 6;
  bool ready_ = false;
};
// --8<-- [end:port]

#endif // MAX22200_MY_PORT_HPP
