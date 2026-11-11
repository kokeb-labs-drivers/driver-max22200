/**
 * @file test_reset.cpp
 * @brief State the chip changes on its own: a power-on reset (UVLO, p22), flags it clears by
 * itself (OLF on a switch-on, p21; HHF on a new cycle, p23), a miscounted write (COMER, p34), and
 * the OCP latch that only an off-on edge releases (p21).
 */
#include <array>
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"
#include "test_harness.hpp"

using max22200::ChannelConfig;
using max22200::Config;
using max22200::DriveMode;
using max22200::Error;
using max22200::Faults;
using max22200::Verify;
using max22200_test::Fault;
using max22200_test::MinimalPort;
namespace reg = max22200::reg;

namespace {

using Driver = max22200::MAX22200<MinimalPort>;

/// Channel 3 in VDR, so a reset (CDR, low side) shows in CFG_CH3.
Config VdrOnThree() {
  Config c;
  c.channels[3].drive = DriveMode::Vdr;
  c.channels[3].hit = 60;
  c.channels[3].hold = 30;
  c.channels[3].hit_time = 10;
  return c;
}

/// A running driver with channel 3 on and driving, then a power-on reset of the chip.
void RunThenReset(Driver& d, MinimalPort& port, bool active_needs_uvm_clear) {
  port.state.chip.active_needs_uvm_clear = active_needs_uvm_clear;
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(3, true).ok());
  port.DelayUs(reg::kWakeUpUs);
  REQUIRE(port.state.chip.DrivenMask() == 0x08U);
  port.state.chip.Brownout();
}

/// The chip is as the reset left it: asleep, nothing on, CFG_CH3 at its reset value.
bool StillReset(const MinimalPort& port) {
  const auto& chip = port.state.chip;
  return ((chip.regs[reg::kStatus] & reg::kActive) == 0U) && (chip.OnMask() == 0U) &&
         (chip.DrivenMask() == 0U) && (chip.regs[reg::CfgChannel(3)] == 0U);
}

using Op = Error (*)(Driver&);

/// Every call that touches the chip.
constexpr std::array<Op, 17> kOps = {
    [](Driver& d) { return d.SetChannelsOn(0x08).error; },
    [](Driver& d) { return d.SetChannel(3, true).error; },
    [](Driver& d) { return d.SetChannel(3, true, Verify::ReadBack).error; },
    [](Driver& d) { return d.ReadChannelsOn().error; },
    [](Driver& d) { return d.ConfigureChannel(3, d.GetConfig().channels[3]).error; },
    [](Driver& d) { return d.SetHoldCode(3, 30).error; },
    [](Driver& d) { return d.SetPairMode(0, max22200::PairMode::Independent).error; },
    [](Driver& d) { return d.SetFaultMask(d.GetConfig().fault_mask).error; },
    [](Driver& d) { return d.SetFaultMask(d.GetConfig().fault_mask, Verify::ReadBack).error; },
    [](Driver& d) { return d.SetDpm(d.GetConfig().dpm).error; },
    [](Driver& d) { return d.ReadChannelConfig(3).error; },
    [](Driver& d) { return d.ReadStatus().error; },
    [](Driver& d) { return d.ReadFaults().error; },
    [](Driver& d) { return d.ClearFaults().error; },
    [](Driver& d) { return d.ReadRegister(reg::kStatus).error; },
    [](Driver& d) { return d.ReadRegister(reg::kFault).error; },
    [](Driver& d) {
      Config c = d.GetConfig();
      c.fault_mask.open_load = !c.fault_mask.open_load;
      return d.ApplyConfig(c).error;
    },
};

} // namespace

// -------------------------------------------------------------------------------------------------
// M1: a power-on reset is detected before anything is written to the reset chip
// -------------------------------------------------------------------------------------------------

TEST_CASE("Reset: SetFaultMask after a POR returns DeviceReset and writes nothing (both models)") {
  for (const bool strict : {true, false}) {
    MinimalPort port;
    Driver d(port, VdrOnThree());
    RunThenReset(d, port, strict);
    const int phases = port.state.chip.data_phases;
    CHECK_EQ(d.SetFaultMask(d.GetConfig().fault_mask).error, Error::DeviceReset);
    CHECK_EQ(port.state.chip.data_phases, phases); // the command byte told: no data went out
    CHECK(!d.IsInitialized());
    CHECK_EQ(d.GetChannelsOn(), 0U);
    port.DelayUs(reg::kWakeUpUs);
    CHECK(StillReset(port));
    CHECK(d.GetLatchedFaults().undervoltage); // the reset's UVM came in the fault byte
  }
}

TEST_CASE("Reset: a raw STATUS read after a POR is DeviceReset; nothing re-activates the chip") {
  for (const bool strict : {true, false}) {
    MinimalPort port;
    Driver d(port, VdrOnThree());
    RunThenReset(d, port, strict);
    CHECK_EQ(d.ReadRegister(reg::kStatus).error, Error::DeviceReset);
    CHECK_EQ(d.SetFaultMask(d.GetConfig().fault_mask).error, Error::NotInitialized);
    CHECK_EQ(d.SetHoldCode(3, 30).error, Error::NotInitialized);
    port.DelayUs(reg::kWakeUpUs);
    CHECK(StillReset(port));
  }
}

TEST_CASE("Reset: a read-back after a POR leaves the driver uninitialised, so no retry writes") {
  for (const bool strict : {true, false}) {
    MinimalPort port;
    Driver d(port);
    RunThenReset(d, port, strict);
    CHECK_EQ(d.SetFaultMask(d.GetConfig().fault_mask, Verify::ReadBack).error, Error::DeviceReset);
    CHECK(!d.IsInitialized());
    CHECK_EQ(d.SetChannel(3, true).error, Error::NotInitialized);
    CHECK_EQ(d.SetFaultMask(d.GetConfig().fault_mask).error, Error::NotInitialized);
    port.DelayUs(reg::kWakeUpUs);
    CHECK_EQ(port.state.chip.DrivenMask(), 0U);
    CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kActive, 0U);
  }
}

