/**
 * @file test_errors.cpp
 * @brief Each chip-specific error, and each base error the driver returns, is produced by a real
 * call; the base errors documented as never returned keep their values.
 */
#include <cstdint>

#include "max22200.hpp"
#include "mock_max22200.hpp"
#include "test_harness.hpp"

using max22200::Error;
using max22200::Verify;
using max22200_test::MinimalPort;
using max22200_test::Miso;
namespace reg = max22200::reg;

namespace {
using Driver = max22200::MAX22200<MinimalPort>;
} // namespace

TEST_CASE("NoDevice: nothing drives SDO at start-up") {
  MinimalPort port;
  port.state.chip.miso = Miso::FloatHigh;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::NoDevice);
}

TEST_CASE("DeviceIdMismatch: SDO stuck low") {
  MinimalPort port;
  port.state.chip.miso = Miso::StuckLow;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::DeviceIdMismatch);
}

TEST_CASE("DeviceIdMismatch: SDI does not reach the chip") {
  MinimalPort port;
  port.state.chip.deaf_sdi = true;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::DeviceIdMismatch);
}

TEST_CASE("DeviceIdMismatch: the STATUS read after the wake-up differs from what was written") {
  MinimalPort port;
  port.state.lose_transfer = 3; // the STATUS write's data never arrives
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::DeviceIdMismatch);
}

TEST_CASE("Pin: CMD cannot be driven") {
  MinimalPort port;
  port.state.fail_pin = 2; // CMD low, ENABLE high, then CMD high for the first command
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::Pin);
  CHECK_EQ(port.state.transfers, 0); // a command that cannot have CMD high is never sent
}

TEST_CASE("CommunicationError: a new COMER after the wake in every start-up attempt") {
  MinimalPort port;
  port.state.chip.comer_bank = reg::CfgChannel(0);
  port.state.chip.comer_bank_writes = 100;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::CommunicationError);
  CHECK(!port.state.chip.enable);
}

TEST_CASE("CommunicationError once at start-up: the procedure is repeated (Figure 6)") {
  MinimalPort port;
  port.state.chip.comer_bank = reg::CfgChannel(0);
  port.state.chip.comer_bank_writes = 1;
  Driver d(port);
  CHECK(d.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, 26 + 48); // the first attempt stopped after its writes
}

TEST_CASE("Timeout: every write miscounted, ACTIVE never latches") {
  max22200::Config c{};
  c.wake_timeout_us = 30000;
  MinimalPort port;
  port.state.chip.comer_writes = 1000;
  Driver d(port, c);
  CHECK_EQ(d.EnsureInitialized().error, Error::Timeout);
}

TEST_CASE("WriteMismatch: a CFG_CH write lost at start-up") {
  max22200::Config c{};
  c.channels[3].hold = 9;
  MinimalPort port;
  port.state.chip.drop_bank = reg::CfgChannel(3); // acknowledged, echoed, and lost
  Driver d(port, c);
  CHECK_EQ(d.EnsureInitialized().error, Error::WriteMismatch);
}

TEST_CASE("Undervoltage: VM stays below UVLO") {
  MinimalPort port;
  port.state.chip.vm_low = true;
  Driver d(port);
  CHECK_EQ(d.EnsureInitialized().error, Error::Undervoltage);
}

TEST_CASE("ChannelsActive: a pair mode change with a channel of the pair on") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  REQUIRE(d.SetChannel(4, true).ok());
  CHECK_EQ(d.SetPairMode(2, max22200::PairMode::Parallel).error, Error::ChannelsActive);
}

TEST_CASE("DeviceReset: ACTIVE read back as 0 on a running driver") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.Brownout();
  CHECK_EQ(d.ReadStatus().error, Error::DeviceReset);
}

TEST_CASE("CommunicationError on a running driver: the next call that sees COMER go 0 to 1") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.chip.comer_writes = 1;
  CHECK(d.SetChannelsOn(0x01).ok()); // the default sends no extra transfer and compares nothing
  CHECK_EQ(port.state.chip.OnMask(), 0U);
  CHECK((d.GetLastStatusByte() & reg::kComer) == 0U); // seen only by the next command
  CHECK_EQ(d.ReadChannelsOn().error, Error::CommunicationError);
  CHECK((d.GetLastStatusByte() & reg::kComer) != 0U);
  CHECK(d.ReadChannelsOn().ok()); // reported once
}

TEST_CASE("Uncertain: a switch in a pair whose mode a failed write left unknown") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1;
  port.state.fail_after_clocking = true;
  CHECK_EQ(d.SetPairMode(3, max22200::PairMode::FullBridge).error, Error::Bus);
  CHECK_EQ(d.SetChannel(6, true).error, Error::Uncertain);
}

TEST_CASE("Unsupported: ReadFaultPin without GpioRead") {
  MinimalPort port;
  Driver d(port);
  REQUIRE(d.EnsureInitialized().ok());
  CHECK_EQ(d.ReadFaultPin().error, Error::Unsupported);
}

TEST_CASE("The base errors documented as not returned keep their values") {
  // Crc, Busy and DeviceFault: the chip sends no CRC, is never busy, and faults are data (Timeout
  // is returned by a wake that runs out, above). The sweep in test_bus_faults.cpp and the tests
  // above cover every path; the names stay for the shared base set.
  CHECK_EQ(static_cast<int>(Error::Timeout), 4);
  CHECK_EQ(static_cast<int>(Error::Crc), 6);
  CHECK_EQ(static_cast<int>(Error::Busy), 7);
  CHECK_EQ(static_cast<int>(Error::DeviceFault), 8);
  CHECK_EQ(static_cast<int>(Error::NoDevice), 16);
  CHECK_EQ(static_cast<int>(Error::DeviceReset), 22);
  CHECK_EQ(static_cast<int>(Error::Uncertain), 23);
}
