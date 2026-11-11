---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# MAX22200 Driver

<p>A C++20 driver for the Analog Devices MAX22200 and MAX22200A octal solenoid and motor drivers. It starts the
chip with the datasheet's procedure, switches channels with one 8-bit write (one transfer per switch), programs
HIT and HOLD in milliamps or percent, and reads the faults, on any MCU.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-max22200){ .md-button }
</div>

The driver has no platform code. You give it a small class that moves bytes and drives CMD and ENABLE; it does the
rest: the programming procedure, the two-phase register access, the unit conversions, the read-back of every
register at start-up, and the fault registers of both parts.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">Octal solenoid and motor driver</span>
<span class="kb-chip__name">MAX22200</span>
<span class="kb-chip__vendor">Analog Devices</span>
</div>

Function
:   Eight push-pull half-bridges, 200 mΩ, up to 1 A RMS each; HIT then HOLD per channel

Regulation
:   Current (CDR, 7-bit, IFS from RREF, low side) or voltage (VDR, PWM duty in 1 % steps)

Configurations
:   Low side, high side, parallel pairs, full bridges for latching valves and DC motors

Diagnostics
:   OCP, open load, HIT current not reached, plunger movement, overtemperature, UVLO, SPI clock count

Interface
:   SPI mode 0, up to 10 MHz; a command byte with CMD high, then 8 or 32 bits; ENABLE, FAULT, TRIGA/TRIGB

Supply
:   VM 4.5 V to 36 V; logic inputs 6 V absolute maximum, 1.2 V high
</div>

## What you get

- **One object per chip.** `MAX22200<YourPort>` takes a reference to your port and does no I/O until
  `EnsureInitialized()`. See [API guide](api-guide.md).
- **Errors you can act on.** Every call returns `Result<T>` with an `Error` such as `Bus`, `ChannelsActive` or
  `DeviceReset`. See [API guide](api-guide.md#handle-errors).
- **Datasheet codes, checked.** `Config{}` holds the reset values of STATUS and every channel; the conversions
  refuse a current that would round to the unregulated code and a HIT time that would round to "forever". See
  [Drive modes](drive-modes.md).
- **A fast hot path.** Switching is one 8-bit write, one transfer after another switch; a read-back is there per
  call when you want it. See [API guide](api-guide.md).
- **Zero-overhead portability.** The port is a CRTP class checked by a concept: the bus, CMD and ENABLE and a
  delay, plus optional FAULT-pin and logging hooks found at compile time. See [Porting](porting.md).
- **No heap, no exceptions, no RTTI.** The host tests build with `-fno-exceptions -fno-rtti`. See
  [Contract](contract.md).
- **Tested without hardware, and honest about it.** Host tests and a fuzzer run the driver against a protocol model
  of the chip; nothing is verified on hardware yet. See [Status and limitations](limitations.md).

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: MAX22200/MAX22200A 36V, 1A Octal Integrated Serial-Controlled Solenoid and Motor Driver (Analog Devices 19-100531) | 1, 2025-03 | [vendor page](https://www.analog.com/en/products/max22200.html) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

## Where to go next

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    ---

    Wire the chip, write the port, open a valve in about 20 lines.

-   :material-chip: **[Hardware](hardware.md)**

    ---

    Pins, bus settings, timing and power.

-   :material-file-document-check-outline: **[Contract](contract.md)**

    ---

    What the driver promises, what it needs, threading and errors.

-   :material-code-braces: **[API guide](api-guide.md)**

    ---

    Start, switch, configure, read faults and handle errors, task by task.

-   :material-swap-horizontal: **[Porting](porting.md)**

    ---

    Implement the port for your MCU.

-   :material-book-open-variant: **[API reference](api-reference.md)**

    ---

    Every class and member, generated from the headers.

</div>
