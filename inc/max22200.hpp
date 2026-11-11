/**
 * @file max22200.hpp
 * @brief MAX22200 driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-max22200 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `max22200::MAX22200<Port>` drives one MAX22200 or MAX22200A octal solenoid and motor driver
 * through a platform port you write (see `max22200_spi_interface.hpp`). The driver has no platform
 * code, allocates nothing, throws nothing, takes no lock and is not thread-safe: give each object
 * one owner, or guard it with one mutex. Section, table and figure names refer to the datasheet,
 * Analog Devices 19-100531 Rev 1 (3/25).
 */
#ifndef MAX22200_HPP
#define MAX22200_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "max22200_detail.hpp"
#include "max22200_registers.hpp"
#include "max22200_spi_interface.hpp"
#include "max22200_types.hpp"
#include "max22200_version.h"

namespace max22200 {

namespace detail {

// Optional hooks: a `Declares*` concept tests that the port has a member of that name (a single
// member by its address, an overload set by a call with the documented arguments), the `Has*`
// concept the exact signature, `noexcept` and the result type; the driver rejects a hook that is
// declared but does not match (KB-CPP20-007).

/// The port declares something called `GpioRead`.
template <typename Port>
concept DeclaresGpioRead = requires {
  &Port::GpioRead;
} || requires(Port& port, GpioSignal& signal) { port.GpioRead(CtrlPin::Fault, signal); };

/// The port reads the FAULT pin: `bool GpioRead(CtrlPin, GpioSignal&) noexcept`. The signal is a
/// non-`const` reference: a hook that takes it by value or as `const&` cannot report it, so it does
/// not match.
template <typename Port>
concept HasGpioRead =
    requires { ExactHook<Port, bool(CtrlPin, GpioSignal&) noexcept>::Take(&Port::GpioRead); };

/// The port declares something called `DelayUs`.
template <typename Port>
concept DeclaresDelayUs =
    requires { &Port::DelayUs; } || requires(Port& port) { port.DelayUs(1U); };

/// The port waits: `void DelayUs(uint32_t us) noexcept`. A narrower parameter would truncate the
/// wake timeout and tDIS.
template <typename Port>
concept HasDelayUs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayUs); };

/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog =
    requires { &Port::Log; } || requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// Wait through the port. A port without `DelayUs()` fails to compile at the first call that needs
/// a wait: tEN and tWU are requirements, never skipped.
template <typename Port>
void DelayUs(Port& port, uint32_t us) noexcept {
  if constexpr (HasDelayUs<Port>) {
    port.DelayUs(us);
  } else {
    static_assert(kAlwaysFalse<Port>,
                  "max22200: the port must implement `void DelayUs(uint32_t us) noexcept`");
  }
}

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    (void)port;
  }
}

/// The two ONCH bits of a pair.
constexpr uint8_t PairBits(uint8_t pair) noexcept {
  return static_cast<uint8_t>(0x03U << (2U * (static_cast<uint32_t>(pair) & 0x03U)));
}

/// The ONCH bit of a channel.
constexpr uint8_t ChannelBit(uint8_t channel) noexcept {
  return static_cast<uint8_t>(uint32_t{1} << (static_cast<uint32_t>(channel) & 0x07U));
}

static_assert((PairBits(3_u8) == 0xC0U) && (ChannelBit(7_u8) == 0x80U),
              "Table 10: ONCH7 is bit 31");

} // namespace detail

/**
 * @defgroup max22200_driver MAX22200 driver
 * @brief The driver class.
 * @{
 */

