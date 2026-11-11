/**
 * @file max22200_types.hpp
 * @brief Public types of the MAX22200 driver: errors, `Result<T>`, the channel and chip settings,
 * the configuration, status and faults, and the unit conversions.
 * @copyright Copyright (c) 2026 The driver-max22200 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency, so handlers, tests and tools can
 * include it without the driver template. Section, table and figure names refer to the datasheet,
 * Analog Devices 19-100531 Rev 1 (3/25).
 */
#ifndef MAX22200_TYPES_HPP
#define MAX22200_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "max22200_detail.hpp"
#include "max22200_registers.hpp"
#include "max22200_version.h"

/** @brief The MAX22200 driver: everything it declares lives here; internals in `detail`. */
namespace max22200 {

/**
 * @defgroup max22200_types Driver types
 * @brief Errors, results, settings and conversions.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. The type is
 * `[[nodiscard]]`: a call that returns a bare `Error` and is used as a statement is a warning
 * (handbook KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0,              ///< The call succeeded.
  NotInitialized = 1,  ///< `EnsureInitialized()` has not succeeded, `Deinitialize()` ran since, or
                       ///< a call found the chip reset (`DeviceReset`).
  InvalidArgument = 2, ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,             ///< The port's `Transfer()` (or `EnsureInitialized()`) returned `false`.
  Timeout = 4,         ///< At start-up, ACTIVE did not read 1 within `Config::wake_timeout_us`
  ///< with UVM clear (Figure 6, "wake-up completed?"). If it persists, cycle VM: ENABLE
  ///< low is not a reset (p14, p22).
  DeviceIdMismatch = 5, ///< At start-up, STATUS read back differs from what was written, or the
                        ///< command echo of a 32-bit write is wrong: not a MAX22200 answering, or
                        ///< SDO or SDI broken.
  Crc = 6,              ///< Not returned by this driver (the chip sends no CRC).
  Busy = 7,             ///< Not returned by this driver (the chip is never busy to SPI).
  DeviceFault = 8,      ///< Not returned by this driver: faults are data (`ReadStatus()`,
                        ///< `ReadFaults()`), never a failed call.
  Unsupported = 9,      ///< The port lacks the optional hook (`ReadFaultPin()`), or a selective
                        ///< fault clear asked of a MAX22200 (only the MAX22200A has it).
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  NoDevice = 16,           ///< At start-up, STATUS read as 0xFFFFFFFF with a 0xFF fault byte:
                           ///< nothing drives SDO (no chip, no supply, ENABLE not reaching it).
  Pin = 17,                ///< The port's `GpioSet()` could not drive CMD or ENABLE. A CMD that
                           ///< cannot go high would turn the command into data: nothing was sent.
  CommunicationError = 18, ///< COMER: the chip counted the wrong number of clocks in a write and
                           ///< ignored it. At start-up after `kStartAttempts` tries (Figure 6),
                           ///< each after ENABLE low and high: if it persists, cycle VM. On a
                           ///< running driver: the first call that sees COMER go from 0 to 1
                           ///< (the write may have been an earlier call's), or a read-back that
                           ///< differs with COMER set.
  WriteMismatch = 19,      ///< A register read back differs from what was written: at start-up,
                           ///< or with `Verify::ReadBack`. The chip did not take the write (a
                           ///< frame lost on a shared bus, a channel still on, a TRIG pin high).
  Undervoltage = 20,       ///< At start-up, UVM was still set at `Config::wake_timeout_us` (the
  ///< chip needs UVM cleared to enable operations, p27). VM is low, rises slowly,
  ///< or UVM is stuck: the driver stays uninitialised, so retry later or
  ///< treat it as a warning; a VM power cycle clears a stuck UVM.
  ChannelsActive = 21, ///< A setting the chip takes only with the channels off (VDRnCDR, HSnLS:
                       ///< every channel; CMxy: both of the pair) asked while one is on.
                       ///< Nothing was sent.
  DeviceReset = 22,    ///< Any call of a running driver saw ACTIVE = 0 in a fault byte or in
                       ///< STATUS: a power-on reset cleared every register. Nothing more was
                       ///< sent to the chip; the driver is uninitialised; call
                       ///< `EnsureInitialized()` to configure the chip again.
  Uncertain = 23,      ///< The call depends on a setting a failed write left unknown (the reply
                       ///< failed, so the chip may hold the old or the new value): a pair mode
                       ///< for a switch-on in that pair (a switch-off is never refused).
                       ///< Nothing was sent. Write the setting again
                       ///< (`SetPairMode()`, `ApplyConfig()`) or read it back with
                       ///< `Verify::ReadBack`, or switch the pair off with `SetChannelsOn()`.
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Bus"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  // A switch over every enumerator, so -Wswitch-enum names a new one that has no string
  // (KB-CPP-053); `default` takes a value that is not an enumerator (MISRA C++:2023 Rule 9.4.2).
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::NoDevice:
    return "NoDevice";
  case Error::Pin:
    return "Pin";
  case Error::CommunicationError:
    return "CommunicationError";
  case Error::WriteMismatch:
    return "WriteMismatch";
  case Error::Undervoltage:
    return "Undervoltage";
  case Error::ChannelsActive:
    return "ChannelsActive";
  case Error::DeviceReset:
    return "DeviceReset";
  case Error::Uncertain:
    return "Uncertain";
  default:
    break;
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised.
  Debug,     ///< Detail for bring-up.
};

/**
 * @brief Extra verification for one call, chosen per call (handbook KB-ARCH-041).
 *
 * The datasheet needs none of it: `Verify::None`, the default, sends only the transfers the chip
 * needs. `Verify::ReadBack` reads the register back, compares it and checks the COMER flag of the
 * read's fault byte; each call that takes it says how many transfers it adds.
 */
enum class Verify : uint8_t {
  None = 0,     ///< No extra transfer and no compare. The default.
  ReadBack = 1, ///< Read the register back and compare it; check COMER.
};

/**
 * @brief Which part is fitted. The chip cannot tell, so it is configured: the two differ only in
 * how a FAULT read clears the flags ("Clearing the FAULT Register", Ordering Information).
 *
 * `Max22200`, the default, is correct on both parts: `ClearFaults()` clears every flag on either,
 * and a selective clear is refused; the two parts cannot be told apart over SPI. Choose
 * `Max22200A` only when
 * the bill of materials says the MAX22200A is fitted and a bench check shows a zero-SDI read
 * keeps a latched flag: a selective clear sent to a MAX22200 clears every flag.
 */
enum class Variant : uint8_t {
  Max22200A = 0, ///< Selective clear on read: only the flags whose SDI bit is high clear.
  Max22200 = 1,  ///< Every FAULT read clears every flag, whatever SDI carries. The default.
};

/** @brief The oscillator, STATUS.FREQM (global): the time base of every channel. */
enum class MasterClock : uint8_t {
  Khz100 = 0, ///< 100 kHz. The reset value.
  Khz80 = 1,  ///< 80 kHz.
};

/** @brief A channel's chopping frequency as a division of the oscillator, FREQ_CFG (Table 1). */
enum class ChopDivider : uint8_t {
  Div4 = 0, ///< fMAIN / 4: 25 kHz (20 kHz at 80 kHz). The reset value.
  Div3 = 1, ///< fMAIN / 3: 33.3 kHz (26.7 kHz).
  Div2 = 2, ///< fMAIN / 2: 50 kHz (40 kHz).
  Div1 = 3, ///< fMAIN: 100 kHz (80 kHz).
};

/** @brief How a channel regulates, VDRnCDR. */
enum class DriveMode : uint8_t {
  Cdr = 0, ///< Current drive regulation: HIT and HOLD are peak currents (Table 4). Low side only.
           ///< The reset value.
  Vdr = 1, ///< Voltage drive regulation: HIT and HOLD are PWM duty cycles in percent (Table 3).
};

/** @brief Where the load is tied, HSnLS. */
enum class Side : uint8_t {
  Low = 0,  ///< Load from OUTn to VM. The reset value.
  High = 1, ///< Load from OUTn to ground: VDR only, no SRC, HFS or DPM.
};

/** @brief What a channel pair does, CMxy in STATUS ("Supported Driver Configurations"). */
enum class PairMode : uint8_t {
  Independent = 0, ///< Two half-bridges. The reset value.
  Parallel = 1,    ///< OUTx and OUTy tied together; the lower channel's CFG_CH drives both.
  FullBridge = 2,  ///< An H-bridge: ONCHy and ONCHx choose the state (Table 7).
};

/** @brief The state of a pair in `PairMode::FullBridge` (Table 7). */
enum class BridgeState : uint8_t {
  HiZ = 0,     ///< ONCHy = 0, ONCHx = 0: both outputs Hi-Z.
  Forward = 1, ///< ONCHy = 0, ONCHx = 1: OUTx at VM, OUTy regulated with CFG_CHy.
  Reverse = 2, ///< ONCHy = 1, ONCHx = 0: OUTy at VM, OUTx regulated with CFG_CHx.
  Brake = 3,   ///< ONCHy = 1, ONCHx = 1: both outputs at ground.
};

namespace detail {
/// @cond INTERNAL
// The last enumerator of each contiguous enumeration, for `IsEnumerator()` (KB-CPP20-025, -026).
template <>
struct EnumLast<LogLevel> {
  static constexpr LogLevel kValue = LogLevel::Debug;
};
template <>
struct EnumLast<Verify> {
  static constexpr Verify kValue = Verify::ReadBack;
};
template <>
struct EnumLast<Variant> {
  static constexpr Variant kValue = Variant::Max22200;
};
template <>
struct EnumLast<MasterClock> {
  static constexpr MasterClock kValue = MasterClock::Khz80;
};
template <>
struct EnumLast<ChopDivider> {
  static constexpr ChopDivider kValue = ChopDivider::Div1;
};
template <>
struct EnumLast<DriveMode> {
  static constexpr DriveMode kValue = DriveMode::Vdr;
};
template <>
struct EnumLast<Side> {
  static constexpr Side kValue = Side::High;
};
template <>
struct EnumLast<PairMode> {
  static constexpr PairMode kValue = PairMode::FullBridge;
};
template <>
struct EnumLast<BridgeState> {
  static constexpr BridgeState kValue = BridgeState::Brake;
};
/// @endcond
} // namespace detail

// FREQ_CFG is two bits and every value is an enumerator, so decoding it needs no check; CMxy is two
// bits with only three enumerators, so its decode does (`DecodePairMode()`).
static_assert(detail::ToUnderlying(ChopDivider::Div1) == 3U, "Table 1: FREQ_CFG 0 to 3");
static_assert(detail::ToUnderlying(PairMode::FullBridge) == 2U, "Register Details: CMxy 0 to 2");

/// Channels of the chip.
inline constexpr uint8_t kChannels = 8;
/// Channel pairs (0-1, 2-3, 4-5, 6-7).
inline constexpr uint8_t kPairs = 4;
/// Highest CDR current code: 127 is "HS off, LS on", the coil at full VM with no regulation
/// (Table 4), which the driver never sends in CDR.
inline constexpr uint8_t kMaxCdrCode = 126;
/// Highest VDR duty code: 100 is 100 % (Table 3); 101 to 127 mean the same and are refused.
inline constexpr uint8_t kMaxVdrCode = 100;
/// HIT_T code for no HIT phase (Table 5).
inline constexpr uint8_t kHitTimeNone = 0;
/// Highest finite HIT_T code (Table 5).
inline constexpr uint8_t kMaxHitTimeCode = 254;
/// HIT_T code for a HIT phase that never ends, "continuous IHIT" (Table 5): not a time, so
/// `HitTimeUs()` refuses it.
inline constexpr uint8_t kHitTimeContinuous = 255;
/// Smallest RREF of the recommended range, in ohms ("Full-Scale Current Setting").
inline constexpr uint32_t kMinRrefOhm = 15000;
/// Largest RREF of the recommended range, in ohms.
inline constexpr uint32_t kMaxRrefOhm = 100000;
/// `Config::rref_ohm` when the board's RREF is not given (the default): the code-level API works,
/// the current conversions (`FullScaleUa()`, `CurrentCode()`, `CurrentUa()`, `DpmCurrentCode()`)
/// return `InvalidArgument` for it.
inline constexpr uint32_t kNoRref = 0;
/// KFS with HFS = 0, in A/A (Electrical Characteristics, KFS0): IFS = KFS x 1 V / RREF.
inline constexpr uint32_t kKfs = 15000;
/// Start-up attempts when the chip reports COMER (Figure 6: "repeat the procedure").
inline constexpr uint8_t kStartAttempts = 3;
/// `Config::wake_timeout_us` by default: 100 ms. tWU is 2.5 ms at most once ACTIVE is taken, but
/// UVM clears only once VM is above UVLO (p22), so the wake also waits for the supply ramp; the
/// default allows a rail that settles within tens of milliseconds. Raise it for a slow supply.
inline constexpr uint32_t kDefaultWakeTimeoutUs = 100000;
/// Shortest `Config::wake_timeout_us`: one tWU.
inline constexpr uint32_t kMinWakeTimeoutUs = reg::kWakeUpUs;
/// Longest `Config::wake_timeout_us`: 10 s.
inline constexpr uint32_t kMaxWakeTimeoutUs = 10000000;
/// Period of the wake loop (Figure 6, "wake-up completed?"): an ACTIVE write, tWU, a STATUS read,
/// then a pause to this period.
inline constexpr uint32_t kWakePollUs = 10000;

/**
 * @brief One channel's CFG_CHn register, as codes (Table 11). `ChannelConfig{}` is the reset
 * value: CDR, low side, every code 0 (no current), 25 kHz, every diagnostic off.
 *
 * The codes are the datasheet's, so nothing is rounded behind your back; the conversions below
 * (`CurrentCode()`, `HitTimeCode()`) turn milliamps and microseconds into them and refuse what the
 * chip cannot do.
 */
struct ChannelConfig {
  DriveMode drive{DriveMode::Cdr}; ///< VDRnCDR. Changes only with every channel off.
  Side side{Side::Low};            ///< HSnLS. Changes only with every channel off.
  /// HIT[6:0]. CDR: peak current code/128 x IFS, 1 to `kMaxCdrCode`, 0 = no current (Table 4).
  /// VDR: duty cycle in percent, 0 to `kMaxVdrCode`; the chip raises 1 to δMIN-1 to δMIN and lowers
  /// δMAX+1 to 99 to δMAX (Tables 2, 3).
  uint8_t hit{0};
  uint8_t hold{0};     ///< HOLD[6:0], as `hit`. Can change on the fly (`SetHoldCode()`).
  uint8_t hit_time{0}; ///< HIT_T[7:0]: 0 none, 1 to 254 (`HitTimeUs()`), 255 continuous (Table 5).
  ChopDivider chop{ChopDivider::Div4}; ///< FREQ_CFG.
  bool half_scale{false};              ///< HFS: IFS halved, RON doubled; low side only.
  bool trigger_pin{false};       ///< TRGnSPI: switched by TRIGA (even) or TRIGB (odd), not ONCH.
  bool slew_rate_control{false}; ///< SRC: low side, fCHOP below 50 kHz only.
  bool open_load_detect{false};  ///< OL_EN.
  bool plunger_detect{false};    ///< DPM_EN: low side only; settings in `DpmConfig`.
  bool hit_current_check{false}; ///< HHF_EN: CDR only.

