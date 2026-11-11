/**
 * @file test_fast_path.cpp
 * @brief The one-transfer ONCH write and every way it is invalidated; the driver's copy after a
 * read-back mismatch; COMER at start-up; CFG_DPM's read-only reserved bits.
 */
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"
#include "test_harness.hpp"

using max22200::ChannelConfig;
using max22200::Config;
using max22200::DriveMode;
using max22200::Error;
using max22200::PairMode;
using max22200::Verify;
using max22200_test::Fault;
using max22200_test::MinimalPort;
namespace reg = max22200::reg;

namespace {

using Driver = max22200::MAX22200<MinimalPort>;

/// Transfers of one `SetChannelsOn()`.
int OnchCost(Driver& d, MinimalPort& port, uint8_t mask) {
  const int t = port.state.transfers;
  if (!d.SetChannelsOn(mask).ok()) {
    return -1;
  }
  return port.state.transfers - t;
}

/// A started driver whose last access was an ONCH write: the next one costs 1 transfer.
void Arm(Driver& d, MinimalPort& port) {
  (void)d.EnsureInitialized();
  (void)OnchCost(d, port, 0x00);
}

} // namespace

// -------------------------------------------------------------------------------------------------
// The one-transfer ONCH write
// -------------------------------------------------------------------------------------------------

TEST_CASE("Fast path: the first ONCH write costs 2 transfers, the next ones 1 (p25)") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(OnchCost(d, port, 0x01), 2);
  const int t = port.state.transfers;
  CHECK_EQ(OnchCost(d, port, 0x03), 1);
  CHECK(!port.state.with_cmd[static_cast<std::size_t>(t)]); // CMD low: a data phase
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t)], 0x03);
  CHECK_EQ(port.state.chip.OnMask(), 0x03U);
  CHECK_EQ(OnchCost(d, port, 0x00), 1);
  CHECK(d.SetChannel(5, true).ok());
  CHECK(d.SetChannel(5, false).ok());
  CHECK_EQ(port.state.transfers - t, 4);
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);
}

TEST_CASE("Fast path: SetBridge uses it too") {
  Config c{};
  c.pairs[1] = PairMode::FullBridge;
  MinimalPort port;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetBridge(1, max22200::BridgeState::Forward).ok());
  const int t = port.state.transfers;
  CHECK(d.SetBridge(1, max22200::BridgeState::Brake).ok());
  CHECK_EQ(port.state.transfers - t, 1);
  CHECK_EQ(port.state.chip.OnMask(), 0x0CU);
}

TEST_CASE("Fast path: the data byte's STATUS[7:0] is kept at no cost (Figure 12)") {
  MinimalPort port;
  Driver d(port);
  Arm(d, port);
  port.state.chip.Overtemperature();
  CHECK_EQ(OnchCost(d, port, 0x01), 1);
  CHECK_EQ(d.GetLastStatusByte(), 0x81U); // OVT, ACTIVE
}

TEST_CASE("Fast path: every other access invalidates it") {
  // Each line: an access, then the next ONCH write must send its command again (2 transfers).
  using Op = bool (*)(Driver&);
  const Op ops[] = {
      [](Driver& d) { return d.ReadStatus().ok(); },
      [](Driver& d) { return d.ReadChannelsOn().ok(); },
      [](Driver& d) { return d.ReadFaults().ok(); },
      [](Driver& d) { return d.ClearFaults().ok(); },
      [](Driver& d) { return d.ReadChannelConfig(0).ok(); },
      [](Driver& d) { return d.ConfigureChannel(1, ChannelConfig{}).ok(); },
      [](Driver& d) { return d.SetHoldCode(1, 3).ok(); },
      [](Driver& d) { return d.SetPairMode(2, PairMode::Parallel).ok(); },
      [](Driver& d) { return d.SetFaultMask({}).ok(); },
      [](Driver& d) { return d.SetDpm({.start = 1, .debounce = 1, .threshold = 1}).ok(); },
      [](Driver& d) {
        Config c = d.GetConfig();
        c.channels[3].hold = 5;
        return d.ApplyConfig(c).ok();
      },
      [](Driver& d) { return d.SetChannelsOn(0x01, Verify::ReadBack).ok(); },
  };
  for (const Op op : ops) {
    MinimalPort port;
    Driver d(port);
    Arm(d, port);
    REQUIRE(op(d));
    CHECK_EQ(OnchCost(d, port, 0x00), 2);
    CHECK_EQ(OnchCost(d, port, 0x00), 1); // and armed again
  }
}

