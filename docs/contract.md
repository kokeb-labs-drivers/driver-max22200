# Contract

What the MAX22200 driver promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - Enable, wake-up and disable times; SPI timing: Electrical Characteristics (tEN, tWU, tDIS, SPI); Figure 1, p. 10
    - ENABLE and the 0.5 ms to SPI ready: Enable Logic Input, p. 14
    - The programming procedure: Programming Procedure; Figure 6, p. 23
    - 32-bit and 8-bit transfers and the command echo: Reading and Writing Operations of Registers; Figures 10 to 13, p. 30
    - Communication error detection: Communication Error Detection (COMF), p. 34

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Promises

- The constructor is `noexcept`, stores the configuration, does no I/O and cannot fail.
- `EnsureInitialized()` is the only way to start the driver. Its first call checks the stored configuration
  (`InvalidArgument`, nothing sent), brings the bus up, drives CMD high and ENABLE high, waits tEN, wakes the chip (bare
  ACTIVE writes until ACTIVE reads 1 with UVM clear, within `Config::wake_timeout_us`), runs the programming procedure
  of Figure 6, then checks that STATUS, every CFG_CHn and CFG_DPM hold what it wrote and that every 32-bit write echoed
  its command. On a wake timeout (`Undervoltage`, `Timeout`) it leaves ENABLE high with every channel off; on any other
  failure it drives ENABLE low. Either way the driver stays uninitialised and the next call starts again from the
  beginning; once it has succeeded, later calls return `Ok` with no I/O.
- Every call that touches the chip returns `Error::NotInitialized` before `EnsureInitialized()` succeeds, and sends
  nothing. No call starts or resets the chip as a side effect. `ApplyConfig()` before the start only stores the
  configuration.
- `InvalidArgument`, `ChannelsActive` and `Unsupported` mean nothing was sent.
- Every port failure propagates: a transfer as `Error::Bus`, a pin write as `Error::Pin`. A command byte is never
  sent unless CMD went high, and CMD is driven low after every command byte, whatever happened.
- A call sends only the transfers the datasheet needs; the read-backs are per call, `Verify::ReadBack`, with their
  cost in the doc comment. The start-up checks are unconditional.
- An ONCH write (`SetChannelsOn()`, `SetChannel()`, `SetBridge()`) is 2 transfers, or 1 (the data byte alone) when
  the last access was an ONCH write without `Verify::ReadBack` and nothing has happened since: no other access, no
  failed transfer or pin write, no ENABLE change, no COMER in a fault byte (p25: the command stays in force until
  rewritten). Its STATUS[7:0] byte updates `GetLastStatusByte()`.
- After a read-back that differs, the driver's copy takes the value read, and the bits that differ are not trusted
  for the `ChannelsActive` check; a channel whose CFG_CH write failed or read back different accepts no write while
  a channel is on.
- The driver never sends a CDR current code of 127 (unregulated full on), a VDR code above 100, CDR on the high
  side, SRC, HFS or DPM on the high side, SRC at 50 kHz or above, HHF in VDR, RFU bits in a command, or non-zero
  bits in CFG_DPM[31:15]. `HitTimeCode()` and `CurrentCode()` never round into 255 or 127.
- A drive-mode or side change is refused while any channel the driver switched (or may have switched, after a
  failed write) is on; a pair-mode change while either channel of the pair is on.
- Every wait is bounded: `EnsureInitialized()` waits tEN, then wakes the chip for at most
  `Config::wake_timeout_us` (one tWU and one STATUS read every `kWakePollUs`), at most `kStartAttempts` attempts.
  tEN and tDIS are waited in full after the pin write returns; a slow pin (an I/O expander) adds latency, never
  shortens them.
- CMD idles high: the driver drives it high before `EnsureInitialized()` touches the chip and back high after every
  data phase, the one-transfer ONCH write included, also when the transfer failed.
- No read hides a clear-on-read flag: every 32-bit STATUS read, the driver's own included, ORs OVT and UVM into a
  latch, and every FAULT read ORs the flags it clears (`GetLatchedFaults()`, emptied only by `ClearLatchedFaults()`;
  handbook KB-CPP-119). A restart does not empty it; the start-up does not keep the power-on UVM it
  clears, and a reply a floating SDO produces (all ones, 0xFF fault byte) is never latched.
- The start-up reads and clears FAULT, so faults latched before it (the chip may keep them across an ENABLE cycle) do
  not hold the FAULT pin; it reports them in `GetStartupFaults()` (handbook KB-CPP-058).
- A load-dependent diagnostic (open load, plunger movement, HIT current) is sent only for a channel that
  `Config::loaded_channels` declares (handbook KB-CPP-131); `Validate()` and `ConfigureChannel()` refuse it elsewhere.
- No public helper returns a sentinel: a conversion without the input it needs, or of a code that is not a value
  (`FullScaleUa()` without RREF, `HitTimeUs()` of the continuous code), returns `InvalidArgument` (KB-CPP-126). A
  register field that holds no enumerator (CMxy = 3) is refused, never stored (`DecodePairMode()`).
