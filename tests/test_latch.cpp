/**
 * @file test_latch.cpp
 * @brief The driver-owned latch of clear-on-read flags (handbook KB-CPP-119), the faults latched
 * before the start (KB-CPP-058), the raw register read (KB-CPP-129), the CMxy decode
 * (KB-CPP20-025) and the declared loads (KB-CPP-131).
 */
#include <array>
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"
#include "test_harness.hpp"

using max22200::ChannelConfig;
using max22200::Config;
using max22200::Error;
using max22200::Faults;
using max22200::PairMode;
using max22200::Variant;
using max22200::Verify;
using max22200_test::Fault;
using max22200_test::FullPort;
using max22200_test::MinimalPort;
using max22200_test::Part;
namespace reg = max22200::reg;

namespace {

using Driver = max22200::MAX22200<MinimalPort>;
using FullDriver = max22200::MAX22200<FullPort>;

/// The bit of `channel` in a FAULT byte.
constexpr uint8_t Bit(uint8_t channel) {
  return static_cast<uint8_t>(uint32_t{1} << channel);
}

} // namespace

// -------------------------------------------------------------------------------------------------
// OVT and UVM: every STATUS read feeds the latch
// -------------------------------------------------------------------------------------------------

TEST_CASE("Latch: a STATUS read-back keeps the OVT it clears (Register Details, Read Clears All)") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(!d.GetLatchedFaults().Any());
  port.state.chip.Overtemperature();
  const int t = port.state.transfers;
  REQUIRE(d.SetFaultMask(d.GetConfig().fault_mask, Verify::ReadBack).ok());
  CHECK_EQ(port.state.transfers - t, 4);                        // the latch costs no transfer
  CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kOvt, 0U); // the chip's flag is gone
  CHECK(d.GetLatchedFaults().overtemperature);                  // the driver kept it
  CHECK(!d.GetLatchedFaults().undervoltage);
  d.ClearLatchedFaults();
  CHECK(!d.GetLatchedFaults().Any());
  CHECK_EQ(port.state.transfers - t, 4); // Get and Clear do no I/O
}

TEST_CASE("Latch: ReadStatus returns UVM and the latch keeps it until ClearLatchedFaults") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(!d.GetLatchedFaults().undervoltage); // the power-on UVM of the start-up is not news
  port.state.chip.Undervoltage();
  const auto s = d.ReadStatus();
  REQUIRE(s.ok());
  CHECK(s.value.undervoltage);
  CHECK(d.GetLatchedFaults().undervoltage);
  REQUIRE(d.ReadStatus().ok()); // cleared on the chip by the first read
  CHECK(d.GetLatchedFaults().undervoltage);
  d.ClearLatchedFaults();
  CHECK(!d.GetLatchedFaults().undervoltage);
}

TEST_CASE("Latch: an OVT seen by the start-up's STATUS reads is kept") {
  MinimalPort port;
  port.state.chip.Overtemperature();
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(d.GetLatchedFaults().overtemperature);
  CHECK(!d.GetLatchedFaults().undervoltage);
}

TEST_CASE("Latch: a raw STATUS read feeds it too; the hot path latches its fault byte for free") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.Overtemperature();
  REQUIRE(d.SetChannelsOn(0x01).ok());
  const int t = port.state.transfers;
  REQUIRE(d.SetChannelsOn(0x03).ok());
  CHECK_EQ(port.state.transfers - t, 1);                         // still one transfer
  CHECK((port.state.chip.regs[reg::kStatus] & reg::kOvt) != 0U); // ONCH writes clear nothing
  CHECK((d.GetLastStatusByte() & reg::kOvt) != 0U);              // the fault byte shows it
  CHECK(d.GetLatchedFaults().overtemperature);                   // and the latch keeps it
  d.ClearLatchedFaults();
  const auto raw = d.ReadRegister(reg::kStatus);
  REQUIRE(raw.ok());
  CHECK((raw.value & reg::kOvt) != 0U);
  CHECK(d.GetLatchedFaults().overtemperature);
}

// -------------------------------------------------------------------------------------------------
// Faults latched before the start (the chip keeps FAULT across ENABLE)
// -------------------------------------------------------------------------------------------------

