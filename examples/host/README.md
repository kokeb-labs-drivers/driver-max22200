# MAX22200 host example

The driver on your PC, with no hardware: the host port in
[`host_max22200_port.hpp`](host_max22200_port.hpp) connects the real driver to a simulated MAX22200 (the protocol model
the host tests use), and [`main.cpp`](main.cpp) configures two solenoid valves, starts the chip, switches the valves
and reads the diagnostics.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/max22200_host_example
```

## Expected output

```text
MAX22200 driver 0.1.0 on the host
max22200: initialised
HIT 500000 uA for 20800 us, HOLD 148437 uA
ONCH 0x01, flags 0x00
faults on channels 0x00
```

The host tests build this folder too and run the example (CTest `max22200_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: configure, start, switch, read the diagnostics |
| [`host_max22200_port.hpp`](host_max22200_port.hpp) | The host port: bus and pin hooks over the simulated chip, `DelayUs()` and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