// clang-format off: the tables below must keep one row per line.
/**
 * @brief Driver for one MAX22200 or MAX22200A on a platform port.
 *
 * Lifecycle: construct with the configuration (no I/O), then `EnsureInitialized()`, the one way
 * to start (ENABLE, tEN, the programming procedure of Figure 6, tWU, read-back of every register),
 * use, optionally `Deinitialize()`. Every call that touches the chip returns `Error::NotInitialized`
 * until `EnsureInitialized()` has succeeded; no call starts the chip as a side effect.
 *
 * **The driver keeps a copy of what it wrote** (STATUS, the eight CFG_CHn, CFG_DPM), so switching
 * channels is one 8-bit write and never a read-modify-write: `SetChannel()` costs what
 * `SetChannelsOn()` costs. An access is two transfers: the command byte with CMD high, then the
 * data with CMD low. The command stays in force until it is rewritten (p25), so an ONCH write right
 * after another ONCH write sends only its data byte: the hot path is one transfer.
 *
 * | Call | Transfers | With `Verify::ReadBack` |
 * | --- | --- | --- |
 * | `SetChannelsOn()`, `SetChannel()`, `SetBridge()` | 1 after an ONCH write with nothing in between, else 2 (8-bit write of ONCH) | 3 or 4 |
 * | `SetHoldCode()` | 2 (8-bit write of HFS and HOLD) | 4 |
 * | `ConfigureChannel()`, `SetPairMode()`, `SetFaultMask()`, `SetDpm()` | 2 (32-bit write) | 4 |
 * | `ReadStatus()`, `ReadFaults()`, `ClearFaults()`, `ReadChannelConfig()` | 2 | - |
 * | `ReadChannelsOn()` | 2 (8-bit read of ONCH) | - |
 * | `EnsureInitialized()` | 48, plus 4 per extra wake poll, 2 per ENABLE cycle that clears a COMER the wake left; repeated on a new COMER | - |
 * | `ApplyConfig()` | 2 per register that changes | - |
 * | `Deinitialize()` | 2 | - |
 * | `ReadRegister()` | 2 (one 32-bit read, no retry) | - |
 * | `ReadFaultPin()`, `GetChannelsOn()`, `GetLastStatusByte()`, `GetLatchedFaults()`, `GetStartupFaults()`, `IsMiscountDetectionDegraded()` | 0 | - |
 *
 * Optional port hooks, found at compile time:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `bool GpioRead(CtrlPin, GpioSignal&) noexcept` | `ReadFaultPin()` (the FAULT pin) | `ReadFaultPin()` returns `Unsupported` |
 * | `void Log(LogLevel, const char* message) noexcept` | One line per lifecycle event or failure | Nothing is logged |
 *
 * A hook declared with the wrong signature (for example `void GpioRead(...)`, or `DelayUs(uint16_t)`)
 * is a compile error. `DelayUs(uint32_t) noexcept` is required.
 *
 * **What every call of a running driver checks, at no extra I/O.** Every command byte clocks out
 * the fault byte STATUS[7:0] (Figure 7), and so does an 8-bit write's data byte (Figure 12):
 *
 * - **A power-on reset.** ACTIVE is only ever written 1 while running, so a fault byte with
 *   ACTIVE = 0 (other than 0xFF, what a floating SDO reads) means a UVLO reset every register
 *   ("Undervoltage Lockout (UVLO)", p22). The access stops before its data phase, so nothing
 *   from the driver's copy reaches the reset chip; the driver marks itself uninitialised, every
 *   channel off, and the call returns `DeviceReset`. Call `EnsureInitialized()` again. A STATUS
 *   read whose value shows ACTIVE = 0 does the same. An SDO stuck low reads the same way: the
 *   driver stops, which is the safe side.
 * - **A miscounted write.** COMER going from 0 to 1 (p34) means a write since the last fault byte
 *   with COMER clear was ignored. The call that sees it returns `CommunicationError` (once); the
 *   ONCH bits those writes changed count as on for the `ChannelsActive` check, the channels they
 *   configured as of either mode, and `ApplyConfig()` writes their registers again. COMER is read
 *   only and may stay set (p36), which would hide every later miscount: the start-up clears a
 *   COMER the wake left with ENABLE low, and while COMER is set on a running driver every
 *   switch-off is read back (2 more transfers each, only in that state), so a switch-off is
 *   never reported `Ok` without proof that it landed. A configuration write whose command byte
 *   shows a new COMER stops before its data phase (`CommunicationError`): what it checked first,
 *   every channel off for a mode change say, may no longer hold. An ONCH write is never stopped.
 * - **An off request is never refused.** In a pair whose mode is in doubt, switching a channel
 *   off (`SetChannel(ch, false)`, `SetBridge(pair, BridgeState::HiZ)`, or a `SetChannelsOn()` mask
 *   that clears any bit of the pair) switches both channels of the pair off: ONCHx = ONCHy = 0
 *   is off in every mode (Tables 6 and 7), and one bit alone could turn a full-bridge Brake into
 *   Forward or Reverse. The partner channel goes off too. A switch-on in such a pair returns
 *   `Uncertain`; in a `SetChannelsOn()` mask that pair is switched off instead, and the rest of
 *   the mask, every switch-off in it, still goes out. `SetChannel(ch, false)` in a known full-bridge
 *   pair sets HiZ, the partner off too. `IsMiscountDetectionDegraded()` says when COMER cannot
 *   show a new miscount.
 * - **All-ones read data.** It carries no fault byte, and a chip reset between a read's command
 *   byte and its data forgets the command and leaves SDO floating: the driver then sends one more
 *   command byte (an 8-bit ONCH read, 2 transfers) to tell a reset from a value.
 * - **Flags the chip clears by itself.** OVT, UVM, OCP, OLF, HHF and DPM in every fault byte go
 *   into `GetLatchedFaults()`, so an OLF a switch-on clears (p21) or an HHF a new cycle clears
 *   (p23) is kept even though no FAULT read saw it.
 *
 * @tparam Port Your class derived from `SpiInterface<Port>`.
 */