  /**
   * @param lhs The first value.
   * @param rhs The value to compare it with.
   * @return true when every field of `lhs` equals `rhs`'s.
   */
  [[nodiscard]] friend constexpr bool operator==(const ChannelConfig& lhs,
                                                 const ChannelConfig& rhs) noexcept = default;
};

/**
 * @brief The fault masks, STATUS[23:17]: a masked fault does not pull FAULT low (its flag is still
 * set). `FaultMask{}` is the reset value: only COMER masked.
 */
struct FaultMask {
  bool overtemperature{false}; ///< M_OVT.
  bool overcurrent{false};     ///< M_OCP.
  bool open_load{false};       ///< M_OLF.
  bool hit_not_reached{false}; ///< M_HHF.
  bool plunger{false};         ///< M_DPM.
  bool communication{true};    ///< M_COMF; masked at reset.
  bool undervoltage{false};    ///< M_UVM.

  /**
   * @param lhs The first value.
   * @param rhs The value to compare it with.
   * @return true when every field of `lhs` equals `rhs`'s.
   */
  [[nodiscard]] friend constexpr bool operator==(const FaultMask& lhs,
                                                 const FaultMask& rhs) noexcept = default;
};

/**
 * @brief The plunger-movement detection settings, CFG_DPM (global, for every channel with
 * `ChannelConfig::plunger_detect`). `DpmConfig{}` is all zero.
 */
struct DpmConfig {
  uint8_t start{0};     ///< DPM_ISTART, 0 to 127: ISTART = code x IFS / 127 (`DpmCurrentCode()`).
  uint8_t debounce{0};  ///< DPM_TDEB, 0 to 15: chopping periods (`DebounceUs()`).
  uint8_t threshold{0}; ///< DPM_IPTH, 0 to 15: dip threshold = code x IFS / 127.

