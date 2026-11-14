# MAX22200 Driver

<!-- generated: chip-summary -->
The **MAX22200** is an eight-channel driver for solenoids, valves, relays and brushed DC motors: each 36 V, 1 A
half-bridge runs a HIT then HOLD sequence under current or voltage regulation, set over SPI (General Description;
Applications; Benefits and Features, p. 1).
<!-- /generated -->

C++20 driver for the Analog Devices MAX22200 and MAX22200A octal 36 V, 1 A serial-controlled solenoid and motor drivers.

[![CI](https://github.com/kokeb-labs-drivers/driver-max22200/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kokeb-labs-drivers/driver-max22200/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-informational)](docs/installation.md#requirements)

**Documentation: [`docs/`](docs/index.md)**, with the API reference built by Doxygen ([how](docs/api-reference.md)).
The site at <https://kokeb-labs-drivers.github.io/driver-max22200/> opens when the repository is public.

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-hero.dark.svg">
  <img src="docs/diagrams/chip-hero.svg"
    alt="Overview of the MAX22200, an eight-channel driver for solenoids, valves, relays and brushed DC
    motors: each 36 V, 1 A half-bridge runs a HIT then HOLD sequence under current or voltage
    regulation, set over SPI. A MCU talks to it over SPI, CMD, ENABLE; it drives Solenoids, valves,
    relays; it reads back Coil current, TRIGA and TRIGB. Headline figures: 4.5 to 36 V supply (VM); 8 x
    1 A half-bridges, 1 A RMS each; 200 mΩ typical on-resistance per output; 10 MHz SPI clock, one chip
    (5 MHz in a daisy chain); 5 x 5 mm TQFN-32 body.">
</picture>
<!-- /figure -->

## What it does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-capabilities.dark.svg">
  <img src="docs/diagrams/chip-capabilities.svg"
    alt="Capability map of the MAX22200: Channel switching (supported): Switches each channel on or off with
    one 8-bit write of the ONCH byte, no read needed; HIT and HOLD sequence (supported): Pulls a plunger
    in at the HIT level for a set time, then holds it at a lower level to save power; CDR and VDR
    regulation (supported): Regulates the coil current against a full scale set by RREF, or the PWM duty
    cycle in 1 % steps; Chopping frequency and slew rate (supported): Sets the oscillator and each
    channel's chopping frequency, with slower edges for less EMI; Parallel and full-bridge pairs
    (supported): Ties two half-bridges for twice the current, or makes an H-bridge for a latched valve
    or a DC motor; Protection and fault flags (supported): Overcurrent, open load, HIT not reached,
    overtemperature, undervoltage: per-channel flags and FAULT; Detection of plunger movement
    (supported): Looks for the current dip a moving plunger makes during HIT, and flags a valve that did
    not move; Trigger inputs (partial): TRIGA and TRIGB switch even and odd channels directly; the
    driver selects them, your code drives the pins; Daisy chain (not covered): Several chips on one SPI
    chain at up to 5 MHz; the driver talks to one chip per object.">
</picture>
<!-- /figure -->

## Where it is used

<!-- generated: chip-uses -->
- Relay drivers
- Solenoid, valve and electromagnet drivers
- Low-side and high-side switches
- Latched (bistable) solenoid valves
- Brushed DC motors

Uses named by the datasheet (General Description; Applications; Benefits and Features, p. 1).
<!-- /generated -->

## Inside the chip

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-blocks.dark.svg">
  <img src="docs/diagrams/chip-blocks.svg"
    alt="Block diagram of the MAX22200: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

## Pins and wiring

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-pinout.dark.svg">
  <img src="docs/diagrams/chip-pinout.svg"
    alt="Pinout of the MAX22200 in TQFN-32, top view: 32 pins and an exposed pad, each with its name, its
    electrical type as a colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-wiring-typical.dark.svg">
  <img src="docs/diagrams/chip-wiring-typical.svg"
    alt="Wiring of the MAX22200 for the Application diagram: MCU SPI SCK to SCK (Transfer()); MCU SPI MOSI to
    SDI (Transfer()); MCU SPI MISO to SDO (Transfer()); MCU SPI CS to CSB (Transfer()); MCU GPIO to CMD
    (GpioSet(CtrlPin::Cmd)); MCU GPIO to ENABLE (GpioSet(CtrlPin::Enable)); MCU GPIO (IRQ) to FAULT
    (GpioRead(CtrlPin::Fault)); MCU GPIO to TRIGA; OUT6 to DC motor M+; OUT7 to DC motor M-; OUT2 to
    Latched valve B; OUT3 to Latched valve A. Supplies, grounds and the support parts are listed under
    the drawing.">
</picture>
<!-- /figure -->

| MCU | MAX22200 | Notes |
| --- | --- | --- |
| SPI SCK | SCK | Mode 0, up to 10 MHz; 1 MHz for bring-up on jumper wires |
| SPI MOSI | SDI | |
| SPI MISO | SDO | Open drain: a pull-up is required, a low value for a fast clock (Electrical Characteristics, note 2) |
| GPIO (chip select) | CSB | Low for each transfer |
| GPIO | CMD | High while the command byte is written |
| GPIO | ENABLE | Or tie it high; it has a 1.5 MΩ pull-down |
| GPIO input (optional) | FAULT | Open drain, active low |
| | IREF | RREF to ground, 15 kΩ to 100 kΩ: IFS = 15 kΩ x 1 A / RREF |
| 4.5 V to 36 V | VM | The load supply; 0.1 µF at each VM pair and 10 µF bulk |

<!-- figure: chip-frame-command -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-frame-command.dark.svg">
  <img src="docs/diagrams/chip-frame-command.svg"
    alt="Command byte of the MAX22200, 8 bits, most significant bit first. Command: RB/W (bit 7), RFU (bits 6
    to 5), A_BNK (bits 4 to 1), 8bit/n32bits (bit 0); Reply: STATUS[7:0] (bits 7 to 0). The STATUS[7:0]
    field of the Reply row: bit 7 OVT, bit 6 OCP, bit 5 OLF, bit 4 HHF, bit 3 DPM, bit 2 COMER, bit 1
    UVM, bit 0 ACTIVE. Below, the SPI mode 0 waveform of one transfer in the same bit grid.">
</picture>
<!-- /figure -->

<!-- figure: chip-frame-switch -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-frame-switch.dark.svg">
  <img src="docs/diagrams/chip-frame-switch.svg"
    alt="8-bit write of the MAX22200, 8 bits, most significant bit first. Write: ONCH[7:0] (STATUS[31:24])
    (bits 7 to 0); Reply: STATUS[7:0] (bits 7 to 0).">
</picture>
<!-- /figure -->

<!-- figure: chip-frame-write32 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/chip-frame-write32.dark.svg">
  <img src="docs/diagrams/chip-frame-write32.svg"
    alt="32-bit write of the MAX22200, 32 bits, most significant bit first. Write: DATAIN[7:0] (bits 31 to
    24), DATAIN[15:8] (bits 23 to 16), DATAIN[23:16] (bits 15 to 8), DATAIN[31:24] (bits 7 to 0); Reply:
    STATUS[7:0] (bits 31 to 24), command echo (bits 23 to 16), 0x00 (bits 15 to 8), 0x00 (bits 7 to 0).">
