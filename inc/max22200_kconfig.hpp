/**
 * @file max22200_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_MAX22200_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-max22200 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to `Config{}`, so `KconfigDefaults()`
 * equals `Config{}`.
 *
 * Kconfig writes a bool option set to "n" as an absent macro, so an absent
 * `CONFIG_MAX22200_CLOCK_80KHZ` means 100 kHz in both cases. The channel settings are not Kconfig
 * options: set them in the `Config` in code.
 *
 * One configuration per program: the function below depends on the `CONFIG_` macros, so every
 * translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h` per
 * build; each host test is its own program). No driver header includes this one (handbook
 * KB-CPP20-018).
 */
#ifndef MAX22200_KCONFIG_HPP
#define MAX22200_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "max22200_types.hpp"

#ifndef CONFIG_MAX22200_RREF_OHM
/** RREF in ohms when Kconfig does not set it: `Config{}.rref_ohm`, not given. */
#define CONFIG_MAX22200_RREF_OHM 0
#endif

namespace max22200 {

/**
 * @brief The configuration menuconfig selected, on top of `Config{}`.
 * @return `Config{}` with the variant, oscillator and RREF from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
#if defined(CONFIG_MAX22200_VARIANT_A) && CONFIG_MAX22200_VARIANT_A
  c.variant = Variant::Max22200A;
#endif
#if defined(CONFIG_MAX22200_CLOCK_80KHZ) && CONFIG_MAX22200_CLOCK_80KHZ
  c.clock = MasterClock::Khz80;
#endif
  c.rref_ohm = CONFIG_MAX22200_RREF_OHM;
  return c;
}

} // namespace max22200

#endif // MAX22200_KCONFIG_HPP
