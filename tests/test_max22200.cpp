/**
 * @file test_max22200.cpp
 * @brief The driver against the protocol model: lifecycle, the start-up procedure of Figure 6,
 * switching, settings, diagnostics and the unit conversions, each checked against the datasheet.
 */
#include <array>
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"
#include "test_harness.hpp"

using max22200::BridgeState;
using max22200::ChannelConfig;
using max22200::ChopDivider;
using max22200::Config;
using max22200::DriveMode;
using max22200::Error;
using max22200::Faults;
using max22200::MasterClock;
using max22200::PairMode;
using max22200::Side;
using max22200::Verify;
using max22200_test::FullPort;
using max22200_test::MinimalPort;
namespace reg = max22200::reg;

namespace {

using Driver = max22200::MAX22200<MinimalPort>;
using FullDriver = max22200::MAX22200<FullPort>;

/// A solenoid channel: CDR, low side, HIT 500 mA for 20 ms then HOLD 200 mA at RREF = 15 kohm.
ChannelConfig Solenoid() {
  ChannelConfig c{};
  c.hit = max22200::CurrentCode(500, 15000, false).value;
  c.hold = max22200::CurrentCode(200, 15000, false).value;
  c.hit_time = max22200::HitTimeCode(20000, MasterClock::Khz100, ChopDivider::Div4).value;
  c.open_load_detect = true;
  return c;
}

/// The reset configuration with a load declared on every channel (KB-CPP-131).
Config Loaded() {
  Config c{};
  c.loaded_channels = 0xFF;
  return c;
}

Config SolenoidBank() {
  Config c{};
  c.rref_ohm = 15000;
  c.loaded_channels = 0xFF; // a valve on every channel: open-load detection allowed
  for (auto& ch : c.channels) {
    ch = Solenoid();
  }
  return c;
}

} // namespace

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

TEST_CASE("Before EnsureInitialized every call returns NotInitialized and sends nothing") {
  MinimalPort port;
  Driver d(port);
  CHECK(!d.IsInitialized());
  CHECK_EQ(d.SetChannelsOn(0x01).error, Error::NotInitialized);
  CHECK_EQ(d.SetChannel(0, true).error, Error::NotInitialized);
  CHECK_EQ(d.SetBridge(0, BridgeState::Forward).error, Error::NotInitialized);
  CHECK_EQ(d.ReadChannelsOn().error, Error::NotInitialized);
  CHECK_EQ(d.ConfigureChannel(0, ChannelConfig{}).error, Error::NotInitialized);
  CHECK_EQ(d.SetHoldCode(0, 1).error, Error::NotInitialized);
  CHECK_EQ(d.SetPairMode(0, PairMode::Parallel).error, Error::NotInitialized);
  CHECK_EQ(d.SetFaultMask({}).error, Error::NotInitialized);
  CHECK_EQ(d.SetDpm({}).error, Error::NotInitialized);
  CHECK_EQ(d.ReadChannelConfig(0).error, Error::NotInitialized);
  CHECK_EQ(d.ReadStatus().error, Error::NotInitialized);
  CHECK_EQ(d.ReadFaults().error, Error::NotInitialized);
  CHECK_EQ(d.ClearFaults().error, Error::NotInitialized);
  CHECK_EQ(d.ReadFaultPin().error, Error::NotInitialized);
  CHECK(d.Deinitialize().ok());
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(port.state.pin_calls, 0);
  CHECK_EQ(port.state.bus_up_calls, 0);
}

TEST_CASE("EnsureInitialized follows Figure 6: STATUS read, wake, STATUS write, channels, checks") {
  MinimalPort port;
  const Config cfg = SolenoidBank();
  Driver d(port, cfg);
  REQUIRE(d.EnsureInitialized().ok());
  const auto& s = port.state;
  CHECK_EQ(s.transfers, 48);
  CHECK_EQ(s.bad_transfers, 0);
  CHECK_EQ(s.chip.early_accesses, 0); // tEN waited before the first access
  CHECK(s.chip.enable);
  CHECK(s.chip.cmd);     // CMD idles high
  CHECK(s.chip.Awake()); // tWU has passed: a channel switched on now drives its output
  CHECK(port.delayed_us >= reg::kEnableUs + reg::kWakeUpUs);
  // The command bytes in order: read STATUS, the bare ACTIVE write, read STATUS (awake), write
  // STATUS, write CFG_CH0..7 and CFG_DPM, read STATUS, read and clear FAULT, read CFG_CH0..7 and
  // CFG_DPM.
  const std::array<uint8_t, 24> want = {0x00, 0x80, 0x00, 0x80, 0x82, 0x84, 0x86, 0x88,
                                        0x8A, 0x8C, 0x8E, 0x90, 0x94, 0x00, 0x12, 0x02,
                                        0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x14};
  for (std::size_t i = 0; i < want.size(); ++i) {
    CHECK(s.with_cmd[2 * i]);
    CHECK(!s.with_cmd[(2 * i) + 1]);
    CHECK_EQ(s.first_byte[2 * i], want[i]);
  }
  CHECK_EQ(s.first_byte[3], 0x01);  // the bare ACTIVE write: LSB byte first, only bit 0
  CHECK_EQ(s.first_byte[29], 0xFF); // the FAULT read: every SDI bit high clears every flag
  CHECK_EQ(s.chip.fault_reads, 1);
  CHECK_EQ(s.chip.regs[reg::kStatus] & reg::kWritableMask, max22200::EncodeStatus(cfg, 0, true));
  CHECK_EQ(s.chip.regs[reg::kStatus] & reg::kUvm, 0U); // step 1 cleared UVM
  for (uint8_t ch = 0; ch < 8; ++ch) {
    CHECK_EQ(s.chip.regs[reg::CfgChannel(ch)], max22200::EncodeChannel(cfg.channels[ch]));
  }
  CHECK_EQ(s.chip.reserved_writes, 0);
  CHECK(d.IsInitialized());
}