</picture>
<!-- /figure -->

## Quick start

**1. Wire it** as in [Pins and wiring](#pins-and-wiring).

**2. Write the port**: a class with `EnsureInitialized()`, `Transfer()` (1 or 4 bytes with CSB low), `GpioSet()`
(CMD and ENABLE) and `DelayUs()`, about 30 lines on most SDKs. [Porting](docs/porting.md) has the contract and a
complete example.

**3. Drive a valve:**

<!-- generated: quickstart-code -->
```cpp
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
```
<!-- /generated -->

`EnsureInitialized()` is the only call that starts the chip. Every other call returns `Error::NotInitialized`
until it has succeeded, and no read ever starts the chip on its own.

## Driver coverage

<!-- generated: chip-coverage -->
| Capability | What the chip does | Driver API | Registers | Status | Datasheet |
| --- | --- | --- | --- | --- | --- |
| [Channel switching](docs/api-guide.md) | Switches each channel on or off with one 8-bit write of the ONCH byte, no read needed | `SetChannel`, `SetChannelsOn`, `ReadChannelsOn` | `ONCH`, `STATUS` | Supported | Programming Procedure, p. 23 |
| [HIT and HOLD sequence](docs/drive-modes.md) | Pulls a plunger in at the HIT level for a set time, then holds it at a lower level to save power | `ConfigureChannel`, `SetHoldCode`, `HitTimeCode` | `CFG_CH0`, `HIT`, `HOLD`, `HIT_T` | Supported | Functional Description - Two Levels Drive Sequence for Solenoid Valves, p. 14 |
| [CDR and VDR regulation](docs/drive-modes.md) | Regulates the coil current against a full scale set by RREF, or the PWM duty cycle in 1 % steps (high-side drive is VDR only) | `CurrentCode`, `FullScaleUa`, `DriveMode::Cdr`, `DriveMode::Vdr`, `DutyLimitsOf` | `VDRnCDR`, `HSnLS`, `HFS` | Supported | Current Drive Regulation (CDR), p. 18 |
| [Chopping frequency and slew rate](docs/drive-modes.md) | Sets the oscillator and each channel's chopping frequency, with slower edges for less EMI | `MasterClock`, `ChopDivider`, `ConfigureChannel` | `FREQM`, `FREQ_CFG`, `SRC` | Supported | Chopping Frequency (FREQM, FRQ_CFG_), p. 16 |
| [Parallel and full-bridge pairs](docs/drive-modes.md) | Ties two half-bridges for twice the current, or makes an H-bridge for a latched valve or a DC motor | `SetPairMode`, `SetBridge`, `PairMode::FullBridge` | `CM10`, `CM32`, `CM54`, `CM76` | Supported | Supported Driver Configurations, p. 20 |
| [Protection and fault flags](docs/api-guide.md) | Overcurrent, open load, HIT not reached, overtemperature, undervoltage: per-channel flags and FAULT | `ReadStatus`, `ReadFaults`, `ClearFaults`, `SetFaultMask`, `ReadFaultPin` | `STATUS`, `FAULT`, `OL_EN`, `HHF_EN`, `M_OVT` | Supported | Protection Circuits, p. 21 |
| [Detection of plunger movement](docs/drive-modes.md) | Looks for the current dip a moving plunger makes during HIT, and flags a valve that did not move | `SetDpm`, `DpmCurrentCode` | `CFG_DPM`, `DPM_EN`, `DPM_ISTART`, `DPM_IPTH`, `DPM_TDEB` | Supported | Detection of Plunger Movement (DPM), p. 22 |
| [Trigger inputs](docs/hardware.md) | TRIGA and TRIGB switch even and odd channels directly; the driver selects them, your code drives the pins | `ConfigureChannel`, `ChannelConfig` | `TRGnSPI` | Partial | Functional Description - Two Levels Drive Sequence for Solenoid Valves, p. 14 |
| [Daisy chain](docs/limitations.md) | Several chips on one SPI chain at up to 5 MHz; the driver talks to one chip per object | - | - | Not covered | SPI Detailed Description, p. 23 |

**Supported**: the driver covers it and a host test checks it; **Partial**: covered in part; the coverage map says which
part; **Not covered**: the chip has it and the driver leaves it alone.

From [`docs/hardware.yml`](docs/hardware.yml); `kokeb-docs hardware --check` confirms that every API name is in
`inc/max22200.hpp`, `inc/max22200_types.hpp` and every register in `inc/max22200_registers.hpp`.
<!-- /generated -->

This driver starts the chip with the datasheet's programming procedure, checks that every register holds what it wrote,
switches channels with one 8-bit write and no read (one transfer when the last access was a switch), refuses settings
the chip cannot do safely (a CDR code that means "full on, no regulation", a HIT time that rounds to "forever", a mode
change while a channel runs), and turns the fault flags into data you can act on. It runs on any MCU: you give it a
small class that moves bytes and drives two pins, and it does the rest.

| | |
| --- | --- |
| **Driver** | Header-only C++20; no heap, exceptions, RTTI or virtual calls; every call returns `Result<T>` |
| **Runs on** | Any MCU with SPI, two GPIO outputs and a C++20 compiler; the host example runs on any PC ([Platforms](docs/platforms.md)) |
| **Status** | 0.1.0, **not verified on hardware**: host-tested against a protocol model of the chip ([Status and limitations](docs/limitations.md)) |

- Starts the chip with the programming procedure of the datasheet (ENABLE, tEN, STATUS, every channel, tWU) and
  reads every register back before it reports success; it clears and reports faults latched before the start, so
  the FAULT pin shows new ones only; nothing touches the chip before `EnsureInitialized()`.
- Switches channels with one 8-bit write from its own copy of ONCH: no STATUS read, which would cost two transfers
  and clear the OVT and UVM flags. The command stays in force (p25), so a switch right after a switch is one
  transfer, and the STATUS[7:0] byte it returns keeps fault monitoring free.
- Converts milliamps and microseconds to the chip's codes exactly (IFS / 128 per step, 40 chopping periods per HIT
  step) and refuses what would round into a dangerous code.
