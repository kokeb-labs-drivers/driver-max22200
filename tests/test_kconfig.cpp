/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "max22200_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr max22200::Config kC = max22200::KconfigDefaults();
  CHECK_EQ(kC.variant, max22200::Variant::Max22200A);
  CHECK_EQ(kC.clock, max22200::MasterClock::Khz80);
  CHECK_EQ(kC.rref_ohm, 30000U);
  CHECK(kC.channels[0] == max22200::ChannelConfig{}); // not a Kconfig option
  CHECK_EQ(max22200::Validate(kC), max22200::Error::Ok);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr max22200::Config kC = max22200::KconfigDefaults();
  constexpr max22200::Config kD{};
  CHECK_EQ(kC.variant, kD.variant);
  CHECK_EQ(kC.clock, kD.clock);
  CHECK_EQ(kC.rref_ohm, kD.rref_ohm);
  CHECK(kC.fault_mask == kD.fault_mask);
}
#endif