TEST_CASE("Reset: every call after a POR returns DeviceReset and sends no data phase") {
  for (const bool strict : {true, false}) {
    for (const Op op : kOps) {
      MinimalPort port;
      Driver d(port, VdrOnThree());
      RunThenReset(d, port, strict);
      const int phases = port.state.chip.data_phases;
      CHECK_EQ(op(d), Error::DeviceReset);
      CHECK_EQ(port.state.chip.data_phases, phases);
      CHECK(!d.IsInitialized());
      const Error again = op(d); // ApplyConfig() only stores a configuration now
      CHECK((again == Error::NotInitialized) || (again == Error::Ok));
      CHECK_EQ(port.state.chip.data_phases, phases);
      port.DelayUs(reg::kWakeUpUs);
      CHECK(StillReset(port));
    }
  }
}

TEST_CASE("Reset: the one-transfer ONCH write after a POR gets no fault byte and asks again") {
  MinimalPort port;
  Driver d(port, VdrOnThree());
  RunThenReset(d, port, true);
  // The last access was the ONCH write: the next one is the data byte alone. The reset chip has
  // no command and answers 0xFF; the command byte sent again shows the reset.
  const int t = port.state.transfers;
  CHECK_EQ(d.SetChannelsOn(0x09).error, Error::DeviceReset);
  CHECK_EQ(port.state.transfers - t, 2);
  CHECK(!d.IsInitialized());
  CHECK_EQ(d.GetChannelsOn(), 0U);
  port.DelayUs(reg::kWakeUpUs);
  CHECK(StillReset(port)); // the reset chip forgot the command: the byte did nothing
}

TEST_CASE("Reset: ACTIVE = 0 in the STATUS data (a POR after the command byte) is DeviceReset") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.replace_transfer = port.state.transfers + 1; // the data of the STATUS read
  port.state.replace_bytes = {0x00, 0x04, 0x00, 0x02};    // the reset value, MSB byte first
  CHECK_EQ(d.ReadStatus().error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
}

TEST_CASE("Reset: EnsureInitialized after DeviceReset restores every register") {
  for (const bool strict : {true, false}) {
    MinimalPort port;
    const Config cfg = VdrOnThree();
    Driver d(port, cfg);
    RunThenReset(d, port, strict);
    CHECK_EQ(d.SetChannel(3, true).error, Error::DeviceReset);
    REQUIRE(d.EnsureInitialized().ok());
    CHECK_EQ(port.state.chip.regs[reg::CfgChannel(3)], max22200::EncodeChannel(cfg.channels[3]));
    CHECK_EQ(port.state.chip.OnMask(), 0U); // every channel off until the application says
    REQUIRE(d.SetChannel(3, true).ok());
    port.DelayUs(reg::kWakeUpUs);
    CHECK_EQ(port.state.chip.DrivenMask(), 0x08U);
  }
}

TEST_CASE("Reset: an SDO stuck low on a running driver stops it (fail-safe), and no start passes") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(1, true).ok());
  port.state.chip.miso = max22200_test::Miso::StuckLow; // 0x00 reads as ACTIVE = 0
  CHECK_EQ(d.ReadFaults().error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
  CHECK(d.EnsureInitialized().error != Error::Ok);
}

TEST_CASE("Reset: a floating fault byte (0xFF) is not a reset") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.miso = max22200_test::Miso::FloatHigh;
  CHECK(d.SetChannel(1, true).ok());
  CHECK(d.IsInitialized());
}

TEST_CASE("Reset: Deinitialize after a POR still puts the chip to sleep") {
  MinimalPort port;
  Driver d(port);
  RunThenReset(d, port, true);
  CHECK(d.Deinitialize().ok());
  CHECK(!port.state.chip.enable);
}

// -------------------------------------------------------------------------------------------------
// M2: flags the chip clears by itself still reach the latch
// -------------------------------------------------------------------------------------------------

TEST_CASE("Self-clearing: an OLF the switch-on clears is latched from the fault byte (p21)") {
  MinimalPort port;
  Config cfg;
  cfg.loaded_channels = 0xFF;
  cfg.channels[2].open_load_detect = true;
  Driver d(port, cfg);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(Fault::Olf, 2);
  const int t = port.state.transfers;
  REQUIRE(d.SetChannel(2, true).ok());
  CHECK_EQ(port.state.transfers - t, 2);           // no extra I/O
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U); // the chip cleared it
  CHECK(d.GetLatchedFaults().open_load);           // the latch did not lose it
  REQUIRE(d.SetChannel(5, true).ok());
  const auto f = d.ReadFaults();
  REQUIRE(f.ok());
  CHECK_EQ(f.value.open_load, 0U);       // too late for the per-channel flag ...
  CHECK(d.GetLatchedFaults().open_load); // ... the summary stays until ClearLatchedFaults()
  d.ClearLatchedFaults();
  CHECK(!d.GetLatchedFaults().Any());
}

TEST_CASE("Self-clearing: an HHF a new cycle clears is latched by any access in between (p23)") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(1, true).ok());
  port.state.chip.RaiseFault(Fault::Hhf, 1);
  REQUIRE(d.SetChannel(4, true).ok()); // the one-transfer write: its fault byte shows HHF
  port.state.chip.NewCycle(1);
  const auto f = d.ReadFaults();
  REQUIRE(f.ok());
  CHECK_EQ(f.value.hit_not_reached, 0U);
  CHECK(d.GetLatchedFaults().hit_not_reached);
  CHECK(!d.GetLatchedFaults().faults.Any());
}

TEST_CASE("Self-clearing: OCP and DPM summary flags are latched too") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(Fault::Dpm, 6);
  port.state.chip.RaiseFault(Fault::Ocp, 0);
  REQUIRE(d.ReadChannelsOn().ok());
  CHECK(d.GetLatchedFaults().plunger);
  CHECK(d.GetLatchedFaults().overcurrent);
}

TEST_CASE("OCP: only an off-on edge of that channel re-arms it; other calls do not (p21)") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x08).ok());
  port.DelayUs(reg::kWakeUpUs);
  port.state.chip.RaiseFault(Fault::Ocp, 3);
  CHECK_EQ(port.state.chip.OnMask(), 0x08U);
  CHECK_EQ(port.state.chip.DrivenMask(), 0U);
  REQUIRE(d.SetChannel(5, false).ok());
  REQUIRE(d.SetChannel(5, true).ok());
  REQUIRE(d.SetFaultMask(d.GetConfig().fault_mask).ok());
  REQUIRE(d.ClearFaults().ok());
  REQUIRE(d.SetPairMode(0, max22200::PairMode::Independent).ok());
  CHECK_EQ(port.state.chip.DrivenMask(), 0x20U); // channel 3 stays off
  CHECK(d.GetLatchedFaults().overcurrent);
  CHECK_EQ(d.GetLatchedFaults().faults.overcurrent, 0x08U);
  REQUIRE(d.SetChannel(3, false).ok());
  REQUIRE(d.SetChannel(3, true).ok());
  CHECK_EQ(port.state.chip.DrivenMask(), 0x28U);
}

