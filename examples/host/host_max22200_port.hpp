/**
 * @file host_max22200_port.hpp
 * @brief The host port: the driver's bus hooks over a simulated MAX22200, so the example runs on
 * any PC.
 *
 * `max22200_test::MockPortBase` (tests/mock_max22200_bus.hpp) supplies the required bus and pin
 * hooks over the protocol model the host tests use. This class adds what a real port adds:
 * `DelayUs()` (here it advances the simulated chip's clock instead of waiting) and `Log()` to the
 * console. A port for a real bus on a PC (Linux spidev, a USB bridge) implements the same hooks:
 * see docs/porting.md.
 */
#ifndef MAX22200_HOST_MAX22200_PORT_HPP
#define MAX22200_HOST_MAX22200_PORT_HPP

#include <cstdint>
#include <cstdio>

#include "mock_max22200.hpp"

/// The host port: simulated chip, real console.
class HostMax22200Port : public max22200_test::MockPortBase<HostMax22200Port> {
public:
  /// The bus settings a real port would use; checked against the driver's bus requirements
  /// (handbook KB-CPP-123) like any port's.
  static constexpr uint32_t kClockHz = 1'000'000;
  static constexpr uint8_t kSpiMode = 0;
  static_assert(kClockHz <= max22200::kMaxClockHz, "clock above the MAX22200's tCLK limit");
  static_assert(kSpiMode == max22200::kSpiMode, "the MAX22200 works in SPI mode 0 only");

  /// Time passes in the simulated chip; nothing sleeps.
  void DelayUs(uint32_t us) noexcept {
    state.chip.Elapse(us);
  }

  /// Driver messages (they carry their own `max22200:` prefix) go to the console, one line each.
  void Log(max22200::LogLevel level, const char* message) noexcept {
    (void)level;
    (void)std::puts(message); // plain text: never pass it as a format string
  }
};

#endif // MAX22200_HOST_MAX22200_PORT_HPP
