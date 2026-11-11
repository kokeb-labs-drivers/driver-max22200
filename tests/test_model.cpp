/**
 * @file test_model.cpp
 * @brief The chip model on its own: its register map against the driver's (two readings of the
 * datasheet that must agree), and the behaviours the driver relies on the model for.
 */
#include <array>
#include <cstdint>

#include "max22200_registers.hpp"
#include "mock_max22200.hpp"
#include "test_harness.hpp"

namespace chip = max22200_test::chip;
namespace reg = max22200::reg;
using max22200_test::Fault;
using max22200_test::Max22200Model;

// The model's map is written from the datasheet separately from inc/max22200_registers.hpp.
static_assert(chip::kBankStatus == reg::kStatus && chip::kBankCfg0 == reg::kCfgCh0 &&
              chip::kBankFault == reg::kFault && chip::kBankDpm == reg::kCfgDpm);
static_assert(chip::kCmdWriteBit == reg::kCmdWrite && chip::kCmd8BitBit == reg::kCmd8Bit);
static_assert(chip::kStatusReset == reg::kStatusReset && chip::kStatusFlags == reg::kFlagsMask);
static_assert(chip::kMaskComfBit == reg::kMaskComf && chip::kFreqmBit == reg::kFreqm);
static_assert(chip::kOvtBit == reg::kOvt && chip::kOcpBit == reg::kOcp &&
              chip::kOlfBit == reg::kOlf && chip::kHhfBit == reg::kHhf &&
              chip::kDpmBit == reg::kDpm && chip::kComerBit == reg::kComer &&
              chip::kUvmBit == reg::kUvm && chip::kActiveBit == reg::kActive);
static_assert(chip::kOnchShift == reg::kOnchShift && chip::kHoldShift == reg::kHoldShift &&
              chip::kHitShift == reg::kHitShift && chip::kHitTShift == reg::kHitTimeShift &&
              chip::kFreqCfgShift == reg::kFreqCfgShift);
static_assert(chip::kHfsBit == reg::kHfs && chip::kTrgBit == reg::kTrgnspi &&
              chip::kVdrBit == reg::kVdrncdr && chip::kHsBit == reg::kHsnls &&
              chip::kSrcBit == reg::kSrc && chip::kOlEnBit == reg::kOlEn &&
              chip::kDpmEnBit == reg::kDpmEn && chip::kHhfEnBit == reg::kHhfEn);
static_assert(chip::kFaultOcpShift == reg::kFaultOcpShift &&
              chip::kFaultHhfShift == reg::kFaultHhfShift &&
              chip::kFaultOlfShift == reg::kFaultOlfShift &&
              chip::kFaultDpmShift == reg::kFaultDpmShift);
static_assert(chip::kDpmUsed == reg::kDpmUsedMask && chip::kEnableUs == reg::kEnableUs &&
              chip::kWakeUpUs == reg::kWakeUpUs);

namespace {

/// A model awake and ready for SPI.
Max22200Model Awake() {
  Max22200Model m;
  m.SetEnable(true);
  m.Elapse(chip::kEnableUs);
  return m;
}

void Command(Max22200Model& m, uint8_t cmd) {
  uint8_t rx = 0;
  m.SetCmd(true);
  m.Transfer(&cmd, &rx, 1);
  m.SetCmd(false);
}

void WriteOnch(Max22200Model& m, uint8_t mask) {
  Command(m, 0x81); // write STATUS, 8 bit
  uint8_t rx = 0;
  m.Transfer(&mask, &rx, 1);
}

} // namespace

TEST_CASE("Model: the command stays in force until it is rewritten (p25)") {
  Max22200Model m = Awake();
  WriteOnch(m, 0x01);
  uint8_t mask = 0x03;
  uint8_t rx = 0;
  m.Transfer(&mask, &rx, 1); // a second data phase, no command
  CHECK_EQ(m.OnMask(), 0x03U);
}

TEST_CASE("Model: a 32-bit write needs its first 32 clocks, then whole bytes (p34)") {
  Max22200Model m = Awake();
  Command(m, 0x82); // write CFG_CH0, 32 bit
  const std::array<uint8_t, 3> short_write = {1, 2, 3};
  std::array<uint8_t, 3> rx{};
  m.Transfer(short_write.data(), rx.data(), short_write.size());
  CHECK((m.regs[chip::kBankStatus] & chip::kComerBit) != 0U);
  CHECK_EQ(m.regs[chip::kBankCfg0], 0U); // ignored
  const std::array<uint8_t, 8> long_write = {9, 9, 9, 9, 0x11, 0x22, 0x33, 0x44};
  std::array<uint8_t, 8> rx8{};
  m.Transfer(long_write.data(), rx8.data(), long_write.size());
  CHECK_EQ(m.regs[chip::kBankCfg0], 0x44332211U); // the last 32 bits stay in the register
}

TEST_CASE("Model: COMER stays set through STATUS reads until ENABLE falls") {
  Max22200Model m = Awake();
  m.regs[chip::kBankStatus] |= chip::kComerBit;
  Command(m, 0x00);
  std::array<uint8_t, 4> tx{};
  std::array<uint8_t, 4> rx{};
  m.Transfer(tx.data(), rx.data(), 4);
  CHECK((m.regs[chip::kBankStatus] & chip::kComerBit) != 0U);
  m.SetEnable(false);
  CHECK_EQ(m.regs[chip::kBankStatus] & chip::kComerBit, 0U);
}