TEST_CASE("A second EnsureInitialized does no I/O") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  const int t = port.state.transfers;
  const int p = port.state.pin_calls;
  CHECK(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, t);
  CHECK_EQ(port.state.pin_calls, p);
}

TEST_CASE("An invalid Config fails EnsureInitialized with no I/O") {
  Config c{};
  c.channels[2].hit = 127; // CDR full-on, never a current
  MinimalPort port;
  Driver d(port, c);
  CHECK_EQ(d.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(port.state.pin_calls, 0);
  CHECK_EQ(port.state.bus_up_calls, 0);
}

TEST_CASE("A failed start leaves ENABLE low and is retried by the next call") {
  MinimalPort port;
  port.state.bus_up_ok = false;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::Bus);
  port.state.bus_up_ok = true;
  port.state.chip.miso = max22200_test::Miso::FloatHigh;
  CHECK_EQ(d.EnsureInitialized().error, Error::NoDevice);
  CHECK(!port.state.chip.enable);
  port.state.chip.miso = max22200_test::Miso::Chip;
  CHECK(d.EnsureInitialized().ok());
  CHECK(port.state.chip.enable);
}

TEST_CASE("UVM stuck at the wake timeout: Undervoltage, uninitialised, still enabled for a retry") {
  Config c{};
  c.wake_timeout_us = 50000;
  MinimalPort port;
  port.state.chip.vm_low = true;
  Driver d(port, c);
  CHECK_EQ(d.EnsureInitialized().error, Error::Undervoltage);
  CHECK(!d.IsInitialized());
  CHECK(port.delayed_us >= reg::kEnableUs + c.wake_timeout_us);
  CHECK(port.delayed_us < reg::kEnableUs + c.wake_timeout_us + max22200::kWakePollUs);
  CHECK(port.state.chip.rejected_wakes > 0); // the chip refuses ACTIVE while UVM is set
  CHECK(port.state.chip.enable);             // ENABLE low would restart the wake
  CHECK_EQ(port.state.chip.OnMask(), 0U);
  port.state.chip.vm_low = false; // the supply recovers: the next call starts
  CHECK(d.EnsureInitialized().ok());
}

TEST_CASE("UVM set for a while: the wake loop re-pokes ACTIVE until it latches") {
  MinimalPort port;
  port.state.chip.uvm_sticky_reads = 3; // VM still rising: three reads leave UVM set
  Driver d(port);
  CHECK(d.EnsureInitialized().ok());
  CHECK(port.state.chip.rejected_wakes >= 2);
  CHECK((port.state.chip.regs[reg::kStatus] & reg::kActive) != 0U);
}

TEST_CASE(
    "A slow supply: ACTIVE is ignored for 75 ms, then latches (Figure 6, wake-up completed?)") {
  MinimalPort port;
  port.state.chip.wake_settle_us = 75000;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(port.state.chip.rejected_wakes >= 7);
  CHECK(port.delayed_us >= 75000U);
  CHECK(port.state.chip.Awake());
  // The rejected writes raised COMER; the start cleared it with ENABLE low, so a later miscount
  // shows.
  CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kComer, 0U);
}

TEST_CASE("ACTIVE never latches within the wake timeout: Timeout") {
  Config c{};
  c.wake_timeout_us = 20000;
  MinimalPort port;
  port.state.chip.wake_settle_us = 1000000;
  Driver d(port, c);
  CHECK_EQ(d.EnsureInitialized().error, Error::Timeout);
  CHECK(!d.IsInitialized());
  CHECK(port.state.chip.enable);
  c.wake_timeout_us = 2000; // below one tWU
  CHECK_EQ(d.ApplyConfig(c).error, Error::InvalidArgument);
  c.wake_timeout_us = 10000001;
  CHECK_EQ(d.ApplyConfig(c).error, Error::InvalidArgument);
}

TEST_CASE("Deinitialize: every channel off, ACTIVE = 0, ENABLE low; then a restart") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x0F).ok());
  const int t = port.state.transfers;
  CHECK(d.Deinitialize().ok());
  CHECK_EQ(port.state.transfers - t, 2);
  CHECK_EQ(port.state.chip.OnMask(), 0U);
  CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kActive, 0U);
  CHECK(!port.state.chip.enable);
  CHECK(!d.IsInitialized());
  CHECK_EQ(d.GetChannelsOn(), 0U);
  CHECK_EQ(d.SetChannelsOn(0x01).error, Error::NotInitialized);
  port.state.chip.Elapse(1000);
  CHECK(d.EnsureInitialized().ok());
  CHECK(d.SetChannelsOn(0x01).ok());
}

TEST_CASE("ApplyConfig before the start stores it with no I/O") {
  MinimalPort port;
  Driver d(port);
  const Config c = SolenoidBank();
  CHECK(d.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(d.GetConfig().channels[5].hit, c.channels[5].hit);
  Config bad = c;
  bad.rref_ohm = 10000; // below the recommended range
  CHECK_EQ(d.ApplyConfig(bad).error, Error::InvalidArgument);
}

TEST_CASE("ApplyConfig on a running driver writes only what changed") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x01).ok());
  Config c = d.GetConfig();
  c.channels[3].hold = 10;
  c.fault_mask.open_load = true;
  c.dpm.start = 20;
  const int t = port.state.transfers;
  CHECK(d.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers - t, 6);     // STATUS, CFG_CH3, CFG_DPM
  CHECK_EQ(port.state.chip.OnMask(), 0x01U); // the STATUS write kept ONCH
  CHECK((port.state.chip.regs[reg::kStatus] & reg::kMaskOlf) != 0U);
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(3)], max22200::EncodeChannel(c.channels[3]));
  const int t2 = port.state.transfers;
  CHECK(d.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers, t2); // nothing changed, nothing sent
}

