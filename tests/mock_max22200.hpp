/**
 * @file mock_max22200.hpp
 * @brief A protocol model of the MAX22200 and MAX22200A for the host tests, plus the mock ports in
 * mock_max22200_bus.hpp.
 *
 * The model is written from datasheet 19-100531 Rev 1 on its own: it has its own register map and
 * field layout (namespace `chip` below, with page numbers) and does not include the driver's
 * `max22200_registers.hpp`, so a wrong constant in the driver cannot hide behind the same wrong
 * constant in the model. tests/test_model.cpp cross-checks the two maps at compile time.
 *
 * It runs the SPI protocol transfer by transfer:
 *
 * - with CMD high a one-byte transfer is the Command Register; SDO carries STATUS[7:0] (p25, Fig.
 * 7). The command stays in force until it is rewritten (p25), so a data phase may follow another;
 * - with CMD low the transfer is the data phase of the last command: 32 bits written LSB byte first
 *   with SDO = STATUS[7:0], the command, 0, 0 (p31, Figure 10); 32 bits read MSB byte first (p32,
 *   Figure 11); 8-bit writes of bits 31:24 with SDO = STATUS[7:0] (p33, Figure 12); 8-bit reads
 *   (p34, Figure 13);
 * - the clock count of a write is checked (p34): a 32-bit write needs its first 32 clocks, then
 *   whole bytes; an 8-bit write whole bytes. A failed check sets COMER and the write is ignored;
 * - COMER is read only (p36): the worst case is modelled, it stays set until ENABLE falls or a
 *   power-on reset; `comer_clears_on_status_read` models the other reading;
 * - reading STATUS clears OVT and UVM ("Read Clears All", p36); reading FAULT clears the flags
 * whose SDI bit is high on the MAX22200A (SDI LSB byte first, Figure 8, p29) and every flag on the
 *   MAX22200 (p29);
 * - CFG_DPM bits 20, 19 and 17 are read only (p40) with no reset value given: they read 1 here, the
 *   case that breaks a full-register compare; bits 31:15 must be written 0 (p29);
 * - VDRnCDR and HSnLS change only while every channel is off and TRIGA/TRIGB are low (p24); CMxy
 *   only while both channels of the pair are off (p37);
 * - a channel switched on clears its OLF (p21) and starts a new cycle, which clears its HHF (p23);
 * an OCP switches the channel off until its ONCH goes off and on again (p21);
 * - ENABLE low, or less than tEN after it rose (p10), the chip does not answer (SDO pulled up:
 * 0xFF) and the access counts as `early_accesses`; ACTIVE rising starts tWU (p10, `Awake()`);
 * - the worst case of what the datasheet leaves open: a STATUS write that sets ACTIVE is ignored,
 *   and raises COMER, while the supply settles (`wake_settle_us` after ENABLE rises) and while UVM
 *   is set (p27: UVM must be cleared "to enable operations"; `active_needs_uvm_clear = false`
 *   models silicon that takes it anyway); UVM is set again while VM is below UVLO (p22: `vm_low`,
 *   `uvm_sticky_reads`);
 * - the chip changes state on its own, as the datasheet says: a UVLO that ends in a power-on reset
 *   puts every register back at its reset value and forgets the command (`Brownout()`, p22); an
 *   OLF clears when its channel switches on (p21) and an HHF when the channel starts a new cycle
 *   (`NewCycle()`, p23); a write with a wrong clock count is ignored and raises COMER
 *   (`comer_writes`, p34).
 *
 * Faults are injected through the public fields; each is a bus or board condition the port contract
 * allows or the datasheet names (docs/limitations.md).
 */
#ifndef MAX22200_MOCK_MAX22200_HPP
#define MAX22200_MOCK_MAX22200_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace max22200_test {