// -------------------------------------------------------------------------------------------------
// M3: a miscounted write is reported and its bits are in doubt
// -------------------------------------------------------------------------------------------------

TEST_CASE("COMER: a miscounted switch-off fails the next call and its channel counts as on") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x08).ok());
  port.DelayUs(reg::kWakeUpUs);
  port.state.chip.comer_writes = 1;
  CHECK(d.SetChannel(3, false).ok()); // the write cannot see its own miscount (p34)
  CHECK_EQ(port.state.chip.DrivenMask(), 0x08U);
  CHECK_EQ(d.ReadFaults().error, Error::CommunicationError);
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  CHECK_EQ(d.ConfigureChannel(3, v).error, Error::ChannelsActive); // channel 3 may be on
  CHECK(d.ReadFaults().ok());                                      // reported once
  REQUIRE(d.SetChannel(3, false).ok());                            // the full mask again
  CHECK_EQ(port.state.chip.DrivenMask(), 0U);
}

TEST_CASE("COMER: a miscounted CFG_CH write is rewritten by ApplyConfig") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  ChannelConfig c{};
  c.hold = 9;
  port.state.chip.comer_writes = 1;
  CHECK(d.ConfigureChannel(1, c).ok());
  CHECK(port.state.chip.regs[reg::CfgChannel(1)] != max22200::EncodeChannel(c));
  CHECK_EQ(d.ReadStatus().error, Error::CommunicationError);
  REQUIRE(d.ApplyConfig(d.GetConfig()).ok()); // the copy matches, the chip may not
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(1)], max22200::EncodeChannel(c));
}

TEST_CASE("COMER: a miscounted STATUS or CFG_DPM write is rewritten by ApplyConfig") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  max22200::FaultMask m = d.GetConfig().fault_mask;
  m.open_load = true;
  port.state.chip.comer_writes = 1;
  CHECK(d.SetFaultMask(m).ok());
  max22200::DpmConfig dpm{};
  dpm.threshold = 3;
  // Its command byte shows the COMER: a configuration write stops before its data phase.
  CHECK_EQ(d.SetDpm(dpm).error, Error::CommunicationError);
  CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kMaskOlf, 0U);
  CHECK_EQ(port.state.chip.regs[reg::kCfgDpm], 0U); // not sent
  CHECK_EQ(d.GetConfig().dpm.threshold, 0U);        // and the copy did not take it
  REQUIRE(d.ApplyConfig(d.GetConfig()).ok());
  CHECK((port.state.chip.regs[reg::kStatus] & reg::kMaskOlf) != 0U);
  REQUIRE(d.SetDpm(dpm).ok());
  CHECK_EQ(port.state.chip.regs[reg::kCfgDpm], max22200::EncodeDpm(dpm));

  MinimalPort port2;
  Driver d2(port2);
  REQUIRE(d2.EnsureInitialized().ok());
  port2.state.chip.comer_writes = 1;
  CHECK(d2.SetDpm(dpm).ok());
  CHECK_EQ(d2.ReadStatus().error, Error::CommunicationError);
  CHECK_EQ(port2.state.chip.regs[reg::kCfgDpm], 0U);
  REQUIRE(d2.ApplyConfig(d2.GetConfig()).ok());
  CHECK_EQ(port2.state.chip.regs[reg::kCfgDpm], max22200::EncodeDpm(dpm));
}

TEST_CASE("COMER: a call that fails for another reason keeps the report for the next one") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.comer_writes = 1;
  CHECK(d.SetChannel(2, true).ok());
  port.state.fail_transfer = port.state.transfers + 1; // the data phase of the next read
  CHECK_EQ(d.ReadChannelsOn().error, Error::Bus);
  CHECK_EQ(d.ReadChannelsOn().error, Error::CommunicationError);
  CHECK(d.ReadChannelsOn().ok());
}

TEST_CASE("COMER: with a chip that clears it on a STATUS read, each new miscount is reported") {
  MinimalPort port;
  port.state.chip.comer_clears_on_status_read = true;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  for (int i = 0; i < 2; ++i) {
    port.state.chip.comer_writes = 1;
    CHECK(d.SetChannel(2, i == 0).ok());
    CHECK_EQ(d.ReadStatus().error, Error::CommunicationError); // the read also clears it
    CHECK(d.ReadStatus().ok());
  }
}

// -------------------------------------------------------------------------------------------------
// MINOR: a clear-on-read register whose value did not arrive
// -------------------------------------------------------------------------------------------------

TEST_CASE("Lost flags: a failed FAULT or STATUS read on a running driver records them as lost") {
  MinimalPort port;
  port.state.chip.part = max22200_test::Part::Max22200; // as Config{} declares
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(Fault::Olf, 4);
  port.state.fail_transfer = port.state.transfers + 1; // the data phase
  port.state.fail_after_clocking = true;               // the chip clocked it: FAULT is clear
  CHECK_EQ(d.ReadFaults().error, Error::Bus);
  CHECK(d.GetLatchedFaults().lost);
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U);
  d.ClearLatchedFaults();
  port.state.fail_transfer = port.state.transfers + 1;
  CHECK_EQ(d.ReadStatus().error, Error::Bus);
  CHECK(d.GetLatchedFaults().lost);
  d.ClearLatchedFaults();
  REQUIRE(d.ReadChannelsOn().ok());
  port.state.fail_transfer = port.state.transfers + 1;
  CHECK_EQ(d.ReadChannelsOn().error, Error::Bus); // ONCH clears nothing
  CHECK(!d.GetLatchedFaults().lost);
}

// -------------------------------------------------------------------------------------------------
// A write whose reply failed may have taken effect
// -------------------------------------------------------------------------------------------------

namespace {

/// Fail the data phase of the next access; `clocked`: the chip took it all the same.
void FailNextData(MinimalPort& port, bool clocked) {
  port.state.fail_transfer = port.state.transfers + 1;
  port.state.fail_after_clocking = clocked;
}

} // namespace