TEST_CASE("Start-up clears and reports faults latched before it (KB-CPP-058)") {
  FullPort port;
  port.state.chip.RaiseFault(Fault::Olf, 2);
  port.state.chip.RaiseFault(Fault::Ocp, 5);
  REQUIRE(port.state.chip.FaultPin()); // held low by the old faults before the start
  FullDriver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U);
  CHECK(!port.state.chip.FaultPin());
  const auto pin = d.ReadFaultPin();
  REQUIRE(pin.ok());
  CHECK(!pin.value); // the FAULT pin shows new faults only
  const Faults old = d.GetStartupFaults();
  CHECK_EQ(old.open_load, Bit(2));
  CHECK_EQ(old.overcurrent, Bit(5));
  CHECK_EQ(old.hit_not_reached, 0U);
  CHECK(d.GetLatchedFaults().faults == old);
  CHECK_EQ(port.state.chip.fault_reads, 1);
  CHECK(port.error_lines == 0); // a warning, not a failure
}

TEST_CASE("Start-up faults: none found, the report is empty; kept until the next start") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(!d.GetStartupFaults().Any());
  REQUIRE(d.Deinitialize().ok());
  port.state.chip.RaiseFault(Fault::Dpm, 0);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(d.GetStartupFaults().plunger, Bit(0));
  d.ClearLatchedFaults();
  CHECK_EQ(d.GetStartupFaults().plunger, Bit(0)); // the report is not the latch
}

TEST_CASE("Start-up faults on the MAX22200A: every SDI bit high clears every flag (Figure 8)") {
  MinimalPort port;
  port.state.chip.part = Part::Max22200A;
  port.state.chip.RaiseFault(Fault::Hhf, 7);
  Config c{};
  c.variant = Variant::Max22200A;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U);
  CHECK_EQ(d.GetStartupFaults().hit_not_reached, Bit(7));
}

TEST_CASE("Start-up faults: a failed FAULT read fails the start with Bus") {
  MinimalPort probe;
  Driver p(probe);
  REQUIRE(p.EnsureInitialized().ok());
  // The FAULT read is the command 0x12 near the end; fail its data phase.
  int index = -1;
  for (int i = 0; i < probe.state.transfers; ++i) {
    if (probe.state.with_cmd[static_cast<std::size_t>(i)] &&
        probe.state.first_byte[static_cast<std::size_t>(i)] == 0x12U) {
      index = i + 1;
    }
  }
  REQUIRE(index > 0);
  MinimalPort port;
  port.state.fail_transfer = index;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::Bus);
  CHECK(!d.IsInitialized());
}

// -------------------------------------------------------------------------------------------------
// FAULT reads feed the latch with what they clear
// -------------------------------------------------------------------------------------------------

TEST_CASE("Latch: on the MAX22200 every FAULT read clears every flag, so the latch keeps them") {
  MinimalPort port;
  port.state.chip.part = Part::Max22200;
  Driver d(port); // Variant::Max22200, the default
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(Fault::Olf, 3);
  const auto f = d.ReadFaults();
  REQUIRE(f.ok());
  CHECK_EQ(f.value.open_load, Bit(3));
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U);
  CHECK_EQ(d.GetLatchedFaults().faults.open_load, Bit(3));
  port.state.chip.RaiseFault(Fault::Ocp, 1);
  const auto raw = d.ReadRegister(reg::kFault);
  REQUIRE(raw.ok());
  CHECK_EQ(raw.value, uint32_t{Bit(1)} << reg::kFaultOcpShift);
  CHECK_EQ(d.GetLatchedFaults().faults.overcurrent, Bit(1));
}

TEST_CASE("Latch: on the MAX22200A only the flags a selective clear clears are latched") {
  MinimalPort port;
  port.state.chip.part = Part::Max22200A;
  Config c{};
  c.variant = Variant::Max22200A;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(Fault::Olf, 3);
  port.state.chip.RaiseFault(Fault::Olf, 4);
  REQUIRE(d.ReadFaults().ok()); // SDI 0: clears nothing
  CHECK(!d.GetLatchedFaults().faults.Any());
  const auto f = d.ClearFaults(Faults{.open_load = Bit(4)});
  REQUIRE(f.ok());
  CHECK_EQ(f.value.open_load, static_cast<uint8_t>(Bit(3) | Bit(4)));
  CHECK_EQ(d.GetLatchedFaults().faults.open_load, Bit(4));
  CHECK_EQ(port.state.chip.regs[reg::kFault], uint32_t{Bit(3)} << reg::kFaultOlfShift);
}

