/**
 * @file max22200_basic_example.cpp
 * @brief Start the MAX22200 from the menuconfig values, cycle a solenoid valve on channel 0 and
 * log the diagnostics.
 *
 * Wiring: examples/esp32/README.md. Expected output: one "initialised" line, then one line per
 * cycle with ONCH, the STATUS flags and the FAULT bytes. Every Result is checked and logged by
 * name.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_max22200_port.hpp"
#include "max22200.hpp"
#include "max22200_kconfig.hpp"

namespace {

constexpr const char* kTag = "max22200_example";

/// Channel 0: a solenoid valve in CDR. Needs RREF in menuconfig; without it the channel stays at
/// its reset value (no current).
max22200::Config ExampleConfig() {
  max22200::Config c = max22200::KconfigDefaults();
  max22200::ChannelConfig& v = c.channels[0];
  c.loaded_channels = 0x01; // a valve on channel 0 only: its open-load detection may run
  if (c.rref_ohm != max22200::kNoRref) {
    v.hit = max22200::CurrentCode(300, c.rref_ohm, false).value; // 0 if out of range
    v.hold = max22200::CurrentCode(100, c.rref_ohm, false).value;
  }
  v.hit_time = max22200::HitTimeCode(20000, c.clock, v.chop).value;
  v.open_load_detect = true;
  return c;
}

} // namespace

extern "C" void app_main(void) {
  static Esp32Max22200Port port; // static: the driver keeps a reference
  static max22200::MAX22200<Esp32Max22200Port> chip(port, ExampleConfig()); // no I/O

  ESP_LOGI(kTag, "MAX22200 driver %s", max22200::GetDriverVersion());
  const auto init = chip.EnsureInitialized();
  if (!init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s", max22200::ToString(init.error));
    return;
  }

  bool on = false;
  while (true) {
    on = !on;
    if (const auto r = chip.SetChannel(0, on, max22200::Verify::ReadBack); !r) {
      ESP_LOGW(kTag, "SetChannel failed: %s", max22200::ToString(r.error));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    const auto s = chip.ReadStatus();
    if (!s) {
      ESP_LOGW(kTag, "ReadStatus failed: %s", max22200::ToString(s.error));
      if (s.error == max22200::Error::DeviceReset) {
        (void)chip.EnsureInitialized(); // the chip lost its configuration: start it again
      }
      continue;
    }
    const auto f = chip.ReadFaults();
    ESP_LOGI(kTag, "ONCH 0x%02X flags 0x%02X OCP 0x%02X HHF 0x%02X OLF 0x%02X DPM 0x%02X",
             s.value.on_mask, static_cast<unsigned>(s.value.raw & 0xFEU), f.value.overcurrent,
             f.value.hit_not_reached, f.value.open_load, f.value.plunger);
  }
}