  /**
   * @param lhs The first value.
   * @param rhs The value to compare it with.
   * @return true when every field of `lhs` equals `rhs`'s.
   */
  [[nodiscard]] friend constexpr bool operator==(const DpmConfig& lhs,
                                                 const DpmConfig& rhs) noexcept = default;
};

/**
 * @brief What the driver is told about the chip and the board. `Config{}` holds the reset values
 * of STATUS and every CFG_CHn (Register Details, p36, p38-39), for a part declared as `Max22200`
 * (correct on both parts) and an RREF not given. CFG_DPM has no reset values in the datasheet
 * (p40-41): `dpm` is 0, the value the driver writes at start-up, not a known reset state.
 *
 * `EnsureInitialized()` writes all of it, in the order of Figure 6; `ApplyConfig()` writes what
 * changes. Verification is not configured here: each call that can read back takes a `Verify`.
 */
struct Config {
  Variant variant{Variant::Max22200}; ///< The fitted part; never sent. Check the BOM first.
  /// The resistor from IREF to ground, in ohms, a board fact: `kMinRrefOhm` to `kMaxRrefOhm`, or
  /// `kNoRref` (the default: not given). Sets IFS for the current conversions (`CurrentCode()`);
  /// never sent. Codes work without it; the conversions refuse `kNoRref` (`InvalidArgument`).
  uint32_t rref_ohm{kNoRref};
  /// How long `EnsureInitialized()` waits for ACTIVE to read 1 with UVM clear, in microseconds:
  /// `kMinWakeTimeoutUs` to `kMaxWakeTimeoutUs`. Long or soft supply rails need more.
  uint32_t wake_timeout_us{kDefaultWakeTimeoutUs};
  /// The channels with a load fitted, bit n for channel n, a board fact; never sent. Only these may
  /// enable a load-dependent diagnostic (open load, plunger movement, HIT current check): on an
  /// unloaded channel it would report a fault that is only the missing load, and pull the shared
  /// FAULT pin (handbook KB-CPP-131). 0 (the default): no channel declared, every diagnostic off.
  uint8_t loaded_channels{0};
  MasterClock clock{MasterClock::Khz100};          ///< FREQM.
  FaultMask fault_mask{};                          ///< M_OVT to M_UVM.
  std::array<PairMode, kPairs> pairs{};            ///< CM10, CM32, CM54, CM76; index = pair.
  std::array<ChannelConfig, kChannels> channels{}; ///< CFG_CH0 to CFG_CH7; index = channel.
  DpmConfig dpm{};                                 ///< CFG_DPM.
};

/** @brief STATUS as read (`ReadStatus()`): the channels that are on and the seven flags. */
struct Status {
  uint8_t on_mask{0};          ///< ONCH[7:0]: bit n = channel n on.
  bool overtemperature{false}; ///< OVT (cleared by this read).
  bool overcurrent{false};     ///< OCP: some channel has an OCP flag in FAULT.
  bool open_load{false};       ///< OLF: some channel has an OLF flag in FAULT.
  bool hit_not_reached{false}; ///< HHF: some channel has an HHF flag in FAULT.
  bool plunger{false};         ///< DPM: some channel has a DPM flag in FAULT.
  bool communication{false};   ///< COMER: a write was ignored for its clock count.
  bool undervoltage{false};    ///< UVM: VM fell below UVLO (cleared by this read).
  bool active{false};          ///< ACTIVE.
  uint32_t raw{0};             ///< The whole register.