TEST_CASE("Fast path: a failed transfer invalidates it") {
  for (const bool clocked : {false, true}) {
    MinimalPort port;
    Driver d(port);
    Arm(d, port);
    port.state.fail_transfer = port.state.transfers;
    port.state.fail_after_clocking = clocked;
    CHECK_EQ(d.SetChannelsOn(0x01).error, Error::Bus);
    CHECK_EQ(OnchCost(d, port, 0x01), 2);
    CHECK_EQ(port.state.chip.OnMask(), 0x01U);
  }
}

TEST_CASE("Fast path: a failed pin write invalidates it") {
  MinimalPort port;
  Driver d(port);
  Arm(d, port);
  port.state.fail_pin = port.state.pin_calls; // CMD high of the STATUS read
  CHECK_EQ(d.ReadStatus().error, Error::Pin);
  CHECK_EQ(OnchCost(d, port, 0x01), 2);
}

TEST_CASE("Fast path: an ENABLE change (Deinitialize, a restart) invalidates it") {
  MinimalPort port;
  Driver d(port);
  Arm(d, port);
  REQUIRE(d.Deinitialize().ok());
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(OnchCost(d, port, 0x01), 2);
}

TEST_CASE("Fast path: a power-on reset found by ReadStatus invalidates it") {
  MinimalPort port;
  Driver d(port);
  Arm(d, port);
  port.state.chip.Brownout();
  CHECK_EQ(d.ReadStatus().error, Error::DeviceReset);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(OnchCost(d, port, 0x01), 2);
}

TEST_CASE("Fast path: COMER in a fault byte invalidates it") {
  MinimalPort port;
  Driver d(port);
  Arm(d, port);
  port.state.chip.comer_writes = 1; // the next write is miscounted and ignored
  CHECK_EQ(OnchCost(d, port, 0x01), 1);
  CHECK_EQ(port.state.chip.OnMask(), 0x00U); // the chip ignored it; COMER shows from the next byte
  const int t = port.state.transfers;
  // The command is still in force: this one lands, and reports the earlier miscount (p34).
  CHECK_EQ(d.SetChannelsOn(0x02).error, Error::CommunicationError);
  CHECK_EQ(port.state.transfers - t, 1);
  CHECK((d.GetLastStatusByte() & reg::kComer) != 0U);
  CHECK_EQ(port.state.chip.OnMask(), 0x02U);
  CHECK_EQ(OnchCost(d, port, 0x03), 2); // after COMER the command goes out again
  CHECK_EQ(port.state.chip.OnMask(), 0x03U);
}

// -------------------------------------------------------------------------------------------------
// The driver's copy after a read-back mismatch
// -------------------------------------------------------------------------------------------------

TEST_CASE("ONCH read-back mismatch: the copy takes the chip's value") {
  MinimalPort port;
  Driver d(port, Config{});
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.regs[reg::kStatus] |= 0x02UL << reg::kOnchShift; // on behind the driver's back
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetChannelsOn(0x00, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(d.GetChannelsOn(), 0x02U);
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  CHECK_EQ(d.ConfigureChannel(0, v).error, Error::ChannelsActive); // the guard sees channel 1
}

TEST_CASE("CFG_CH read-back mismatch: the copy takes the chip's value") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.trig_high = true; // the chip keeps the drive mode
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  v.hit = 60;
  CHECK_EQ(d.ConfigureChannel(2, v, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(d.GetConfig().channels[2].drive, DriveMode::Cdr);
  CHECK_EQ(d.GetConfig().channels[2].hit, 60U); // the codes did land
}

TEST_CASE("HOLD read-back mismatch: the copy takes the chip's value") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetHoldCode(0, 10).ok());
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetHoldCode(0, 20, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(d.GetConfig().channels[0].hold, 10U);
}

TEST_CASE("STATUS read-back mismatch: the copy takes the chip's value") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetPairMode(1, PairMode::Parallel, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(d.GetConfig().pairs[1], PairMode::Independent);
  max22200::FaultMask m{};
  m.open_load = true;
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetFaultMask(m, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK(!d.GetConfig().fault_mask.open_load);
}

TEST_CASE("CFG_DPM read-back mismatch: the copy takes the chip's value") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetDpm({.start = 9, .debounce = 2, .threshold = 1}, Verify::ReadBack).error,
           Error::WriteMismatch);
  CHECK(d.GetConfig().dpm == max22200::DpmConfig{});
}

