# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - Enable, wake-up and disable times; SPI timing: Electrical Characteristics (tEN, tWU, tDIS, SPI); Figure 1, p. 10
    - Pins, CMD, ENABLE, FAULT and the TRIG inputs: Pin Configuration; Pin Description, p. 12
    - The SPI, the 8-bit accesses and the daisy chain: SPI Detailed Description; Figure 5, p. 23
    - The Command Register and the fault byte: Command Register Description (COMMAND); Table 9; Figure 7, p. 25
    - 32-bit and 8-bit transfers and the command echo: Reading and Writing Operations of Registers; Figures 10 to 13, p. 30

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `max22200::SpiInterface<Self>` (in
[`inc/max22200_spi_interface.hpp`](../inc/max22200_spi_interface.hpp)). The base uses CRTP and declares no hook
itself; the driver template is constrained by the `max22200::SpiPort` concept, so calls are resolved at compile
time (no virtual functions, nothing to allocate) and a missing or misdeclared hook is a compile error at the point
of use.

## What you implement

| Hook | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the bus, CMD and ENABLE up on the first call, each pin at its idle level before it becomes an output (below); `true` at once afterwards |
| `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | yes | One full-duplex transfer of 1 or 4 bytes, both buffers given, CSB low for all of them and high after |
| `bool GpioSet(CtrlPin pin, GpioSignal signal) noexcept` | yes | Drive `CtrlPin::Cmd` or `CtrlPin::Enable`; `false` when the pin could not be driven |
| `void DelayUs(uint32_t us) noexcept` | yes | Wait at least `us` microseconds: tEN (500 µs) and tWU (2500 µs) at start-up |
| `bool GpioRead(CtrlPin pin, GpioSignal& signal) noexcept` | no | Read `CtrlPin::Fault`: `ACTIVE` while the pin is low |
| `void Log(LogLevel level, const char* message) noexcept` | no | One line per lifecycle event or failure; plain text, never a format string |

The required hooks are checked exactly by the `SpiPort` concept: each parameter must have exactly the type above
and each hook must be `noexcept` and return `bool`. A `Transfer()` that takes a `uint16_t` length, or a `GpioSet()`
without `noexcept`, is a compile error, not a silent conversion (handbook KB-CPP20-006). An optional hook that is
declared with another signature (`void GpioRead(...)`, `DelayUs(uint16_t)`) is a compile error too, never ignored
(KB-CPP20-007).

## Bus requirements

What the chip needs from the bus is in `max22200_spi_interface.hpp` as `constexpr` values (handbook KB-CPP-123). Check
your port's settings against them with `static_assert`, as the host and ESP32 ports do:

| Constant | Value | From |
| --- | --- | --- |
| `kSpiMode` | 0 (CPOL = 0, CPHA = 0) | SPI Detailed Description, p23 |
| `kMsbFirst` | true | Figure 1 |
| `kMaxClockHz` | 10 000 000 (tCLK 100 ns; the SDO pull-up may set less) | Electrical Characteristics, SPI rows, p10 |
| `kMinCsHighNs` | 30 (tCSPW) | the same |
| `kCsSetupNs` | 30 (tCSS) | the same |
| `kCsHoldNs` | 30 (tCSH) | the same |
| `kContinuousWord` | false: no limit on idle SCK time inside a transfer | the chip counts clocks while CSB is low (p34) |
| `kCmdSetupNs` | 20 (tCMS); tCMH has no value | Electrical Characteristics, Figure 1 |
| `kTransferBytes` | 1 and 4: the only lengths the driver asks for | Figures 10 to 13 |
| `kSdoOpenDrain` | true: fit a pull-up on SDO | Pin Description, p12 |

The port below checks its clock this way (`kClockHz`).

## Control pins and their idle levels

The port drives each control output to its idle level before it enables the pin's output driver and before the first
transfer (handbook KB-HW-018): set the level, then configure the pin as an output.

| Pin | `CtrlPin` | Polarity | Idle level | Why |
| --- | --- | --- | --- | --- |
| CMD | `Cmd` | active high: `ACTIVE` = high selects the Command Register | high (`ACTIVE`) | The Command Register is written with CMD high for the whole transfer and at the CSB rising edge (p25-26); only a data phase lowers it |
| ENABLE | `Enable` | active high: `ACTIVE` = high wakes the chip | low (`INACTIVE`) | The chip sleeps, every output Hi-Z, until `EnsureInitialized()` raises it |
| FAULT | `Fault` | open drain, active low: `ACTIVE` = low = a fault | input with a pull-up | Read only (`GpioRead()`) |

## Timing

The bus: SPI mode 0, MSB first, up to 10 MHz. The driver calls `Transfer()` once for the command byte (with CMD
high) and once for the data (with CMD low), then drives CMD high again: CMD idles high between transactions, and your
port's `EnsureInitialized()` should leave it high. Never more than 4 bytes, so a polling transfer is fine. CSB must go high
between the two. `GpioSet(CtrlPin::Cmd, ...)` must have taken effect before `Transfer()` starts (tCMS, 20 ns before
the CSB falling edge, Figure 1) and CMD must still be high when CSB rises (p26), then for tCMH, for which the
datasheet gives no value: a GPIO write that returns after the pin changed, before and after `Transfer()`, does both.
A board that ties ENABLE high returns `true` for `CtrlPin::Enable` and does nothing.
tEN and tDIS are minimum waits: the driver calls `DelayUs()` for them after `GpioSet()` returns. A pin behind a slow
I/O expander adds its latency on top and never shortens them; it does stretch every CMD toggle, so put CMD on an MCU
pin where you can.

The port moves raw bytes: it never retries, never interprets the data and never configures the chip.

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

## Optional hooks

A port with every hook, as the ESP32 example has:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

With `GpioRead()`, `ReadFaultPin()` reads the FAULT pin without SPI; without it, it returns `Unsupported`. A use:

```cpp
--8<-- "tests/docs/ports.cpp:fault-pin"
```

A `GpioSet()` that returns `void`, or none at all, is a compile error: the driver must see a failed pin write, and
CMD is part of the protocol. A missing `DelayUs()` is a compile error too. The [compile-fail tests](testing.md)
check each rule.

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_max22200_port.hpp`](../examples/host/host_max22200_port.hpp): any PC, against a simulated
  chip. Start from it for a host build or a test bench; it is also the smallest complete port.
- [`examples/esp32/main/esp32_max22200_port.hpp`](../examples/esp32/main/esp32_max22200_port.hpp): ESP-IDF, with
  every hook. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Call `EnsureInitialized()`: `Ok` proves that CMD, ENABLE, SDI and SDO work: every 32-bit write echoed its
   command and every register read back what was written. `NoDevice` means SDO reads 0xFF (no supply, ENABLE low,
   no pull-up); `DeviceIdMismatch` means the chip did not take or echo the commands (SDI, CMD or CSB).
3. With a load on channel 0: `SetChannel(0, true, Verify::ReadBack)` returns `Ok`, the load is energised, and
   `ReadFaults()` shows no OLF for channel 0 while it is on. **Not verified on hardware** with this driver yet.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
