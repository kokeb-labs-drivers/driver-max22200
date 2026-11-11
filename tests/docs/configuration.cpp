/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "max22200.hpp"
#include "max22200_kconfig.hpp"
#include "my_port.hpp"

// --8<-- [start:custom]
max22200::Config ValveBoard() {
  max22200::Config c{};                        // the chip's reset state, MAX22200A
  c.rref_ohm = 30000;                          // IFS = 500 mA
  c.fault_mask.communication = false;          // let COMER pull FAULT low as well
  c.pairs[3] = max22200::PairMode::FullBridge; // channels 6 and 7: a latching valve
  c.loaded_channels = 0xFF;                    // a load on every channel: diagnostics allowed
  for (uint8_t ch = 0; ch < 6; ++ch) {
    max22200::ChannelConfig& v = c.channels[ch];
    v.hit = max22200::CurrentCode(400, c.rref_ohm, false).value; // checked: ok() in real code
    v.hold = max22200::CurrentCode(120, c.rref_ohm, false).value;
    v.hit_time = max22200::HitTimeCode(25000, c.clock, v.chop).value;
    v.open_load_detect = true;
    v.hit_current_check = true; // HHF if 400 mA is not reached within 25 ms
  }
  return c; // max22200::Validate(c) checks it without the chip
}

max22200::Error StartValves(MyPort& port) {
  static max22200::MAX22200<MyPort> chip(port, ValveBoard()); // stored, no I/O
  return chip.EnsureInitialized().error; // InvalidArgument if the config is out of range
}
// --8<-- [end:custom]

// --8<-- [start:kconfig]
max22200::Error StartFromMenuconfig(MyPort& port) {
  static max22200::MAX22200<MyPort> chip(port, max22200::KconfigDefaults());
  return chip.EnsureInitialized().error;
}
// --8<-- [end:kconfig]