TEST_CASE("Failed reply: an unknown pair mode refuses a switch-on in that pair, with no I/O") {
  for (const bool clocked : {true, false}) {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    FailNextData(port, clocked);
    CHECK_EQ(d.SetPairMode(0, max22200::PairMode::FullBridge).error, Error::Bus);
    CHECK_EQ(d.GetConfig().pairs[0], max22200::PairMode::Independent); // the copy keeps the old
    const int t = port.state.transfers;
    CHECK_EQ(d.SetChannel(0, true).error, Error::Uncertain);
    CHECK_EQ(d.SetChannelsOn(0x01).error, Error::Uncertain);
    CHECK_EQ(d.SetBridge(0, max22200::BridgeState::Forward).error, Error::Uncertain);
    CHECK_EQ(port.state.transfers, t);  // refused with no I/O
    CHECK(d.SetChannel(1, false).ok()); // an off request is never refused
    CHECK(d.SetChannelsOn(0x10).ok());  // another pair, the unknown one off: same in every mode
    // A read-back settles it: the chip now holds what the copy says.
    REQUIRE(d.SetPairMode(0, max22200::PairMode::FullBridge, Verify::ReadBack).ok());
    CHECK(d.SetBridge(0, max22200::BridgeState::Forward).ok());
  }
}

TEST_CASE(
    "Failed reply: a later STATUS write settles an unknown pair mode only if the pair is off") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(1, max22200::PairMode::Parallel).error, Error::Bus);
  CHECK_EQ(d.SetChannelsOn(0x04).error, Error::Uncertain);
  REQUIRE(d.SetFaultMask(d.GetConfig().fault_mask).ok()); // carries CMxy = independent, pair off
  CHECK(d.SetChannelsOn(0x04).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0x04U); // independent: channel 2 alone
}

TEST_CASE("Failed reply: an unknown pair mode counts as a mode change while the pair may be on") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(1, max22200::PairMode::Parallel).error, Error::Bus);
  FailNextData(port, true);
  CHECK_EQ(d.SetChannelsOn(0x0C).error, Error::Uncertain); // refused before any I/O
  CHECK_EQ(port.state.chip.OnMask(), 0U);
  port.state.fail_transfer = -1;
  // A failed ONCH write in another pair leaves channel 0 maybe on; pair 0's mode is known.
  FailNextData(port, true);
  CHECK_EQ(d.SetChannelsOn(0x01).error, Error::Bus);
  CHECK_EQ(d.SetPairMode(0, max22200::PairMode::Parallel).error, Error::ChannelsActive);
}

TEST_CASE("Failed reply: an unknown FREQM makes a channel be valid at either clock") {
  // Slew-rate control needs a chopping period above 20 us (Table 3): fMAIN / 2 passes at 80 kHz
  // (25 us) and fails at 100 kHz (20 us).
  ChannelConfig only80{};
  only80.slew_rate_control = true;
  only80.chop = max22200::ChopDivider::Div2;
  REQUIRE(max22200::ValidateChannel(only80, max22200::MasterClock::Khz80) == Error::Ok);
  REQUIRE(max22200::ValidateChannel(only80, max22200::MasterClock::Khz100) != Error::Ok);
  MinimalPort port;
  Config cfg;
  cfg.clock = max22200::MasterClock::Khz80;
  Driver d(port, cfg);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(d.ConfigureChannel(1, only80).ok());
  REQUIRE(d.ConfigureChannel(1, ChannelConfig{}).ok());
  Config next = d.GetConfig();
  next.clock = max22200::MasterClock::Khz100;
  FailNextData(port, true);
  CHECK_EQ(d.ApplyConfig(next).error, Error::Bus);
  CHECK_EQ(d.GetConfig().clock, max22200::MasterClock::Khz80); // the copy keeps the old
  const int t = port.state.transfers;
  CHECK_EQ(d.ConfigureChannel(1, only80).error, Error::InvalidArgument); // 100 kHz may apply
  CHECK_EQ(port.state.transfers, t);
  REQUIRE(d.SetFaultMask(d.GetConfig().fault_mask).ok()); // writes FREQM as the copy holds it
  CHECK(d.ConfigureChannel(1, only80).ok());
}

TEST_CASE("Failed reply: a CFG_CH write that landed is written back by ApplyConfig") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  const Config before = d.GetConfig();
  ChannelConfig c{};
  c.hold = 40;
  FailNextData(port, true);
  CHECK_EQ(d.ConfigureChannel(2, c).error, Error::Bus);
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(2)], max22200::EncodeChannel(c)); // it landed
  REQUIRE(d.ApplyConfig(before).ok()); // the copy matches `before`, the chip did not
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(2)], max22200::EncodeChannel(before.channels[2]));
}

TEST_CASE("Failed reply: a HOLD or CFG_DPM write that landed is written back by ApplyConfig") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  const Config before = d.GetConfig();
  FailNextData(port, true);
  CHECK_EQ(d.SetHoldCode(5, 20).error, Error::Bus);
  max22200::DpmConfig dpm{};
  dpm.threshold = 5;
  FailNextData(port, true);
  CHECK_EQ(d.SetDpm(dpm).error, Error::Bus);
  CHECK(port.state.chip.regs[reg::CfgChannel(5)] != max22200::EncodeChannel(before.channels[5]));
  REQUIRE(d.ApplyConfig(before).ok());
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(5)], max22200::EncodeChannel(before.channels[5]));
  CHECK_EQ(port.state.chip.regs[reg::kCfgDpm], max22200::EncodeDpm(before.dpm));
}

TEST_CASE("Failed reply: SetBridge in a pair whose mode is unknown returns Uncertain") {
  MinimalPort port;
  Config cfg;
  cfg.pairs[2] = max22200::PairMode::FullBridge;
  Driver d(port, cfg);
  REQUIRE(d.EnsureInitialized().ok());
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(2, max22200::PairMode::Independent).error, Error::Bus);
  const int t = port.state.transfers;
  CHECK_EQ(d.SetBridge(2, max22200::BridgeState::Forward).error, Error::Uncertain);
  CHECK_EQ(port.state.transfers, t);
}

TEST_CASE("Reset: a POR seen by a switch's read-back leaves the copy at every channel off") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  // The ONCH write (command, data), then the read-back's command answers as a reset chip.
  port.state.replace_transfer = port.state.transfers + 2;
  port.state.replace_bytes = {0x02, 0x00, 0x00, 0x00};
  CHECK_EQ(d.SetChannelsOn(0x05, Verify::ReadBack).error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
  CHECK_EQ(d.GetChannelsOn(), 0U);
}