TEST_CASE("Model: switching a channel on clears its OLF and HHF (p21, p23)") {
  Max22200Model m = Awake();
  m.RaiseFault(Fault::Olf, 2);
  m.RaiseFault(Fault::Hhf, 2);
  m.RaiseFault(Fault::Olf, 3);
  WriteOnch(m, 0x04);
  CHECK_EQ(m.regs[chip::kBankFault], 0x00000800U); // channel 3's OLF only
  CHECK((m.regs[chip::kBankStatus] & chip::kOlfBit) != 0U);
  CHECK_EQ(m.regs[chip::kBankStatus] & chip::kHhfBit, 0U);
}

TEST_CASE("Model: an OCP channel stays off until ONCH goes off and on again (p21)") {
  Max22200Model m = Awake();
  Command(m, 0x00); // read STATUS: clears UVM, which would block ACTIVE (p27)
  std::array<uint8_t, 4> sdi{};
  std::array<uint8_t, 4> status{};
  m.Transfer(sdi.data(), status.data(), 4);
  Command(m, 0x80);                                               // write STATUS, 32 bit: ACTIVE
  const std::array<uint8_t, 4> active = {0x01, 0x00, 0x04, 0x01}; // ONCH = 0x01
  std::array<uint8_t, 4> rx{};
  m.Transfer(active.data(), rx.data(), 4);
  m.Elapse(chip::kWakeUpUs);
  CHECK_EQ(m.DrivenMask(), 0x01U);
  m.RaiseFault(Fault::Ocp, 0);
  CHECK_EQ(m.DrivenMask(), 0x00U);
  WriteOnch(m, 0x01); // still on: still off
  CHECK_EQ(m.DrivenMask(), 0x00U);
  WriteOnch(m, 0x00);
  WriteOnch(m, 0x01);
  CHECK_EQ(m.DrivenMask(), 0x01U);
}

TEST_CASE("Model: CFG_DPM's read-only reserved bits read 1 and are not stored") {
  Max22200Model m = Awake();
  Command(m, 0x14); // read CFG_DPM, 32 bit
  std::array<uint8_t, 4> tx{};
  std::array<uint8_t, 4> rx{};
  m.Transfer(tx.data(), rx.data(), 4);
  CHECK_EQ(rx[1], 0x1AU); // bits 20, 19, 17
}

namespace {

/// Write STATUS (32 bit) with ACTIVE = 1 and ONCH = `onch`.
void WriteActive(Max22200Model& m, uint8_t onch) {
  Command(m, 0x80);
  const std::array<uint8_t, 4> tx = {0x01, 0x00, 0x04, onch};
  std::array<uint8_t, 4> rx{};
  m.Transfer(tx.data(), rx.data(), 4);
}

/// The fault byte the next command clocks out.
uint8_t FaultByte(Max22200Model& m) {
  uint8_t cmd = 0x01; // read STATUS, 8 bit: no side effect
  uint8_t rx = 0;
  m.SetCmd(true);
  m.Transfer(&cmd, &rx, 1);
  m.SetCmd(false);
  uint8_t data = 0;
  m.Transfer(&data, &data, 1);
  return rx;
}

} // namespace

TEST_CASE("Model: a power-on reset restores every reset value and forgets the command (p22)") {
  Max22200Model m = Awake();
  Command(m, 0x00); // read STATUS: clears UVM
  std::array<uint8_t, 4> rx{};
  std::array<uint8_t, 4> tx{};
  m.Transfer(tx.data(), rx.data(), 4);
  WriteActive(m, 0x08);
  m.regs[chip::kBankCfg0 + 3] = 0x80U; // VDR
  m.Elapse(chip::kWakeUpUs);
  REQUIRE(m.DrivenMask() == 0x08U);
  m.Brownout();
  CHECK_EQ(m.regs[chip::kBankStatus], chip::kStatusReset);
  CHECK_EQ(m.regs[chip::kBankCfg0 + 3], 0U); // CDR, low side (p38-39)
  CHECK_EQ(m.DrivenMask(), 0U);
  uint8_t data = 0x08;
  uint8_t out = 0;
  m.Transfer(&data, &out, 1); // a data phase with no command: ignored, SDO floats
  CHECK_EQ(out, 0xFFU);
  CHECK_EQ(m.OnMask(), 0U);
  CHECK_EQ(FaultByte(m), 0x02U); // UVM, ACTIVE = 0
}

TEST_CASE("Model: ACTIVE with UVM set is refused, or taken with active_needs_uvm_clear off") {
  for (const bool strict : {true, false}) {
    Max22200Model m = Awake();
    m.active_needs_uvm_clear = strict;
    WriteActive(m, 0x00); // UVM still set from power-up
    CHECK_EQ((m.regs[chip::kBankStatus] & chip::kActiveBit) != 0U, !strict);
    CHECK_EQ(m.rejected_wakes, strict ? 1 : 0);
  }
}

TEST_CASE("Model: a new cycle clears the channel's HHF, and only its (p23)") {
  Max22200Model m = Awake();
  m.RaiseFault(Fault::Hhf, 1);
  m.RaiseFault(Fault::Hhf, 2);
  m.NewCycle(1);
  CHECK_EQ(m.regs[chip::kBankFault], 1U << (chip::kFaultHhfShift + 2U));
  CHECK((m.regs[chip::kBankStatus] & chip::kHhfBit) != 0U);
  m.NewCycle(2);
  CHECK_EQ(m.regs[chip::kBankStatus] & chip::kHhfBit, 0U);
}