/// The chip's register map and fields, from the datasheet's Register Map and Register Details.
namespace chip {
// Banks (p35).
inline constexpr uint8_t kBankStatus = 0x00;
inline constexpr uint8_t kBankCfg0 = 0x01; // CFG_CH0..CFG_CH7 at 0x01..0x08
inline constexpr uint8_t kBankFault = 0x09;
inline constexpr uint8_t kBankDpm = 0x0A;
inline constexpr uint8_t kBankCount = 11;
// Command Register (p26, Table 9): bit 7 RB/W (1 = write), bits 6:5 RFU, 4:1 A_BNK, 0 8bit/n32.
inline constexpr uint8_t kCmdWriteBit = 0x80;
inline constexpr uint8_t kCmdRfuBits = 0x60;
inline constexpr uint8_t kCmd8BitBit = 0x01;
// STATUS (p36).
inline constexpr uint32_t kOnchShift = 24;
inline constexpr uint32_t kMaskComfBit = 0x00040000U; // bit 18, reset 1
inline constexpr uint32_t kFreqmBit = 0x00010000U;    // bit 16
inline constexpr uint32_t kOvtBit = 0x80U;            // bit 7, read clears
inline constexpr uint32_t kOcpBit = 0x40U;
inline constexpr uint32_t kOlfBit = 0x20U;
inline constexpr uint32_t kHhfBit = 0x10U;
inline constexpr uint32_t kDpmBit = 0x08U;
inline constexpr uint32_t kComerBit = 0x04U; // read only
inline constexpr uint32_t kUvmBit = 0x02U;   // bit 1, reset 1, read clears
inline constexpr uint32_t kActiveBit = 0x01U;
inline constexpr uint32_t kStatusFlags = 0xFEU;       // bits 7:1 read only
inline constexpr uint32_t kStatusReset = 0x00040002U; // M_COMF = 1, UVM = 1 (p36)
// CFG_CHn (p38-39).
inline constexpr uint32_t kHfsBit = 0x80000000U;
inline constexpr uint32_t kHoldShift = 24;
inline constexpr uint32_t kTrgBit = 0x00800000U;
inline constexpr uint32_t kHitShift = 16;
inline constexpr uint32_t kHitTShift = 8;
inline constexpr uint32_t kVdrBit = 0x80U;
inline constexpr uint32_t kHsBit = 0x40U;
inline constexpr uint32_t kFreqCfgShift = 4;
inline constexpr uint32_t kSrcBit = 0x08U;
inline constexpr uint32_t kOlEnBit = 0x04U;
inline constexpr uint32_t kDpmEnBit = 0x02U;
inline constexpr uint32_t kHhfEnBit = 0x01U;
// FAULT (p40): one byte per type, channel 0 the LSB.
inline constexpr uint32_t kFaultOcpShift = 24;
inline constexpr uint32_t kFaultHhfShift = 16;
inline constexpr uint32_t kFaultOlfShift = 8;
inline constexpr uint32_t kFaultDpmShift = 0;
// CFG_DPM (p40-41).
inline constexpr uint32_t kDpmUsed = 0x00007FFFU;     // ISTART 14:8, TDEB 7:4, IPTH 3:0
inline constexpr uint32_t kDpmReadOnly = 0x001A0000U; // bits 20, 19, 17
// Timing (p10).
inline constexpr uint32_t kEnableUs = 500;
inline constexpr uint32_t kWakeUpUs = 2500;
} // namespace chip

/// What drives SDO.
enum class Miso : uint8_t {
  Chip = 0,      ///< The chip.
  FloatHigh = 1, ///< Nothing: the pull-up reads 0xFF.
  StuckLow = 2,  ///< Shorted to ground: 0x00.
  Noise = 3,     ///< Random bytes.
};

/// Which part: how a FAULT read clears (p29).
enum class Part : uint8_t {
  Max22200A = 0, ///< Selective clear on read.
  Max22200 = 1,  ///< Every read clears every flag.
};

/// A per-channel fault the board can cause.
enum class Fault : uint8_t { Ocp, Hhf, Olf, Dpm };