- `Config{}` holds the reset values of STATUS and every CFG_CHn (p36, p38-39). The datasheet gives no reset values
  for CFG_DPM (p40-41): `Config{}.dpm` is 0, the value written at start-up, not a known reset state.
- `Config{}.variant` is `Variant::Max22200`, correct on both parts (a selective clear is refused); declare
  `Variant::Max22200A` only when the BOM says it is fitted.
- A missing `GpioSet()` or `DelayUs()`, or a `GpioSet()` that returns `void`, is a compile error; a missing
  `GpioRead()` gives `Error::Unsupported` from `ReadFaultPin()`; a missing `Log()` logs nothing.
- `Deinitialize()` leaves every channel off, ACTIVE = 0 and ENABLE low, even after a failed write.

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `max22200_version.h` |
| Your port | | The bus, CMD, ENABLE and the delay; optionally the FAULT pin and logging ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Use each driver object from one task at a time, or guard every
call on it with one mutex; the layer that owns the driver owns that lock, and the bus lock when several chips share
a bus. Nothing may run in an ISR: every call can block for a bus transfer or a delay (`EnsureInitialized()` for 3 ms
or more). Separate objects share no state.

## Errors

`max22200::Error` inside `max22200::Result<T>`. Values 0 to 9 are the shared base set; chip-specific values start
at 16, and each is produced by a test (`tests/test_errors.cpp`).

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` has not succeeded, `Deinitialize()` ran since, or a call returned `DeviceReset` |
| `InvalidArgument` | 2 | A `Config` field or argument is out of range (`Validate()`, `ValidateChannel()`); nothing was sent |
| `Bus` | 3 | The port's `Transfer()` or `EnsureInitialized()` returned `false` |
| `Timeout` | 4 | At start-up, ACTIVE did not read 1 within `Config::wake_timeout_us` while UVM read clear. Uninitialised, every channel off, ENABLE high; retry, and cycle VM if it persists |
| `DeviceIdMismatch` | 5 | At start-up, STATUS read back differs from what was written, or a write did not echo its command |
| `Crc` | 6 | Never: the chip sends no CRC |
| `Busy` | 7 | Never: the chip is never busy to SPI |
| `DeviceFault` | 8 | Never: faults are data (`ReadStatus()`, `ReadFaults()`) |
| `Unsupported` | 9 | `ReadFaultPin()` without `GpioRead()`; `ClearFaults(which)` on the MAX22200 |
| `NoDevice` | 16 | At start-up, STATUS reads 0xFFFFFFFF with a 0xFF fault byte |
| `Pin` | 17 | The port's `GpioSet()` (or `GpioRead()`) returned `false` |
| `CommunicationError` | 18 | A COMER that went from 0 to 1 during each start-up attempt; on a running driver, the first call that sees COMER go from 0 to 1 in a fault byte (the miscounted write may be an earlier call's), or with `Verify::ReadBack` a read-back that differs while the fault byte shows COMER |
| `WriteMismatch` | 19 | A register read back differs from what was written: at start-up, or with `Verify::ReadBack` |
| `Undervoltage` | 20 | At start-up, UVM was still set at `Config::wake_timeout_us`. Uninitialised, every channel off, ENABLE high; retry later or report a supply fault; a VM power cycle clears a stuck UVM |
| `ChannelsActive` | 21 | A drive-mode, side or pair-mode change while a channel concerned is on; nothing was sent |
| `DeviceReset` | 22 | Any I/O call of a running driver found ACTIVE = 0 in a fault byte or in STATUS: a power-on reset. Nothing more was sent; the driver is uninitialised afterwards |
| `Uncertain` | 23 | A switch-on in a pair whose mode a write with a failed reply left unknown; that switch-on was not sent, the rest of a `SetChannelsOn()` mask was. A switch-off is never refused |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A
driver object holds a reference to the port, a `Config` (120 bytes) and a few bytes of state: 136 bytes on a
64-bit host, less on a 32-bit MCU. The largest
stack buffer is 4 bytes (one 32-bit data phase).

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port in
[`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

- **No chip ID.** The MAX22200 has no identification register. The start-up identity check is the datasheet's own
  procedure plus a read-back: the command echo of every 32-bit write (Figure 10), STATUS read back after tWU with
  ACTIVE set and every writable bit as written, and every configuration register read back. A floating or stuck SDO,
  an SDI that does not reach the chip, and a CMD that never goes high all fail it.
- **No reset command.** The chip resets only at power-on. `EnsureInitialized()` writes every register the driver
  uses (STATUS, CFG_CH0 to CFG_CH7, CFG_DPM), so the start does not depend on what an earlier program left; FAULT is
  read-only and keeps its flags until they are cleared.
- **One driver per chip, no daisy chain.** The datasheet's daisy chain (Figure 5) is not supported.
