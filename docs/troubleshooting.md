# Troubleshooting

Symptoms, causes and fixes, cheapest check first. Hook the optional `Log()` up first: every failure logs one line.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - Overcurrent and open-load protection: Protection Circuits; Overcurrent Protection (OCP); Open-Load Detection (OL), p. 21
    - Plunger-movement detection, thermal shutdown, UVLO and the HIT check: Detection of Plunger Movement (DPM); Thermal Shutdown (TSD); Undervoltage Lockout (UVLO); HIT Current Not Reached Flag (HHF), p. 22
    - Communication error detection: Communication Error Detection (COMF), p. 34

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## EnsureInitialized returns Bus or Pin

The port reported a failure: `Bus` from `EnsureInitialized()` or `Transfer()`, `Pin` from `GpioSet()`.

1. Check that your port's `EnsureInitialized()` returns `true` on its own (log it).
2. Check the CMD and ENABLE pins in your port against the board; a board with ENABLE tied high still returns `true`
   for `CtrlPin::Enable`.
3. With a logic analyser: ENABLE rises, then after 0.5 ms the first transfer, one byte `0x00` with CMD high.

## EnsureInitialized returns NoDevice

STATUS read 0xFFFFFFFF with a 0xFF fault byte: nothing pulls SDO low. SDO is open drain, so a missing chip, an
unpowered chip and a sleeping chip all read 0xFF through the pull-up.

1. VM between 4.5 V and 36 V at the chip, and V18 at 1.8 V (the chip makes its own logic supply from VM).
2. ENABLE high at the pin (above 0.9 V) when the first transfer starts.
3. SDO wired to MISO, and CSB going low for each transfer.

## EnsureInitialized returns DeviceIdMismatch

The chip did not answer as a MAX22200 does: a 32-bit write did not echo its command byte (Figure 10), or STATUS
read back after the wake-up does not hold what was written (ACTIVE set, every writable bit as sent).

1. SDO stuck low (reads 0x00): a short, or a pull-up too strong for the chip to pull down.
2. SDI not reaching the chip: the chip keeps taking `0x00` (a STATUS read) and never sees a write.
3. CMD not reaching the chip, or changing too late: the command byte is taken as data. CMD must be high before the
   transfer starts and at the CSB rising edge.
4. SPI mode: mode 0 only (CPOL = 0, CPHA = 0), MSB first.
5. A slow SDO edge: lower the clock to 1 MHz, use a stronger pull-up (1 kΩ).

## EnsureInitialized returns WriteMismatch

A CFG_CHn or CFG_DPM register read back differs from what was written: the chip ignored or lost a write. On a bus
shared with other chips a lost frame does this; the driver reports it instead of starting with an unprogrammed
channel. Retry `EnsureInitialized()`; if it persists, check the chip select of the other devices (only one low at
a time).

## EnsureInitialized returns Undervoltage

UVM was still set when `Config::wake_timeout_us` (100 ms by default) ran out, with ACTIVE written again every
`kWakePollUs`: VM is below the UVLO threshold (4 V typ), keeps dipping, or rises slowly. The driver is
uninitialised, every channel off, ENABLE left high. Measure VM at the chip during the start; for a slow supply raise
`Config::wake_timeout_us` (up to 10 s) or call `EnsureInitialized()` again later. ENABLE low is not a reset (p14):
if UVM persists with VM in range, cycle VM, which ends in a power-on reset (p22).

## EnsureInitialized returns Timeout

UVM read clear but ACTIVE never read 1 within `Config::wake_timeout_us`, although ACTIVE was written again every
`kWakePollUs`. The chip is not accepting writes: check SDI and CMD (CMD left low turns commands into data), then
cycle VM.

## EnsureInitialized returns CommunicationError

Every attempt saw COMER go from 0 to 1: the chip counted a number of clocks other than 8 or 32 in a write and
ignored it ("Communication Error Detection (COMF)"). Check that CSB stays low for exactly the 1 or 4 bytes of a
transfer (no gap that releases it, no extra byte), and SCK for glitches.

## A setter returns ChannelsActive

The chip changes the drive mode or the side only with every channel off, and a pair mode only with both channels of
the pair off (Programming Procedure, step 5c; Register Details). Switch them off with `SetChannelsOn()` first. After
a failed ONCH write the channels it would have switched count as on until a write succeeds:
`SetChannelsOn(GetChannelsOn())` settles it.