TEST_CASE("Failed reply: a channel of either mode is not rewritten while a channel is on") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(7, true).ok());
  ChannelConfig vdr{};
  vdr.drive = DriveMode::Vdr;
  // The CFG write before a channel went on failed: the chip may run channel 2 in either mode.
  REQUIRE(d.SetChannel(7, false).ok());
  FailNextData(port, true);
  CHECK_EQ(d.ConfigureChannel(2, vdr).error, Error::Bus);
  REQUIRE(d.SetChannel(7, true).ok());
  const int t = port.state.transfers;
  CHECK_EQ(d.ConfigureChannel(2, d.GetConfig().channels[2]).error, Error::ChannelsActive);
  CHECK_EQ(d.ApplyConfig(d.GetConfig()).error, Error::ChannelsActive); // stale, but locked
  CHECK_EQ(port.state.transfers, t);
  CHECK_EQ(d.SetHoldCode(2, max22200::kMaxCdrCode).error, Error::InvalidArgument); // VDR range
  REQUIRE(d.SetChannel(7, false).ok());
  REQUIRE(d.ApplyConfig(d.GetConfig()).ok()); // every channel off: the chip takes the mode
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(2)],
           max22200::EncodeChannel(d.GetConfig().channels[2]));
}

// -------------------------------------------------------------------------------------------------
// An off request is never refused, whatever is in doubt
// -------------------------------------------------------------------------------------------------

TEST_CASE("Off: a COMER on a pair-mode write, then a switch-on: every off request still works") {
  // The pair's mode is in doubt while it is on (the review's P1 and P2).
  for (const auto mode : {max22200::PairMode::Parallel, max22200::PairMode::FullBridge}) {
    for (int how = 0; how < 4; ++how) {
      MinimalPort port;
      Driver d(port);
      REQUIRE(d.EnsureInitialized().ok());
      port.state.chip.comer_writes = 1;
      CHECK(d.SetPairMode(0, mode).ok()); // miscounted, not yet seen
      const Error on = (mode == max22200::PairMode::FullBridge)
                           ? d.SetBridge(0, max22200::BridgeState::Brake).error
                           : d.SetChannel(0, true).error;
      CHECK_EQ(on, Error::CommunicationError); // the switch-on landed, the COMER is reported
      port.DelayUs(reg::kWakeUpUs);
      REQUIRE(port.state.chip.DrivenMask() != 0U);
      const uint8_t partner_on = port.state.chip.OnMask();
      Error off = Error::Ok;
      if (how == 0) {
        off = d.SetChannel(0, false).error;
      } else if (how == 1) {
        off = d.SetChannel(1, false).error;
      } else if (how == 2) {
        off = d.SetBridge(0, max22200::BridgeState::HiZ).error;
      } else {
        off = d.SetChannelsOn(static_cast<uint8_t>(partner_on & 0x01U)).error; // one bit off
      }
      CHECK_EQ(off, Error::Ok);
      CHECK_EQ(port.state.chip.OnMask() & 0x03U, 0U); // both bits off: never Brake to Reverse
      CHECK_EQ(port.state.chip.DrivenMask(), 0U);
      CHECK_EQ(d.GetChannelsOn() & 0x03U, 0U);
    }
  }
}

TEST_CASE("Off: an unknown pair mode, clock, channel mode or ONCH bit never blocks a switch-off") {
  // Pair: a failed STATUS write with the pair off, then channel 2 on in another pair.
  {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    REQUIRE(d.SetChannel(2, true).ok());
    FailNextData(port, true);
    CHECK_EQ(d.SetPairMode(0, max22200::PairMode::Parallel).error, Error::Bus);
    CHECK(d.SetChannel(2, false).ok()); // the pair bits of pair 0 are 0: nothing in doubt goes out
    CHECK(d.SetChannelsOn(0x00).ok());
  }
  // Clock: a failed FREQM change.
  {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    REQUIRE(d.SetChannel(4, true).ok());
    Config next = d.GetConfig();
    next.clock = max22200::MasterClock::Khz80;
    FailNextData(port, true);
    CHECK_EQ(d.ApplyConfig(next).error, Error::Bus);
    CHECK(d.SetChannel(4, false).ok());
    CHECK_EQ(port.state.chip.OnMask(), 0U);
  }
  // Channel mode: a failed CFG_CH write.
  {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    ChannelConfig vdr{};
    vdr.drive = DriveMode::Vdr;
    FailNextData(port, true);
    CHECK_EQ(d.ConfigureChannel(5, vdr).error, Error::Bus);
    REQUIRE(d.SetChannel(5, true).ok());
    CHECK(d.SetChannel(5, false).ok());
    CHECK_EQ(port.state.chip.OnMask(), 0U);
  }
  // ONCH: a failed switch-on leaves channel 6 maybe on.
  {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    FailNextData(port, true);
    CHECK_EQ(d.SetChannel(6, true).error, Error::Bus);
    CHECK(d.SetChannel(6, false).ok());
    CHECK_EQ(port.state.chip.OnMask(), 0U);
  }
}

// -------------------------------------------------------------------------------------------------
// Miscount detection stays on, or the session reads every switch-off back
// -------------------------------------------------------------------------------------------------

TEST_CASE("COMER: a COMER the wake leaves is cleared by ENABLE low, so a miscount still shows") {
  for (int mode = 0; mode < 2; ++mode) { // the review's m_probe2: a slow supply, a sticky UVM
    MinimalPort port;
    if (mode == 0) {
      port.state.chip.wake_settle_us = 1500;
    } else {
      port.state.chip.uvm_sticky_reads = 2;
    }
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    CHECK(port.state.chip.rejected_wakes > 0);
    CHECK_EQ(d.GetLastStatusByte() & reg::kComer, 0U);
    REQUIRE(d.SetChannelsOn(0x08).ok());
    port.state.chip.comer_writes = 1;
    CHECK(d.SetChannel(3, false).ok());
    CHECK_EQ(d.ReadFaults().error, Error::CommunicationError);
  }
}

