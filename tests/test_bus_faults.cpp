/**
 * @file test_bus_faults.cpp
 * @brief Every transfer and every pin write of every operation failed in turn: each failure is
 * reported (`Bus` or `Pin`), CMD is left low, and the driver works again on the next call.
 */
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"
#include "test_harness.hpp"

using max22200::Error;
using max22200::Verify;
using max22200_test::MinimalPort;

namespace {

using Driver = max22200::MAX22200<MinimalPort>;
using Op = Error (*)(Driver&);

max22200::Config BridgeConfig() {
  max22200::Config c{};
  c.rref_ohm = 30000;
  c.pairs[3] = max22200::PairMode::FullBridge;
  c.channels[0].hit = 100;
  c.channels[0].hold = 40;
  c.channels[0].hit_time = 20;
  return c;
}

Error SetOn(Driver& d) {
  return d.SetChannelsOn(0x05).error;
}
Error SetOnFast(Driver& d) {
  // The second write is the one-transfer ONCH write.
  if (const Error e = d.SetChannelsOn(0x01).error; e != Error::Ok) {
    return e;
  }
  return d.SetChannelsOn(0x05).error;
}
Error SetOnVerified(Driver& d) {
  return d.SetChannelsOn(0x05, Verify::ReadBack).error;
}
Error SetOne(Driver& d) {
  return d.SetChannel(2, true, Verify::ReadBack).error;
}
Error Bridge(Driver& d) {
  return d.SetBridge(3, max22200::BridgeState::Reverse, Verify::ReadBack).error;
}
Error ReadOn(Driver& d) {
  return d.ReadChannelsOn().error;
}
Error Configure(Driver& d) {
  max22200::ChannelConfig c{};
  c.hit = 64;
  return d.ConfigureChannel(1, c, Verify::ReadBack).error;
}
Error Hold(Driver& d) {
  return d.SetHoldCode(0, 20, Verify::ReadBack).error;
}
Error Pair(Driver& d) {
  return d.SetPairMode(1, max22200::PairMode::Parallel, Verify::ReadBack).error;
}
Error Mask(Driver& d) {
  max22200::FaultMask m{};
  m.open_load = true;
  return d.SetFaultMask(m, Verify::ReadBack).error;
}
Error Dpm(Driver& d) {
  return d.SetDpm({.start = 10, .debounce = 2, .threshold = 1}, Verify::ReadBack).error;
}
Error ReadCfg(Driver& d) {
  return d.ReadChannelConfig(0).error;
}
Error Status(Driver& d) {
  return d.ReadStatus().error;
}
Error Faults(Driver& d) {
  return d.ReadFaults().error;
}
Error Clear(Driver& d) {
  return d.ClearFaults().error;
}
Error Apply(Driver& d) {
  max22200::Config c = d.GetConfig();
  c.channels[6].hold = 3;
  c.dpm.debounce = 4;
  c.fault_mask.plunger = true;
  return d.ApplyConfig(c).error;
}
Error Shutdown(Driver& d) {
  return d.Deinitialize().error;
}

constexpr Op kOps[] = {SetOn,     SetOnFast, SetOnVerified, SetOne, Bridge,  ReadOn,
                       Configure, Hold,      Pair,          Mask,   Dpm,     ReadCfg,
                       Status,    Faults,    Clear,         Apply,  Shutdown};

/// Run `op` on a started driver once to count its transfers and pin writes.
void Count(Op op, int& transfers, int& pins) {
  MinimalPort port;
  Driver d(port, BridgeConfig());
  REQUIRE(d.EnsureInitialized().ok());
  const int t = port.state.transfers;
  const int p = port.state.pin_calls;
  REQUIRE(op(d) == Error::Ok);
  transfers = port.state.transfers - t;
  pins = port.state.pin_calls - p;
}

} // namespace

TEST_CASE("Every transfer of every operation failed in turn is reported as Bus") {
  for (const Op op : kOps) {
    int transfers = 0;
    int pins = 0;
    Count(op, transfers, pins);
    CHECK(transfers > 0);
    for (int i = 0; i < transfers; ++i) {
      for (const bool clocked : {false, true}) {
        MinimalPort port;
        Driver d(port, BridgeConfig());
        REQUIRE(d.EnsureInitialized().ok());
        port.state.fail_transfer = port.state.transfers + i;
        port.state.fail_after_clocking = clocked;
        CHECK_EQ(op(d), Error::Bus);
        CHECK(port.state.chip.cmd); // CMD back at its idle level, high, whatever failed
        // The driver works again: a fresh read of STATUS (or a restart after Deinitialize).
        if (!d.IsInitialized()) {
          CHECK(d.EnsureInitialized().ok());
        }
        CHECK(d.ReadChannelConfig(0).ok());
      }
    }
  }
}

TEST_CASE("Every pin write of every operation failed in turn is reported as Pin") {
  for (const Op op : kOps) {
    int transfers = 0;
    int pins = 0;
    Count(op, transfers, pins);
    for (int i = 0; i < pins; ++i) {
      MinimalPort port;
      Driver d(port, BridgeConfig());
      REQUIRE(d.EnsureInitialized().ok());
      port.state.fail_pin = port.state.pin_calls + i;
      const int t = port.state.transfers;
      CHECK_EQ(op(d), Error::Pin);
      (void)t;
      // The driver works again on the next call (a restart after Deinitialize).
      if (!d.IsInitialized()) {
        CHECK(d.EnsureInitialized().ok());
      }
      CHECK(d.ReadChannelConfig(0).ok());
      CHECK(port.state.chip.cmd);
    }
  }
}

TEST_CASE("Every transfer and pin write of EnsureInitialized failed in turn") {
  MinimalPort probe;
  Driver p(probe, BridgeConfig());
  REQUIRE(p.EnsureInitialized().ok());
  const int transfers = probe.state.transfers;
  const int pins = probe.state.pin_calls;
  CHECK_EQ(transfers, 48);
  for (int i = 0; i < transfers; ++i) {
    MinimalPort port;
    port.state.fail_transfer = i;
    Driver d(port, BridgeConfig());
    CHECK_EQ(d.EnsureInitialized().error, Error::Bus);
    CHECK(!port.state.chip.enable); // the chip is left asleep
    CHECK(!d.IsInitialized());
    CHECK(d.EnsureInitialized().ok()); // and starts on the next call
  }
  for (int i = 0; i < pins; ++i) {
    MinimalPort port;
    port.state.fail_pin = i;
    Driver d(port, BridgeConfig());
    CHECK_EQ(d.EnsureInitialized().error, Error::Pin);
    CHECK(d.EnsureInitialized().ok());
  }
}
