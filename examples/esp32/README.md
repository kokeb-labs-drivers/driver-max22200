# MAX22200 ESP32 examples

ESP-IDF applications that run the MAX22200 driver on an ESP32-C6, built by CI on every pull request.

## Wiring (ESP32-C6 DevKitC-1)

| ESP32-C6 | MAX22200 | Notes |
| --- | --- | --- |
| GPIO6 | SCK | SPI2, mode 0, 1 MHz in the example (up to 10 MHz) |
| GPIO7 | SDI | |
| GPIO2 | SDO | Open drain: fit a pull-up to 3V3 (1 kohm to 4.7 kohm) |
| GPIO10 | CSB | Driven by spi_master, low for each transfer |
| GPIO11 | CMD | High while the command byte is written |
| GPIO3 | ENABLE | Set `enable = -1` in `Esp32Max22200Pins` when tied high |
| GPIO4 | FAULT | Open drain, active low; set `fault = -1` when not wired |
| 3V3 | logic level | The logic inputs need 1.2 V high; the chip makes its own 1.8 V (V18) from VM |
| GND | GND, EP | |

VM (4.5 V to 36 V) and the loads come from the board; RREF from IREF to ground sets the full-scale current
(15 kohm: 1 A). Enter its value in menuconfig.

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh max22200_basic_example Debug
./scripts/flash_app.sh flash_monitor max22200_basic_example Debug
```

`idf.py menuconfig` shows "MAX22200 solenoid driver" under Component config: the fitted part, the oscillator
and RREF that `max22200::KconfigDefaults()` returns.

## Expected output

```text
I (312) max22200_example: MAX22200 driver 0.1.0
max22200: initialised
I (820) max22200_example: ONCH 0x01 flags 0x00 OCP 0x00 HHF 0x00 OLF 0x00 DPM 0x00
I (1322) max22200_example: ONCH 0x00 flags 0x20 OCP 0x00 HHF 0x00 OLF 0x01 DPM 0x00
```

With no valve on channel 0, open-load detection reports OLF while the channel is off. An
`EnsureInitialized failed: NoDevice` line means SDO reads high: check VM, ENABLE and the SDO pull-up;
`DeviceIdMismatch` means the chip does not echo its commands: check SDI, CMD and CSB. Not verified on
hardware: this output is what the driver prints against the chip model.

## Files

| File | What it is |
| --- | --- |
| [`main/max22200_basic_example.cpp`](main/max22200_basic_example.cpp) | The example app |
| [`main/esp32_max22200_port.hpp`](main/esp32_max22200_port.hpp) | The ESP-IDF port: bus, CMD, ENABLE, FAULT, delay, log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