TEST_CASE("A STATUS write settles a failed ONCH write's uncertain channels") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1; // the data byte reached the chip
  port.state.fail_after_clocking = true;
  CHECK_EQ(d.SetChannel(4, true).error, Error::Bus);
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  CHECK_EQ(d.ConfigureChannel(0, v).error, Error::ChannelsActive);
  REQUIRE(d.SetFaultMask(d.GetConfig().fault_mask).ok()); // carries ONCH = 0
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);
  CHECK(d.ConfigureChannel(0, v).ok());
}

// -------------------------------------------------------------------------------------------------
// COMER at start-up
// -------------------------------------------------------------------------------------------------

TEST_CASE("COMER already set before the start: ENABLE low clears it, the start passes") {
  MinimalPort port;
  port.state.chip.regs[reg::kStatus] |= reg::kComer; // sticky: no STATUS read clears it
  Driver d(port);
  CHECK(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, 50); // one attempt, one ENABLE cycle and a STATUS read
  CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kComer, 0U); // miscount detection armed
}

TEST_CASE("COMER that a STATUS read clears (the other reading): the start passes") {
  MinimalPort port;
  port.state.chip.comer_clears_on_status_read = true;
  port.state.chip.regs[reg::kStatus] |= reg::kComer;
  Driver d(port);
  CHECK(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, 48);
}

TEST_CASE("A new COMER repeats the procedure after ENABLE low for tDIS, then high for tEN") {
  MinimalPort port;
  port.state.chip.comer_bank = reg::CfgChannel(0);
  port.state.chip.comer_bank_writes = 1;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, 26 + 48);
  // tEN, tWU is skipped by the failed attempt, then tDIS + tEN + tWU.
  CHECK(port.delayed_us >= reg::kEnableUs + reg::kDisableUs + reg::kEnableUs + reg::kWakeUpUs);
  CHECK(port.state.chip.enable);
}

TEST_CASE("A pin failure while ENABLE restarts between attempts is Pin") {
  // Pin calls: CMD high, ENABLE high, 13 accesses of 3 (CMD high, low, high) in attempt 1, then
  // ENABLE low.
  MinimalPort port;
  port.state.chip.comer_bank = reg::CfgChannel(0);
  port.state.chip.comer_bank_writes = 1;
  port.state.fail_pin = 2 + (13 * 3);
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::Pin);
  MinimalPort port2;
  port2.state.chip.comer_bank = reg::CfgChannel(0);
  port2.state.chip.comer_bank_writes = 1;
  port2.state.fail_pin = 2 + (13 * 3) + 1; // ENABLE high again
  Driver d2(port2);
  CHECK_EQ(d2.EnsureInitialized().error, Error::Pin);
}

// -------------------------------------------------------------------------------------------------
// CFG_DPM's reserved bits
// -------------------------------------------------------------------------------------------------

TEST_CASE("CFG_DPM read-only reserved bits reading 1 do not fail the checks (p40-41)") {
  Config c{};
  c.dpm = {.start = 40, .debounce = 3, .threshold = 2};
  MinimalPort port;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK((port.state.chip.DpmReadValue() & ~reg::kDpmUsedMask) != 0U); // the bits are there
  CHECK(d.SetDpm({.start = 41, .debounce = 3, .threshold = 2}, Verify::ReadBack).ok());
  CHECK_EQ(port.state.chip.reserved_writes, 0);
}