TEST_CASE("COMER: while it stays set, every switch-off is read back (2 more transfers)") {
  MinimalPort port;
  port.state.chip.comer_survives_enable = true; // only a power-on reset clears it
  port.state.chip.regs[reg::kStatus] |= reg::kComer;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok()); // degraded, not failed
  REQUIRE(d.SetChannelsOn(0x09).ok());
  int t = port.state.transfers;
  REQUIRE(d.SetChannelsOn(0x0B).ok()); // a switch-on: no read-back
  CHECK_EQ(port.state.transfers - t, 2);
  port.state.chip.comer_writes = 1;
  t = port.state.transfers;
  CHECK_EQ(d.SetChannel(3, false).error, Error::CommunicationError); // read back: still on
  CHECK_EQ(port.state.transfers - t, 4);
  CHECK((d.GetChannelsOn() & 0x08U) != 0U); // the copy takes what the chip holds
  CHECK(d.SetChannel(3, false).ok());       // again: lands, read back
  CHECK_EQ(port.state.chip.OnMask(), 0x03U);
}

TEST_CASE("COMER: after the first report in a session, a second miscounted switch-off is caught") {
  // The review's P3: COMER stays set after the first report (the worst case of "Read Only").
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x09).ok());
  port.state.chip.comer_writes = 1;
  CHECK(d.SetChannel(3, false).ok());
  CHECK_EQ(d.ReadFaults().error, Error::CommunicationError);
  CHECK(d.SetChannel(3, false).ok()); // read back from now on
  CHECK_EQ(port.state.chip.OnMask(), 0x01U);
  port.state.chip.comer_writes = 1;
  CHECK_EQ(d.SetChannel(0, false).error, Error::CommunicationError);
  CHECK_EQ(d.GetChannelsOn(), 0x01U); // not "off": the chip still drives it
  CHECK(d.SetChannel(0, false).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0U);
}

// -------------------------------------------------------------------------------------------------
// A power-on reset between a read's command byte and its data
// -------------------------------------------------------------------------------------------------

TEST_CASE("Reset: all-ones data is checked with one more command byte, so no garbage returns") {
  using ReadOp = Error (*)(Driver&);
  const std::array<ReadOp, 5> reads = {
      [](Driver& d) { return d.ReadChannelConfig(3).error; },
      [](Driver& d) { return d.ReadRegister(reg::CfgChannel(2)).error; },
      [](Driver& d) { return d.ReadChannelsOn().error; },
      [](Driver& d) { return d.ReadFaults().error; },
      [](Driver& d) { return d.ReadStatus().error; },
  };
  for (const ReadOp read : reads) {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    // The command byte reaches a running chip; the reset comes before the data phase, which then
    // floats: the reset chip forgot the command.
    port.state.brownout_at_transfer = port.state.transfers + 1;
    const int t = port.state.transfers;
    CHECK_EQ(read(d), Error::DeviceReset);
    CHECK_EQ(port.state.transfers - t, 3); // the read, then the probe's command byte
    CHECK(!d.IsInitialized());
  }
}

TEST_CASE("Reset: all-ones data from a floating SDO costs the probe and is not a reset") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.miso = max22200_test::Miso::FloatHigh;
  const int t = port.state.transfers;
  CHECK(d.ReadChannelConfig(1).ok());
  CHECK_EQ(port.state.transfers - t, 4);
  CHECK(d.IsInitialized());
}

// -------------------------------------------------------------------------------------------------
// Mixed masks and full bridges: every switch-off goes out
// -------------------------------------------------------------------------------------------------

TEST_CASE("Off: a mixed mask sends its switch-offs and drops only the unknown switch-on") {
  // The review's m_probe4: channel 4 on, then a failed-reply mode change of pair 0.
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x10).ok());
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(0, max22200::PairMode::FullBridge).error, Error::Bus);
  CHECK_EQ(d.SetChannelsOn(0x01).error, Error::Uncertain); // pair 0 on refused ...
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);               // ... channel 4 off sent
  CHECK_EQ(d.GetChannelsOn(), 0x00U);
}

TEST_CASE("Off: a mixed mask across several pairs, some unknown, some known") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  // Pairs 0 and 2 unknown (two failed-reply mode changes, both pairs off).
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(0, max22200::PairMode::Parallel).error, Error::Bus);
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(2, max22200::PairMode::FullBridge).error, Error::Bus);
  REQUIRE(d.SetChannelsOn(0x48).ok()); // channels 3 and 6: known pairs 1 and 3
  // Ask pair 0 on, pair 2 on (both unknown), keep 3, switch 6 off, switch 2 on (known pair 1).
  CHECK_EQ(d.SetChannelsOn(0x1D).error, Error::Uncertain);
  CHECK_EQ(port.state.chip.OnMask(), 0x0CU); // 2 and 3 on, 6 off, pairs 0 and 2 untouched (off)
  // Nothing but a dropped switch-on: the switch-off of 6 is not vouched for yet (no fault byte
  // since), so the mask goes out once more; after that, nothing is sent.
  int t = port.state.transfers;
  CHECK_EQ(d.SetChannelsOn(0x0D).error, Error::Uncertain);
  CHECK_EQ(port.state.transfers - t, 1); // the one-transfer write
  t = port.state.transfers;
  CHECK_EQ(d.SetChannelsOn(0x0D).error, Error::Uncertain);
  CHECK_EQ(port.state.transfers, t);
}

TEST_CASE("Off: an unknown pair that is on: a mask that clears any of its bits clears both") {
  for (int how = 0; how < 3; ++how) {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    port.state.chip.comer_writes = 1;
    CHECK(d.SetPairMode(0, max22200::PairMode::FullBridge).ok());
    CHECK_EQ(d.SetBridge(0, max22200::BridgeState::Brake).error, Error::CommunicationError);
    REQUIRE((port.state.chip.OnMask() & 0x03U) == 0x03U); // pair 0 on, its mode unknown
    if (how == 0) {
      CHECK(d.SetChannelsOn(0x07).ok()); // pair 0 kept, channel 2 on: no refusal
      CHECK_EQ(port.state.chip.OnMask(), 0x07U);
    } else if (how == 1) {
      CHECK(d.SetChannelsOn(0x05).ok()); // one bit of pair 0 off: both off, channel 2 on
      CHECK_EQ(port.state.chip.OnMask(), 0x04U);
    } else {
      CHECK(d.SetChannelsOn(0x02).ok()); // Brake to "Reverse" asked: both off instead
      CHECK_EQ(port.state.chip.OnMask(), 0x00U);
    }
  }
}

