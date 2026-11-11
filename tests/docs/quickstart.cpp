/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include "my_port.hpp"

// --8<-- [start:quickstart]
#include "max22200.hpp"

int open_valve_0(MyPort& port) {
  max22200::Config cfg{};
  cfg.rref_ohm = 15000; // IFS = 15k x 1 V / 15 kohm = 1 A
  max22200::ChannelConfig& v = cfg.channels[0];
  v.hit = max22200::CurrentCode(600, cfg.rref_ohm, false).value;      // 600 mA peak to pull in
  v.hold = max22200::CurrentCode(200, cfg.rref_ohm, false).value;     // 200 mA to hold
  v.hit_time = max22200::HitTimeCode(30000, cfg.clock, v.chop).value; // 30 ms of HIT

  static max22200::MAX22200<MyPort> chip(port, cfg); // no I/O yet
  if (auto r = chip.EnsureInitialized(); !r) { // ENABLE, Figure 6, read-back of every register
    return static_cast<int>(r.error);          // NoDevice, DeviceIdMismatch: check the wiring
  }
  if (auto r = chip.SetChannel(0, true); !r) { // one 8-bit write: 2 transfers, then 1 per switch
    return static_cast<int>(r.error);
  }
  return 0; // HIT for 30 ms, then HOLD until SetChannel(0, false)
}
// --8<-- [end:quickstart]