/// The MAX22200 at the protocol level.
class Max22200Model {
public:
  // ---- Part and board (a test sets these) ----
  Part part = Part::Max22200A;
  Miso miso = Miso::Chip;
  uint16_t noise_state = 0xACE1;
  bool vm_low = false;                      ///< VM below UVLO: UVM set again after every read.
  int uvm_sticky_reads = 0;                 ///< The next n STATUS reads leave UVM set.
  uint32_t wake_settle_us = 0;              ///< ACTIVE writes ignored this long after ENABLE rises.
  bool trig_high = false;                   ///< TRIGA or TRIGB high: VDRnCDR, HSnLS locked.
  bool deaf_sdi = false;                    ///< SDI does not reach the chip: it clocks in 0x00.
  bool comer_clears_on_status_read = false; ///< The other reading of COMER's "Read Only".
  bool active_needs_uvm_clear = true; ///< ACTIVE writes ignored while UVM is set (p27, worst case).
  bool comer_survives_enable = false; ///< COMER kept through ENABLE low too: only a POR clears it.
  bool fault_sdi_msb_first = false;   ///< Selective clear mask MSB byte first (Figure 8 reads LSB).
  int comer_writes = 0;               ///< The next n writes are miscounted: COMER, ignored.
  int comer_after_writes = 0; ///< After this many writes that land, the next one is miscounted.
  uint8_t comer_bank = 0xFF;  ///< Writes to this bank are miscounted ...
  int comer_bank_writes = 0;  ///< ... this many times.
  int drop_writes = 0;        ///< The next n writes are acknowledged but lost.
  uint8_t drop_bank = 0xFF;   ///< Every write to this bank is acknowledged but lost.

  // ---- State (a test inspects it) ----
  std::array<uint32_t, chip::kBankCount> regs{};
  uint8_t command = 0;
  bool command_valid = false;
  bool enable = false;
  bool cmd = false;
  uint64_t now_us = 0;
  uint64_t enabled_at_us = 0;
  uint64_t active_at_us = 0;
  uint8_t ocp_latched = 0; ///< Channels switched off by OCP until ONCH goes off and on again.
  int early_accesses = 0;  ///< SPI while ENABLE was low or within tEN of its rising edge.
  int reserved_writes = 0; ///< Non-zero RFU bits in a command or CFG_DPM[31:15] written.
  int status_reads = 0;    ///< 32-bit STATUS reads.
  int fault_reads = 0;     ///< 32-bit FAULT reads.
  int data_phases = 0;     ///< Data phases taken (each with CMD low).
  int rejected_wakes = 0;  ///< ACTIVE writes ignored while settling or with UVM set.

  Max22200Model() noexcept {
    PowerOn();
  }