TEST_CASE("ApplyConfig refuses a mode change while a channel is on, and sends nothing") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x80).ok());
  Config c = d.GetConfig();
  c.channels[0].drive = DriveMode::Vdr;
  c.channels[0].hit = 50;
  c.channels[0].hold = 30;
  const int t = port.state.transfers;
  CHECK_EQ(d.ApplyConfig(c).error, Error::ChannelsActive);
  Config p = d.GetConfig();
  p.pairs[3] = PairMode::Parallel; // channel 7 is on
  CHECK_EQ(d.ApplyConfig(p).error, Error::ChannelsActive);
  CHECK_EQ(port.state.transfers, t);
  REQUIRE(d.SetChannelsOn(0x00).ok());
  CHECK(d.ApplyConfig(c).ok());
  CHECK_EQ(max22200::DecodeChannel(port.state.chip.regs[reg::CfgChannel(0)]).drive, DriveMode::Vdr);
}

// -------------------------------------------------------------------------------------------------
// Switching
// -------------------------------------------------------------------------------------------------

TEST_CASE("SetChannelsOn is one 8-bit ONCH write: 2 transfers") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  const int t = port.state.transfers;
  CHECK(d.SetChannelsOn(0xA5).ok());
  CHECK_EQ(port.state.transfers - t, 2);
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t)], 0x81); // write, STATUS, 8 bit
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t) + 1], 0xA5);
  CHECK_EQ(port.state.chip.OnMask(), 0xA5U);
  CHECK_EQ(d.GetChannelsOn(), 0xA5U);
  CHECK((port.state.chip.regs[reg::kStatus] & reg::kActive) != 0U); // the low bytes are untouched
}

TEST_CASE("SetChannelsOn with ReadBack reads ONCH back: 4 transfers") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  const int t = port.state.transfers;
  CHECK(d.SetChannelsOn(0x3C, Verify::ReadBack).ok());
  CHECK_EQ(port.state.transfers - t, 4);
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t) + 2], 0x01); // read STATUS, 8 bit
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetChannelsOn(0x00, Verify::ReadBack).error, Error::WriteMismatch);
  port.state.chip.comer_writes = 1;
  CHECK_EQ(d.SetChannelsOn(0x00, Verify::ReadBack).error, Error::CommunicationError);
  CHECK(d.SetChannelsOn(0x00, Verify::ReadBack).ok());
  const auto bad_verify =
      static_cast<Verify>(2); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(d.SetChannelsOn(0x00, bad_verify).error, Error::InvalidArgument);
}

TEST_CASE("SetChannel switches one channel from the driver's copy, with no STATUS read") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x11).ok());
  const int reads = port.state.chip.status_reads;
  const int t = port.state.transfers;
  CHECK(d.SetChannel(6, true).ok());
  CHECK(d.SetChannel(0, false).ok());
  CHECK_EQ(port.state.transfers - t, 2); // one data byte each: the ONCH command is in force
  CHECK_EQ(port.state.chip.status_reads, reads);
  CHECK_EQ(port.state.chip.OnMask(), 0x50U);
  CHECK_EQ(d.SetChannel(8, true).error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers - t, 2);
}

TEST_CASE("A parallel pair switches as one: both ONCH bits (Table 6)") {
  Config c = SolenoidBank();
  c.pairs[1] = PairMode::Parallel; // channels 2 and 3
  MinimalPort port;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ((port.state.chip.regs[reg::kStatus] >> reg::PairModeShift(1)) & 3U, 1U);
  CHECK(d.SetChannel(3, true).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0x0CU);
  CHECK(d.SetChannel(2, false).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0x00U);
  const int t = port.state.transfers;
  CHECK_EQ(d.SetChannelsOn(0x04).error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, t);
  CHECK(d.SetChannelsOn(0x0C).ok());
}

TEST_CASE("SetBridge writes the ONCH pattern of Table 7") {
  Config c{};
  c.pairs[2] = PairMode::FullBridge; // channels 4 (x) and 5 (y)
  MinimalPort port;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(0, true).ok());
  CHECK(d.SetBridge(2, BridgeState::Forward).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0x11U); // ONCHx = 1, ONCHy = 0
  CHECK(d.SetBridge(2, BridgeState::Reverse).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0x21U);
  CHECK(d.SetBridge(2, BridgeState::Brake).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0x31U);
  CHECK(d.SetBridge(2, BridgeState::HiZ, Verify::ReadBack).ok());
  CHECK_EQ(port.state.chip.OnMask(), 0x01U);
  CHECK_EQ(d.SetBridge(1, BridgeState::Forward).error, Error::InvalidArgument); // not a bridge
  CHECK_EQ(d.SetBridge(4, BridgeState::Forward).error, Error::InvalidArgument);
  CHECK_EQ(d.SetChannel(4, true).error, Error::InvalidArgument);
}

