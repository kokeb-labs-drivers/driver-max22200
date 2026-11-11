/**
 * @file units.cpp
 * @brief The Drive modes page's conversions, checked at compile time by the host build.
 */
#include "max22200.hpp"

using max22200::ChopDivider;
using max22200::Error;
using max22200::MasterClock;

// --8<-- [start:currents]
// RREF = 15 kΩ: IFS = 1 A, one code = 1 A / 128 = 7.8125 mA (Table 4)
static_assert(max22200::CurrentCode(500, 15000, false).value == 64); // 500 mA
static_assert(max22200::CurrentCode(984, 15000, false).value ==
              126); // the highest regulated current
static_assert(max22200::CurrentCode(990, 15000, false).error ==
              Error::InvalidArgument); // would be 127
static_assert(max22200::CurrentCode(3, 15000, false).error == Error::InvalidArgument); // would be 0
static_assert(max22200::CurrentCode(250, 15000, true).value == 64); // HFS: IFS = 500 mA
static_assert(max22200::CurrentUa(64, 15000, false).value == 500000);
// --8<-- [end:currents]

// --8<-- [start:hit-time]
// One HIT_T step is 40 chopping periods (Table 5)
static_assert(max22200::HitTimeCode(30000, MasterClock::Khz100, ChopDivider::Div4).value == 19);
static_assert(max22200::HitTimeUs(19, MasterClock::Khz100, ChopDivider::Div4).value == 30400);
static_assert(max22200::HitTimeUs(100, MasterClock::Khz100, ChopDivider::Div3).value ==
              120000); // exact
static_assert(max22200::HitTimeUs(254, MasterClock::Khz80, ChopDivider::Div4).value ==
              508000); // 20 kHz
static_assert(max22200::HitTimeCode(408000, MasterClock::Khz100, ChopDivider::Div4).error ==
              Error::InvalidArgument); // beyond 254 steps: never rounded to 255 (forever)
static_assert(max22200::HitTimeUs(255, MasterClock::Khz100, ChopDivider::Div4).error ==
              Error::InvalidArgument); // 255 is continuous: not a time
// --8<-- [end:hit-time]