  /** @return true when any of the seven flags is set. */
  [[nodiscard]] constexpr bool AnyFlag() const noexcept {
    return (raw & reg::kFlagsMask) != 0U;
  }
};

/**
 * @brief The FAULT register: one byte per fault type, bit n for channel n (Table 12). Also used to
 * say which flags `ClearFaults()` clears.
 */
struct Faults {
  uint8_t overcurrent{0};     ///< OCP[7:0]: the channel was switched off (turn it off, then on).
  uint8_t hit_not_reached{0}; ///< HHF[7:0]: IHIT not reached by the end of tHIT.
  uint8_t open_load{0};       ///< OLF[7:0].
  uint8_t plunger{0};         ///< DPM[7:0]: no valid current dip during HIT.

  /** @return Every flag of every channel. */
  static constexpr Faults All() noexcept {
    return {.overcurrent = 0xFF, .hit_not_reached = 0xFF, .open_load = 0xFF, .plunger = 0xFF};
  }
  /** @return true when any flag is set. */
  [[nodiscard]] constexpr bool Any() const noexcept {
    return Channels() != 0U;
  }
  /** @return The channels with any flag, bit n for channel n. */
  [[nodiscard]] constexpr uint8_t Channels() const noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(overcurrent) |
                                static_cast<uint32_t>(hit_not_reached) |
                                static_cast<uint32_t>(open_load) | static_cast<uint32_t>(plunger));
  }
  /**
   * @param other The flags to add.
   * @return Every flag set here or in `other`.
   */
  [[nodiscard]] constexpr Faults Or(const Faults& other) const noexcept {
    return {.overcurrent = static_cast<uint8_t>(static_cast<uint32_t>(overcurrent) |
                                                static_cast<uint32_t>(other.overcurrent)),
            .hit_not_reached = static_cast<uint8_t>(static_cast<uint32_t>(hit_not_reached) |
                                                    static_cast<uint32_t>(other.hit_not_reached)),
            .open_load = static_cast<uint8_t>(static_cast<uint32_t>(open_load) |
                                              static_cast<uint32_t>(other.open_load)),
            .plunger = static_cast<uint8_t>(static_cast<uint32_t>(plunger) |
                                            static_cast<uint32_t>(other.plunger))};
  }
  /**
   * @param mask The flags to keep.
   * @return Every flag set both here and in `mask`.
   */
  [[nodiscard]] constexpr Faults And(const Faults& mask) const noexcept {
    return {.overcurrent = static_cast<uint8_t>(static_cast<uint32_t>(overcurrent) &
                                                static_cast<uint32_t>(mask.overcurrent)),
            .hit_not_reached = static_cast<uint8_t>(static_cast<uint32_t>(hit_not_reached) &
                                                    static_cast<uint32_t>(mask.hit_not_reached)),
            .open_load = static_cast<uint8_t>(static_cast<uint32_t>(open_load) &
                                              static_cast<uint32_t>(mask.open_load)),
            .plunger = static_cast<uint8_t>(static_cast<uint32_t>(plunger) &
                                            static_cast<uint32_t>(mask.plunger))};
  }
  /**
   * @param lhs The first value.
   * @param rhs The value to compare it with.
   * @return true when every field of `lhs` equals `rhs`'s.
   */
  [[nodiscard]] friend constexpr bool operator==(const Faults& lhs,
                                                 const Faults& rhs) noexcept = default;
};

/**
 * @brief The flags the driver has seen since the last `ClearLatchedFaults()` (handbook KB-CPP-119):
 * OVT and UVM from every 32-bit STATUS read ("Read Clears All", Register Details), the FAULT flags
 * every FAULT read clears ("Clearing the FAULT Register"), and the summary flags of every fault
 * byte (STATUS[7:0]) any access clocks out, at no extra I/O: the chip clears OLF when a channel
 * switches on (p21) and HHF when it starts a new cycle (p23), so a flag can come and go between
 * two FAULT reads. `GetLatchedFaults()` returns it with no I/O.
 */
struct LatchedFaults {
  bool overtemperature{false}; ///< OVT seen by a STATUS read or in a fault byte.
  bool undervoltage{false};    ///< UVM seen by a STATUS read or in a fault byte (the start-up's
                               ///< own power-on UVM is not kept).
  bool overcurrent{false};     ///< OCP in a fault byte: some channel had an OCP flag.
  bool open_load{false};       ///< OLF in a fault byte: some channel had an OLF flag.
  bool hit_not_reached{false}; ///< HHF in a fault byte: some channel had an HHF flag.
  bool plunger{false};         ///< DPM in a fault byte: some channel had a DPM flag.
  bool lost{false};            ///< A read of a running driver that clears flags (STATUS, or
                               ///< FAULT where the read clears) did not deliver them: the
                               ///< transfer failed or SDO floated. Flags may be gone unseen.
  Faults faults{};             ///< FAULT flags a FAULT read cleared, per channel. A summary
                               ///< flag above without its channel here: the chip cleared it first.