TEST_CASE("Off: a mixed mask goes out whole with a clock, channel-mode or ONCH bit in doubt") {
  for (int flag = 0; flag < 3; ++flag) {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    REQUIRE(d.SetChannelsOn(0x30).ok());
    FailNextData(port, true);
    if (flag == 0) {
      Config next = d.GetConfig();
      next.clock = max22200::MasterClock::Khz80;
      CHECK_EQ(d.ApplyConfig(next).error, Error::Bus);
    } else if (flag == 1) {
      ChannelConfig c{};
      c.hold = 3;
      CHECK_EQ(d.ConfigureChannel(4, c).error, Error::Bus);
    } else {
      port.state.fail_transfer = port.state.transfers; // the one-transfer switch: its data byte
      CHECK_EQ(d.SetChannel(7, true).error, Error::Bus);
    }
    CHECK(d.SetChannelsOn(0x21).ok()); // 0 on, 4 off, 5 kept, 7 off
    CHECK_EQ(port.state.chip.OnMask(), 0x21U);
  }
}

TEST_CASE("Off: SetChannel off in a known full bridge is HiZ, both channels off") {
  for (const uint8_t ch : {uint8_t{2}, uint8_t{3}}) {
    MinimalPort port;
    Config cfg;
    cfg.pairs[1] = max22200::PairMode::FullBridge;
    Driver d(port, cfg);
    REQUIRE(d.EnsureInitialized().ok());
    REQUIRE(d.SetBridge(1, max22200::BridgeState::Brake).ok());
    CHECK_EQ(d.SetChannel(ch, true).error, Error::InvalidArgument); // on: use SetBridge()
    CHECK(d.SetChannel(ch, false).ok());
    CHECK_EQ(port.state.chip.OnMask() & 0x0CU, 0U);
    CHECK_EQ(d.GetChannelsOn(), 0U);
  }
}

TEST_CASE("COMER: IsMiscountDetectionDegraded says when a miscount cannot be seen (no I/O)") {
  MinimalPort port;
  Driver d(port);
  CHECK(!d.IsMiscountDetectionDegraded()); // not running
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(!d.IsMiscountDetectionDegraded());
  port.state.chip.comer_writes = 1;
  CHECK(d.SetChannel(1, true).ok());
  const int t = port.state.transfers;
  CHECK(!d.IsMiscountDetectionDegraded()); // not seen yet
  CHECK_EQ(port.state.transfers, t);
  CHECK_EQ(d.ReadFaults().error, Error::CommunicationError);
  CHECK(d.IsMiscountDetectionDegraded()); // COMER stays set in this model
  REQUIRE(d.Deinitialize().ok());
  CHECK(!d.IsMiscountDetectionDegraded());
  REQUIRE(d.EnsureInitialized().ok()); // ENABLE low cleared it
  CHECK(!d.IsMiscountDetectionDegraded());

  MinimalPort sticky;
  sticky.state.chip.comer_survives_enable = true;
  sticky.state.chip.regs[reg::kStatus] |= reg::kComer;
  Driver d2(sticky);
  REQUIRE(d2.EnsureInitialized().ok());
  CHECK(d2.IsMiscountDetectionDegraded());
}

TEST_CASE("Off: a channel a failed write may have switched on is switched off by a refused mask") {
  // The review's m_probe5: pair 0's mode unknown, channel 4 maybe on (the copy says off).
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(0, max22200::PairMode::FullBridge).error, Error::Bus);
  FailNextData(port, true);
  CHECK_EQ(d.SetChannel(4, true).error, Error::Bus);
  REQUIRE(port.state.chip.OnMask() == 0x10U);
  REQUIRE(d.GetChannelsOn() == 0x00U);
  const int t = port.state.transfers;
  CHECK_EQ(d.SetChannelsOn(0x01).error, Error::Uncertain); // pair 0 on refused ...
  CHECK(port.state.transfers > t);                         // ... but the mask went out
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);               // channel 4 off
}

TEST_CASE("Off: in a pair of unknown mode, a bit that may be on and is asked off clears both") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.comer_writes = 1;
  CHECK(d.SetPairMode(1, max22200::PairMode::FullBridge).ok()); // miscounted: still independent
  // The next write's command byte shows the COMER (pair 1 now in doubt); its data lands, the reply
  // fails: channel 3 may be on while the copy says off.
  FailNextData(port, true);
  CHECK_EQ(d.SetBridge(1, max22200::BridgeState::Reverse).error, Error::Bus);
  REQUIRE(port.state.chip.OnMask() == 0x08U);
  REQUIRE(d.GetChannelsOn() == 0x00U);
  // Channel 2 on is refused and channel 3 asked off: both go off. The COMER still to report wins
  // over Uncertain. COMER is already set (degraded), so that switch-off was read back, which proves
  // it: the next identical call has nothing to send.
  CHECK_EQ(d.SetChannelsOn(0x04).error, Error::CommunicationError);
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);
  const int t = port.state.transfers;
  CHECK_EQ(d.SetChannelsOn(0x04).error, Error::Uncertain);
  CHECK_EQ(port.state.transfers, t);
}

TEST_CASE("COMER: a switch-off miscounted in the call that first sees COMER stays in doubt") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x38).ok());
  port.state.chip.comer_writes = 2;
  CHECK(d.SetChannelsOn(0x39).ok()); // miscounted, not seen yet
  // This call's own byte shows the COMER; its write is miscounted too: 3, 4 and 5 still drive.
  CHECK_EQ(d.SetChannelsOn(0x01).error, Error::CommunicationError);
  REQUIRE(port.state.chip.OnMask() == 0x38U);
  ChannelConfig vdr{};
  vdr.drive = DriveMode::Vdr;
  CHECK_EQ(d.ConfigureChannel(3, vdr).error, Error::ChannelsActive); // 3 may be on
  CHECK(d.SetChannelsOn(0x01).ok()); // the same mask goes out again, read back: lands
  CHECK_EQ(port.state.chip.OnMask(), 0x01U);
}

TEST_CASE("COMER: a degraded switch-off whose read-back fails stays in doubt") {
  MinimalPort port;
  port.state.chip.comer_survives_enable = true;
  port.state.chip.regs[reg::kStatus] |= reg::kComer;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.IsMiscountDetectionDegraded());
  REQUIRE(d.SetChannelsOn(0x08).ok());
  port.state.chip.comer_writes = 1;                    // the switch-off is miscounted ...
  port.state.fail_transfer = port.state.transfers + 3; // ... and its read-back's data fails
  CHECK_EQ(d.SetChannel(3, false).error, Error::Bus);
  REQUIRE(port.state.chip.OnMask() == 0x08U);
  REQUIRE(d.GetChannelsOn() == 0x00U);
  ChannelConfig vdr{};
  vdr.drive = DriveMode::Vdr;
  CHECK_EQ(d.ConfigureChannel(3, vdr).error, Error::ChannelsActive); // 3 may still be on
  CHECK(d.SetChannel(3, false).ok());                                // goes out again, read back
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);
}