TEST_CASE("ReadChannelsOn reads ONCH and leaves the driver's copy alone") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x05).ok());
  port.state.chip.regs[reg::kStatus] &= 0x00FFFFFFUL; // the chip lost it
  const auto r = d.ReadChannelsOn();
  REQUIRE(r.ok());
  CHECK_EQ(r.value, 0U);
  CHECK_EQ(d.GetChannelsOn(), 0x05U);
}

// -------------------------------------------------------------------------------------------------
// Settings
// -------------------------------------------------------------------------------------------------

TEST_CASE("ConfigureChannel writes CFG_CHn, LSB byte first (Figure 10)") {
  MinimalPort port;
  Driver d(port, Loaded());
  REQUIRE(d.EnsureInitialized().ok());
  const ChannelConfig c = Solenoid();
  const int t = port.state.transfers;
  CHECK(d.ConfigureChannel(6, c).ok());
  CHECK_EQ(port.state.transfers - t, 2);
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t)], 0x8E);
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t) + 1],
           static_cast<uint8_t>(max22200::EncodeChannel(c) & 0xFFU));
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(6)], max22200::EncodeChannel(c));
  CHECK(d.GetConfig().channels[6] == c);
  const auto back = d.ReadChannelConfig(6);
  REQUIRE(back.ok());
  CHECK(back.value == c);
}

TEST_CASE("ConfigureChannel with ReadBack: 4 transfers; a lost write is WriteMismatch") {
  MinimalPort port;
  Driver d(port, Loaded());
  REQUIRE(d.EnsureInitialized().ok());
  const int t = port.state.transfers;
  CHECK(d.ConfigureChannel(1, Solenoid(), Verify::ReadBack).ok());
  CHECK_EQ(port.state.transfers - t, 4);
  port.state.chip.drop_writes = 1;
  ChannelConfig c = Solenoid();
  c.hold = 7;
  CHECK_EQ(d.ConfigureChannel(1, c, Verify::ReadBack).error, Error::WriteMismatch);
  port.state.chip.comer_writes = 1;
  CHECK_EQ(d.ConfigureChannel(1, c, Verify::ReadBack).error, Error::CommunicationError);
  CHECK(d.ConfigureChannel(1, c, Verify::ReadBack).ok());
}

TEST_CASE("ConfigureChannel: a drive mode or side change needs every channel off (5c)") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(7, true).ok());
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  v.side = Side::High;
  v.hit = 100;
  v.hold = 40;
  const int t = port.state.transfers;
  CHECK_EQ(d.ConfigureChannel(0, v).error, Error::ChannelsActive);
  CHECK_EQ(port.state.transfers, t);
  ChannelConfig same_mode = Solenoid();
  same_mode.chop = ChopDivider::Div1; // on the fly
  CHECK(d.ConfigureChannel(0, same_mode).ok());
  REQUIRE(d.SetChannelsOn(0).ok());
  CHECK(d.ConfigureChannel(0, v).ok());
  // A TRIG pin high: the chip ignores the change, which only the read-back sees.
  port.state.chip.trig_high = true;
  CHECK_EQ(d.ConfigureChannel(0, Solenoid(), Verify::ReadBack).error, Error::WriteMismatch);
}

TEST_CASE("ValidateChannel refuses what the datasheet forbids") {
  using max22200::ValidateChannel;
  ChannelConfig c{};
  CHECK_EQ(ValidateChannel(c, MasterClock::Khz100), Error::Ok);
  c.hit = 127; // Table 4: HS off, LS on, no regulation
  CHECK_EQ(ValidateChannel(c, MasterClock::Khz100), Error::InvalidArgument);
  c.hit = 126;
  CHECK_EQ(ValidateChannel(c, MasterClock::Khz100), Error::Ok);
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  v.hit = 100; // 100 % (Table 3)
  CHECK_EQ(ValidateChannel(v, MasterClock::Khz100), Error::Ok);
  v.hold = 101;
  CHECK_EQ(ValidateChannel(v, MasterClock::Khz100), Error::InvalidArgument);
  v.hold = 0;
  v.side = Side::High;
  CHECK_EQ(ValidateChannel(v, MasterClock::Khz100), Error::Ok);
  v.slew_rate_control = true; // SRC: low side only
  CHECK_EQ(ValidateChannel(v, MasterClock::Khz100), Error::InvalidArgument);
  v.slew_rate_control = false;
  v.half_scale = true; // HFS: low side only
  CHECK_EQ(ValidateChannel(v, MasterClock::Khz100), Error::InvalidArgument);
  v.half_scale = false;
  v.plunger_detect = true; // DPM: low side only
  CHECK_EQ(ValidateChannel(v, MasterClock::Khz100), Error::InvalidArgument);
  ChannelConfig hs{};
  hs.side = Side::High; // CDR is low side only
  CHECK_EQ(ValidateChannel(hs, MasterClock::Khz100), Error::InvalidArgument);
  ChannelConfig src{};
  src.slew_rate_control = true;
  src.chop = ChopDivider::Div3; // 33.3 kHz
  CHECK_EQ(ValidateChannel(src, MasterClock::Khz100), Error::Ok);
  src.chop = ChopDivider::Div2; // 50 kHz: not below 50
  CHECK_EQ(ValidateChannel(src, MasterClock::Khz100), Error::InvalidArgument);
  CHECK_EQ(ValidateChannel(src, MasterClock::Khz80), Error::Ok); // 40 kHz
  src.chop = ChopDivider::Div1;
  CHECK_EQ(ValidateChannel(src, MasterClock::Khz80), Error::InvalidArgument);
  ChannelConfig hhf{};
  hhf.drive = DriveMode::Vdr;
  hhf.hit_current_check = true; // CDR only
  CHECK_EQ(ValidateChannel(hhf, MasterClock::Khz100), Error::InvalidArgument);
  ChannelConfig bad{};
  bad.chop = static_cast<ChopDivider>(4); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(ValidateChannel(bad, MasterClock::Khz100), Error::InvalidArgument);
}

