# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the MAX22200

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the MAX22200: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## What the MAX22200 does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
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

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## MAX22200: Application diagram

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the MAX22200 application diagram: the chip with U2 MCU, C1 capacitor 0.1 µF, C2
    capacitor 0.1 µF, C3 polarized-capacitor 10 µF, C4 capacitor 2.2 µF, C5 capacitor 22 nF, C6
    capacitor 1 µF, R1 resistor 15 kΩ, R2 resistor 10 kΩ, R3 resistor 10 kΩ, L0 solenoid, L1 solenoid,
    L3 solenoid, J1 DC motor, J2 Latched valve; the MCU wires carry the port hooks
    GpioRead(CtrlPin::Fault), GpioSet(CtrlPin::Cmd), GpioSet(CtrlPin::Enable), Transfer().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## MAX22200: Command byte

<!-- figure: chip-frame-command -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-frame-command.dark.svg">
  <img src="chip-frame-command.svg"
    alt="Command byte of the MAX22200, 8 bits, most significant bit first. Command: RB/W (bit 7), RFU (bits 6
    to 5), A_BNK (bits 4 to 1), 8bit/n32bits (bit 0); Reply: STATUS[7:0] (bits 7 to 0). The STATUS[7:0]
    field of the Reply row: bit 7 OVT, bit 6 OCP, bit 5 OLF, bit 4 HHF, bit 3 DPM, bit 2 COMER, bit 1
    UVM, bit 0 ACTIVE. Below, the SPI mode 0 waveform of one transfer in the same bit grid.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## MAX22200: 8-bit write

<!-- figure: chip-frame-switch -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-frame-switch.dark.svg">
  <img src="chip-frame-switch.svg"
    alt="8-bit write of the MAX22200, 8 bits, most significant bit first. Write: ONCH[7:0] (STATUS[31:24])
    (bits 7 to 0); Reply: STATUS[7:0] (bits 7 to 0).">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## MAX22200: 32-bit write

<!-- figure: chip-frame-write32 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-frame-write32.dark.svg">
  <img src="chip-frame-write32.svg"
    alt="32-bit write of the MAX22200, 32 bits, most significant bit first. Write: DATAIN[7:0] (bits 31 to
    24), DATAIN[15:8] (bits 23 to 16), DATAIN[23:16] (bits 15 to 8), DATAIN[31:24] (bits 7 to 0); Reply:
    STATUS[7:0] (bits 31 to 24), command echo (bits 23 to 16), 0x00 (bits 15 to 8), 0x00 (bits 7 to 0).">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## MAX22200

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the MAX22200, an eight-channel driver for solenoids, valves, relays and brushed DC
    motors: each 36 V, 1 A half-bridge runs a HIT then HOLD sequence under current or voltage
    regulation, set over SPI. A MCU talks to it over SPI, CMD, ENABLE; it drives Solenoids, valves,
    relays; it reads back Coil current, TRIGA and TRIGB. Headline figures: 4.5 to 36 V supply (VM); 8 x
    1 A half-bridges, 1 A RMS each; 200 mΩ typical on-resistance per output; 10 MHz SPI clock, one chip
    (5 MHz in a daisy chain); 5 x 5 mm TQFN-32 body.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## MAX22200 pinout, TQFN-32

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the MAX22200 in TQFN-32, top view: 32 pins and an exposed pad, each with its name, its
    electrical type as a colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## Wiring: Application diagram

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-typical.dark.svg">
  <img src="chip-wiring-typical.svg"
    alt="Wiring of the MAX22200 for the Application diagram: MCU SPI SCK to SCK (Transfer()); MCU SPI MOSI to
    SDI (Transfer()); MCU SPI MISO to SDO (Transfer()); MCU SPI CS to CSB (Transfer()); MCU GPIO to CMD
    (GpioSet(CtrlPin::Cmd)); MCU GPIO to ENABLE (GpioSet(CtrlPin::Enable)); MCU GPIO (IRQ) to FAULT
    (GpioRead(CtrlPin::Fault)); MCU GPIO to TRIGA; OUT6 to DC motor M+; OUT7 to DC motor M-; OUT2 to
    Latched valve B; OUT3 to Latched valve A. Supplies, grounds and the support parts are listed under
    the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the MAX22200 core, which sends register reads and writes
    through the SPI port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/max22200.hpp`](../../inc/max22200.hpp)
- `SpiInterface` in [`inc/max22200_spi_interface.hpp`](../../inc/max22200_spi_interface.hpp)

## One call through the layers

<!-- figure: one-call-through-the-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="one-call-through-the-layers.dark.svg">
  <img src="one-call-through-the-layers.svg"
    alt="One call through the layers: your code calls SetChannel(2, true), the core exchanges frames with the
    chip through your port and returns the result to your code.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)

Drawn from:

- [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)
