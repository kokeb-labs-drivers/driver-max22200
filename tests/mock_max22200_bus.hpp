/**
 * @file mock_max22200_bus.hpp
 * @brief SPI mock ports over the chip model.
 *
 * `MockPortBase<Self>` implements the required hooks of the SPI port (bus, transfer, CMD and
 * ENABLE) over a `MockBusState`. Derive from it to build a port with any set of optional hooks:
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `MinimalPort` | the required ones plus `DelayUs()` |
 * | `FullPort` | also `GpioRead()` (the FAULT pin) and `Log()` |
 */
#ifndef MAX22200_MOCK_MAX22200_BUS_HPP
#define MAX22200_MOCK_MAX22200_BUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "max22200_spi_interface.hpp"
#include "max22200_types.hpp"
#include "mock_max22200.hpp"

namespace max22200_test {

/// Shared state of the mock ports: the chip, fault injection and counters.
struct MockBusState {
  Max22200Model chip;
  bool bus_up_ok = true;            ///< EnsureInitialized() result.
  int fail_transfer = -1;           ///< Fail the transfer with this index (0-based); -1 never.
  bool fail_after_clocking = false; ///< The failed transfer reached the chip all the same.
  int lose_transfer = -1;           ///< Report this transfer as done; the chip never sees it.
  int replace_transfer = -1;        ///< Replace what SDO carried in this transfer.
  std::array<uint8_t, 4> replace_bytes{};
  int fail_pin = -1; ///< Fail the GpioSet() call with this index; the pin keeps its level.
  int brownout_at_transfer = -1; ///< A power-on reset just before the transfer with this index.
  int transfers = 0;             ///< Transfers so far.
  int pin_calls = 0;             ///< GpioSet() calls so far.
  int bus_up_calls = 0;          ///< EnsureInitialized() calls so far.
  int bad_transfers = 0; ///< Transfers outside the port contract (not 1 or 4 bytes, no buffer).
  std::array<uint8_t, 4096> first_byte{}; ///< First byte sent in each transfer.
  std::array<bool, 4096> with_cmd{};      ///< CMD was high during the transfer.

  bool DoTransfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    const int index = transfers++;
    if (index == brownout_at_transfer) {
      chip.Brownout();
    }
    if (tx == nullptr || rx == nullptr || (len != 1 && len != 4)) {
      bad_transfers++;
      return false;
    }
    if (static_cast<std::size_t>(index) < first_byte.size()) {
      first_byte[static_cast<std::size_t>(index)] = tx[0];
      with_cmd[static_cast<std::size_t>(index)] = chip.cmd;
    }
    if (index == fail_transfer) {
      if (fail_after_clocking) {
        std::array<uint8_t, 4> sink{};
        chip.Transfer(tx, sink.data(), len);
      }
      return false;
    }
    if (index == lose_transfer) {
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = 0xFF;
      }
    } else {
      chip.Transfer(tx, rx, len);
    }
    if (index == replace_transfer) {
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = replace_bytes[i];
      }
    }
    return true;
  }

  bool DoPin(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    const int index = pin_calls++;
    if (index == fail_pin) {
      return false;
    }
    const bool active = signal == max22200::GpioSignal::ACTIVE;
    if (pin == max22200::CtrlPin::Enable) {
      chip.SetEnable(active);
    } else if (pin == max22200::CtrlPin::Cmd) {
      chip.SetCmd(active);
    }
    return true;
  }

  /// How many transfers from `from` on were sent with CMD low (data phases).
  [[nodiscard]] int CountData(int from = 0) const noexcept {
    int n = 0;
    for (int i = from; i < transfers && static_cast<std::size_t>(i) < first_byte.size(); ++i) {
      n += with_cmd[static_cast<std::size_t>(i)] ? 0 : 1;
    }
    return n;
  }

  /// How many transfers from `from` on were the command byte `command` (sent with CMD high).
  [[nodiscard]] int CountCommand(uint8_t command, int from = 0) const noexcept {
    int n = 0;
    for (int i = from; i < transfers && static_cast<std::size_t>(i) < first_byte.size(); ++i) {
      const auto k = static_cast<std::size_t>(i);
      n += with_cmd[k] && first_byte[k] == command ? 1 : 0;
    }
    return n;
  }
};

/// The required SPI hooks over a MockBusState. Derive `class P : public MockPortBase<P>`.
template <typename Self>
class MockPortBase : public max22200::SpiInterface<Self> {
public:
  MockBusState state;

  bool EnsureInitialized() noexcept {
    state.bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return state.DoTransfer(tx, rx, len);
  }
  bool GpioSet(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    return state.DoPin(pin, signal);
  }
};

/// Required hooks plus the delay; time passes in the model as the driver waits.
class MinimalPort : public MockPortBase<MinimalPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
};

/// Every optional hook, with counters.
class FullPort : public MockPortBase<FullPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.
  int log_lines = 0;       ///< Log() calls.
  int error_lines = 0;     ///< Log() calls at LogLevel::Error.
  bool fault_read_ok = true;

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  bool GpioRead(max22200::CtrlPin pin, max22200::GpioSignal& signal) noexcept {
    if (pin != max22200::CtrlPin::Fault || !fault_read_ok) {
      return false;
    }
    signal = state.chip.FaultPin() ? max22200::GpioSignal::ACTIVE : max22200::GpioSignal::INACTIVE;
    return true;
  }
  void Log(max22200::LogLevel level, const char* message) noexcept {
    (void)message;
    log_lines++;
    if (level == max22200::LogLevel::Error) {
      error_lines++;
    }
  }
};

} // namespace max22200_test

#endif // MAX22200_MOCK_MAX22200_BUS_HPP
