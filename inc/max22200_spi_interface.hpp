// clang-format off: the hook table below must keep one row per line.
/**
 * @file max22200_spi_interface.hpp
 * @brief The SPI port of the MAX22200 driver: the CRTP base, its pin enums, the `SpiPort` concept
 * and the two-phase register access.
 * @copyright Copyright (c) 2026 The driver-max22200 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `max22200::SpiInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus and the CMD and ENABLE outputs up on the first call (CMD high, its idle level); `true` at once afterwards |
 * | `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | One full-duplex transfer of `len` = 1 or 4 bytes, both buffers given, CSB low throughout and high after |
 * | `bool GpioSet(CtrlPin pin, GpioSignal signal) noexcept` | Drive CMD or ENABLE; `false` when the pin could not be driven |
 * | `void DelayUs(uint32_t us) noexcept` | Wait at least `us` microseconds (tEN and tWU at start-up) |
 *
 * What the chip needs from the bus is below as `constexpr` values (`kSpiMode` to
 * `kCmdSetupNs`, from "SPI Detailed Description" and the SPI rows of the Electrical
 * Characteristics, Figure 1); a port checks its settings against them with `static_assert`
 * (handbook KB-CPP-123). SDO is open drain: the board needs a pull-up, and its RC sets how fast the
 * bus can run (Electrical Characteristics, note 2). CMD idles high: the driver drives it low only
 * around each data transfer and high again after it, so a GPIO write that returns after the pin
 * changed meets tCMS and tCMH on any MCU.
 *
 * Control pins and their idle levels (handbook KB-HW-018): the port drives each output to its
 * idle level before it enables the pin's output driver and before the first transfer. CMD idles
 * high (`GpioSignal::ACTIVE`: the level that selects the Command Register; the driver also drives
 * it high first). ENABLE idles low (`GpioSignal::INACTIVE`: the chip asleep, every output Hi-Z,
 * until `EnsureInitialized()` raises it). A board that ties ENABLE high returns `true` for
 * `CtrlPin::Enable` without doing anything. FAULT is an input. The port moves raw bytes: it never
 * retries, never interprets the data and never configures the chip. The optional hooks (`GpioRead`
 * for the FAULT pin, `DelayUs` is required, `Log`) are described in `max22200.hpp`.
 */
// clang-format on
#ifndef MAX22200_SPI_INTERFACE_HPP
#define MAX22200_SPI_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "max22200_detail.hpp"
#include "max22200_registers.hpp"
#include "max22200_types.hpp"