// -------------------------------------------------------------------------------------------------
// Raw access (KB-CPP-129)
// -------------------------------------------------------------------------------------------------

TEST_CASE(
    "ReadRegister: one 32-bit read, no retry; NotInitialized and InvalidArgument send nothing") {
  MinimalPort port;
  Driver d(port);
  CHECK_EQ(d.ReadRegister(reg::kStatus).error, Error::NotInitialized);
  CHECK_EQ(port.state.transfers, 0);
  REQUIRE(d.EnsureInitialized().ok());
  int t = port.state.transfers;
  CHECK_EQ(d.ReadRegister(static_cast<uint8_t>(reg::kCfgDpm + 1U)).error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, t);
  const auto cfg = d.ReadRegister(reg::CfgChannel(4));
  REQUIRE(cfg.ok());
  CHECK_EQ(cfg.value, port.state.chip.regs[reg::CfgChannel(4)]);
  CHECK_EQ(port.state.transfers - t, 2);
  t = port.state.transfers;
  port.state.fail_transfer = t + 1;
  CHECK_EQ(d.ReadRegister(reg::kCfgDpm).error, Error::Bus);
  CHECK_EQ(port.state.transfers - t, 2); // no retry
  CHECK(d.IsInitialized());
}

// -------------------------------------------------------------------------------------------------
// CMxy = 3 is no mode (KB-CPP20-025)
// -------------------------------------------------------------------------------------------------

TEST_CASE("A STATUS read-back with CMxy = 3 is a mismatch and never stored as a PairMode") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  Config next = d.GetConfig();
  next.pairs[0] = PairMode::Parallel;
  // The read-back's data phase (the 4th transfer of the call) carries CM76 = 3.
  const uint32_t forged = max22200::EncodeStatus(next, 0, true) | (0x03U << reg::PairModeShift(3));
  port.state.replace_transfer = port.state.transfers + 3;
  port.state.replace_bytes = {static_cast<uint8_t>(forged >> 24U),
                              static_cast<uint8_t>(forged >> 16U),
                              static_cast<uint8_t>(forged >> 8U), static_cast<uint8_t>(forged)};
  CHECK_EQ(d.SetPairMode(0, PairMode::Parallel, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(d.GetConfig().pairs[0], PairMode::Parallel);    // read as written
  CHECK_EQ(d.GetConfig().pairs[3], PairMode::Independent); // 3 refused: the copy keeps its mode
  CHECK_EQ(max22200::Validate(d.GetConfig()), Error::Ok);
  CHECK_EQ(max22200::DecodePairMode(forged, 3).error, Error::WriteMismatch);
}

// -------------------------------------------------------------------------------------------------
// Load-dependent diagnostics only on declared loads (KB-CPP-131)
// -------------------------------------------------------------------------------------------------

TEST_CASE("Validate refuses a load diagnostic on a channel without a declared load") {
  Config c{};
  c.channels[3].open_load_detect = true;
  CHECK_EQ(max22200::Validate(c), Error::InvalidArgument);
  c.loaded_channels = Bit(3);
  CHECK_EQ(max22200::Validate(c), Error::Ok);
  c.channels[2].plunger_detect = true;
  CHECK_EQ(max22200::Validate(c), Error::InvalidArgument);
  c.channels[2].plunger_detect = false;
  c.channels[1].hit_current_check = true;
  CHECK_EQ(max22200::Validate(c), Error::InvalidArgument);
  c.loaded_channels = static_cast<uint8_t>(Bit(1) | Bit(3));
  CHECK_EQ(max22200::Validate(c), Error::Ok);
  CHECK(max22200::ValidLoadDiagnostics(ChannelConfig{}, 0, 0)); // no diagnostic: always valid
}

TEST_CASE("ConfigureChannel refuses a load diagnostic on an unloaded channel and sends nothing") {
  MinimalPort port;
  Config c{};
  c.loaded_channels = Bit(0);
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  ChannelConfig v{};
  v.open_load_detect = true;
  const int t = port.state.transfers;
  CHECK_EQ(d.ConfigureChannel(1, v).error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, t);
  CHECK(d.ConfigureChannel(0, v).ok());
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(0)] & reg::kOlEn, reg::kOlEn);
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(1)] & reg::kOlEn, 0U);
}