  /// Power-on reset: every register at its reset value (p36, p38-39); the command is forgotten.
  void PowerOn() noexcept {
    regs = {};
    regs[chip::kBankStatus] = chip::kStatusReset;
    command = 0;
    command_valid = false;
    ocp_latched = 0;
  }
  /// A UVLO event that ended in a power-on reset (p22): every register at its reset value (STATUS
  /// 0x00040002: ACTIVE = 0, UVM = 1, every ONCH 0; every CFG_CHn 0: CDR, low side), the command
  /// forgotten. ENABLE keeps its level: the chip answers again once V18 is back.
  void Brownout() noexcept {
    PowerOn();
  }
  /// The channel starts a new cycle (an ONCH or TRIG rising edge the driver did not send, a PWM
  /// restart): its HHF clears (p23, "every time the channel initiates a new cycle").
  void NewCycle(uint8_t channel) noexcept {
    regs[chip::kBankFault] &= ~(1U << (chip::kFaultHhfShift + (channel & 7U)));
    updateSummary();
  }
  void Elapse(uint32_t us) noexcept {
    now_us += us;
  }
  void SetEnable(bool high) noexcept {
    if (high && !enable) {
      enabled_at_us = now_us;
    }
    if (!high && enable) {
      if (!comer_survives_enable) {
        regs[chip::kBankStatus] &= ~chip::kComerBit; // only ENABLE or POR clears it
      }
      command_valid = false; // not specified: assume the command is lost
    }
    enable = high;
  }
  void SetCmd(bool high) noexcept {
    cmd = high;
  }
  /// ACTIVE is set and tWU has passed: the outputs follow ONCH.
  [[nodiscard]] bool Awake() const noexcept {
    return (regs[chip::kBankStatus] & chip::kActiveBit) != 0U &&
           now_us - active_at_us >= chip::kWakeUpUs;
  }
  /// The FAULT pin: some flag set whose mask bit (16 bits higher) is 0.
  [[nodiscard]] bool FaultPin() const noexcept {
    const uint32_t s = regs[chip::kBankStatus];
    const uint32_t masked = (s >> 16U) & chip::kStatusFlags;
    return ((s & chip::kStatusFlags) & ~masked) != 0U;
  }
  [[nodiscard]] uint8_t OnMask() const noexcept {
    return static_cast<uint8_t>(regs[chip::kBankStatus] >> chip::kOnchShift);
  }
  /// The channels that really drive their output.
  [[nodiscard]] uint8_t DrivenMask() const noexcept {
    return Awake() ? static_cast<uint8_t>(static_cast<uint32_t>(OnMask()) &
                                          ~static_cast<uint32_t>(ocp_latched))
                   : uint8_t{0};
  }
  static uint32_t FaultShift(Fault f) noexcept {
    switch (f) {
    case Fault::Ocp:
      return chip::kFaultOcpShift;
    case Fault::Hhf:
      return chip::kFaultHhfShift;
    case Fault::Olf:
      return chip::kFaultOlfShift;
    case Fault::Dpm:
    default:
      break;
    }
    return chip::kFaultDpmShift;
  }
  void RaiseFault(Fault f, uint8_t channel) noexcept {
    const uint32_t shift = FaultShift(f);
    regs[chip::kBankFault] |= 1U << (shift + (channel & 7U));
    if (f == Fault::Ocp) {
      ocp_latched = static_cast<uint8_t>(ocp_latched | (1U << (channel & 7U)));
    }
    updateSummary();
  }
  void Overtemperature() noexcept {
    regs[chip::kBankStatus] |= chip::kOvtBit;
  }
  void Undervoltage() noexcept {
    regs[chip::kBankStatus] |= chip::kUvmBit;
  }
  /// What a CFG_DPM read returns: the stored bits and the read-only reserved ones.
  [[nodiscard]] uint32_t DpmReadValue() const noexcept {
    return regs[chip::kBankDpm] | chip::kDpmReadOnly;
  }

  /// One SPI transfer with CSB low throughout, `len` bytes.
  void Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    if (len == 0) {
      return;
    }
    if (!enable || now_us - enabled_at_us < chip::kEnableUs) {
      early_accesses++;
      fill(rx, len, 0xFF);
      return;
    }
    // The chip shifts the bytes in; it keeps the last four (or the last one).
    std::array<uint8_t, 4> in{};
    const std::size_t keep = len < 4 ? len : 4;
    for (std::size_t i = 0; i < keep; ++i) {
      in[i] = deaf_sdi ? uint8_t{0} : tx[len - keep + i];
    }
    std::array<uint8_t, 4> out = {0xFF, 0xFF, 0xFF, 0xFF};
    if (cmd) {
      commandPhase(in, out, len);
    } else {
      dataPhase(in, keep, out, len);
    }
    for (std::size_t i = 0; i < len; ++i) {
      rx[i] = sdo(i < 4 ? out[i] : uint8_t{0x00});
    }
  }

