/**
 * @file main.cpp
 * @brief The MAX22200 driver on your PC: start a simulated chip, drive two solenoid valves and read
 * the diagnostics.
 *
 * No hardware and no SDK: the host port (host_max22200_port.hpp) connects the real driver to the
 * protocol model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>

#include "host_max22200_port.hpp"
#include "max22200.hpp"

namespace {

/// Two solenoid valves on channels 0 and 1: 500 mA for 20 ms, then 150 mA, at RREF = 15 kohm.
max22200::Config ValveConfig() {
  max22200::Config c{};
  c.rref_ohm = 15000;
  c.loaded_channels = 0x03; // valves on channels 0 and 1: their load diagnostics may run
  for (uint8_t ch = 0; ch < 2; ++ch) {
    max22200::ChannelConfig& v = c.channels[ch];
    v.hit = max22200::CurrentCode(500, c.rref_ohm, false).value;
    v.hold = max22200::CurrentCode(150, c.rref_ohm, false).value;
    v.hit_time = max22200::HitTimeCode(20000, c.clock, v.chop).value;
    v.open_load_detect = true;
  }
  return c;
}

int Fail(const char* what, max22200::Error e) {
  std::printf("%s failed: %s\n", what, max22200::ToString(e));
  return 1;
}

} // namespace

int main() {
  HostMax22200Port port;
  max22200::MAX22200<HostMax22200Port> chip(port, ValveConfig()); // no I/O yet

  std::printf("MAX22200 driver %s on the host\n", max22200::GetDriverVersion());
  if (const auto r = chip.EnsureInitialized(); !r) {
    return Fail("EnsureInitialized", r.error);
  }
  const max22200::Config& c = chip.GetConfig();
  const max22200::ChannelConfig& v = c.channels[0];
  const auto hit = max22200::CurrentUa(v.hit, c.rref_ohm, v.half_scale);
  const auto hit_time = max22200::HitTimeUs(v.hit_time, c.clock, v.chop);
  const auto hold = max22200::CurrentUa(v.hold, c.rref_ohm, v.half_scale);
  if (!hit) {
    return Fail("CurrentUa", hit.error);
  }
  if (!hit_time) {
    return Fail("HitTimeUs", hit_time.error);
  }
  if (!hold) {
    return Fail("CurrentUa", hold.error);
  }
  std::printf("HIT %u uA for %u us, HOLD %u uA\n", static_cast<unsigned>(hit.value),
              static_cast<unsigned>(hit_time.value), static_cast<unsigned>(hold.value));

  if (const auto r = chip.SetChannelsOn(0x03, max22200::Verify::ReadBack); !r) {
    return Fail("SetChannelsOn", r.error);
  }
  port.DelayUs(50'000); // the valves pull in and hold
  if (const auto r = chip.SetChannel(1, false); !r) {
    return Fail("SetChannel", r.error);
  }
  const auto s = chip.ReadStatus();
  if (!s) {
    return Fail("ReadStatus", s.error);
  }
  std::printf("ONCH 0x%02X, flags 0x%02X\n", static_cast<unsigned>(s.value.on_mask),
              static_cast<unsigned>(s.value.raw & max22200::reg::kFlagsMask));
  const auto f = chip.ReadFaults();
  if (!f) {
    return Fail("ReadFaults", f.error);
  }
  std::printf("faults on channels 0x%02X\n", static_cast<unsigned>(f.value.Channels()));
  if (const auto r = chip.Deinitialize(); !r) {
    return Fail("Deinitialize", r.error);
  }
  return 0;
}