TEST_CASE("Unloaded channels: no load diagnostic is armed and the FAULT pin stays released") {
  FullPort port;
  Config c{};
  c.loaded_channels = Bit(0);
  c.channels[0].open_load_detect = true;
  FullDriver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  for (uint8_t ch = 1; ch < max22200::kChannels; ++ch) {
    const uint32_t diag = reg::kOlEn | reg::kDpmEn | reg::kHhfEn;
    CHECK_EQ(port.state.chip.regs[reg::CfgChannel(ch)] & diag, 0U);
  }
  const auto pin = d.ReadFaultPin();
  REQUIRE(pin.ok());
  CHECK(!pin.value);
}

TEST_CASE("ApplyConfig on a running driver keeps the new loads, so the copy stays restartable") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  Config c = d.GetConfig();
  c.loaded_channels = Bit(3);
  c.wake_timeout_us = 20000;
  c.channels[3].open_load_detect = true;
  REQUIRE(d.ApplyConfig(c).ok());
  CHECK_EQ(d.GetConfig().loaded_channels, Bit(3));
  CHECK_EQ(d.GetConfig().wake_timeout_us, 20000U);
  CHECK_EQ(max22200::Validate(d.GetConfig()), Error::Ok);
  CHECK(d.ConfigureChannel(3, c.channels[3]).ok());
  REQUIRE(d.Deinitialize().ok());
  CHECK(d.EnsureInitialized().ok());
}

TEST_CASE("Latch: a floating SDO is not data, so nothing is latched (KB-CPP-116)") {
  MinimalPort port;
  port.state.chip.miso = max22200_test::Miso::FloatHigh;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::NoDevice);
  CHECK(!d.GetLatchedFaults().Any());
  port.state.chip.miso = max22200_test::Miso::Chip;
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.miso = max22200_test::Miso::FloatHigh;
  // No flag is taken from an all-ones reply. The MAX22200's FAULT read clears every flag, and a
  // STATUS read clears OVT and UVM: what they held is unknown now, and the latch says so.
  CHECK(d.ReadFaults().ok());
  max22200::LatchedFaults l = d.GetLatchedFaults();
  CHECK(!l.overtemperature && !l.undervoltage && !l.faults.Any() && !l.open_load);
  CHECK(l.lost);
  d.ClearLatchedFaults();
  CHECK(d.ReadRegister(reg::kStatus).ok());
  l = d.GetLatchedFaults();
  CHECK(!l.overtemperature && !l.undervoltage && !l.faults.Any());
  CHECK(l.lost);
}

TEST_CASE(
    "Latch: a floating FAULT read on the MAX22200A with SDI 0 clears nothing, loses nothing") {
  MinimalPort port;
  Config cfg;
  cfg.variant = Variant::Max22200A;
  Driver d(port, cfg);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.miso = max22200_test::Miso::FloatHigh;
  CHECK(d.ReadFaults().ok());
  CHECK(!d.GetLatchedFaults().Any());
  CHECK(d.ClearFaults(Faults{.overcurrent = 0x01}).ok()); // this one clears: OCP0 is unknown
  CHECK(d.GetLatchedFaults().lost);
}

TEST_CASE("Latch: a restart does not empty it; the UVM of a power-on reset survives") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.Brownout();
  CHECK_EQ(d.ReadStatus().error, Error::DeviceReset);
  CHECK(d.GetLatchedFaults().undervoltage);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(d.GetLatchedFaults().undervoltage);
  d.ClearLatchedFaults();
  CHECK(!d.GetLatchedFaults().Any());
}