namespace max22200 {

/**
 * @name Bus requirements
 * What the MAX22200 needs from the SPI bus ("SPI Detailed Description", p23; Electrical
 * Characteristics, SPI rows, p10; Figure 1, p11). A port and a board configuration check their
 * settings against these with `static_assert` (handbook KB-CPP-123).
 * @{
 */
/// SPI mode 0: CPOL = 0, CPHA = 0 ("SPI Detailed Description": the only mode the chip supports).
inline constexpr uint8_t kSpiMode = 0;
/// Each byte is clocked most significant bit first (Figure 1). The byte order differs per frame
/// and is the driver's business: a 32-bit write goes LSB byte first, a 32-bit read comes MSB byte
/// first (Figures 10, 11).
inline constexpr bool kMsbFirst = true;
/// Highest SCK frequency: tCLK min 100 ns (Electrical Characteristics). "SPI Detailed Description"
/// also says the interface "can operate up to 5MHz" next to the daisy chain, which this driver does
/// not use; the SDO pull-up's RC can set a lower limit (note 2).
inline constexpr uint32_t kMaxClockHz = 10'000'000;
/// Shortest time CSB stays high between two transfers, tCSPW.
inline constexpr uint32_t kMinCsHighNs = 30;
/// CSB falling edge to the first SCK rising edge, tCSS.
inline constexpr uint32_t kCsSetupNs = 30;
/// Last SCK edge to CSB rising, tCSH.
inline constexpr uint32_t kCsHoldNs = 30;
/// `false`: the datasheet sets no limit on idle SCK time between the bytes of one transfer; the
/// chip counts the clocks while CSB is low (a wrong count of a write sets COMER, p34).
inline constexpr bool kContinuousWord = false;
/// CMD valid before the CSB falling edge that starts a transfer, tCMS. CMD must also stay high
/// through the CSB rising edge that ends a command byte (p26); the hold time tCMH is drawn in
/// Figure 1 with no value, so the driver changes CMD only after `Transfer()` has returned.
inline constexpr uint32_t kCmdSetupNs = 20;
/// The transfer lengths the driver asks for, in bytes: 1 (the Command Register, an 8-bit data
/// phase) and 4 (a 32-bit data phase). Nothing else.
inline constexpr std::array<std::size_t, 2> kTransferBytes = {1, 4};
/// SDO is open drain (Pin Description): the board fits a pull-up.
inline constexpr bool kSdoOpenDrain = true;
/** @} */

static_assert((kMaxClockHz * 100ULL) == 1'000'000'000ULL, "tCLK min 100 ns");

/** @brief The chip's control pins the port drives or reads. */
enum class CtrlPin : uint8_t {
  Enable = 0, ///< ENABLE, active high: ACTIVE wakes the chip, INACTIVE puts it to sleep (< 11 µA).
  Cmd = 1,    ///< CMD, active high: ACTIVE while the Command Register is written.
  Fault = 2,  ///< FAULT, open drain, active low on the pin: read only (`GpioRead`), ACTIVE while
              ///< an unmasked fault is present.
};

/** @brief Logical level of a control pin; the port maps it to the physical polarity. */
enum class GpioSignal : uint8_t {
  INACTIVE = 0, ///< The pin function is deasserted.
  ACTIVE = 1,   ///< The pin function is asserted.
};

/**
 * @brief CRTP base of the platform SPI port.
 *
 * Derive `class MyPort : public max22200::SpiInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself (a forwarding hook would turn a missing
 * override into infinite recursion) and no data; the driver checks your class with the `SpiPort`
 * concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class SpiInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface(const SpiInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface& operator=(const SpiInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  SpiInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~SpiInterface() noexcept = default;
};

namespace detail {

/// The port drives the control pins: exactly `bool GpioSet(CtrlPin, GpioSignal) noexcept`.
/// Required: CMD selects the Command Register and ENABLE wakes the chip.
template <typename Port>
concept HasGpioSet =
    requires { ExactHook<Port, bool(CtrlPin, GpioSignal) noexcept>::Take(&Port::GpioSet); };

} // namespace detail

/**
 * @brief Satisfied by a class that derives from `SpiInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` and returning `bool`: `EnsureInitialized()`,
 * `Transfer()` and `GpioSet()` (CMD and ENABLE).
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member function of exactly
 * the signature and converts no value: a hook declared with a narrower length (`uint16_t len`),
 * another pin type or without `noexcept` does not satisfy the concept, instead of truncating or
 * misrouting silently (KB-CPP20-006).
 * `DelayUs()` is checked by the driver with a message that names it.
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept SpiPort =
    std::derived_from<Port, SpiInterface<Port>> && detail::HasGpioSet<Port> && requires {
      detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
      detail::ExactHook<Port, bool(const uint8_t*, uint8_t*, std::size_t) noexcept>::Take(
          &Port::Transfer);
    };

namespace detail {

/// The four bytes of a 32-bit write, LSB byte first as SDI carries them (Figure 10). Free and
/// `constexpr`, so it exists once whatever the port type (KB-CPP20-009).
constexpr std::array<uint8_t, 4> LsbFirst(uint32_t v) noexcept {
  return {static_cast<uint8_t>(v & 0xFFU), static_cast<uint8_t>((v >> 8U) & 0xFFU),
          static_cast<uint8_t>((v >> 16U) & 0xFFU), static_cast<uint8_t>(v >> 24U)};
}
static_assert((LsbFirst(0x1122'3344_u32)[0] == 0x44U) && (LsbFirst(0x1122'3344_u32)[3] == 0x11U),
              "Figure 10: the least significant byte goes first");

/// What one access returned besides the data.
struct Reply {
  uint8_t status{0xFF}; ///< STATUS[7:0], clocked out while the command was written (Figure 7),
                        ///< or during an 8-bit write's data byte (Figure 12): the newest one.
  uint8_t echo{0xFF};   ///< The second SDO byte of a 32-bit write: the command register (Fig. 10).
  uint8_t flags{0};     ///< Every STATUS[7:0] byte of the access other than 0xFF, ORed: a flag
                        ///< the chip clears by itself between two bytes is still in here.
};

/// A fault byte that is data (not 0xFF, what a floating SDO reads) with ACTIVE = 0. On a running
/// chip ACTIVE is only ever written 1, so this byte means a power-on reset cleared every register
/// ("Undervoltage Lockout (UVLO)", p22; ACTIVE resets to 0, p26-27; every command byte clocks it
/// out, Figure 7).
constexpr bool ResetByte(uint8_t status) noexcept {
  return (status != 0xFFU) && ((static_cast<uint32_t>(status) & reg::kActive) == 0U);
}
static_assert(ResetByte(0x02U) && !ResetByte(0xFFU) && !ResetByte(0x01U),
              "p36: STATUS[7:0] reads 0x02 (UVM, ACTIVE = 0) after a power-on reset");

/// A fault byte that is data (not 0xFF) showing COMER (p34): a write since the last clean byte was
/// miscounted. A configuration write whose preconditions (every channel off for a mode change,
/// a pair's mode) the driver checked before this byte stops here: they may no longer hold.
constexpr bool StopOnComer(uint8_t status) noexcept {
  return (status != 0xFFU) && ((static_cast<uint32_t>(status) & reg::kComer) != 0U);
}

/**
 * @brief The two-phase access of "Command Register Description" and "Reading and Writing
 * Operations of Registers": the command with CMD high, then the data with CMD low.
 *
 * CMD idles high: it is high before every command byte and is driven high again after every data
 * phase, the one-transfer ONCH write included. The Command Register is written with CMD high for
 * the whole transfer and at the CSB rising edge (p25-26, Figure 1, tCMS before CSB falls), and
 * every other register with CMD low; keeping CMD high between accesses means a command byte never
 * depends on a CMD edge timed against CSB, and only a data phase lowers it, around its own
 * transfer.
 */
template <SpiPort Port>
class RegisterBus {
public:
  explicit RegisterBus(Port& port) noexcept : port_(port) {}

  Port& GetPort() & noexcept {
    return port_;
  }

  /// Read 32 bits of `bank`, MSB byte first (Figure 11). `sdi` is what SDI carries, LSB byte first
  /// like a write (Figures 8 and 10): 0 for every register but FAULT, where a 1 clears that flag on
  /// the MAX22200A. With `guard`, a command byte answered by a `ResetByte()` ends the access with
  /// `Error::DeviceReset` before the data phase; so do the other accesses below.
  Error Read32(uint8_t bank, uint32_t sdi, uint32_t& value, Reply& reply, bool guard) noexcept {
    value = 0xFFFFFFFFU;
    if (const Error e = command(reg::Command(bank, false, false), reply, guard); e != Error::Ok) {
      return e;
    }
    const std::array<uint8_t, 4> tx = LsbFirst(sdi);
    std::array<uint8_t, 4> rx = {0xFF, 0xFF, 0xFF, 0xFF};
    if (const Error e = data<4>(tx, rx); e != Error::Ok) {
      return e;
    }
    value = (static_cast<uint32_t>(rx[0]) << 24U) | (static_cast<uint32_t>(rx[1]) << 16U) |
            (static_cast<uint32_t>(rx[2]) << 8U) | static_cast<uint32_t>(rx[3]);
    return Error::Ok;
  }

  /// Write 32 bits to `bank`, LSB byte first; SDO returns STATUS[7:0] and the command (Figure 10).
  /// With `guard`, no data goes to a chip that answered the command as a reset one; with
  /// `stop_on_comer`, none to one whose fault byte shows COMER (see `StopOnComer()`).
  Error Write32(uint8_t bank, uint32_t value, Reply& reply, bool guard,
                bool stop_on_comer) noexcept {
    if (const Error e = command(reg::Command(bank, true, false), reply, guard); e != Error::Ok) {
      return e;
    }
    if (stop_on_comer && StopOnComer(reply.status)) {
      return Error::CommunicationError;
    }
    const std::array<uint8_t, 4> tx = LsbFirst(value);
    std::array<uint8_t, 4> rx = {0xFF, 0xFF, 0xFF, 0xFF};
    if (const Error e = data<4>(tx, rx); e != Error::Ok) {
      return e;
    }
    reply.echo = rx[1];
    return Error::Ok;
  }

  /// Read bits 31:24 of `bank` (Figure 13).
  Error Read8(uint8_t bank, uint8_t& value, Reply& reply, bool guard) noexcept {
    value = 0xFF;
    if (const Error e = command(reg::Command(bank, false, true), reply, guard); e != Error::Ok) {
      return e;
    }
    const std::array<uint8_t, 1> tx = {0x00};
    std::array<uint8_t, 1> rx = {0xFF};
    const Error e = data<1>(tx, rx);
    value = rx[0];
    return e;
  }

  /// Write bits 31:24 of `bank` (Figure 12). SDO returns STATUS[7:0] during the data byte, a newer
  /// reading than the command's: `reply.status` holds it once the data byte went out.
  Error Write8(uint8_t bank, uint8_t value, Reply& reply, bool guard, bool stop_on_comer) noexcept {
    if (const Error e = command(reg::Command(bank, true, true), reply, guard); e != Error::Ok) {
      return e;
    }
    if (stop_on_comer && StopOnComer(reply.status)) {
      return Error::CommunicationError;
    }
    return Data8(value, reply);
  }

  /// One 8-bit data phase under the command already in force ("Once the user has written the
  /// Command Register, all the following SPI transfers remain of the same type until the Command
  /// Register is rewritten", p25). `reply.status` gets the STATUS[7:0] byte of SDO. The byte comes
  /// with the data: the caller checks it for a reset afterwards.
  Error Data8(uint8_t value, Reply& reply) noexcept {
    const std::array<uint8_t, 1> tx = {value};
    std::array<uint8_t, 1> rx = {0xFF};
    if (const Error e = data<1>(tx, rx); e != Error::Ok) {
      return e;
    }
    take(rx[0], reply);
    return Error::Ok;
  }

  /// Drive a control pin; `Error::Pin` when the port could not.
  Error SetPin(CtrlPin pin, GpioSignal signal) noexcept {
    return port_.GpioSet(pin, signal) ? Error::Ok : Error::Pin;
  }

private:
  /// The Command Register: CMD high (its idle level) for the whole transfer and at the CSB edges.
  /// A CMD that cannot be driven high sends nothing: the byte would be taken as data. With
  /// `guard`, a `ResetByte()` answer stops the access before its data phase: the reset chip has
  /// forgotten its configuration, and a write could set ACTIVE over reset CFG_CHn values.
  Error command(uint8_t cmd, Reply& reply, bool guard) noexcept {
    reply = Reply{};
    if (!port_.GpioSet(CtrlPin::Cmd, GpioSignal::ACTIVE)) {
      return Error::Pin;
    }
    const std::array<uint8_t, 1> tx = {cmd};
    std::array<uint8_t, 1> rx = {0xFF};
    if (!port_.Transfer(tx.data(), rx.data(), rx.size())) {
      return Error::Bus;
    }
    take(rx[0], reply);
    return (guard && ResetByte(rx[0])) ? Error::DeviceReset : Error::Ok;
  }

  /// Keep a STATUS[7:0] byte: the newest in `status`, and in `flags` unless it is 0xFF.
  static void take(uint8_t status, Reply& reply) noexcept {
    reply.status = status;
    if (status != 0xFFU) {
      reply.flags =
          static_cast<uint8_t>(static_cast<uint32_t>(reply.flags) | static_cast<uint32_t>(status));
    }
  }

  /// A data phase of `N` bytes (1 or 4, checked at compile time; spans of fixed extent, so the
  /// length handed to the port is the buffers' own, KB-CPP20-042): CMD low, the transfer, CMD high
  /// again whatever happened. A CMD that cannot go low sends nothing: the data would be taken as a
  /// command.
  template <std::size_t N>
    requires((N == kTransferBytes[0]) || (N == kTransferBytes[1]))
  Error data(std::span<const uint8_t, N> tx, std::span<uint8_t, N> rx) noexcept {
    if (!port_.GpioSet(CtrlPin::Cmd, GpioSignal::INACTIVE)) {
      (void)port_.GpioSet(CtrlPin::Cmd, GpioSignal::ACTIVE);
      return Error::Pin;
    }
    const bool sent = port_.Transfer(tx.data(), rx.data(), rx.size());
    const bool high = port_.GpioSet(CtrlPin::Cmd, GpioSignal::ACTIVE);
    if (!sent) {
      return Error::Bus;
    }
    return high ? Error::Ok : Error::Pin;
  }

  Port& port_;
};

} // namespace detail

} // namespace max22200

#endif // MAX22200_SPI_INTERFACE_HPP
