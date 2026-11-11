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