TEST_CASE("Validate checks RREF, pair modes and DPM widths") {
  Config c{};
  CHECK_EQ(max22200::Validate(c), Error::Ok);
  c.rref_ohm = 15000;
  CHECK_EQ(max22200::Validate(c), Error::Ok);
  c.rref_ohm = 100001;
  CHECK_EQ(max22200::Validate(c), Error::InvalidArgument);
  c.rref_ohm = 0;
  c.pairs[1] = static_cast<PairMode>(3); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(max22200::Validate(c), Error::InvalidArgument);
  c.pairs[1] = PairMode::Independent;
  c.dpm.threshold = 16;
  CHECK_EQ(max22200::Validate(c), Error::InvalidArgument);
  c.dpm.threshold = 15;
  c.clock = static_cast<MasterClock>(2); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(max22200::Validate(c), Error::InvalidArgument);
}

TEST_CASE("SetHoldCode is one 8-bit write of HFS and HOLD (5b)") {
  Config c = SolenoidBank();
  c.channels[2].half_scale = true;
  MinimalPort port;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(2, true).ok());
  const int t = port.state.transfers;
  CHECK(d.SetHoldCode(2, 40).ok()); // on the fly, the channel stays on
  CHECK_EQ(port.state.transfers - t, 2);
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t)], 0x87); // write CFG_CH2, 8 bit
  CHECK_EQ(port.state.first_byte[static_cast<std::size_t>(t) + 1], 0x80 | 40);
  CHECK_EQ(max22200::DecodeChannel(port.state.chip.regs[reg::CfgChannel(2)]).hold, 40U);
  CHECK(max22200::DecodeChannel(port.state.chip.regs[reg::CfgChannel(2)]).half_scale);
  CHECK_EQ(d.GetConfig().channels[2].hold, 40U);
  CHECK(d.SetHoldCode(2, 41, Verify::ReadBack).ok());
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetHoldCode(2, 42, Verify::ReadBack).error, Error::WriteMismatch);
  port.state.chip.comer_writes = 1;
  CHECK_EQ(d.SetHoldCode(2, 42, Verify::ReadBack).error, Error::CommunicationError);
  CHECK_EQ(d.SetHoldCode(2, 127).error, Error::InvalidArgument);
  CHECK_EQ(d.SetHoldCode(8, 1).error, Error::InvalidArgument);
}

TEST_CASE("SetPairMode needs both channels of the pair off") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(1, true).ok());
  const int t = port.state.transfers;
  CHECK_EQ(d.SetPairMode(0, PairMode::FullBridge).error, Error::ChannelsActive);
  CHECK_EQ(port.state.transfers, t);
  CHECK(d.SetPairMode(3, PairMode::FullBridge, Verify::ReadBack).ok());
  CHECK_EQ((port.state.chip.regs[reg::kStatus] >> reg::PairModeShift(3)) & 3U, 2U);
  CHECK_EQ(port.state.chip.OnMask(), 0x02U); // ONCH kept by the STATUS write
  CHECK_EQ(d.GetConfig().pairs[3], PairMode::FullBridge);
  CHECK_EQ(d.SetPairMode(4, PairMode::Parallel).error, Error::InvalidArgument);
  const auto reserved =
      static_cast<PairMode>(3); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(d.SetPairMode(2, reserved).error, Error::InvalidArgument);
}

TEST_CASE("SetFaultMask and SetDpm write their registers") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  max22200::FaultMask m{};
  m.open_load = true;
  m.communication = false;
  CHECK(d.SetFaultMask(m, Verify::ReadBack).ok());
  CHECK((port.state.chip.regs[reg::kStatus] & reg::kMaskOlf) != 0U);
  CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kMaskComf, 0U);
  port.state.chip.drop_writes = 1;
  CHECK_EQ(d.SetFaultMask({}, Verify::ReadBack).error, Error::WriteMismatch);
  const max22200::DpmConfig dpm{.start = 90, .debounce = 5, .threshold = 3};
  CHECK(d.SetDpm(dpm, Verify::ReadBack).ok());
  CHECK_EQ(port.state.chip.regs[reg::kCfgDpm], (90UL << 8U) | (5UL << 4U) | 3UL);
  CHECK_EQ(d.SetDpm({.start = 128, .debounce = 0, .threshold = 0}).error, Error::InvalidArgument);
  CHECK_EQ(d.SetDpm({.start = 0, .debounce = 16, .threshold = 0}).error, Error::InvalidArgument);
  CHECK_EQ(d.SetDpm({.start = 0, .debounce = 0, .threshold = 16}).error, Error::InvalidArgument);
  const auto bad_verify =
      static_cast<Verify>(9); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(d.SetFaultMask({}, bad_verify).error, Error::InvalidArgument);
  CHECK_EQ(port.state.chip.reserved_writes, 0);
}

TEST_CASE("ReadChannelConfig refuses a channel above 7") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(d.ReadChannelConfig(8).error, Error::InvalidArgument);
}

// -------------------------------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------------------------------