// clang-format on
template <SpiPort Port>
class MAX22200 {
  static_assert(!detail::DeclaresDelayUs<Port> || detail::HasDelayUs<Port>,
                "max22200: the DelayUs hook must be `void DelayUs(uint32_t us) noexcept`, so that "
                "no wait is truncated");
  static_assert(!detail::DeclaresGpioRead<Port> || detail::HasGpioRead<Port>,
                "max22200: the optional GpioRead hook must be `bool GpioRead(CtrlPin, GpioSignal&) "
                "noexcept`, so that a failed pin read is reported");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "max22200: the optional Log hook must be `void Log(LogLevel, const char* message) "
                "noexcept`");

public:
  /**
   * @brief Bind the driver to a port and store the configuration. No I/O, cannot fail.
   * @param port The port; it must outlive the driver.
   * @param config What `EnsureInitialized()` will write; checked there, before any I/O.
   */
  explicit MAX22200(Port& port, const Config& config = {}) noexcept : bus_(port), config_(config) {}

  /** @brief One driver object per chip. */
  MAX22200(const MAX22200&) = delete;
  /** @brief One driver object per chip. */
  MAX22200& operator=(const MAX22200&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Start the chip, once: the only way to initialise the driver.
   *
   * In order: `Validate()` the configuration (no I/O); bring the bus up; CMD high (its idle level),
   * ENABLE high and tEN; then the programming procedure of Figure 6: read STATUS (clears UVM); wake
   * the chip ("wake-up completed?"): a bare ACTIVE = 1 write, tWU, a STATUS read, repeated every
   * `kWakePollUs` until ACTIVE reads 1 with UVM clear, for at most `Config::wake_timeout_us`; then
   * write STATUS (masks, FREQM, pair modes, every channel off, ACTIVE = 1), CFG_CH0 to CFG_CH7 and
   * CFG_DPM. Then the start-up checks: STATUS must hold what was written, and every 32-bit write
   * must have echoed its command (Figure 10). Next FAULT is read and every flag cleared: the chip
   * keeps its registers until a power-on reset ("Undervoltage Lockout (UVLO)", p22) and ENABLE low
   * is not one ("Enable Logic Input", p14), so
   * flags from before the start would hold the FAULT pin and look new (handbook KB-CPP-058); they
   * are reported by `GetStartupFaults()` and in `GetLatchedFaults()`. Last, every CFG_CHn and
   * CFG_DPM (bits 14:0) read back must equal what was written. COMER is read only and the datasheet
   * does not say what clears it; a rejected wake write can set it, so it is armed only after the
   * wake: if set then, ENABLE goes low for tDIS and high for tEN and STATUS is read again (2
   * transfers), `kStartAttempts` times at most, so that miscounts show in the session; if it still
   * reads set the attempt relies on the read-back, and the session reads every switch-off back.
   * A COMER in a later fault byte
   * repeats the whole procedure, after ENABLE low for tDIS and high again for tEN (both waited in
   * full after the pin write), `kStartAttempts` times at most. If every attempt fails, cycle VM. 48
   * transfers when the chip wakes at the first poll, 4 more per extra poll.
   *
   * The start-up's STATUS reads feed the latch of `GetLatchedFaults()` like any other, except for
   * UVM before the wake has completed (step 1 reads STATUS precisely to clear the power-on UVM, and
   * the wake polls until it stays clear). The start-up does not empty the latch: a flag latched
   * before it (the UVM of a `DeviceReset`, say) stays until `ClearLatchedFaults()`.
   *
   * At the wake timeout: `Undervoltage` with UVM still set, `Timeout` with UVM clear. The driver
   * stays uninitialised with every channel off and ENABLE left high, so a retry continues the
   * wake; retry later or report a supply fault, and cycle VM if it persists. On any other failure
   * ENABLE is driven low again.
   *
   * After it succeeds, later calls return `Ok` at once with no I/O; after it fails, the next call
   * starts again from the beginning.
   *
   * @return `Ok`, `InvalidArgument` (the configuration fails `Validate()`; no I/O), `Bus`, `Pin`,
   *         `NoDevice`, `DeviceIdMismatch`, `WriteMismatch`, `Undervoltage`, `Timeout` or
   *         `CommunicationError`.
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()` and before `Deinitialize()`. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Switch every channel off and the chip to sleep, and mark the driver uninitialised: one
   * STATUS write with every ONCH 0 and ACTIVE = 0 (channels Hi-Z), then ENABLE low. The outputs are
   * Hi-Z at the latest tDIS after ENABLE falls; the call does not wait for it.
   *
   * ENABLE is driven low even if the write failed, and the driver is uninitialised afterwards
   * whatever happened; the next `EnsureInitialized()` configures the chip again. On an
   * uninitialised driver it does nothing and returns `Ok`.
   *
   * @return `Ok`, `Bus` or `Pin` (the first failure).
   */
  Result<void> Deinitialize() noexcept;

  /**
   * @brief Replace the configuration.
   *
   * Before the start it only stores it, with no I/O. On a running driver it writes each register
   * that differs (STATUS, a CFG_CHn, CFG_DPM), in that order, with the channels' on/off state
   * kept. `variant` and `rref_ohm` are never sent.
   *
   * @param config Checked with `Validate()`.
   * A register a miscounted write may have missed (see the class) is written even when the copy
   * already holds the new value.
   *
   * @return `Ok`, `InvalidArgument` (nothing sent), `ChannelsActive` (it changes a drive mode or
   *         side while a channel is on, or a pair mode while one of the pair is on; nothing sent),
   *         `Bus`, `Pin`, `DeviceReset` or `CommunicationError`. After a failure `GetConfig()`
   * holds what was written before it.
   */
  Result<void> ApplyConfig(const Config& config) noexcept;

  /** @return The configuration last applied or stored. No I/O. */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (KB-CPP20-044). */
  void GetConfig() const&& = delete;

  /// @}
  /// @name Switching channels (the hot path)
  /// @{

  /**
   * @brief Switch every channel at once: one 8-bit write of STATUS[31:24] (ONCH). A channel with
   * `trigger_pin` follows its TRIG pin instead.
   *
   * 2 transfers (command, data), or 1 (the data byte alone, CMD low) when the last access was an
   * ONCH write without `Verify::ReadBack` and nothing has happened since: no other register access,
   * no failed transfer or pin write, no ENABLE change, no COMER in a fault byte. The data byte's
   * SDO carries STATUS[7:0] (Figure 12), kept in `GetLastStatusByte()` at no cost. When that byte
   * is 0xFF (no answer: a chip that lost the command in a reset clocks nothing out) the write goes
   * out again with its command, 2 more transfers, so the command byte shows whether the chip was
   * reset.
   *
   * With `Verify::ReadBack`, an 8-bit read of ONCH follows (2 more transfers): it must equal
   * `mask`, and its fault byte must not show COMER. On `WriteMismatch` the driver's copy takes the
   * value read.
   *
   * In a pair whose mode is in doubt (see the class), a mask that changes the pair's bits switches
   * both of its channels off; if it set a bit there, the call returns `Uncertain` after sending
   * the rest of the mask, so every switch-off in it goes out. Nothing is sent when nothing changes
   * and no channel that may still be on is asked off: one a failed write may have switched on, or
   * one whose switch-off no fault byte with COMER clear has vouched for yet.
   *
   * @param mask Bit n switches channel n on. Both bits of a parallel pair must be equal (Table 6).
   * @param verify `Verify::None` (the default) or `Verify::ReadBack`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `Bus`, `Pin`,
   *         `DeviceReset`, `CommunicationError` (a new COMER, see the class), and with
   *         `Verify::ReadBack` `WriteMismatch`. The copy the driver keeps
   *         (`GetChannelsOn()`) changes only when the write went out (on a read-back mismatch it
   *         becomes what was read); after `Bus` the channels it would have changed count as on for
   *         the `ChannelsActive` check until a write succeeds.
   */
  Result<void> SetChannelsOn(uint8_t mask, Verify verify = Verify::None) noexcept;

  /**
   * @brief Switch one channel, keeping the others as the driver last wrote them: the same single
   * write as `SetChannelsOn()`, with no read first. In a parallel pair both channels switch.
   *
   * In a pair whose mode is in doubt, `on = false` switches both channels of the pair off (the
   * partner too: the one state that is off in every mode), whatever mode the copy records, and
   * `on = true` returns `Uncertain`.
   *
   * In a full-bridge pair `on = false` sets HiZ (both bits 0, Table 7): the partner goes off too.
   *
   * @param channel 0 to 7; in a full-bridge pair only to switch off (use `SetBridge()`).
   * @param on true to switch it on.
   * @param verify As `SetChannelsOn()`.
   * @return As `SetChannelsOn()`.
   */
  Result<void> SetChannel(uint8_t channel, bool on, Verify verify = Verify::None) noexcept;

  /**
   * @brief Set a full-bridge pair's state (Table 7): the same single write as `SetChannelsOn()`.
   * @param pair 0 to 3, in `PairMode::FullBridge`.
   * @param state The state.
   * @param verify As `SetChannelsOn()`.
   * @return As `SetChannelsOn()`; `InvalidArgument` for a pair not in full-bridge mode. `HiZ` in a
   *         pair whose mode is in doubt is sent (both bits 0, off in every mode); any other
   *         state there returns `Uncertain`.
   */
  Result<void> SetBridge(uint8_t pair, BridgeState state, Verify verify = Verify::None) noexcept;

  /**
   * @return true while a running driver cannot see a miscounted write: COMER read set in the last
   * fault byte that is data (p36: it may stay set), so a new miscount would not change it. Every
   * switch-off is then read back (2 more transfers each); use `Verify::ReadBack` for the other
   * writes that must be known to land, or restart (`Deinitialize()`, `EnsureInitialized()`), which
   * tries to clear COMER with ENABLE low (it stays degraded on a chip that keeps COMER until a
   * power-on reset). No I/O.
   */
  [[nodiscard]] bool IsMiscountDetectionDegraded() const noexcept {
    return initialized_ && !comer_clear_;
  }

  /** @return ONCH as the driver last wrote it, bit n for channel n. No I/O. */
  [[nodiscard]] uint8_t GetChannelsOn() const noexcept {
    return on_mask_;
  }

  /**
   * @brief Read ONCH from the chip: one 8-bit read, 2 transfers. Does not change `GetChannelsOn()`;
   * a channel found on that the copy says is off counts as on for the `ChannelsActive` check.
   * @return The mask, or `NotInitialized`, `Bus`, `Pin`, `DeviceReset` or `CommunicationError`.
   */
  Result<uint8_t> ReadChannelsOn() noexcept;

  /// @}
  /// @name Channel and chip settings
  /// @{

  /**
   * @brief Write one channel's CFG_CHn: 32 bits, 2 transfers. Everything but the drive mode and
   * the side can change while the channel runs (Programming Procedure, step 5c).
   *
   * With `Verify::ReadBack`, a 32-bit read of the register follows (2 transfers): it must equal
   * what was written, the write must have echoed its command (Figure 10), and no fault byte may
   * show COMER.
   *
   * @param channel 0 to 7.
   * @param config Checked with `ValidateChannel()` and `ValidLoadDiagnostics()` against
   *        `Config::loaded_channels`.
   * @param verify `Verify::None` (the default) or `Verify::ReadBack`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `ChannelsActive` (the drive
   *         mode or side changes while a channel is on; nothing sent), `Bus`, `Pin`,
   *         `DeviceReset`, `CommunicationError`, and with `Verify::ReadBack` `WriteMismatch`.
   *         The driver cannot see the
   *         TRIG pins: with one high the chip ignores a mode change, which only `Verify::ReadBack`
   *         reports. On a read-back mismatch the driver's copy takes the value read. After a failed
   *         write or a mismatch the channel may hold either mode: any further write to it waits,
   *         with `ChannelsActive`, until every channel is off.
   */
  Result<void> ConfigureChannel(uint8_t channel, const ChannelConfig& config,
                                Verify verify = Verify::None) noexcept;

  /**
   * @brief Change a channel's HOLD code on the fly: one 8-bit write of CFG_CHn[31:24] (HFS and
   * HOLD), 2 transfers (Programming Procedure, step 5b). HFS is kept.
   *
   * With `Verify::ReadBack`, an 8-bit read of the same byte follows (2 transfers).
   *
   * @param channel 0 to 7.
   * @param code 0 to `kMaxCdrCode` in CDR, 0 to `kMaxVdrCode` in VDR; 0 to `kMaxVdrCode` while the
   *        channel's last `ConfigureChannel()` failed or read back different (either mode may
   * apply).
   * @param verify `Verify::None` (the default) or `Verify::ReadBack`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `Bus`, `Pin`,
   *         `DeviceReset`, `CommunicationError`, and with `Verify::ReadBack` `WriteMismatch` (the
   *         driver's copy of HFS and HOLD then takes the value read).
   */
  Result<void> SetHoldCode(uint8_t channel, uint8_t code, Verify verify = Verify::None) noexcept;

  /**
   * @brief Change one pair's mode: a 32-bit STATUS write, 2 transfers. The chip takes CMxy only
   * with both channels of the pair off.
   * @param pair 0 to 3.
   * @param mode The mode.
   * @param verify `Verify::ReadBack` adds a 32-bit STATUS read (2 transfers; it also clears OVT
   *        and UVM on the chip, as every STATUS read does: the driver keeps them in
   *        `GetLatchedFaults()`).
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `ChannelsActive` (nothing sent), `Bus`,
   *         `Pin`, `DeviceReset`, `WriteMismatch` or `CommunicationError` (the driver's copy of
   *         STATUS then takes
   *         the value read; a pair field that reads as the reserved value 3 keeps the copy's mode).
   */
  Result<void> SetPairMode(uint8_t pair, PairMode mode, Verify verify = Verify::None) noexcept;

  /**
   * @brief Change the fault masks: a 32-bit STATUS write, 2 transfers.
   * @param mask The masks.
   * @param verify As `SetPairMode()`.
   * @return `Ok`, `NotInitialized`, `Bus`, `Pin`, `DeviceReset`, `WriteMismatch` or
   *         `CommunicationError`.
   */
  Result<void> SetFaultMask(const FaultMask& mask, Verify verify = Verify::None) noexcept;

  /**
   * @brief Write the DPM settings, CFG_DPM: 32 bits, 2 transfers.
   * @param dpm The settings.
   * @param verify `Verify::ReadBack` adds a 32-bit read (2 transfers).
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `Bus`, `Pin`,
   *         `DeviceReset`, `WriteMismatch` or `CommunicationError` (the copy then takes the value
   *         read). Only
   *         bits 14:0 are compared: bits 31:15 are reserved, some read only with no reset value.
   */
  Result<void> SetDpm(const DpmConfig& dpm, Verify verify = Verify::None) noexcept;

  /**
   * @brief Read one channel's CFG_CHn from the chip: 2 transfers.
   * @param channel 0 to 7.
   * @return The configuration, or `NotInitialized`, `InvalidArgument`, `Bus`, `Pin`,
   *         `DeviceReset` or `CommunicationError`.
   */
  Result<ChannelConfig> ReadChannelConfig(uint8_t channel) noexcept;

  /// @}
  /// @name Diagnostics
  /// @{

  /**
   * @brief Read STATUS: 2 transfers. The read clears OVT and UVM on the chip (Register Details:
   * "Read Clears All") and releases FAULT for them; the returned `Status` has them as read, and the
   * driver ORs them into `GetLatchedFaults()`, as it does for its own STATUS reads.
   *
   * ACTIVE = 0 on a running driver, in the command's fault byte or in the register, means a
   * power-on reset cleared every register: the driver marks itself uninitialised and returns
   * `DeviceReset` (UVLO, "requires reconfiguration"), as every other call does.
   *
   * @return The status, or `NotInitialized`, `Bus`, `Pin`, `DeviceReset` or
   *         `CommunicationError`.
   */
  Result<Status> ReadStatus() noexcept;

  /**
   * @brief Read FAULT without clearing what can be kept: 2 transfers, SDI all 0.
   *
   * On the MAX22200A this clears nothing. On the MAX22200 every FAULT read clears every flag
   * ("Clearing the FAULT Register"): a fault that arrives during the read is lost. The flags the
   * read clears are also ORed into `GetLatchedFaults()`.
   *
   * @return The flags, or `NotInitialized`, `Bus`, `Pin`, `DeviceReset` or `CommunicationError`
   *         (the flags read are latched all the same). A failed read that clears flags sets
   *         `LatchedFaults::lost`.
   */
  Result<Faults> ReadFaults() noexcept;

  /**
   * @brief Read FAULT and clear the flags in `which`: 2 transfers. On the MAX22200A only those
   * flags clear (selective clear on read, Figure 8); the MAX22200 clears all, so it accepts only
   * `Faults::All()`. OLF also clears when the channel is switched on, HHF when it starts a new
   * cycle. An OCP channel stays off until it is switched off and on again.
   *
   * The flags it clears are also ORed into `GetLatchedFaults()`; that latch is emptied only by
   * `ClearLatchedFaults()`.
   *
   * @param which The flags to clear; `Faults::All()` (the default) for every one.
   * @return The flags as they were before the clear, or `NotInitialized`, `Unsupported` (a
   *         selective clear on the MAX22200; nothing sent), `Bus`, `Pin`, `DeviceReset` or
   *         `CommunicationError`.
   */
  Result<Faults> ClearFaults(const Faults& which = Faults::All()) noexcept;

  /**
   * @brief Read the FAULT pin through the optional `GpioRead()` hook. No SPI.
   * @return true while the pin is asserted (an unmasked fault), or `NotInitialized`, `Unsupported`
   *         (no hook) or `Pin` (the hook failed).
   */
  Result<bool> ReadFaultPin() noexcept;

  /**
   * @brief The fault byte, STATUS[7:0], the chip clocked out during the last command byte or the
   * last 8-bit write's data byte (Figure 12). No I/O, no cost: every access returns one, the
   * one-transfer ONCH write included. 0xFF before the first access.
   * @return The byte; `reg::kComer` in it means a write since the last check was ignored.
   */
  [[nodiscard]] uint8_t GetLastStatusByte() const noexcept {
    return last_status_;
  }

  /**
   * @brief The clear-on-read flags any read of the driver took from the chip since the last
   * `ClearLatchedFaults()`, the driver's own reads included (handbook KB-CPP-119). No I/O.
   *
   * OVT and UVM come from every 32-bit STATUS read: `ReadStatus()`, the read-back of
   * `SetPairMode()` and `SetFaultMask()`, `ReadRegister(reg::kStatus)` and the start-up's reads.
   * The FAULT flags come from every FAULT read, for the flags it clears: all of them on the
   * MAX22200 (`Variant::Max22200`), those in `which` for `ClearFaults()` on the MAX22200A, and
   * every flag the start-up clears. The summary flags (OVT, UVM, OCP, OLF, HHF, DPM) of every
   * fault byte any access clocks out, the one-transfer ONCH write included, at no cost. Only
   * `ClearLatchedFaults()` empties it, a restart included;
   * the start-up does not keep the power-on UVM it clears (see `EnsureInitialized()`). A reply a
   * floating SDO produces (all ones with a 0xFF fault byte) is never taken as flags: a FAULT or
   * STATUS read that clears flags and gets one, or fails, sets `lost` instead (what the chip held
   * is unknown; a genuine FAULT of 0xFFFFFFFF with a 0xFF fault byte reads the same way). An 8-bit
   * read (ONCH, HOLD) returns neither OVT nor UVM, and the datasheet does not say it clears them;
   * the driver assumes it does not.
   *
   * @return The latched flags.
   */
  [[nodiscard]] LatchedFaults GetLatchedFaults() const noexcept {
    return latched_;
  }

  /** @brief Empty the latch of `GetLatchedFaults()`. No I/O; nothing is sent to the chip. */
  void ClearLatchedFaults() noexcept {
    latched_ = LatchedFaults{};
  }

  /**
   * @brief The FAULT flags the last start-up found and cleared: faults latched before the start
   * (the chip may keep them across an ENABLE cycle), ORed over every start-up attempt. No I/O.
   * Empty before the first start and after a start that found none; kept until the next start.
   * @return The flags.
   */
  [[nodiscard]] Faults GetStartupFaults() const noexcept {
    return startup_faults_;
  }

  /**
   * @brief Read one register once: one 32-bit read, 2 transfers, no retry, no decoding and no check
   * (handbook KB-CPP-129). For bring-up and diagnostics.
   *
   * SDI carries 0. Side effects are the chip's own: reading STATUS clears OVT and UVM (the driver
   * latches them, see `GetLatchedFaults()`); reading FAULT clears every flag on the MAX22200 (the
   * driver latches them when `Config::variant` is `Variant::Max22200`) and none on the MAX22200A.
   * The fault byte the command clocked out is kept in `GetLastStatusByte()`. A fault byte or a
   * STATUS that shows ACTIVE = 0 is a reset, as for every call: `DeviceReset`.
   *
   * @param bank `reg::kStatus`, `reg::CfgChannel(n)`, `reg::kFault` or `reg::kCfgDpm`.
   * @return The register, MSB byte first as read, or `NotInitialized`, `InvalidArgument` (no such
   *         bank; nothing sent), `Bus`, `Pin`, `DeviceReset` or `CommunicationError`.
   */
  Result<uint32_t> ReadRegister(uint8_t bank) noexcept;

  /// @}

private:
  Result<void> initialize() noexcept;
  Result<void> attempt() noexcept;
  Result<void> restartEnable() noexcept;
  Result<uint32_t> wake() noexcept;
  Result<void> checkStatus() noexcept;
  Result<void> checkRegisters() noexcept;
  Result<void> writeAll() noexcept;
  Result<void> applyRunning(const Config& next) noexcept;
  Result<void> writeOnch(uint8_t mask, Verify verify) noexcept;
  /// The ONCH read-back after a write: adopt what was read, or keep `maybe_on` in doubt.
  Result<void> verifyOnch(uint8_t mask, uint8_t maybe_on) noexcept;
  Result<void> writeStatus(const Config& next, Verify verify) noexcept;
  /// After a STATUS write: move the doubt it settles into the unverified sets (see the .ipp).
  void settleStatus(uint32_t changed, uint32_t off, bool clock_changed, bool read_back) noexcept;
  Result<void> writeChannel(uint8_t channel, const ChannelConfig& config, Verify verify) noexcept;
  Result<detail::Reply> put32(uint8_t bank, uint32_t value) noexcept;
  Result<void> checkWord(uint8_t bank, uint32_t value, uint32_t mask, uint8_t echo,
                         uint32_t& read) noexcept;
  Result<void> check8(uint8_t bank, uint8_t want, uint8_t& read) noexcept;
  void adoptStatus(uint32_t value) noexcept;
  Result<uint32_t> read32(uint8_t bank, uint32_t sdi = 0) noexcept;
  /// Latch what a 32-bit STATUS read cleared; false (after `onReset()`) for ACTIVE = 0 running.
  bool takeStatus(uint32_t value) noexcept;
  Result<uint8_t> read8(uint8_t bank) noexcept;
  Result<void> clearStartupFaults() noexcept;
  void latchFaults(uint32_t fault, uint32_t cleared) noexcept;
  /// All ones with a 0xFF fault byte: what a floating SDO reads, never latched as flags.
  [[nodiscard]] bool floatingReply(uint32_t value) const noexcept {
    return (value == 0xFFFFFFFFU) && (last_status_ == 0xFFU);
  }
  [[nodiscard]] bool validOnMask(uint8_t mask) const noexcept;
  /// `rewrite`: channels written even where `next` equals the copy (bit n for channel n).
  [[nodiscard]] bool modeChangeBlocked(const Config& next, uint8_t rewrite) const noexcept;
  [[nodiscard]] static constexpr bool isVerify(Verify verify) noexcept {
    return detail::IsEnumerator(verify);
  }
  /// Take an access's fault bytes: keep the newest, latch their flags, and on a running driver
  /// act on a reset (returns `DeviceReset`) or a new COMER. Returns `e` otherwise.
  Error note(const detail::Reply& reply, Error e) noexcept;
  void latchStatusByte(uint8_t flags) noexcept;
  void onReset() noexcept;
  /// The channels that may be on: the copy, a bit a failed write may have switched on, and a
  /// switch-off no fault byte with COMER clear has vouched for yet (the chip may have ignored it).
  [[nodiscard]] uint8_t maybeOn() const noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(on_mask_) |
                                static_cast<uint32_t>(uncertain_) |
                                static_cast<uint32_t>(unverified_onch_));
  }
  /// A write went out on a running driver: in doubt until a fault byte shows COMER clear.
  void wrote(uint8_t bank, uint8_t onch_changed) noexcept;
  /// A write of `bank` failed after it may have reached the chip: rewrite it on `ApplyConfig()`.
  void failedWrite(uint8_t bank, Error e) noexcept {
    if (initialized_ && (e != Error::DeviceReset)) {
      stale_banks_ |= uint32_t{1} << (static_cast<uint32_t>(bank) & 0x0FU);
    }
  }
  /// Adjust `mask` for pairs whose mode is unknown (an off request clears both bits); true when it
  /// asks for a switch-on in one.
  [[nodiscard]] bool pairUnknown(uint8_t& mask) const noexcept;
  /// After all-ones data: true (and the driver reset) when one more command byte shows a reset.
  bool probeReset() noexcept;
  Result<void> clearComer() noexcept;
  /// A configuration write stops before its data at a new COMER (an ONCH write never does: a
  /// switch-off always goes out). Only while a COMER would be news: degraded, nothing would stop.
  [[nodiscard]] bool stopOnComer() const noexcept {
    return initialized_ && comer_clear_;
  }
  [[nodiscard]] bool stale(uint8_t bank) const noexcept {
    return ((stale_banks_ >> (static_cast<uint32_t>(bank) & 0x0FU)) & 1U) != 0U;
  }
  template <typename T>
  Result<T> report(Result<T> r) noexcept;
  template <typename T>
  Result<T> fail(Error error, const char* message) noexcept;

  detail::RegisterBus<Port> bus_;
  Config config_{};
  uint8_t on_mask_{0};
  uint8_t uncertain_{0};     ///< ONCH bits a failed or mismatched write leaves untrusted.
  uint8_t cfg_uncertain_{0}; ///< Channels whose last CFG_CH write failed or read back different.
  uint8_t last_status_{0xFF};
  LatchedFaults latched_{};      ///< Clear-on-read flags seen since ClearLatchedFaults().
  Faults startup_faults_{};      ///< FAULT flags the last start-up cleared.
  bool latch_uvm_{true};         ///< false while start-up waits out the power-on UVM.
  bool comer_seen_{false};       ///< COMER went from 0 to 1 during the current start-up attempt.
  bool comer_armed_{false};      ///< COMER read 0 at the start of the attempt, so a 1 is new.
  bool onch_ready_{false};       ///< The command in force is the 8-bit ONCH write, nothing since.
  bool echo_bad_{false};         ///< A start-up 32-bit write echoed another command.
  bool comer_clear_{false};      ///< Running: the last fault byte that is data showed COMER clear.
  bool comer_new_{false};        ///< Running: COMER went from 0 to 1; the next call reports it.
  uint8_t unverified_onch_{0};   ///< ONCH bits changed since the last fault byte with COMER clear.
  uint32_t unverified_banks_{0}; ///< Banks (bit n for bank n) written since that byte.
  uint32_t stale_banks_{0};      ///< Banks a miscounted or failed write may have missed or
                                 ///< changed: rewritten by `ApplyConfig()` even when the copy
                                 ///< already matches.
  uint8_t pair_uncertain_{0};    ///< Pairs (bit p) whose CMxy a failed STATUS write left unknown.
  bool clock_uncertain_{false};  ///< FREQM a failed STATUS write left unknown.
  uint8_t unverified_pairs_{0};  ///< Pairs whose CMxy changed since the last clean fault byte.
  bool unverified_clock_{false}; ///< FREQM changed since the last clean fault byte.
  bool initialized_{false};
};

/** @} */

} // namespace max22200

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/max22200.ipp" // IWYU pragma: export

#endif // MAX22200_HPP
