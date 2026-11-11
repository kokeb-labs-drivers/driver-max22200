# Platforms

The driver is MCU-agnostic: `inc/` and `src/` contain no SDK, RTOS or vendor header. A platform is three
optional pieces around it, and only `examples/<platform>/` holds platform code.

| Piece | Where | What it is |
| --- | --- | --- |
| Port | `examples/<platform>/` | The class that implements the bus hooks for that MCU or OS ([Porting](porting.md)) |
| Example | `examples/<platform>/` | A program that runs the driver there, built by CI |
| Build integration | the root `CMakeLists.txt` | How that platform's build system finds the driver |

## What exists today

| Platform | Port | Example | Build integration | Built by CI |
| --- | --- | --- | --- | --- |
| Host (Linux, macOS, Windows) | [`examples/host/host_max22200_port.hpp`](../examples/host/host_max22200_port.hpp) (simulated chip) | [`examples/host/`](../examples/host/README.md) | Plain CMake | yes, run as a test |
| ESP32 (ESP-IDF) | [`examples/esp32/main/esp32_max22200_port.hpp`](../examples/esp32/main/esp32_max22200_port.hpp) | [`examples/esp32/`](../examples/esp32/README.md) | Root `CMakeLists.txt` under `ESP_PLATFORM` (component) | yes |
| STM32, RP2040, nRF, other MCUs | write one ([Porting](porting.md)) | none yet | Plain CMake | no |

Plain CMake (`add_subdirectory()` or `find_package()`, see [Installation](installation.md)) covers STM32, RP2040,
nRF and host builds. The root `CMakeLists.txt` also covers ESP-IDF: under `ESP_PLATFORM` it registers the
repository as a component (a repository scaffolded without the ESP32 platform adds that branch back from the
template when it needs it). Zephyr modules, Arduino library manifests and PlatformIO manifests are added only when a
product needs them, and an example is added only together with a CI job that builds it.

## Add a platform

The [`add-port` skill](../.claude/skills/add-port/SKILL.md) has
the details per MCU. In short:

1. Create `examples/<platform>/` with the port (`<platform>_max22200_port.hpp`), a small example program and a
   README with the wiring and the expected output. The folder is self-contained: it carries its own port and
   builds on its own.
2. Build it in CI: a job in `.github/workflows/` that builds the example, needed by the `CI gate`. A platform that
   CI does not build is not added.
3. If its build system cannot use the root `CMakeLists.txt`, add the smallest manifest it needs, and say why in
   the pull request. Never put platform code in `inc/` or `src/`.
4. Add a row to the table above and a page section if the platform needs one.