- Reads and clears the FAULT register the way the fitted part does it: the default suits both parts; a selective
  clear needs the MAX22200A declared (check the BOM).
- Keeps every clear-on-read flag its reads take from the chip (OVT, UVM, the FAULT flags) in a latch you read and
  clear without I/O, and offers a single-shot raw register read for bring-up.
- Offers a read-back per call (`Verify::ReadBack`) where a lost frame matters, at a stated cost in transfers.

## Documentation

| Page | Read it when you want to |
| --- | --- |
| [Quickstart](docs/quickstart.md) | Go from a bare chip to a valve that opens on your MCU |
| [Hardware](docs/hardware.md) | Wire it, check the bus settings, the timing and the power |
| [Porting](docs/porting.md) | Write the port class for your MCU's SDK |
| [API guide](docs/api-guide.md) | Use each call: start, switch, configure, read faults, handle errors |
| [Drive modes](docs/drive-modes.md) | Choose CDR or VDR, HIT and HOLD, the chopping frequency, bridges and DPM |
| [Configuration](docs/configuration.md) | Choose every setting |
| [Status and limitations](docs/limitations.md) | Know what is and is not verified, and the datasheet findings |
| [Troubleshooting](docs/troubleshooting.md) | Find the cause of an error or a wrong reading |
| [Contract](docs/contract.md) | Know exactly what the driver promises, needs and returns |
| [Datasheet map](docs/datasheet.md) | Find the datasheet section behind each part of the driver |
| [API reference](docs/api-reference.md) | Look up a class or member (Doxygen; built by CI and locally) |

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: MAX22200/MAX22200A 36V, 1A Octal Integrated Serial-Controlled Solenoid and Motor Driver (Analog Devices 19-100531) | 1, 2025-03 | [vendor page](https://www.analog.com/en/products/max22200.html) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](docs/datasheets.yml). Where each
part of the driver is in the datasheet: [Datasheet map](docs/datasheet.md).
<!-- /generated -->

Every docs page lists the datasheet sections it relies on, and code comments cite the section they implement.

## Try it on your PC

No hardware needed: [`examples/host`](examples/host/README.md) runs the driver against a simulated chip.
You need git, CMake 3.16+ and a C++20 compiler (GCC 10+, Clang 13+ or MSVC 2022).

1. Get the source:

   ```bash
   git clone https://github.com/kokeb-labs-drivers/driver-max22200.git && cd driver-max22200
   ```

2. Build and run the example:

   ```bash
   cmake -S examples/host -B build-example
   cmake --build build-example
   ./build-example/max22200_host_example
   ```

3. You should see the driver start the simulated chip, switch two valves and read the diagnostics:

   ```text
   MAX22200 driver 0.1.0 on the host
   max22200: initialised
   HIT 500000 uA for 20800 us, HOLD 148437 uA
   ONCH 0x01, flags 0x00
   faults on channels 0x00
   ```

To run the whole test suite instead: `cmake -S . -B build -DMAX22200_BUILD_TESTS=ON && cmake --build build && ctest
--test-dir build`.

## Add it to your project

Pick the row that matches your build; each is a few steps, and [Installation](docs/installation.md) has the
details.

| Your build | Steps below |
| --- | --- |
| Plain CMake: STM32 (CubeMX), RP2040 (Pico SDK), nRF Connect, a host app | [A](#a-cmake-any-mcu) |
| ESP-IDF | [B](#b-esp-idf) |
| An SDK or OS image that installs libraries | [C](#c-installed-package) |

### A. CMake, any MCU

1. Add the driver as a submodule, pinned to the version you chose:

   ```bash
   git submodule add https://github.com/kokeb-labs-drivers/driver-max22200.git external/driver-max22200
   ```

2. In your `CMakeLists.txt`, after `project()`:

   ```cmake
   add_subdirectory(external/driver-max22200)
   target_link_libraries(my_app PRIVATE max22200::max22200)   # brings the headers and C++20
   ```

3. In your code, `#include "max22200.hpp"`, write the port ([Porting](docs/porting.md)) and build. A first build
   that compiles the quickstart above is the check that the driver is wired in.

### B. ESP-IDF

1. Add the driver under your project's `components/`; ESP-IDF finds it there and names it after the folder:

   ```bash
   git submodule add https://github.com/kokeb-labs-drivers/driver-max22200.git components/max22200
   ```

2. In `main/CMakeLists.txt`, list it: `idf_component_register(SRCS "main.cpp" REQUIRES max22200)`.
3. `idf.py menuconfig` now has a **MAX22200 solenoid driver** menu (fitted part, oscillator, RREF),
   and `idf.py build` builds the driver with your app. [`examples/esp32`](examples/esp32) is a complete
   project with a port for the board.

### C. Installed package

1. Install it once into a prefix:

   ```bash
   cmake -S external/driver-max22200 -B build-max22200 && cmake --install build-max22200 --prefix /opt/sdk
   ```

2. In your project, with `CMAKE_PREFIX_PATH=/opt/sdk`:

   ```cmake
   find_package(max22200 0.1 REQUIRED)
   target_link_libraries(my_app PRIVATE max22200::max22200)
   ```

The target is `max22200::max22200` every way, so switching later changes nothing else.

## Tests and quality

```bash
cmake -S . -B build -DMAX22200_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

The host tests run the driver against a protocol model of the chip, fail every bus transfer and pin write in turn,
and produce each error the driver can return. A fuzzer fails, corrupts and replaces what the chip sends back and
checks the driver's promises after every call. CI also builds for Cortex-M and gates coverage, complexity, sanitizers,
fuzzing, clang-tidy, cppcheck and the handbook rules (`.kokeb/handbook.yml`). See [Testing and CI](docs/testing.md),
which lists every gate and its limit.

## License

MIT. See [LICENSE](LICENSE).