TEST_CASE("A CFG_CH write that failed or read back different guards the channel's mode") {
  for (const bool mismatch : {false, true}) {
    MinimalPort port;
    Driver d(port);
    REQUIRE(d.EnsureInitialized().ok());
    ChannelConfig v{};
    v.drive = DriveMode::Vdr;
    v.hit = 50;
    if (mismatch) {
      port.state.chip.drop_writes = 1;
      CHECK_EQ(d.ConfigureChannel(3, v, Verify::ReadBack).error, Error::WriteMismatch);
    } else {
      port.state.fail_transfer = port.state.transfers + 1; // the data reached the chip
      port.state.fail_after_clocking = true;
      CHECK_EQ(d.ConfigureChannel(3, v).error, Error::Bus);
    }
    REQUIRE(d.SetChannel(0, true).ok());
    // Channel 3 may hold either mode: even a same-mode write waits for the channels to be off.
    ChannelConfig same = d.GetConfig().channels[3];
    same.hold = 7;
    CHECK_EQ(d.ConfigureChannel(3, same).error, Error::ChannelsActive);
    CHECK(d.ConfigureChannel(4, ChannelConfig{.hit = 3}).ok()); // other channels are not affected
    REQUIRE(d.SetChannelsOn(0x00).ok());
    CHECK(d.ConfigureChannel(3, same).ok()); // settled
    REQUIRE(d.SetChannel(0, true).ok());
    same.hold = 8;
    CHECK(d.ConfigureChannel(3, same).ok());
  }
}

TEST_CASE("An ONCH read-back that differs leaves the differing channels untrusted") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetChannelsOn(0x10, Verify::ReadBack).error, Error::WriteMismatch);
  CHECK_EQ(d.GetChannelsOn(), 0x00U); // the chip's value
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  CHECK_EQ(d.ConfigureChannel(0, v).error, Error::ChannelsActive); // channel 4 is not trusted
  REQUIRE(d.SetChannelsOn(0x00).ok());
  CHECK(d.ConfigureChannel(0, v).ok());
}

TEST_CASE("While a channel's mode is uncertain, its HOLD code must be valid in both modes") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.ConfigureChannel(2, v, Verify::ReadBack).error, Error::WriteMismatch);
  const int t = port.state.transfers;
  CHECK_EQ(d.SetHoldCode(2, 120).error, Error::InvalidArgument); // a CDR code, not a VDR one
  CHECK_EQ(port.state.transfers, t);
  CHECK(d.SetHoldCode(2, 90).ok());
}

TEST_CASE("ReadChannelsOn adds doubt but never clears it") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1;
  port.state.fail_after_clocking = true;
  CHECK_EQ(d.SetChannel(6, true).error, Error::Bus);      // channel 6 may be on
  port.state.replace_transfer = port.state.transfers + 1; // the data byte, not the fault byte
  port.state.replace_bytes = {0x00, 0x00, 0x00, 0x00};
  REQUIRE(d.ReadChannelsOn().ok()); // reads 0 through a broken SDO
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  CHECK_EQ(d.ConfigureChannel(0, v).error, Error::ChannelsActive);
}

// -------------------------------------------------------------------------------------------------
// CMD idles high
// -------------------------------------------------------------------------------------------------

TEST_CASE("CMD idles high after every call, the one-transfer ONCH write included") {
  using Op = bool (*)(Driver&);
  const Op ops[] = {
      [](Driver& d) { return d.SetChannelsOn(0x01).ok(); },
      [](Driver& d) { return d.SetChannelsOn(0x03).ok(); }, // the data byte alone
      [](Driver& d) { return d.SetChannelsOn(0x00, Verify::ReadBack).ok(); },
      [](Driver& d) { return d.ReadStatus().ok(); },
      [](Driver& d) { return d.ReadChannelsOn().ok(); },
      [](Driver& d) { return d.ClearFaults().ok(); },
      [](Driver& d) { return d.ConfigureChannel(1, ChannelConfig{}, Verify::ReadBack).ok(); },
      [](Driver& d) { return d.SetHoldCode(1, 3, Verify::ReadBack).ok(); },
      [](Driver& d) { return d.SetDpm({}, Verify::ReadBack).ok(); },
      [](Driver& d) { return d.Deinitialize().ok(); },
  };
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(port.state.chip.cmd);
  for (const Op op : ops) {
    REQUIRE(op(d));
    CHECK(port.state.chip.cmd);
  }
  // Every data transfer had CMD low, every command byte CMD high: the model counts a data phase
  // only with CMD low, so the chip took them all.
  CHECK(port.state.chip.data_phases > 0);
}