  /** @return true when any flag is latched. */
  [[nodiscard]] constexpr bool Any() const noexcept {
    return overtemperature || undervoltage || overcurrent || open_load || hit_not_reached ||
           plunger || lost || faults.Any();
  }
  /**
   * @param lhs The first value.
   * @param rhs The value to compare it with.
   * @return true when every field of `lhs` equals `rhs`'s.
   */
  [[nodiscard]] friend constexpr bool operator==(const LatchedFaults& lhs,
                                                 const LatchedFaults& rhs) noexcept = default;
};

/** @brief The duty-cycle limits the chip applies (Table 2), in percent. */
struct DutyLimits {
  uint8_t min_percent; ///< δMIN.
  uint8_t max_percent; ///< δMAX.
};

// -------------------------------------------------------------------------------------------------
// Register encoding
// -------------------------------------------------------------------------------------------------

/**
 * @brief Encode a channel configuration as its CFG_CHn value (Table 11).
 * @param c The configuration; the codes are masked to their width.
 * @return The 32-bit register value.
 */
constexpr uint32_t EncodeChannel(const ChannelConfig& c) noexcept {
  uint32_t v = 0;
  v |= c.half_scale ? reg::kHfs : 0U;
  v |= (static_cast<uint32_t>(c.hold) & reg::kCodeMask) << reg::kHoldShift;
  v |= c.trigger_pin ? reg::kTrgnspi : 0U;
  v |= (static_cast<uint32_t>(c.hit) & reg::kCodeMask) << reg::kHitShift;
  v |= static_cast<uint32_t>(c.hit_time) << reg::kHitTimeShift;
  v |= (c.drive == DriveMode::Vdr) ? reg::kVdrncdr : 0U;
  v |= (c.side == Side::High) ? reg::kHsnls : 0U;
  v |= (static_cast<uint32_t>(detail::ToUnderlying(c.chop)) & 0x03U) << reg::kFreqCfgShift;
  v |= c.slew_rate_control ? reg::kSrc : 0U;
  v |= c.open_load_detect ? reg::kOlEn : 0U;
  v |= c.plunger_detect ? reg::kDpmEn : 0U;
  v |= c.hit_current_check ? reg::kHhfEn : 0U;
  return v;
}

/**
 * @brief Decode a CFG_CHn value (Table 11).
 * @param v The register value.
 * @return The configuration it holds.
 */
constexpr ChannelConfig DecodeChannel(uint32_t v) noexcept {
  ChannelConfig c{};
  c.half_scale = (v & reg::kHfs) != 0U;
  c.hold = static_cast<uint8_t>((v >> reg::kHoldShift) & reg::kCodeMask);
  c.trigger_pin = (v & reg::kTrgnspi) != 0U;
  c.hit = static_cast<uint8_t>((v >> reg::kHitShift) & reg::kCodeMask);
  c.hit_time = static_cast<uint8_t>((v >> reg::kHitTimeShift) & 0xFFU);
  c.drive = ((v & reg::kVdrncdr) != 0U) ? DriveMode::Vdr : DriveMode::Cdr;
  c.side = ((v & reg::kHsnls) != 0U) ? Side::High : Side::Low;
  c.chop = static_cast<ChopDivider>((v >> reg::kFreqCfgShift) & 0x03U); // 0 to 3: all enumerators
  c.slew_rate_control = (v & reg::kSrc) != 0U;
  c.open_load_detect = (v & reg::kOlEn) != 0U;
  c.plunger_detect = (v & reg::kDpmEn) != 0U;
  c.hit_current_check = (v & reg::kHhfEn) != 0U;
  return c;
}

/**
 * @brief The STATUS value the driver writes (Table 10): ONCH, the masks, FREQM, CMxy and ACTIVE.
 * @param config The masks, clock and pair modes.
 * @param on_mask ONCH[7:0].
 * @param active ACTIVE.
 * @return The 32-bit register value; the read-only flags are 0.
 */
constexpr uint32_t EncodeStatus(const Config& config, uint8_t on_mask, bool active) noexcept {
  const FaultMask& m = config.fault_mask;
  uint32_t v = static_cast<uint32_t>(on_mask) << reg::kOnchShift;
  v |= m.overtemperature ? reg::kMaskOvt : 0U;
  v |= m.overcurrent ? reg::kMaskOcp : 0U;
  v |= m.open_load ? reg::kMaskOlf : 0U;
  v |= m.hit_not_reached ? reg::kMaskHhf : 0U;
  v |= m.plunger ? reg::kMaskDpm : 0U;
  v |= m.communication ? reg::kMaskComf : 0U;
  v |= m.undervoltage ? reg::kMaskUvm : 0U;
  v |= (config.clock == MasterClock::Khz80) ? reg::kFreqm : 0U;
  for (uint8_t p = 0; p < kPairs; ++p) {
    v |= (static_cast<uint32_t>(detail::ToUnderlying(config.pairs[p])) & 0x03U)
         << reg::PairModeShift(p);
  }
  v |= active ? reg::kActive : 0U;
  return v;
}

/**
 * @brief Decode a STATUS value.
 * @param v The register value.
 * @return ONCH, the flags and ACTIVE.
 */
constexpr Status DecodeStatus(uint32_t v) noexcept {
  Status s{};
  s.on_mask = static_cast<uint8_t>(v >> reg::kOnchShift);
  s.overtemperature = (v & reg::kOvt) != 0U;
  s.overcurrent = (v & reg::kOcp) != 0U;
  s.open_load = (v & reg::kOlf) != 0U;
  s.hit_not_reached = (v & reg::kHhf) != 0U;
  s.plunger = (v & reg::kDpm) != 0U;
  s.communication = (v & reg::kComer) != 0U;
  s.undervoltage = (v & reg::kUvm) != 0U;
  s.active = (v & reg::kActive) != 0U;
  s.raw = v;
  return s;
}

/**
 * @brief Decode one pair's CMxy field of a STATUS value. The field is two bits wide but only 0 to 2
 * are modes (Register Details); 3 is refused, never stored as a `PairMode` (KB-CPP20-025).
 * @param status The STATUS value.
 * @param pair 0 to 3.
 * @return The mode, or `WriteMismatch` for the reserved value 3 (a STATUS that does not hold what
 *         the driver can have written).
 */
constexpr Result<PairMode> DecodePairMode(uint32_t status, uint8_t pair) noexcept {
  const uint32_t field = (status >> reg::PairModeShift(pair)) & 0x03U;
  if (field > static_cast<uint32_t>(detail::ToUnderlying(PairMode::FullBridge))) {
    return Result<PairMode>::Failure(Error::WriteMismatch);
  }
  return Result<PairMode>::Success(static_cast<PairMode>(field));
}