## ReadBack returns WriteMismatch for a drive-mode change

The chip ignored the change: a channel is on that the driver did not switch (TRIG pin, an earlier program), or TRIGA
or TRIGB is high. The driver cannot see the TRIG pins: keep them low while changing a drive mode or a side.

## A call returns DeviceReset

A fault byte (or STATUS) read ACTIVE = 0: a supply dip reset every register (UVLO with a power-on reset). Any call can
return it; the call sent nothing after the command byte, so the reset chip stays asleep with every channel off. The
driver is uninitialised; call `EnsureInitialized()` to configure the chip again, then switch the channels you need.
If it repeats, look at VM under load (inrush of the valves). If it appears with no supply dip, check SDO: a line stuck
low reads as ACTIVE = 0 too.

## A call returns CommunicationError on a running driver

A fault byte showed COMER going from 0 to 1: a write since the last clean byte had the wrong number of clocks and was
ignored (p34), possibly an earlier call's. Switch every channel off first (`SetChannelsOn(0)`: a switch-off is never
refused, and while COMER is set it is read back), then call `ApplyConfig(GetConfig())` to write the registers in doubt
again (it returns `ChannelsActive` while a channel whose mode is in doubt could still be on) and switch the channels
back on. Check SCK and CSB for glitches. The copy is not proof while COMER stays set: a switch-off is read back, but a
miscounted switch-on or configuration write is seen only with `Verify::ReadBack`. `IsMiscountDetectionDegraded()`
returns true in that state (no I/O): use `Verify::ReadBack` for the writes that must land, or restart with
`Deinitialize()` and `EnsureInitialized()`, which takes ENABLE low to clear COMER.

While COMER stays set, the driver keeps doubt rather than trust a write it cannot prove, by design:

- A channel that `ApplyConfig()` rewrote while another channel was on stays "of either mode", so a further
  `ApplyConfig()` change to it returns `ChannelsActive`, even one that changes only HOLD.
- `SetPairMode()` without `Verify::ReadBack` leaves its pair's mode in doubt, so the next switch-on in that pair returns
  `Uncertain`.

The ways out: `SetHoldCode()` for a HOLD change, `ConfigureChannel()` with every channel off,
`SetPairMode(pair, mode, Verify::ReadBack)`, or a restart.

## A switch returns Uncertain

A write to STATUS failed after it may have reached the chip, and it changed this pair's mode: the chip may run either
mode, and what ONCHx and ONCHy do depends on it (Tables 6 and 7). Write the mode again with `SetPairMode()` (or
`Verify::ReadBack`), or `ApplyConfig()`, while the pair is off. A switch-off always works: `SetChannel(ch, false)` or
`SetBridge(pair, BridgeState::HiZ)` switches both channels of the pair off. A `SetChannelsOn()` that returns `Uncertain`
still sent the rest of its mask; the unknown pair itself was switched off.

## A channel does not drive

1. `ReadStatus()`: `active` set, `on_mask` has the channel, no `overtemperature`.
2. `ReadFaults()`: OCP switches the channel off and keeps it off until it is switched off in ONCH and on again; OLF
   with a load fitted means the load does not reach VM (low side) or ground (high side).
3. CDR: `hit` and `hold` not 0, `Config::rref_ohm` matches the resistor (IFS = 15 kΩ x 1 A / RREF), and the HIT
   current above the valve's pull-in current. VDR: the duty and VM give enough current.
4. `trigger_pin` set: the channel follows TRIGA (even) or TRIGB (odd), not ONCH.

## HHF or DPM flags on a working valve

HHF: the current did not reach IHIT by the end of tHIT: lengthen the HIT time or lower IHIT. DPM: no dip deeper than
the threshold and longer than the debounce came between ISTART and IHIT: lower the threshold or the debounce, set
ISTART just below the dip, and check that the current rises slower than 700 mA/ms above ISTART ([Drive
modes](drive-modes.md#plunger-movement-detection-dpm)).

## The current is lower than set

CDR regulates the peak current; the average is lower by half the ripple, which grows as the chopping frequency falls.
The accuracy is ±6 % above 71 % of IFS and ±25 % from 5 % to 10 %: for small currents choose a larger RREF or HFS.