TEST_CASE("ReadStatus decodes the flags; the read clears OVT and UVM") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x03).ok());
  port.state.chip.Overtemperature();
  port.state.chip.Undervoltage();
  port.state.chip.RaiseFault(max22200_test::Fault::Olf, 1);
  const auto s = d.ReadStatus();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.on_mask, 0x03U);
  CHECK(s.value.overtemperature);
  CHECK(s.value.undervoltage);
  CHECK(s.value.open_load);
  CHECK(!s.value.overcurrent);
  CHECK(s.value.active);
  CHECK(s.value.AnyFlag());
  const auto s2 = d.ReadStatus();
  REQUIRE(s2.ok());
  CHECK(!s2.value.overtemperature);
  CHECK(!s2.value.undervoltage);
  CHECK(s2.value.open_load); // cleared through FAULT only
}

TEST_CASE("ReadStatus after a power-on reset: DeviceReset, then a restart") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannelsOn(0x01).ok());
  port.state.chip.Brownout();
  CHECK_EQ(d.ReadStatus().error, Error::DeviceReset);
  CHECK(!d.IsInitialized());
  CHECK_EQ(d.SetChannelsOn(0x01).error, Error::NotInitialized);
  CHECK(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.regs[reg::CfgChannel(0)],
           max22200::EncodeChannel(d.GetConfig().channels[0]));
}

TEST_CASE("MAX22200A: ReadFaults keeps the flags, ClearFaults clears only those asked") {
  Config cfg{};
  cfg.variant = max22200::Variant::Max22200A;
  MinimalPort port;
  port.state.chip.fault_sdi_msb_first = !reg::kFaultClearLsbByteFirst; // the order the driver uses
  Driver d(port, cfg);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(max22200_test::Fault::Ocp, 4);
  port.state.chip.RaiseFault(max22200_test::Fault::Dpm, 4);
  port.state.chip.RaiseFault(max22200_test::Fault::Olf, 0);
  const auto f = d.ReadFaults();
  REQUIRE(f.ok());
  CHECK_EQ(f.value.overcurrent, 0x10U);
  CHECK_EQ(f.value.plunger, 0x10U);
  CHECK_EQ(f.value.open_load, 0x01U);
  CHECK_EQ(f.value.Channels(), 0x11U);
  CHECK(f.value.Any());
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0x10000110UL); // nothing cleared
  // Figure 8: clear OCP[4] only. The DPM flag of the same channel must stay.
  const auto c = d.ClearFaults(Faults{.overcurrent = 0x10});
  REQUIRE(c.ok());
  CHECK(c.value == f.value); // the flags before the clear
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0x00000110UL);
  CHECK(d.ClearFaults().ok());
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U);
  CHECK_EQ(port.state.chip.regs[reg::kStatus] & reg::kFlagsMask, 0U);
}

TEST_CASE("MAX22200: every FAULT read clears every flag; a selective clear is Unsupported") {
  Config c{};
  c.variant = max22200::Variant::Max22200;
  MinimalPort port;
  port.state.chip.part = max22200_test::Part::Max22200;
  Driver d(port, c);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(max22200_test::Fault::Hhf, 2);
  const int t = port.state.transfers;
  CHECK_EQ(d.ClearFaults(Faults{.hit_not_reached = 0x04}).error, Error::Unsupported);
  CHECK_EQ(port.state.transfers, t);
  const auto f = d.ReadFaults();
  REQUIRE(f.ok());
  CHECK_EQ(f.value.hit_not_reached, 0x04U);
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U);
  port.state.chip.RaiseFault(max22200_test::Fault::Hhf, 2);
  CHECK(d.ClearFaults().ok());
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0U);
}

TEST_CASE("ReadFaultPin follows the pin, through the optional hook only") {
  FullPort port;
  FullDriver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  auto p = d.ReadFaultPin();
  REQUIRE(p.ok());
  CHECK(!p.value);
  port.state.chip.RaiseFault(max22200_test::Fault::Ocp, 0);
  p = d.ReadFaultPin();
  REQUIRE(p.ok());
  CHECK(p.value);
  port.fault_read_ok = false;
  CHECK_EQ(d.ReadFaultPin().error, Error::Pin);
  CHECK(port.error_lines > 0);
  MinimalPort minimal;
  Driver m(minimal);
  REQUIRE(m.EnsureInitialized().ok());
  CHECK_EQ(m.ReadFaultPin().error, Error::Unsupported);
}

TEST_CASE("The fault byte of every command is kept, at no cost") {
  MinimalPort port;
  Driver d(port);
  CHECK_EQ(d.GetLastStatusByte(), 0xFFU);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(d.GetLastStatusByte(), 0x01U); // ACTIVE
  port.state.chip.Overtemperature();
  REQUIRE(d.SetChannelsOn(0).ok());
  CHECK_EQ(d.GetLastStatusByte(), 0x81U);
}

TEST_CASE("FullPort logs the start and each failure") {
  FullPort port;
  FullDriver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(port.log_lines, 1);
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(d.SetChannelsOn(1).error, Error::Bus);
  CHECK_EQ(port.error_lines, 1);
}

// -------------------------------------------------------------------------------------------------
// Encoding and units
// -------------------------------------------------------------------------------------------------