/**
 * @brief Decode a FAULT value (Table 12).
 * @param v The register value.
 * @return One byte per fault type.
 */
constexpr Faults DecodeFaults(uint32_t v) noexcept {
  return {.overcurrent = static_cast<uint8_t>(v >> reg::kFaultOcpShift),
          .hit_not_reached = static_cast<uint8_t>(v >> reg::kFaultHhfShift),
          .open_load = static_cast<uint8_t>(v >> reg::kFaultOlfShift),
          .plunger = static_cast<uint8_t>(v >> reg::kFaultDpmShift)};
}

/**
 * @brief Encode the DPM settings as the CFG_DPM value; bits 31:15 stay 0.
 * @param d The settings; each field is masked to its width.
 * @return The register value.
 */
constexpr uint32_t EncodeDpm(const DpmConfig& d) noexcept {
  return ((static_cast<uint32_t>(d.start) & reg::kDpmIstartMask) << reg::kDpmIstartShift) |
         ((static_cast<uint32_t>(d.debounce) & reg::kDpmTdebMask) << reg::kDpmTdebShift) |
         ((static_cast<uint32_t>(d.threshold) & reg::kDpmIpthMask) << reg::kDpmIpthShift);
}

/**
 * @brief Decode a CFG_DPM value.
 * @param v The register value.
 * @return The settings.
 */
constexpr DpmConfig DecodeDpm(uint32_t v) noexcept {
  return {.start = static_cast<uint8_t>((v >> reg::kDpmIstartShift) & reg::kDpmIstartMask),
          .debounce = static_cast<uint8_t>((v >> reg::kDpmTdebShift) & reg::kDpmTdebMask),
          .threshold = static_cast<uint8_t>((v >> reg::kDpmIpthShift) & reg::kDpmIpthMask)};
}

// -------------------------------------------------------------------------------------------------
// Units
// -------------------------------------------------------------------------------------------------

/**
 * @brief One chopping period, in nanoseconds: divider / fMAIN (Table 1). Exact.
 * @param clock FREQM.
 * @param chop FREQ_CFG.
 * @return 10000 to 40000 ns at 100 kHz, 12500 to 50000 ns at 80 kHz.
 */
constexpr uint32_t ChopPeriodNs(MasterClock clock, ChopDivider chop) noexcept {
  const uint32_t main_period_ns = (clock == MasterClock::Khz80) ? 12500U : 10000U;
  return main_period_ns * (4U - (static_cast<uint32_t>(detail::ToUnderlying(chop)) & 0x03U));
}

/**
 * @brief The duty-cycle limits the chip applies to a channel (Table 2). The chip clamps the codes
 * by itself; the driver never moves a code into these limits, so 0 % and 100 % stay reachable.
 * @param chop FREQ_CFG.
 * @param slew_rate_control SRC.
 * @return 8 to 92 % at fMAIN, 7 to 93 % with SRC, 4 to 96 % otherwise.
 */
constexpr DutyLimits DutyLimitsOf(ChopDivider chop, bool slew_rate_control) noexcept {
  if (slew_rate_control) {
    return {.min_percent = 7, .max_percent = 93};
  }
  return (chop == ChopDivider::Div1) ? DutyLimits{.min_percent = 8, .max_percent = 92}
                                     : DutyLimits{.min_percent = 4, .max_percent = 96};
}

/**
 * @brief The HIT time of a code: code x 40 / fCHOP (Table 5). Exact.
 * @param code HIT_T, 0 to 255.
 * @param clock FREQM.
 * @param chop FREQ_CFG.
 * @return Microseconds (0 for code 0); `InvalidArgument` for `kHitTimeContinuous`, a HIT phase
 *         that never ends and so has no time (KB-CPP-126: no sentinel value).
 */
constexpr Result<uint32_t> HitTimeUs(uint8_t code, MasterClock clock, ChopDivider chop) noexcept {
  if (code == kHitTimeContinuous) {
    return Result<uint32_t>::Failure(Error::InvalidArgument);
  }
  return Result<uint32_t>::Success((static_cast<uint32_t>(code) * 40U * ChopPeriodNs(clock, chop)) /
                                   1000U);
}

/**
 * @brief The HIT_T code nearest to a HIT time (Table 5).
 *
 * A time beyond the longest finite one is refused: the code above it, 255, is a HIT phase that
 * never ends, which a rounding must never produce. For it use `kHitTimeContinuous` directly.
 *
 * @param us The HIT time in microseconds; 0 for no HIT phase.
 * @param clock FREQM.
 * @param chop FREQ_CFG.
 * @return The code (1 to 254 for a non-zero time, never 0), or `InvalidArgument` above
 *         254.5 steps.
 */
constexpr Result<uint8_t> HitTimeCode(uint32_t us, MasterClock clock, ChopDivider chop) noexcept {
  if (us == 0U) {
    return Result<uint8_t>::Success(kHitTimeNone);
  }
  const uint64_t step_ns = 40ULL * ChopPeriodNs(clock, chop);
  const uint64_t code = ((static_cast<uint64_t>(us) * 1000U) + (step_ns / 2U)) / step_ns;
  if (code > kMaxHitTimeCode) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  return Result<uint8_t>::Success((code == 0U) ? uint8_t{1} : static_cast<uint8_t>(code));
}

/**
 * @brief The DPM debounce time of a code: code / fCHOP (CFG_DPM). Exact.
 * @param code DPM_TDEB, 0 to 15.
 * @param clock FREQM.
 * @param chop FREQ_CFG of the channels that use DPM.
 * @return Microseconds, rounded down.
 */
constexpr uint32_t DebounceUs(uint8_t code, MasterClock clock, ChopDivider chop) noexcept {
  return ((static_cast<uint32_t>(code) & reg::kDpmTdebMask) * ChopPeriodNs(clock, chop)) / 1000U;
}

/**
 * @brief The full-scale current IFS = KFS x 1 V / RREF ("Full-Scale Current Setting"); HFS halves
 * KFS.
 * @param rref_ohm RREF, `kMinRrefOhm` to `kMaxRrefOhm`.
 * @param half_scale HFS.
 * @return IFS in microamps (1000000 for 15 kohm and HFS = 0), or `InvalidArgument` for an RREF
 *         out of range, `kNoRref` included (KB-CPP-126: no sentinel value).
 */
