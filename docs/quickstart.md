# Quickstart

Run the driver on your PC first, with no hardware; then open a valve from your MCU: wire the chip, write a small
port class, call two driver functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - Pins, CMD, ENABLE, FAULT and the TRIG inputs: Pin Configuration; Pin Description, p. 12

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated MAX22200, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/max22200_host_example
```

```text
MAX22200 driver 0.1.0 on the host
max22200: initialised
HIT 500000 uA for 20800 us, HOLD 148437 uA
ONCH 0x01, flags 0x00
faults on channels 0x00
```

The calls in [`main.cpp`](../examples/host/main.cpp) are the ones below; only the port differs on an MCU. CI builds
and runs this example on every pull request.

## 1. Wire it

| MCU | MAX22200 | Notes |
| --- | --- | --- |
| SPI SCK | SCK | Mode 0, up to 10 MHz; 1 MHz for bring-up on jumper wires |
| SPI MOSI | SDI | |
| SPI MISO | SDO | Open drain: fit a pull-up (1 kΩ to 4.7 kΩ to the MCU's supply) |
| GPIO (chip select) | CSB | Low for each transfer |
| GPIO | CMD | High while the command byte is written |
| GPIO | ENABLE | Or tie it high |
| | IREF | 15 kΩ to ground: IFS = 1 A |
| 24 V (4.5 V to 36 V) | VM | 0.1 µF at each VM pair, 10 µF bulk |
| GND | GND, EP | |

Connect the valve from OUT0 to VM (low side). FAULT, TRIGA and TRIGB can stay unconnected for this example (TRIG
has a pull-down). The [Hardware](hardware.md) page has the full pin list, the timing and the power.

## 2. Add the driver

Add the repository as a submodule and link the `max22200::max22200` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-max22200.git external/driver-max22200
```

## 3. Write the port

The port is the only platform code: it brings the bus up, moves bytes and drives two pins. Replace the
`platform_*` calls with your SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each method, the optional hooks and the transfer rules.

## 4. Open the valve

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What each step does:

1. The `Config` declares RREF, so the driver can turn milliamps into the chip's codes: `CurrentCode()` gives
   600 mA as code 77 (77/128 x 1 A) and refuses a current it cannot regulate; `HitTimeCode()` gives 30 ms at
   25 kHz as 19 steps of 1.6 ms (30.4 ms).
2. `EnsureInitialized()` drives ENABLE high, waits tEN, runs the datasheet's programming procedure (STATUS read,
   STATUS write with ACTIVE = 1, every CFG_CHn, CFG_DPM), waits tWU and reads every register back.
3. `SetChannel(0, true)` is one 8-bit write of ONCH (2 transfers; the next switch is 1). The chip drives HIT for
   30 ms, then HOLD, until `SetChannel(0, false)`.

This example is built by the host CI, so it compiles against the current headers. It is **not verified on
hardware** ([Status and limitations](limitations.md)).

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `EnsureInitialized()` returns `Bus` | Your port's `EnsureInitialized()` or `Transfer()` returned `false` |
| `EnsureInitialized()` returns `NoDevice` | SDO reads 0xFF: VM, ENABLE, the SDO pull-up ([Troubleshooting](troubleshooting.md)) |
| `EnsureInitialized()` returns `DeviceIdMismatch` | SDI, CMD or CSB do not reach the chip |
| `EnsureInitialized()` returns `Undervoltage` | VM is below about 4 V, or still rising |
| The valve does not move | IREF and RREF, the HIT current against the valve's pull-in current, `ReadFaults()` |

## Next

- [API guide](api-guide.md): switching, HOLD on the fly, faults, errors.
- [Drive modes](drive-modes.md): CDR and VDR, HIT and HOLD, bridges, DPM.
- [Configuration](configuration.md): every `Config` field, Kconfig.
- [Examples](examples.md): the host example and the MCU examples.