TEST_CASE("Off: a refused mask re-sends a switch-off no fault byte has vouched for (CI fuzz)") {
  // The CI fuzz input regress-unverified-switch-off-held-back: a switch-off is miscounted, the
  // next access's fault bytes are lost (0xFF), so the COMER is not seen; a refused mask must not
  // skip the write while that switch-off is unverified.
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  FailNextData(port, true);
  CHECK_EQ(d.SetPairMode(0, max22200::PairMode::FullBridge).error, Error::Bus); // pair 0 unknown
  REQUIRE(d.SetChannelsOn(0x30).ok());
  port.state.chip.comer_writes = 1;
  CHECK(d.SetChannelsOn(0x00).ok()); // miscounted: the chip still drives 4 and 5
  REQUIRE(port.state.chip.OnMask() == 0x30U);
  // The next access's fault byte is lost on the bus: the driver does not see the COMER.
  port.state.replace_transfer = port.state.transfers;
  port.state.replace_bytes = {0xFF, 0xFF, 0xFF, 0xFF};
  CHECK(d.ConfigureChannel(7, ChannelConfig{}).ok());
  REQUIRE((d.GetLastStatusByte() & reg::kComer) != 0U); // 0xFF: no answer, not a COMER seen
  const Error e = d.SetChannelsOn(0x01).error;          // pair 0 on refused; 4 and 5 asked off
  CHECK((e == Error::Uncertain) || (e == Error::CommunicationError));
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);
}

TEST_CASE("COMER: doubt a miscounted settling write cleared comes back (review m_probe8)") {
  // via 0: the settling write is an ONCH write (path A); via 1: a STATUS write (path B).
  for (int via = 0; via < 2; ++via) {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    FailNextData(port, true);
    CHECK_EQ(d.SetPairMode(0, max22200::PairMode::FullBridge).error, Error::Bus); // pair 0 unknown
    FailNextData(port, true);
    CHECK_EQ(d.SetChannel(4, true).error, Error::Bus); // channel 4 maybe on
    REQUIRE(port.state.chip.OnMask() == 0x10U);
    port.state.fail_transfer = -1;
    port.state.chip.comer_writes = 1; // the settling write is miscounted
    const Error w =
        (via == 0) ? d.SetChannelsOn(0x00).error : d.SetFaultMask(d.GetConfig().fault_mask).error;
    CHECK_EQ(w, Error::Ok); // it cannot see its own miscount
    REQUIRE(port.state.chip.OnMask() == 0x10U);
    CHECK_EQ(d.ReadFaults().error, Error::CommunicationError);
    ChannelConfig c = d.GetConfig().channels[6];
    c.drive = (c.drive == DriveMode::Vdr) ? DriveMode::Cdr : DriveMode::Vdr;
    CHECK_EQ(d.ConfigureChannel(6, c).error, Error::ChannelsActive); // channel 4 may still be on
    const Error e = d.SetChannelsOn(0x01).error; // pair 0 on: its mode is unknown again
    CHECK_EQ(e, Error::Uncertain);
    CHECK_EQ(port.state.chip.OnMask(), 0x00U); // channel 4 asked off: off; nothing driven in pair 0
  }
}

TEST_CASE("COMER: a configuration write stops before its data at a new COMER (an ONCH one never)") {
  // A miscounted switch-off, then a drive-mode change: its command byte shows the COMER, the
  // channels may still be on, so the mode change must not go out (5c) nor clear the doubt.
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x2D).ok());
  port.state.chip.comer_writes = 1;
  CHECK(d.SetChannelsOn(0x00).ok()); // miscounted: the chip still drives 0x2D
  ChannelConfig vdr{};
  vdr.drive = DriveMode::Vdr;
  const uint32_t before = port.state.chip.regs[reg::CfgChannel(7)];
  const int phases = port.state.chip.data_phases;
  CHECK_EQ(d.ConfigureChannel(7, vdr).error, Error::CommunicationError);
  CHECK_EQ(port.state.chip.data_phases, phases); // the data phase did not go out
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(7)], before);
  CHECK_EQ(d.ConfigureChannel(7, vdr).error, Error::ChannelsActive); // channels may be on
  // A switch-off at a new COMER still goes out.
  MinimalPort p2;
  Driver d2(p2);
  REQUIRE(d2.EnsureInitialized().ok());
  REQUIRE(d2.SetChannelsOn(0x03).ok());
  p2.state.chip.comer_writes = 1;
  REQUIRE(d2.ReadChannelsOn().ok());                                 // breaks the one-transfer path
  CHECK(d2.SetChannelsOn(0x01).ok());                                // miscounted
  CHECK_EQ(d2.SetChannelsOn(0x00).error, Error::CommunicationError); // sees the COMER, still sent
  CHECK_EQ(p2.state.chip.OnMask(), 0x00U);
}

TEST_CASE("COMER: while degraded, ConfigureChannel reads back, so a miscounted mode change shows") {
  MinimalPort port;
  port.state.chip.comer_survives_enable = true;
  port.state.chip.regs[reg::kStatus] |= reg::kComer;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.IsMiscountDetectionDegraded());
  ChannelConfig vdr{};
  vdr.drive = DriveMode::Vdr;
  port.state.chip.comer_writes = 1;
  const int t = port.state.transfers;
  CHECK_EQ(d.ConfigureChannel(7, vdr).error, Error::CommunicationError); // read back: not taken
  CHECK_EQ(port.state.transfers - t, 4);
  REQUIRE(d.SetChannel(3, true).ok());
  // The chip still runs channel 7 in CDR: a "same mode" write now would be a mode change.
  CHECK_EQ(d.ConfigureChannel(7, vdr).error, Error::ChannelsActive);
  REQUIRE(d.SetChannel(3, false).ok());
  CHECK(d.ConfigureChannel(7, vdr).ok());
  CHECK((port.state.chip.regs[reg::CfgChannel(7)] & reg::kVdrncdr) != 0U);
}