constexpr Result<uint32_t> FullScaleUa(uint32_t rref_ohm, bool half_scale) noexcept {
  if ((rref_ohm < kMinRrefOhm) || (rref_ohm > kMaxRrefOhm)) {
    return Result<uint32_t>::Failure(Error::InvalidArgument);
  }
  const uint64_t ua = (static_cast<uint64_t>(kKfs) * 1000000ULL) / rref_ohm;
  return Result<uint32_t>::Success(static_cast<uint32_t>(half_scale ? (ua / 2U) : ua));
}

/**
 * @brief The CDR peak current of a code: code / 128 x IFS (Table 4, "/ 2^7").
 * @param code HIT or HOLD, 0 to 126.
 * @param rref_ohm RREF.
 * @param half_scale HFS of the channel.
 * @return Microamps, rounded down (0 for code 0); `InvalidArgument` for code 127 (the unregulated
 *         full-on state, not a current) or an RREF out of range, `kNoRref` included.
 */
constexpr Result<uint32_t> CurrentUa(uint8_t code, uint32_t rref_ohm, bool half_scale) noexcept {
  const auto ifs = FullScaleUa(rref_ohm, half_scale);
  if ((code > kMaxCdrCode) || !ifs) {
    return Result<uint32_t>::Failure(Error::InvalidArgument);
  }
  return Result<uint32_t>::Success(
      static_cast<uint32_t>((static_cast<uint64_t>(code) * ifs.value) / 128U));
}

/**
 * @brief The CDR code nearest to a peak current: current x 128 / IFS (Table 4).
 * @param ma The current in milliamps; 0 for no current (code 0).
 * @param rref_ohm RREF.
 * @param half_scale HFS of the channel.
 * @return 1 to `kMaxCdrCode` for a non-zero current; `InvalidArgument` for an RREF out of range, a
 *         current that rounds to 0 (below the resolution), or one above 126/128 x IFS (code 127 is
 *         the unregulated full-on state, never a current).
 */
constexpr Result<uint8_t> CurrentCode(uint32_t ma, uint32_t rref_ohm, bool half_scale) noexcept {
  const auto ifs = FullScaleUa(rref_ohm, half_scale);
  if (!ifs) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  const uint32_t ifs_ua = ifs.value;
  if (ma == 0U) {
    return Result<uint8_t>::Success(0);
  }
  const uint64_t code = ((static_cast<uint64_t>(ma) * 128000U) + (ifs_ua / 2U)) / ifs_ua;
  if ((code == 0U) || (code > kMaxCdrCode)) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  return Result<uint8_t>::Success(static_cast<uint8_t>(code));
}

/**
 * @brief The DPM_ISTART or DPM_IPTH code nearest to a current: current x 127 / IFS (CFG_DPM; note
 * 127 here, 128 in Table 4). IFS with HFS = 0: the datasheet does not say that HFS scales DPM.
 * @param ma The current in milliamps.
 * @param rref_ohm RREF.
 * @param max_code 127 for ISTART, 15 for IPTH.
 * @return The code; `InvalidArgument` for an RREF out of range or a code above `max_code`.
 */
constexpr Result<uint8_t> DpmCurrentCode(uint32_t ma, uint32_t rref_ohm,
                                         uint8_t max_code) noexcept {
  const auto ifs = FullScaleUa(rref_ohm, false);
  if (!ifs) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  const uint32_t ifs_ua = ifs.value;
  const uint64_t code = ((static_cast<uint64_t>(ma) * 127000U) + (ifs_ua / 2U)) / ifs_ua;
  if (code > max_code) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  return Result<uint8_t>::Success(static_cast<uint8_t>(code));
}

// -------------------------------------------------------------------------------------------------
// Validation
// -------------------------------------------------------------------------------------------------

/**
 * @brief Check a HIT or HOLD code against its channel's drive mode.
 * @param code The code.
 * @param drive The channel's mode.
 * @return true for 0 to `kMaxCdrCode` in CDR, 0 to `kMaxVdrCode` in VDR.
 */
constexpr bool ValidCode(uint8_t code, DriveMode drive) noexcept {
  return code <= ((drive == DriveMode::Cdr) ? kMaxCdrCode : kMaxVdrCode);
}

/**
 * @brief Check what the high side lacks ("Low-Side/High-Side Operations (HSnLS)").
 * @param c The channel's configuration.
 * @return true on the low side, or on the high side in VDR with no SRC, HFS or DPM.
 */
constexpr bool ValidHighSide(const ChannelConfig& c) noexcept {
  return (c.side == Side::Low) || ((c.drive == DriveMode::Vdr) && !c.slew_rate_control &&
                                   !c.half_scale && !c.plunger_detect);
}

/**
 * @brief Check one channel's configuration without touching the chip.
 * @param c The channel's configuration.
 * @param clock FREQM, which sets whether SRC is allowed at `c.chop`.
 * @return `Ok`, or `InvalidArgument` for: a value that is not an enumerator; a code out of
 *         `ValidCode()`; CDR on the high side (the chip ignores HSnLS in CDR); SRC, HFS or DPM on
 *         the high side; SRC at 50 kHz or above ("Slew-Rate Controlled Mode"); HHF in VDR (the
 *         check is CDR only).
 */
constexpr Error ValidateChannel(const ChannelConfig& c, MasterClock clock) noexcept {
  if (!detail::IsEnumerator(c.drive) || !detail::IsEnumerator(c.side) ||
      !detail::IsEnumerator(c.chop) || !detail::IsEnumerator(clock)) {
    return Error::InvalidArgument;
  }
  if (!ValidCode(c.hit, c.drive) || !ValidCode(c.hold, c.drive)) {
    return Error::InvalidArgument;
  }
  if (!ValidHighSide(c)) {
    return Error::InvalidArgument;
  }
  if (c.slew_rate_control && (ChopPeriodNs(clock, c.chop) <= 20000U)) {
    return Error::InvalidArgument; // fCHOP >= 50 kHz
  }
  if (c.hit_current_check && (c.drive == DriveMode::Vdr)) {
    return Error::InvalidArgument;
  }
  return Error::Ok;
}

/**
 * @brief Check that a channel enables a load-dependent diagnostic only when a load is declared on
 * it (handbook KB-CPP-131).
 * @param c The channel's configuration.
 * @param loaded_channels `Config::loaded_channels`.
 * @param channel 0 to 7.
 * @return true when none of open load, plunger movement and HIT current check is on, or the
 *         channel's bit is set in `loaded_channels`.
 */