TEST_CASE("Register encoding round-trips; Config{} is the reset state") {
  CHECK_EQ(max22200::EncodeStatus(Config{}, 0, false), reg::kStatusReset & reg::kWritableMask);
  CHECK_EQ(max22200::EncodeChannel(ChannelConfig{}), 0U);
  CHECK_EQ(max22200::EncodeDpm({}), 0U);
  ChannelConfig c{};
  c.drive = DriveMode::Vdr;
  c.side = Side::High;
  c.hit = 100;
  c.hold = 33;
  c.hit_time = 255;
  c.chop = ChopDivider::Div1;
  c.trigger_pin = true;
  c.open_load_detect = true;
  CHECK(max22200::DecodeChannel(max22200::EncodeChannel(c)) == c);
  CHECK_EQ(max22200::EncodeChannel(c), 0x21E4FFF4UL);
  ChannelConfig l{};
  l.half_scale = true;
  l.slew_rate_control = true;
  l.plunger_detect = true;
  l.hit_current_check = true;
  CHECK_EQ(max22200::EncodeChannel(l), 0x8000000BUL);
  CHECK(max22200::DecodeChannel(max22200::EncodeChannel(l)) == l);
  const max22200::DpmConfig d{.start = 127, .debounce = 15, .threshold = 15};
  CHECK_EQ(max22200::EncodeDpm(d), 0x7FFFUL);
  CHECK(max22200::DecodeDpm(0xFFFFFFFFUL) == d);
  Config s{};
  s.clock = MasterClock::Khz80;
  s.pairs = {PairMode::Parallel, PairMode::FullBridge, PairMode::Independent, PairMode::FullBridge};
  s.fault_mask = {true, true, true, true, true, false, true};
  CHECK_EQ(max22200::EncodeStatus(s, 0xF0, true), 0xF0FB8901UL);
  const auto st = max22200::DecodeStatus(0xF0FB89FFUL);
  CHECK_EQ(st.on_mask, 0xF0U);
  CHECK(st.overtemperature && st.overcurrent && st.open_load && st.hit_not_reached && st.plunger &&
        st.communication && st.undervoltage && st.active);
  CHECK(max22200::DecodeFaults(0x01020408U) ==
        (Faults{.overcurrent = 1, .hit_not_reached = 2, .open_load = 4, .plunger = 8}));
}

TEST_CASE("CDR currents: IFS = 15k x 1 V / RREF, I = code / 128 x IFS (Table 4)") {
  CHECK_EQ(max22200::FullScaleUa(15000, false).value, 1000000U);
  CHECK_EQ(max22200::FullScaleUa(30000, false).value, 500000U);
  CHECK_EQ(max22200::FullScaleUa(15000, true).value, 500000U);                 // HFS: KFS 7.5k
  CHECK_EQ(max22200::FullScaleUa(14999, false).error, Error::InvalidArgument); // no sentinel 0
  CHECK_EQ(max22200::FullScaleUa(100001, false).error, Error::InvalidArgument);
  CHECK_EQ(max22200::FullScaleUa(max22200::kNoRref, false).error, Error::InvalidArgument);
  CHECK_EQ(max22200::CurrentCode(500, 15000, false).value, 64U);
  CHECK_EQ(max22200::CurrentUa(64, 15000, false).value, 500000U);
  CHECK_EQ(max22200::CurrentUa(64, max22200::kNoRref, false).error, Error::InvalidArgument);
  CHECK_EQ(max22200::CurrentCode(500, max22200::kNoRref, false).error, Error::InvalidArgument);
  CHECK_EQ(max22200::DpmCurrentCode(100, max22200::kNoRref, 127).error, Error::InvalidArgument);
  CHECK_EQ(max22200::Config{}.rref_ohm, max22200::kNoRref);
  CHECK_EQ(max22200::CurrentCode(984, 15000, false).value, 126U);                   // 126/128 A
  CHECK_EQ(max22200::CurrentCode(990, 15000, false).error, Error::InvalidArgument); // code 127
  CHECK_EQ(max22200::CurrentCode(1000, 15000, false).error, Error::InvalidArgument);
  CHECK_EQ(max22200::CurrentCode(0, 15000, false).value, 0U);
  CHECK_EQ(max22200::CurrentCode(3, 15000, false).error, Error::InvalidArgument); // < 1/2 LSB
  CHECK_EQ(max22200::CurrentCode(250, 15000, true).value, 64U);
  CHECK_EQ(max22200::CurrentCode(100, 0, false).error, Error::InvalidArgument);
  CHECK_EQ(max22200::CurrentUa(127, 15000, false).error, Error::InvalidArgument);
  CHECK_EQ(max22200::CurrentUa(0, 15000, false).value, 0U);
}

TEST_CASE("DPM currents use / 127 (CFG_DPM)") {
  CHECK_EQ(max22200::DpmCurrentCode(1000, 15000, 127).value, 127U);
  CHECK_EQ(max22200::DpmCurrentCode(118, 15000, 15).value, 15U);
  CHECK_EQ(max22200::DpmCurrentCode(130, 15000, 15).error, Error::InvalidArgument);
  CHECK_EQ(max22200::DpmCurrentCode(10, 5000, 15).error, Error::InvalidArgument);
}

