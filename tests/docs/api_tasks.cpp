/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <cstdio>

#include "max22200.hpp"
#include "my_port.hpp"

using Chip = max22200::MAX22200<MyPort>;

// --8<-- [start:lifecycle]
max22200::Error BringUp(Chip& chip) {
  // The one entry point: starts the chip on the first call, then returns Ok with no I/O. After a
  // failure the next call starts again from the beginning.
  const auto r = chip.EnsureInitialized();
  if (!r) {
    std::printf("MAX22200 init: %s\n", max22200::ToString(r.error));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:switching]
max22200::Error ApplyValveMask(Chip& chip, uint8_t want, bool safety_critical) {
  // Every channel at once: one 8-bit write. ReadBack adds one 8-bit read and a compare; use it
  // where a lost frame matters more than the two transfers.
  const auto verify = safety_critical ? max22200::Verify::ReadBack : max22200::Verify::None;
  return chip.SetChannelsOn(want, verify).error;
}

max22200::Error OpenOne(Chip& chip, uint8_t channel) {
  // From the driver's copy of ONCH: no STATUS read first (it would clear OVT and UVM).
  return chip.SetChannel(channel, true).error;
}
// --8<-- [end:switching]

// --8<-- [start:hold]
max22200::Error LowerHold(Chip& chip, uint8_t channel) {
  // On the fly, while the channel holds: one 8-bit write of HFS and HOLD.
  const auto code = max22200::CurrentCode(150, chip.GetConfig().rref_ohm,
                                          chip.GetConfig().channels[channel].half_scale);
  if (!code) {
    return code.error; // below the resolution or above 126/128 x IFS
  }
  return chip.SetHoldCode(channel, code.value).error;
}
// --8<-- [end:hold]

// --8<-- [start:faults]
void ServiceFaults(Chip& chip) {
  const auto s = chip.ReadStatus(); // clears OVT and UVM
  if (!s) {
    if (s.error == max22200::Error::DeviceReset) {
      (void)chip.EnsureInitialized(); // power-on reset: configure the chip again
    }
    return;
  }
  if (!s.value.overcurrent && !s.value.open_load && !s.value.hit_not_reached && !s.value.plunger) {
    return;
  }
  const auto f = chip.ClearFaults(); // returns the flags it cleared
  if (f && f.value.overcurrent != 0U) {
    // OCP switched those channels off: switch them off, then on again when it is safe.
    const uint32_t on = chip.GetChannelsOn();
    const auto keep = static_cast<uint8_t>(on & ~static_cast<uint32_t>(f.value.overcurrent));
    (void)chip.SetChannelsOn(keep);
  }
}
// --8<-- [end:faults]

// --8<-- [start:errors]
const char* Describe(max22200::Error e) {
  switch (e) {
  case max22200::Error::NoDevice:
  case max22200::Error::DeviceIdMismatch:
    return "check SDO pull-up, SDI, CSB, CMD, ENABLE and VM";
  case max22200::Error::Pin:
    return "the port could not drive CMD or ENABLE";
  case max22200::Error::CommunicationError:
    return "the chip counted the wrong number of clocks: check SCK and CSB";
  case max22200::Error::ChannelsActive:
    return "switch the channels off first";
  case max22200::Error::Uncertain:
    return "a write's reply failed: write the pair mode again or read it back";
  case max22200::Error::Ok:
  case max22200::Error::NotInitialized:
  case max22200::Error::InvalidArgument:
  case max22200::Error::Bus:
  case max22200::Error::Timeout:
  case max22200::Error::Crc:
  case max22200::Error::Busy:
  case max22200::Error::DeviceFault:
  case max22200::Error::Unsupported:
  case max22200::Error::WriteMismatch:
  case max22200::Error::Undervoltage:
  case max22200::Error::DeviceReset:
  default:
    break;
  }
  return max22200::ToString(e);
}
// --8<-- [end:errors]

// --8<-- [start:deinit]
void PowerDown(Chip& chip) {
  if (auto r = chip.Deinitialize(); !r) {
    std::printf("MAX22200 shutdown failed: %s\n", max22200::ToString(r.error));
  }
}
// --8<-- [end:deinit]