constexpr bool ValidLoadDiagnostics(const ChannelConfig& c, uint8_t loaded_channels,
                                    uint8_t channel) noexcept {
  const bool diagnostics = c.open_load_detect || c.plunger_detect || c.hit_current_check;
  const bool loaded =
      ((static_cast<uint32_t>(loaded_channels) >> (static_cast<uint32_t>(channel) & 0x07U)) & 1U) !=
      0U;
  return !diagnostics || loaded;
}

namespace detail {
/**
 * @param config The configuration.
 * @return true when RREF is `kNoRref` or in the recommended range and the wake timeout is in
 *         range.
 */
constexpr bool ValidTimings(const Config& config) noexcept {
  const bool rref_ok = (config.rref_ohm == kNoRref) ||
                       ((config.rref_ohm >= kMinRrefOhm) && (config.rref_ohm <= kMaxRrefOhm));
  return rref_ok && (config.wake_timeout_us >= kMinWakeTimeoutUs) &&
         (config.wake_timeout_us <= kMaxWakeTimeoutUs);
}
} // namespace detail

/**
 * @brief Check a configuration without touching the chip.
 * @param config The configuration.
 * @return `Ok`, or `InvalidArgument` for a value that is not an enumerator, an RREF that is neither
 *         `kNoRref` nor in the recommended range, a wake timeout out of range, a pair mode 3, a
 *         channel `ValidateChannel()` refuses, a load-dependent diagnostic on a channel
 *         `loaded_channels` does not declare (`ValidLoadDiagnostics()`), or a DPM code above its
 *         width. `kNoRref` (RREF
 *         not given) passes: the driver works in codes, and only the milliamp conversions need
 *         RREF (they return `InvalidArgument` without it).
 */
constexpr Error Validate(const Config& config) noexcept {
  if (!detail::IsEnumerator(config.variant) || !detail::IsEnumerator(config.clock)) {
    return Error::InvalidArgument;
  }
  if (!detail::ValidTimings(config)) {
    return Error::InvalidArgument;
  }
  for (const PairMode p : config.pairs) {
    if (!detail::IsEnumerator(p)) {
      return Error::InvalidArgument;
    }
  }
  for (uint8_t ch = 0; ch < kChannels; ++ch) {
    const ChannelConfig& c = config.channels[ch];
    if ((ValidateChannel(c, config.clock) != Error::Ok) ||
        !ValidLoadDiagnostics(c, config.loaded_channels, ch)) {
      return Error::InvalidArgument;
    }
  }
  const DpmConfig& d = config.dpm;
  if ((d.start > reg::kDpmIstartMask) || (d.debounce > reg::kDpmTdebMask) ||
      (d.threshold > reg::kDpmIpthMask)) {
    return Error::InvalidArgument;
  }
  return Error::Ok;
}

// --- Compile-time proofs (KB-CPP20-012) ---------------------------------------------------------
// Inside detail, where the typed literals (_u8, _u32) are declared.
namespace detail {
// The reset configuration encodes to the reset values (Register Details) and is valid.
static_assert(EncodeChannel(ChannelConfig{}) == 0U, "Register Details: CFG_CHn resets to 0");
static_assert(EncodeStatus(Config{}, 0_u8, false) == (reg::kStatusReset & reg::kWritableMask),
              "Register Details: STATUS resets with only M_COMF set among the writable bits");
static_assert(Validate(Config{}) == Error::Ok, "Config{} is a valid configuration");
// Every field lands where Table 11 puts it and decodes back.
static_assert(DecodeChannel(EncodeChannel({.drive = DriveMode::Vdr,
                                           .side = Side::High,
                                           .hit = 100,
                                           .hold = 30,
                                           .hit_time = 254,
                                           .chop = ChopDivider::Div1,
                                           .trigger_pin = true,
                                           .open_load_detect = true})) ==
              ChannelConfig{.drive = DriveMode::Vdr,
                            .side = Side::High,
                            .hit = 100,
                            .hold = 30,
                            .hit_time = 254,
                            .chop = ChopDivider::Div1,
                            .trigger_pin = true,
                            .open_load_detect = true});
static_assert(DecodeChannel(EncodeChannel({.hit = 126,
                                           .hold = 1,
                                           .hit_time = 1,
                                           .chop = ChopDivider::Div3,
                                           .half_scale = true,
                                           .slew_rate_control = true,
                                           .plunger_detect = true,
                                           .hit_current_check = true})) ==
              ChannelConfig{.hit = 126,
                            .hold = 1,
                            .hit_time = 1,
                            .chop = ChopDivider::Div3,
                            .half_scale = true,
                            .slew_rate_control = true,
                            .plunger_detect = true,
                            .hit_current_check = true});
static_assert(DecodeDpm(EncodeDpm({.start = 127, .debounce = 15, .threshold = 15})) ==
              DpmConfig{.start = 127, .debounce = 15, .threshold = 15});
static_assert(EncodeDpm({.start = 127, .debounce = 15, .threshold = 15}) == reg::kDpmUsedMask);
// CMxy: every mode round-trips, the reserved value 3 is refused.
static_assert(DecodePairMode(EncodeStatus({.pairs = {PairMode::Independent, PairMode::Parallel,
                                                     PairMode::FullBridge, PairMode::Parallel}},
                                          0_u8, true),
                             2_u8)
                  .value == PairMode::FullBridge);
static_assert(!DecodePairMode(0x0000'C000_u32, 3_u8).ok(), "CMxy = 3 is not a mode");
static_assert(DecodeFaults(0x0102'0408_u32) == Faults{.overcurrent = 0x01,
                                                      .hit_not_reached = 0x02,
                                                      .open_load = 0x04,
                                                      .plunger = 0x08});
// RREF and the current are both uint32_t: a call with the two swapped is always refused, because no
// current the chip can regulate is a valid RREF and no valid RREF is a current it can regulate.
static_assert(CurrentCode(250_u32, 30000_u32, false).ok() &&
              !CurrentCode(30000_u32, 250_u32, false).ok());
static_assert(!CurrentCode(kMinRrefOhm, kMaxRrefOhm, false).ok());
static_assert(!FullScaleUa(kNoRref, false).ok() &&
              (FullScaleUa(15000_u32, false).value == 1000000U));
static_assert(!HitTimeUs(kHitTimeContinuous, MasterClock::Khz100, ChopDivider::Div4).ok());
} // namespace detail

/** @return The driver version, e.g. `"0.1.0"`, from the generated `max22200_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return MAX22200_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return MAX22200_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return MAX22200_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return MAX22200_VERSION_PATCH;
}

/** @} */

} // namespace max22200

#endif // MAX22200_TYPES_HPP