TEST_CASE("HIT time: code x 40 / fCHOP, exact at 33.3 and 26.7 kHz (Table 5)") {
  using max22200::HitTimeCode;
  using max22200::HitTimeUs;
  // The datasheet's example: 20 kHz gives 0 to 508 ms in 2 ms steps.
  CHECK_EQ(HitTimeUs(1, MasterClock::Khz80, ChopDivider::Div4).value, 2000U);
  CHECK_EQ(HitTimeUs(254, MasterClock::Khz80, ChopDivider::Div4).value, 508000U);
  CHECK_EQ(HitTimeUs(100, MasterClock::Khz100, ChopDivider::Div3).value, 120000U); // not 121.2 ms
  CHECK_EQ(HitTimeUs(100, MasterClock::Khz80, ChopDivider::Div3).value, 150000U);
  CHECK_EQ(HitTimeUs(0, MasterClock::Khz100, ChopDivider::Div1).value, 0U);
  CHECK_EQ(HitTimeUs(255, MasterClock::Khz100, ChopDivider::Div1).error, Error::InvalidArgument);
  CHECK_EQ(HitTimeCode(120000, MasterClock::Khz100, ChopDivider::Div3).value, 100U);
  CHECK_EQ(HitTimeCode(0, MasterClock::Khz100, ChopDivider::Div3).value, 0U);
  CHECK_EQ(HitTimeCode(1, MasterClock::Khz100, ChopDivider::Div1).value, 1U); // never "none"
  CHECK_EQ(HitTimeCode(406400, MasterClock::Khz100, ChopDivider::Div4).value, 254U);
  // Beyond 254 steps is refused: 255 would be a HIT phase that never ends.
  CHECK_EQ(HitTimeCode(408000, MasterClock::Khz100, ChopDivider::Div4).error,
           Error::InvalidArgument);
  CHECK_EQ(max22200::DebounceUs(15, MasterClock::Khz100, ChopDivider::Div4), 600U);
  CHECK_EQ(max22200::ChopPeriodNs(MasterClock::Khz100, ChopDivider::Div1), 10000U);
  CHECK_EQ(max22200::ChopPeriodNs(MasterClock::Khz80, ChopDivider::Div4), 50000U);
}

TEST_CASE("Duty limits of Table 2") {
  CHECK_EQ(max22200::DutyLimitsOf(ChopDivider::Div1, false).min_percent, 8U);
  CHECK_EQ(max22200::DutyLimitsOf(ChopDivider::Div1, false).max_percent, 92U);
  CHECK_EQ(max22200::DutyLimitsOf(ChopDivider::Div4, false).min_percent, 4U);
  CHECK_EQ(max22200::DutyLimitsOf(ChopDivider::Div3, true).max_percent, 93U);
}

TEST_CASE("Error names") {
  CHECK(max22200::ToString(Error::Ok)[0] == 'O');
  CHECK(max22200::ToString(Error::DeviceReset)[0] == 'D');
  const auto gap = static_cast<Error>(12); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  const auto beyond =
      static_cast<Error>(200); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK(max22200::ToString(gap)[0] == 'U');
  CHECK(max22200::ToString(beyond)[0] == 'U');
}

TEST_CASE("After a failed ONCH write, its channels count as on for the mode-change check") {
  MinimalPort port;
  Driver d(port, SolenoidBank());
  REQUIRE(d.EnsureInitialized().ok());
  // The data byte reached the chip, then the port reported a failure: channel 2 may be on.
  port.state.fail_transfer = port.state.transfers + 1;
  port.state.fail_after_clocking = true;
  CHECK_EQ(d.SetChannel(2, true).error, Error::Bus);
  CHECK_EQ(d.GetChannelsOn(), 0U);           // the copy holds only what went out
  CHECK_EQ(port.state.chip.OnMask(), 0x04U); // the chip took it
  ChannelConfig v{};
  v.drive = DriveMode::Vdr;
  v.hit = 100;
  CHECK_EQ(d.ConfigureChannel(0, v).error, Error::ChannelsActive);
  REQUIRE(d.SetChannelsOn(0x00).ok()); // a successful write settles it
  CHECK(d.ConfigureChannel(0, v).ok());
  // ReadChannelsOn finds a channel on that the copy does not know of.
  port.state.chip.regs[reg::kStatus] |= 1UL << reg::kOnchShift;
  REQUIRE(d.ReadChannelsOn().ok());
  CHECK_EQ(d.ConfigureChannel(0, Solenoid()).error, Error::ChannelsActive);
}

TEST_CASE("Start-up: a STATUS read-back without ACTIVE is DeviceIdMismatch") {
  MinimalPort port;
  port.state.replace_transfer = 23; // the data of the STATUS read after tWU
  port.state.replace_bytes = {0x00, 0x04, 0x00, 0x00};
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::DeviceIdMismatch);
}

TEST_CASE("Setters refuse bad arguments before any transfer") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  const int t = port.state.transfers;
  ChannelConfig bad{};
  bad.hit = 127;
  CHECK_EQ(d.ConfigureChannel(0, bad).error, Error::InvalidArgument);
  CHECK_EQ(d.ConfigureChannel(8, ChannelConfig{}).error, Error::InvalidArgument);
  CHECK_EQ(d.SetDpm({.start = 1, .debounce = 1, .threshold = 1}).ok(), true);
  CHECK_EQ(port.state.transfers - t, 2);
}

TEST_CASE("A selective clear in the other SDI byte order clears the wrong flag type") {
  // Why kFaultClearLsbByteFirst needs a bench check: against a chip that reads the other order,
  // asking for OCP[4] clears DPM[4] and leaves OCP[4].
  Config cfg{};
  cfg.variant = max22200::Variant::Max22200A;
  MinimalPort port;
  port.state.chip.fault_sdi_msb_first = reg::kFaultClearLsbByteFirst; // the other order
  Driver d(port, cfg);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.RaiseFault(max22200_test::Fault::Ocp, 4);
  port.state.chip.RaiseFault(max22200_test::Fault::Dpm, 4);
  REQUIRE(d.ClearFaults(Faults{.overcurrent = 0x10}).ok());
  CHECK_EQ(port.state.chip.regs[reg::kFault], 0x10000000UL);
}

TEST_CASE("FaultClearSdi follows the configured byte order") {
  constexpr uint32_t kMask = 0x10000020UL; // OCP[4] and DPM[5]
  if constexpr (reg::kFaultClearLsbByteFirst) {
    CHECK_EQ(reg::FaultClearSdi(kMask), kMask);
  } else {
    CHECK_EQ(reg::FaultClearSdi(kMask), 0x20000010UL);
  }
}