private:
  static void fill(uint8_t* rx, std::size_t len, uint8_t v) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
      rx[i] = v;
    }
  }
  [[nodiscard]] uint8_t statusByte() const noexcept {
    return static_cast<uint8_t>(regs[chip::kBankStatus] & 0xFFU);
  }

  uint8_t sdo(uint8_t byte) noexcept {
    switch (miso) {
    case Miso::Chip:
      return byte;
    case Miso::FloatHigh:
      return 0xFF;
    case Miso::StuckLow:
      return 0x00;
    case Miso::Noise:
    default:
      break;
    }
    const uint32_t n = noise_state;
    const uint32_t bit = ((n >> 0U) ^ (n >> 2U) ^ (n >> 3U) ^ (n >> 5U)) & 1U;
    noise_state = static_cast<uint16_t>((n >> 1U) | (bit << 15U));
    return static_cast<uint8_t>(noise_state);
  }

  void commandPhase(const std::array<uint8_t, 4>& in, std::array<uint8_t, 4>& out,
                    std::size_t len) noexcept {
    out[0] = statusByte();
    if (len != 1) {
      regs[chip::kBankStatus] |= chip::kComerBit;
      return;
    }
    command = in[0];
    command_valid = true;
    if ((in[0] & chip::kCmdRfuBits) != 0U) {
      reserved_writes++;
    }
  }

  void dataPhase(const std::array<uint8_t, 4>& in, std::size_t keep, std::array<uint8_t, 4>& out,
                 std::size_t len) noexcept {
    const auto bank = static_cast<uint8_t>((static_cast<uint32_t>(command) >> 1U) & 0x0FU);
    if (!command_valid || bank >= chip::kBankCount) {
      return;
    }
    data_phases++;
    const bool eight = (command & chip::kCmd8BitBit) != 0U;
    if ((command & chip::kCmdWriteBit) != 0U) {
      write(bank, eight, in, keep, out, len);
    } else {
      read(bank, eight, in, out, len);
    }
  }

  void write(uint8_t bank, bool eight, const std::array<uint8_t, 4>& in, std::size_t keep,
             std::array<uint8_t, 4>& out, std::size_t len) noexcept {
    out[0] = statusByte();
    if (!eight) {
      out[1] = command;
      out[2] = 0;
      out[3] = 0;
    }
    // p34: the first 32 clocks of a 32-bit write, then every 8; every 8 for an 8-bit write.
    const bool counted = eight ? len >= 1 : len >= 4;
    const bool bank_miss = bank == comer_bank && comer_bank_writes > 0;
    if (!counted || comer_writes > 0 || bank_miss) {
      comer_writes = comer_writes > 0 ? comer_writes - 1 : 0;
      comer_bank_writes = bank_miss ? comer_bank_writes - 1 : comer_bank_writes;
      regs[chip::kBankStatus] |= chip::kComerBit;
      return;
    }
    if (comer_after_writes > 0) {
      comer_after_writes--;
      comer_writes = comer_after_writes == 0 ? 1 : comer_writes; // the write after this one
    }
    if (drop_writes > 0 || bank == drop_bank) {
      drop_writes = drop_writes > 0 ? drop_writes - 1 : 0;
      return;
    }
    const uint32_t value =
        eight ? ((regs[bank] & 0x00FFFFFFU) | (static_cast<uint32_t>(in[keep - 1]) << 24U))
              : lsbFirst(in);
    apply(bank, value);
  }

  void read(uint8_t bank, bool eight, const std::array<uint8_t, 4>& in, std::array<uint8_t, 4>& out,
            std::size_t len) noexcept {
    const uint32_t value = bank == chip::kBankDpm ? DpmReadValue() : regs[bank];
    if (eight) {
      out[0] = static_cast<uint8_t>(value >> 24U);
      return;
    }
    if (len < 4) {
      return;
    }
    for (std::size_t i = 0; i < 4; ++i) {
      out[i] = static_cast<uint8_t>(value >> (24U - (8U * i)));
    }
    if (bank == chip::kBankStatus) {
      readStatusSideEffects();
    } else if (bank == chip::kBankFault) {
      fault_reads++;
      const uint32_t sdi = fault_sdi_msb_first ? msbFirst(in) : lsbFirst(in);
      const uint32_t clear = part == Part::Max22200A ? sdi : 0xFFFFFFFFU;
      regs[chip::kBankFault] &= ~clear;
      updateSummary();
    }
  }

  void readStatusSideEffects() noexcept {
    status_reads++;
    uint32_t clear = chip::kOvtBit | chip::kUvmBit;
    if (comer_clears_on_status_read) {
      clear |= chip::kComerBit;
    }
    regs[chip::kBankStatus] &= ~clear;
    if (vm_low || uvm_sticky_reads > 0) {
      regs[chip::kBankStatus] |= chip::kUvmBit;
      uvm_sticky_reads = uvm_sticky_reads > 0 ? uvm_sticky_reads - 1 : 0;
    }
  }

  void apply(uint8_t bank, uint32_t value) noexcept {
    const auto old_on = OnMask();
    if (bank == chip::kBankStatus) {
      applyStatus(value, old_on);
    } else if (bank == chip::kBankFault) {
      return; // read only
    } else if (bank == chip::kBankDpm) {
      if ((value & ~chip::kDpmUsed) != 0U) {
        reserved_writes++;
      }
      regs[bank] = value & ~chip::kDpmReadOnly;
    } else {
      uint32_t v = value;
      if (old_on != 0U || trig_high) {
        const uint32_t locked = chip::kVdrBit | chip::kHsBit;
        v = (v & ~locked) | (regs[bank] & locked);
      }
      regs[bank] = v;
    }
  }

  void applyStatus(uint32_t value, uint8_t old_on) noexcept {
    const uint32_t old = regs[chip::kBankStatus];
    const bool settling = now_us - enabled_at_us < wake_settle_us;
    if ((old & chip::kActiveBit) == 0U && (value & chip::kActiveBit) != 0U &&
        (settling || (active_needs_uvm_clear && (old & chip::kUvmBit) != 0U))) {
      rejected_wakes++;
      regs[chip::kBankStatus] |=
          chip::kComerBit; // the worst case: a refused write counts as miscounted
      return;
    }
    uint32_t v = (value & ~chip::kStatusFlags) | (old & chip::kStatusFlags);
    for (uint8_t p = 0; p < 4U; ++p) {
      const uint32_t field = 0x03U << (8U + (2U * p));
      if ((old_on & (0x03U << (2U * p))) != 0U) {
        v = (v & ~field) | (old & field); // CMxy: only with both channels of the pair off
      }
    }
    if ((old & chip::kActiveBit) == 0U && (v & chip::kActiveBit) != 0U) {
      active_at_us = now_us;
    }
    regs[chip::kBankStatus] = v;
    const auto new_on = static_cast<uint8_t>(v >> chip::kOnchShift);
    const uint32_t now_on = new_on;
    const uint32_t was_on = old_on;
    const auto rising = static_cast<uint8_t>(now_on & ~was_on);
    const auto falling = static_cast<uint8_t>(was_on & ~now_on);
    ocp_latched = static_cast<uint8_t>(static_cast<uint32_t>(ocp_latched) &
                                       ~static_cast<uint32_t>(falling)); // off releases OCP
    // On: OLF clears (p21) and a new cycle clears HHF (p23).
    regs[chip::kBankFault] &= ~((static_cast<uint32_t>(rising) << chip::kFaultOlfShift) |
                                (static_cast<uint32_t>(rising) << chip::kFaultHhfShift));
    updateSummary();
  }

  void updateSummary() noexcept {
    const uint32_t f = regs[chip::kBankFault];
    uint32_t s =
        regs[chip::kBankStatus] & ~(chip::kOcpBit | chip::kHhfBit | chip::kOlfBit | chip::kDpmBit);
    s |= ((f >> chip::kFaultOcpShift) & 0xFFU) != 0U ? chip::kOcpBit : 0U;
    s |= ((f >> chip::kFaultHhfShift) & 0xFFU) != 0U ? chip::kHhfBit : 0U;
    s |= ((f >> chip::kFaultOlfShift) & 0xFFU) != 0U ? chip::kOlfBit : 0U;
    s |= (f & 0xFFU) != 0U ? chip::kDpmBit : 0U;
    regs[chip::kBankStatus] = s;
  }

  static uint32_t msbFirst(const std::array<uint8_t, 4>& b) noexcept {
    return (static_cast<uint32_t>(b[0]) << 24U) | (static_cast<uint32_t>(b[1]) << 16U) |
           (static_cast<uint32_t>(b[2]) << 8U) | static_cast<uint32_t>(b[3]);
  }
  static uint32_t lsbFirst(const std::array<uint8_t, 4>& b) noexcept {
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8U) |
           (static_cast<uint32_t>(b[2]) << 16U) | (static_cast<uint32_t>(b[3]) << 24U);
  }
};

} // namespace max22200_test

#include "mock_max22200_bus.hpp"

#endif // MAX22200_MOCK_MAX22200_HPP
