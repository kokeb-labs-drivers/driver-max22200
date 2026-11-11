/**
 * @file fuzz_max22200.cpp
 * @brief Fuzz target: the driver over a chip model and a bus that the fuzzer controls.
 *
 * Everything the driver receives comes from SDO and from the port's return values, so those are
 * the untrusted input. The fuzzer's bytes choose a configuration, then a sequence of driver calls;
 * on every transfer they choose what the bus does: pass the chip model's reply, fail the transfer,
 * lose it (the chip never sees it and SDO floats), flip bits of the reply or replace it; on every
 * pin write they may fail it. Between calls they change the board: SDO floating, stuck low or
 * noise, SDI not reaching the chip, a power-on reset, VM below UVLO, miscounted or dropped writes,
 * a TRIG pin held high, and channel faults.
 *
 * A call is "trusted" when none of its transfers or pin writes was tampered with and SDO came from
 * the chip. After every call the target checks the driver's promises (docs/contract.md), and
 * aborts, naming the line, when one breaks:
 *
 * - a failed call carries an `Error` the enum defines and the driver documents, and a value of
 *   `T{}`; every transfer is 1 or 4 bytes with both buffers, a 4-byte transfer never has CMD high;
 * - before `EnsureInitialized()` succeeds, no call makes a transfer or a pin write;
 *   `InvalidArgument`, `ChannelsActive` and `Unsupported` mean nothing was sent;
 * - CMD is high (its idle level) after every call that drove pins, all successfully; ENABLE is low
 * after a failed start and after `Deinitialize()`, unless a pin write failed;
 * - the chip never receives a command with RFU bits set, a CFG_DPM write with bits 31:15 set, or a
 *   channel configuration `ValidateChannel()` refuses (CDR code 127, HSnLS with CDR, ...);
 * - a lone ONCH data byte (the one-transfer write) goes out only while the chip's command in force
 *   is the 8-bit STATUS write, unless the board reset the chip, SDI was deaf or a transfer was lost
 *   since the last command;
 * - `EnsureInitialized()` never returns `Ok` on a dead bus (SDO floating or stuck, SDI not reaching
 *   the chip) unless a transfer of the call was tampered with;
 * - while every call since the last start was trusted and the board did nothing behind the
 *   driver's back, the chip holds exactly what the driver says it wrote: ONCH, STATUS, every
 *   CFG_CHn and CFG_DPM.
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang,
 * `-DMAX22200_BUILD_FUZZERS=ON`), and as a plain executable that replays the corpus in
 * tests/fuzz/corpus/ as a CTest on any compiler.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "max22200.hpp"
#include "mock_max22200.hpp"

namespace {

using max22200::Error;
using max22200::Verify;
using max22200_test::Fault;
using max22200_test::Max22200Model;
using max22200_test::Miso;
using max22200_test::Part;
namespace reg = max22200::reg;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_max22200: promise broken at line %d\n", line);
    std::abort();
  }
}

/// The SPI port: the chip model behind a bus the fuzzer controls.
class FuzzPort : public max22200::SpiInterface<FuzzPort> {
public:
  explicit FuzzPort(Input& in) noexcept : in_(in) {}

  [[nodiscard]] bool EnsureInitialized() const noexcept {
    return bus_up_ok_;
  }

  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    Check(tx != nullptr && rx != nullptr && (len == 1 || len == 4));
    Check(len == 1 || !chip_.cmd); // 32-bit data never goes out with CMD high
    CheckFastPath(len);
    transfers_++;
    uint8_t action = in_.Byte();
    if (fail_next_data_ && !chip_.cmd && chip_.command_valid && ((chip_.command & 0x80U) != 0U)) {
      fail_next_data_ = false; // the board's "failed reply" event: the next write's data phase
      action = 0xFBU;
    }
    if (action == 0xFFU) {
      tampered_++;
      return false; // the bus failed; nothing reached the chip
    }
    if (action == 0xFBU) {
      tampered_++; // the transfer reached the chip, the port reported a failure afterwards
      chip_.Transfer(tx, rx, len);
      return false;
    }
    if (action == 0xFEU) {
      tampered_++; // lost: the chip never sees it, SDO floats
      lost_++;
      excuse_fast_ = true;
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = 0xFF;
      }
      return true;
    }
    chip_.Transfer(tx, rx, len);
    if (chip_.cmd && len == 1) {
      excuse_fast_ = chip_.deaf_sdi; // the chip took the driver's command, or 0x00 from a deaf SDI
    }
    if (action == 0xFDU || action == 0xFCU) {
      tampered_++;
      forged_++;
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = action == 0xFDU ? static_cast<uint8_t>(rx[i] ^ in_.Byte()) : in_.Byte();
      }
    }
    if (chip_.miso != Miso::Chip || chip_.deaf_sdi) {
      untrusted_++;
    }
    return true;
  }

  bool GpioSet(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    pins_++;
    if (in_.Byte() == 0xFFU) {
      pin_failures_++;
      return false; // the pin keeps its level
    }
    const bool active = signal == max22200::GpioSignal::ACTIVE;
    if (pin == max22200::CtrlPin::Enable) {
      chip_.SetEnable(active);
    } else if (pin == max22200::CtrlPin::Cmd) {
      chip_.SetCmd(active);
    }
    return true;
  }

  /// The one-transfer ONCH write: a lone data byte (CMD low, not right after a command) may go out
  /// only while the command in force is the 8-bit STATUS write (0x81), unless the board reset the
  /// chip, SDI was deaf or a transfer was lost since the last command.
  void CheckFastPath(std::size_t len) noexcept {
    const bool lone_data = !chip_.cmd && len == 1 && !prev_was_cmd_;
    prev_was_cmd_ = chip_.cmd;
    if (lone_data) {
      fast_writes_++;
      Check(excuse_fast_ || chip_.deaf_sdi || (chip_.command_valid && chip_.command == 0x81U));
    }
  }
  /// The next write's data phase reaches the chip and the port reports it as failed.
  void FailNextData() noexcept {
    fail_next_data_ = true;
  }
  /// The board did something that makes the chip forget or change its command.
  void ExcuseFastPath() noexcept {
    excuse_fast_ = true;
  }
  /// Replies forged on the bus (bits flipped or replaced): the chip sends no CRC, so the driver
  /// cannot tell a forged fault byte from the chip's own.
  [[nodiscard]] int Forged() const noexcept {
    return forged_;
  }
  [[nodiscard]] int FastWrites() const noexcept {
    return fast_writes_;
  }

  bool GpioRead(max22200::CtrlPin pin, max22200::GpioSignal& signal) noexcept {
    if (pin != max22200::CtrlPin::Fault || in_.Byte() == 0xFFU) {
      return false;
    }
    signal = chip_.FaultPin() ? max22200::GpioSignal::ACTIVE : max22200::GpioSignal::INACTIVE;
    return true;
  }

  void DelayUs(uint32_t us) noexcept {
    chip_.Elapse(us);
  }

  Max22200Model& Chip() noexcept {
    return chip_;
  }
  [[nodiscard]] int Transfers() const noexcept {
    return transfers_;
  }
  [[nodiscard]] int Pins() const noexcept {
    return pins_;
  }
  [[nodiscard]] int Tampered() const noexcept {
    return tampered_;
  }
  [[nodiscard]] int Untrusted() const noexcept {
    return untrusted_;
  }
  [[nodiscard]] int Lost() const noexcept {
    return lost_;
  }
  [[nodiscard]] int PinFailures() const noexcept {
    return pin_failures_;
  }
  void SetBusUp(bool ok) noexcept {
    bus_up_ok_ = ok;
  }

private:
  Input& in_;
  Max22200Model chip_;
  int transfers_ = 0;
  int pins_ = 0;
  int tampered_ = 0;
  int untrusted_ = 0;
  int pin_failures_ = 0;
  bool fail_next_data_ = false;
  int forged_ = 0;
  int lost_ = 0;
  int fast_writes_ = 0;
  bool prev_was_cmd_ = false;
  bool excuse_fast_ = false;
  bool bus_up_ok_ = true;
};

using Driver = max22200::MAX22200<FuzzPort>;

bool KnownError(Error e) noexcept {
  switch (e) {
  case Error::Ok:
  case Error::NotInitialized:
  case Error::InvalidArgument:
  case Error::Bus:
  case Error::DeviceIdMismatch:
  case Error::Unsupported:
  case Error::NoDevice:
  case Error::Pin:
  case Error::CommunicationError:
  case Error::WriteMismatch:
  case Error::Undervoltage:
  case Error::ChannelsActive:
  case Error::DeviceReset:
  case Error::Uncertain:
    return true;
  case Error::Timeout:
    return true;
  case Error::Crc: // documented as never returned
  case Error::Busy:
  case Error::DeviceFault:
    return false;
  default:
    break;
  }
  return false;
}

/// The fuzzer's state between calls.
struct World {
  Verify verify = Verify::None;
  bool clean = false;        ///< Every call trusted and no board event since the last start.
  bool hidden_state = false; ///< A TRIG pin held high, or a write acknowledged and lost: the chip
                             ///< may hold a mode or a channel the driver cannot see.
  bool reset_watch = false;  ///< A power-on reset on a readable bus, and no start since: nothing
                             ///< may set ACTIVE again (p22: "requires reconfiguration").
  int reset_tampered = 0;    ///< The port's tamper count at the reset.
  int forged_mark = 0;       ///< The port's forged-reply count at the last start.
};

/// A channel configuration from two bytes and maybe more, often invalid on purpose.
max22200::ChannelConfig FuzzChannel(Input& in) noexcept {
  const uint8_t a = in.Byte();
  max22200::ChannelConfig c{};
  c.drive = (a & 0x01U) != 0U ? max22200::DriveMode::Vdr : max22200::DriveMode::Cdr;
  c.side = (a & 0x02U) != 0U ? max22200::Side::High : max22200::Side::Low;
  c.chop = static_cast<max22200::ChopDivider>((static_cast<uint32_t>(a) >> 2U) & 0x03U);
  c.slew_rate_control = (a & 0x10U) != 0U;
  c.open_load_detect = (a & 0x20U) != 0U;
  c.hit_current_check = (a & 0x40U) != 0U;
  c.plunger_detect = (a & 0x80U) != 0U;
  const uint8_t b = in.Byte();
  c.half_scale = (b & 0x01U) != 0U;
  c.trigger_pin = (b & 0x02U) != 0U;
  c.hit = static_cast<uint8_t>(in.Byte() & 0x7FU);
  c.hold = static_cast<uint8_t>(in.Byte() & 0x7FU);
  c.hit_time = in.Byte();
  return c;
}

/// A configuration from the fuzzer's bytes. The variant matches the chip unless it says otherwise.
max22200::Config FuzzConfig(Input& in, const Max22200Model& chip, World& w) noexcept {
  const uint8_t a = in.Byte();
  max22200::Config c{};
  c.variant = (a & 0x01U) != 0U ? max22200::Variant::Max22200 : max22200::Variant::Max22200A;
  if ((a & 0x02U) == 0U) {
    c.variant =
        chip.part == Part::Max22200 ? max22200::Variant::Max22200 : max22200::Variant::Max22200A;
  }
  c.clock = (a & 0x04U) != 0U ? max22200::MasterClock::Khz80 : max22200::MasterClock::Khz100;
  c.rref_ohm = (a & 0x08U) != 0U ? 15000U : 0U;
  c.wake_timeout_us = (a & 0x40U) != 0U ? max22200::kMinWakeTimeoutUs : 20000U;
  w.verify = (a & 0x10U) != 0U ? Verify::ReadBack : Verify::None;
  const uint8_t pairs = in.Byte();
  for (uint8_t p = 0; p < max22200::kPairs; ++p) {
    c.pairs[p] = static_cast<max22200::PairMode>((static_cast<uint32_t>(pairs) >> (2U * p)) &
                                                 0x03U); // 3 is invalid
  }
  const uint8_t masks = in.Byte();
  c.fault_mask = {(masks & 0x01U) != 0U, (masks & 0x02U) != 0U, (masks & 0x04U) != 0U,
                  (masks & 0x08U) != 0U, (masks & 0x10U) != 0U, (masks & 0x20U) != 0U,
                  (masks & 0x40U) != 0U};
  const uint8_t which = in.Byte(); // channels that get fuzzed settings; the rest stay at reset
  for (uint8_t ch = 0; ch < max22200::kChannels; ++ch) {
    if (((static_cast<uint32_t>(which) >> ch) & 1U) != 0U) {
      c.channels[ch] = FuzzChannel(in);
    }
  }
  // Loads on the fuzzed channels, or on every other one of them only, so that a load diagnostic on
  // an unloaded channel is refused too (KB-CPP-131).
  c.loaded_channels =
      (a & 0x80U) != 0U ? static_cast<uint8_t>(static_cast<uint32_t>(which) & 0x55U) : which;
  if ((a & 0x20U) != 0U) {
    c.dpm = {static_cast<uint8_t>(in.Byte() & 0xFFU), static_cast<uint8_t>(in.Byte() & 0x1FU),
             static_cast<uint8_t>(in.Byte() & 0x1FU)};
  }
  return c;
}

/// The board between calls: what a cable, a supply or a worn valve does to the chip.
void FuzzBoard(FuzzPort& port, Input& in, World& w) noexcept {
  Max22200Model& chip = port.Chip();
  const uint8_t a = in.Byte();
  const uint8_t b = in.Byte();
  chip.miso = (a & 0x07U) < 5U ? Miso::Chip : static_cast<Miso>(1U + (a % 3U));
  chip.deaf_sdi = (a & 0x18U) == 0x18U;
  if ((a & 0xE0U) == 0xE0U) {
    chip.Brownout();
    port.ExcuseFastPath();
    w.clean = false;
    w.reset_watch = true;
    w.reset_tampered = port.Tampered();
  }
  if ((a & 0xE0U) == 0xC0U) {
    chip.regs[0] |= 0x04U; // COMER left set from earlier: sticky until ENABLE falls
  }
  chip.vm_low = (b & 0x07U) == 0x07U;
  chip.uvm_sticky_reads = (b & 0x07U) == 0x06U ? 3 : 0;
  chip.wake_settle_us = (a & 0x07U) == 0x04U ? 15000U : 0U;
  chip.trig_high = (b & 0x18U) == 0x18U;
  // A TRIG pin held high keeps a drive mode; SDI that does not reach the chip loses every write
  // the driver sends, an ONCH write too, which leaves a channel on and so locks the mode. Without a
  // read-back the driver sees neither.
  w.hidden_state = w.hidden_state || chip.trig_high || chip.deaf_sdi;
  if ((b & 0x20U) != 0U) {
    // A miscount is not hidden state: the driver sees COMER in the next fault byte (or reads every
    // switch-off back while COMER stays set), so the switch-off checks keep running after it.
    chip.comer_writes = 1;
    w.clean = false;
  }
  if (((b & 0x60U) == 0x60U) && ((a & 0x03U) == 0x01U)) {
    // A failed reply, then a miscount of the next write: the write that would settle the doubt
    // the failed reply left is the one the chip ignores (p34).
    port.FailNextData();
    chip.comer_writes = 0;
    chip.comer_after_writes = 1;
  } else if ((b & 0x40U) != 0U) {
    chip.drop_writes = 1;
    w.clean = false;
    w.hidden_state = true;
  }
  if ((b & 0x80U) != 0U) {
    const uint8_t f = in.Byte();
    chip.RaiseFault(static_cast<Fault>((static_cast<uint32_t>(f) >> 3U) & 0x03U),
                    static_cast<uint8_t>(static_cast<uint32_t>(f) & 0x07U));
    if ((f & 0x20U) != 0U) {
      chip.Overtemperature();
    }
  }
  port.SetBusUp((in.Byte() & 0x0FU) != 0U);
  if (chip.miso != Miso::Chip || chip.deaf_sdi || chip.trig_high) {
    w.clean = false; // stays false until the next start on a working bus
  }
  if (chip.miso != Miso::Chip || chip.deaf_sdi) {
    w.reset_watch = false; // the fault byte that shows the reset cannot be read
  }
}

/// What a call may change, taken before it.
struct Before {
  bool up = false;
  int transfers = 0;
  int pins = 0;
  int tampered = 0;
  int untrusted = 0;
  int pin_failures = 0;
};

Before Snapshot(const Driver& d, const FuzzPort& port) noexcept {
  return {.up = d.IsInitialized(),
          .transfers = port.Transfers(),
          .pins = port.Pins(),
          .tampered = port.Tampered(),
          .untrusted = port.Untrusted(),
          .pin_failures = port.PinFailures()};
}

/// The chip never holds a channel setting the driver would refuse, nor reserved bits. A transfer
/// the port reports as done although the chip never saw it is outside every SPI contract: a lost
/// command leaves the previous one in force, so the next data lands in the previous register
/// (docs/limitations.md). Then, and after a board fault the driver cannot see without a read-back,
/// may the chip hold what the driver never sent.
void CheckChipSane(FuzzPort& port, const World& w) noexcept {
  const Max22200Model& chip = port.Chip();
  if (port.Lost() != 0 || w.hidden_state) {
    // A TRIG pin held high keeps the old drive mode; a lost ONCH write leaves a channel on that the
    // driver thinks is off, which locks the mode as well. Only Verify::ReadBack sees either.
    return;
  }
  Check(chip.reserved_writes == 0);
  for (uint8_t ch = 0; ch < max22200::kChannels; ++ch) {
    const max22200::ChannelConfig c = max22200::DecodeChannel(chip.regs[reg::CfgChannel(ch)]);
    Check(max22200::ValidCode(c.hit, c.drive) && max22200::ValidCode(c.hold, c.drive));
    Check(c.side == max22200::Side::Low || c.drive == max22200::DriveMode::Vdr);
  }
}

/// Promises every call keeps; returns true when the call was trusted.
bool CheckCommon(FuzzPort& port, const Before& b, Error e, bool lifecycle, World& w) noexcept {
  Check(KnownError(e));
  const int sent = port.Transfers() - b.transfers;
  const int pins = port.Pins() - b.pins;
  if (!b.up && !lifecycle) {
    Check(e == Error::NotInitialized && sent == 0 && pins == 0);
  }
  if (e == Error::InvalidArgument || e == Error::ChannelsActive || e == Error::Unsupported) {
    Check(sent == 0);
  }
  // Uncertain: a switch-on in a pair of unknown mode was dropped; the rest of a SetChannelsOn()
  // mask (its switch-offs) may have gone out, so it may send.
  const bool pins_ok = port.PinFailures() == b.pin_failures;
  if (pins_ok && pins > 0) {
    Check(port.Chip().cmd); // a call that drove pins leaves CMD at its idle level, high
  }
  const bool trusted = port.Untrusted() == b.untrusted && port.Tampered() == b.tampered && pins_ok;
  w.clean = w.clean && trusted;
  CheckChipSane(port, w);
  return trusted;
}

/// While the bus is clean, the chip holds what the driver says it wrote.
void CheckRegisters(const Driver& d, FuzzPort& port, const World& w) noexcept {
  if (!w.clean || !d.IsInitialized()) {
    return;
  }
  const max22200::Config& c = d.GetConfig();
  const Max22200Model& chip = port.Chip();
  Check(chip.OnMask() == d.GetChannelsOn());
  Check((chip.regs[reg::kStatus] & reg::kWritableMask) ==
        max22200::EncodeStatus(c, d.GetChannelsOn(), true));
  for (uint8_t ch = 0; ch < max22200::kChannels; ++ch) {
    Check(chip.regs[reg::CfgChannel(ch)] == max22200::EncodeChannel(c.channels[ch]));
  }
  Check(chip.regs[reg::kCfgDpm] == max22200::EncodeDpm(c.dpm));
}

/// A miscount the chip has flagged (COMER) that the driver has not seen yet: its copy may be wrong
/// until the next fault byte shows it.
bool MiscountUnseen(const Driver& d, FuzzPort& port) noexcept {
  const auto seen = static_cast<uint32_t>(d.GetLastStatusByte());
  return ((port.Chip().regs[reg::kStatus] & reg::kComer) != 0U) &&
         ((seen == 0xFFU) || ((seen & reg::kComer) == 0U)); // 0xFF is no answer, not a COMER
}

/// A miscount the driver cannot see at all: COMER is set on the chip while SDO does not carry the
/// chip's fault bytes. Until a start, the chip may then hold a write the driver believes it never
/// sent, or not hold one it believes it sent: hidden state, like a dropped write.
void NoteBlindMiscount(const Driver& d, FuzzPort& port, World& w) noexcept {
  if ((port.Chip().miso != Miso::Chip) && MiscountUnseen(d, port)) {
    w.hidden_state = true;
  }
  // A forged reply can show a clean fault byte that vouches for a write the chip ignored, or hide
  // a COMER: until a start, the chip may hold what the driver cannot know.
  if (port.Forged() != w.forged_mark) {
    w.hidden_state = true;
  }
}

void FuzzLifecycle(Driver& d, FuzzPort& port, Input& in, World& w, uint8_t op) {
  const Before b = Snapshot(d, port);
  if (op == 0) {
    const Max22200Model& chip = port.Chip();
    const bool dead = chip.miso == Miso::FloatHigh || chip.miso == Miso::StuckLow || chip.deaf_sdi;
    const auto r = d.EnsureInitialized();
    NoteBlindMiscount(d, port, w);
    const bool trusted = CheckCommon(port, b, r.error, true, w);
    Check(r.ok() == d.IsInitialized());
    if (b.up) {
      Check(r.ok() && port.Transfers() == b.transfers); // idempotent, no I/O once started
    } else if (r.ok()) {
      Check(!dead || port.Tampered() > b.tampered); // a dead bus never passes
      // tWU has passed, unless a lost or forged transfer showed ACTIVE before it was set (the
      // driver cannot tell; regress-forged-wake-read).
      Check(chip.Awake() || port.Tampered() > b.tampered);
      // A MAX22200 declared as a MAX22200A is the one wrong declaration; the reverse is safe.
      const bool part_ok =
          d.GetConfig().variant != max22200::Variant::Max22200A || chip.part != Part::Max22200;
      w.clean = trusted && chip.miso == Miso::Chip && !chip.trig_high && part_ok;
      // The start wrote and read back every register: the chip holds what the driver knows again,
      // unless a board condition that hides state is still in force.
      w.hidden_state = chip.trig_high || chip.deaf_sdi || chip.drop_writes > 0 ||
                       chip.drop_bank != 0xFFU || chip.miso != Miso::Chip || !trusted;
      w.forged_mark = port.Forged();
    } else if (port.PinFailures() == b.pin_failures && port.Pins() > b.pins &&
               r.error != Error::Undervoltage && r.error != Error::Timeout) {
      Check(!chip.enable); // a failed start that drove ENABLE leaves the chip asleep (a wake that
                           // timed out stays enabled, every channel off, for a retry)
    }
  } else if (op == 1) {
    const auto r = d.Deinitialize();
    NoteBlindMiscount(d, port, w);
    CheckCommon(port, b, r.error, true, w);
    Check(!d.IsInitialized());
    if (b.up && port.PinFailures() == b.pin_failures) {
      Check(!port.Chip().enable);
    }
  } else {
    const auto r = d.ApplyConfig(FuzzConfig(in, port.Chip(), w));
    NoteBlindMiscount(d, port, w);
    CheckCommon(port, b, r.error, true, w);
    if (!b.up) {
      Check(port.Transfers() == b.transfers); // stored only
    }
  }
  CheckRegisters(d, port, w);
}

/// Before a call: no board condition that would lose a write unseen. A miscount armed for the
/// call (`comer_writes`) is left to the caller: the driver reports it, but the write may not land.
bool BoardHonest(FuzzPort& port) noexcept {
  const Max22200Model& chip = port.Chip();
  // A transfer lost earlier leaves another command in force (outside the SPI contract), and a
  // trusted-looking write after it may land elsewhere: only a session with no lost transfer counts.
  return port.Lost() == 0 && chip.miso == Miso::Chip && !chip.deaf_sdi && chip.drop_writes == 0 &&
         chip.drop_bank == 0xFFU && chip.comer_bank_writes == 0 && chip.enable;
}

/// After the call: the bus delivered every transfer of it as sent.
bool WritesLanded(FuzzPort& port, bool honest, int tampered, int pin_failures) noexcept {
  return honest && port.Tampered() == tampered && port.PinFailures() == pin_failures &&
         port.Lost() == 0;
}

/// A switch-off is never held back: after SetChannelsOn() returns Uncertain, each pair ends at
/// the request or only lost bits on the chip (nothing switched on in a pair of unknown mode), and
/// every channel asked off that the driver could know was on (`known`) is off.
void CheckUncertainMask(FuzzPort& port, uint8_t request, uint8_t before, uint8_t known) noexcept {
  const uint32_t after = port.Chip().OnMask();
  for (uint32_t p = 0; p < 4U; ++p) {
    const uint32_t pb = 0x03U << (2U * p);
    const bool shrank = ((after & pb) & ~(static_cast<uint32_t>(before) & pb)) == 0U;
    Check(shrank || ((after & pb) == (static_cast<uint32_t>(request) & pb)));
  }
  Check((static_cast<uint32_t>(known) & ~static_cast<uint32_t>(request) & after) == 0U);
}

Error FuzzSwitch(Driver& d, FuzzPort& port, Input& in, const World& w, uint8_t op) {
  const uint8_t x = in.Byte();
  const Verify v = (x & 0x80U) != 0U ? w.verify : Verify::None;
  const uint8_t before = port.Chip().OnMask();
  const int tampered = port.Tampered();
  const int pin_failures = port.PinFailures();
  // No write lost unseen since the start (hidden state), none lost by the board for this call.
  const bool honest = BoardHonest(port) && !w.hidden_state;
  // A miscount armed for this call may drop its own write: only a driver already reading every
  // switch-off back (degraded) still promises it landed.
  const bool armed = (port.Chip().comer_writes > 0) || (port.Chip().comer_after_writes > 0);
  const bool degraded = d.IsMiscountDetectionDegraded();
  // What the driver could know was on: the chip, unless an earlier miscount it has not seen yet
  // made its copy differ (it learns of it from this call's first fault byte).
  const uint8_t known = MiscountUnseen(d, port) ? d.GetChannelsOn() : before;
  switch (op) {
  case 3: {
    const uint8_t request = in.Byte();
    const Error e = d.SetChannelsOn(request, v).error;
    if (e == Error::Uncertain && !armed && WritesLanded(port, honest, tampered, pin_failures)) {
      CheckUncertainMask(port, request, before, known);
    }
    return e;
  }
  case 4: {
    const auto ch = static_cast<uint8_t>(x % 9U);
    const bool on = (x & 0x40U) != 0U;
    const Error e = d.SetChannel(ch, on, v).error;
    if (!on && ch < 8U && e == Error::Ok && (!armed || degraded) &&
        WritesLanded(port, honest, tampered, pin_failures)) {
      Check(((static_cast<uint32_t>(port.Chip().OnMask()) >> ch) & 1U) ==
            0U); // an off that returns Ok is off
    }
    return e;
  }
  case 5:
    return d
        .SetBridge(static_cast<uint8_t>(x % 5U), static_cast<max22200::BridgeState>(in.Byte() % 5U),
                   v)
        .error;
  default:
    break;
  }
  const auto r = d.ReadChannelsOn();
  Check(r.ok() || r.value == 0U);
  return r.error;
}

Error FuzzSetting(Driver& d, FuzzPort& port, Input& in, const World& w, uint8_t op) {
  const uint8_t x = in.Byte();
  const Verify v = (x & 0x80U) != 0U ? w.verify : Verify::None;
  switch (op) {
  case 7: {
    const auto ch = static_cast<uint8_t>(x % 9U);
    const max22200::ChannelConfig c = FuzzChannel(in);
    const int tampered = port.Tampered();
    const int pin_failures = port.PinFailures();
    const bool honest = BoardHonest(port) && !w.hidden_state && !port.Chip().trig_high;
    const bool armed = (port.Chip().comer_writes > 0) || (port.Chip().comer_after_writes > 0);
    const Error e = d.ConfigureChannel(ch, c, v).error;
    if (e == Error::Ok && ch < 8U && !armed && WritesLanded(port, honest, tampered, pin_failures)) {
      // An Ok mode change is in force: the chip takes VDRnCDR and HSnLS only with every channel
      // off (5c), so the driver may not report Ok while a channel may still be on.
      const max22200::ChannelConfig held =
          max22200::DecodeChannel(port.Chip().regs[reg::CfgChannel(ch)]);
      Check(held.drive == c.drive && held.side == c.side);
    }
    return e;
  }
  case 8:
    return d.SetHoldCode(static_cast<uint8_t>(x % 9U), in.Byte(), v).error;
  case 9:
    return d
        .SetPairMode(static_cast<uint8_t>(x % 5U), static_cast<max22200::PairMode>(in.Byte() % 4U),
                     v)
        .error;
  case 10: {
    const uint8_t m = in.Byte();
    return d
        .SetFaultMask({(m & 1U) != 0U, (m & 2U) != 0U, (m & 4U) != 0U, (m & 8U) != 0U,
                       (m & 16U) != 0U, (m & 32U) != 0U, (m & 64U) != 0U},
                      v)
        .error;
  }
  default:
    break;
  }
  return d
      .SetDpm({in.Byte(), static_cast<uint8_t>(in.Byte() & 0x1FU),
               static_cast<uint8_t>(in.Byte() & 0x1FU)},
              v)
      .error;
}

Error FuzzDiagnostic(Driver& d, Input& in, uint8_t op) {
  switch (op) {
  case 12: {
    const auto r = d.ReadChannelConfig(in.Byte() % 9U);
    Check(r.ok() || r.value == max22200::ChannelConfig{});
    return r.error;
  }
  case 13: {
    const auto r = d.ReadStatus();
    Check(r.ok() || r.value.raw == 0U);
    Check(r.error != Error::DeviceReset || !d.IsInitialized());
    return r.error;
  }
  case 14:
    return d.ReadFaults().error;
  case 15: {
    const uint8_t m = in.Byte();
    const max22200::Faults which = (m & 0x80U) != 0U
                                       ? max22200::Faults::All()
                                       : max22200::Faults{.overcurrent = m, .open_load = in.Byte()};
    return d.ClearFaults(which).error;
  }
  default:
    break;
  }
  const auto r = d.ReadFaultPin();
  Check(r.ok() || !r.value);
  return r.error;
}

void RunOne(Driver& d, FuzzPort& port, Input& in, World& w) {
  const uint8_t op = in.Byte() % 18U;
  if (op <= 2) {
    w.reset_watch = w.reset_watch && (op != 0); // EnsureInitialized() reconfigures
    FuzzLifecycle(d, port, in, w, op);
    return;
  }
  if (op == 17) {
    FuzzBoard(port, in, w);
    return;
  }
  const Before b = Snapshot(d, port);
  Error e = Error::Ok;
  if (op <= 6) {
    e = FuzzSwitch(d, port, in, w, op);
  } else if (op <= 11) {
    e = FuzzSetting(d, port, in, w, op);
  } else {
    e = FuzzDiagnostic(d, in, op);
  }
  NoteBlindMiscount(d, port, w);
  CheckCommon(port, b, e, false, w);
  if (w.reset_watch && port.Tampered() == w.reset_tampered) {
    // A reset chip is never written back to life from the driver's copy.
    Check((port.Chip().regs[reg::kStatus] & reg::kActive) == 0U);
    // A call that reached the chip and succeeded has seen the reset, unless it sent no command.
    Check(!d.IsInitialized() || e != Error::Ok || port.Transfers() == b.transfers ||
          port.Transfers() - b.transfers == 1);
  }
  if (e == Error::WriteMismatch || e == Error::CommunicationError) {
    w.clean = false; // the chip and the driver's copy may differ: that is what it reports
  }
  CheckRegisters(d, port, w);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  FuzzPort port(in);
  World world{};
  const uint8_t setup = in.Byte();
  port.Chip().part = (setup & 0x01U) != 0U ? Part::Max22200 : Part::Max22200A;
  port.Chip().active_needs_uvm_clear = (setup & 0x02U) == 0U; // silicon may take ACTIVE anyway
  port.Chip().comer_survives_enable = (setup & 0x04U) != 0U;  // only a power-on reset clears it
  Driver driver(port, FuzzConfig(in, port.Chip(), world));
  for (int calls = 0; calls < 64 && !in.Empty(); ++calls) {
    RunOne(driver, port, in, world);
  }
  return 0;
}
