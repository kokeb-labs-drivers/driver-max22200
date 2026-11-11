# Status and limitations

What this driver has and has not been shown to do, so you can decide whether it is ready for your use.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - High side, parallel pairs and full bridges: Supported Driver Configurations; Tables 6, 7 and 8, p. 20
    - The SPI, the 8-bit accesses and the daisy chain: SPI Detailed Description; Figure 5, p. 23
    - FAULT and its two clear-on-read behaviours: Fault Register Description (FAULT); Table 12; Clearing the FAULT Register; Figure 8, p. 28

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Status

| | |
| --- | --- |
| **Version** | 0.1.0, not released |
| **On hardware** | **Not verified on hardware.** No board has run this driver yet: run the bench steps below first |
| **Tested against** | A protocol model of both parts ([Testing and CI](testing.md)), a fuzzer and the quality gates |
| **Assurance level** | `high` (Kokeb Labs handbook): not qualified for use in a safety function |
| **Parts** | MAX22200 and MAX22200A (datasheet 19-100531 Rev 1). One chip per driver object; no daisy chain |

## Start-up and wake

`EnsureInitialized()` follows the programming procedure of Figure 6 (p23-24):

1. CMD high, ENABLE high, then tEN (0.5 ms, "Enable Logic Input", p14). CMD idles high because the Command Register
   is written with CMD high for the whole transfer and at the CSB rising edge (p25-26); a data phase lowers it only
   around its own transfer. tEN and tDIS are minimums: the driver waits them in full after its pin write returns. A
   port whose pins sit behind a slow I/O expander adds its own latency on top; it never shortens them.
2. STATUS is read: step 1 of the procedure, which clears UVM, set at power-up, and releases FAULT (p23, p27; "FAULT is
   always asserted at power-up", p22). All ones with a 0xFF fault byte is `NoDevice`: nothing drives the open-drain
   SDO (p12).
3. Wake loop, the procedure's "wake-up completed?" decision: write ACTIVE = 1 alone, wait tWU (2.5 ms, p10), read
   STATUS. Done when ACTIVE = 1 and UVM = 0. Otherwise wait to the next 10 ms poll and write ACTIVE again. The
   masks, pair modes and channels are written only after the wake, so a write the chip refuses while UVM is set
   ("must be cleared ... to enable operations", p27) costs only a retry. UVM clears once VM is above the UVLO
   threshold (p22), so how long this takes depends on the supply ramp, not on tWU: `Config::wake_timeout_us`
   (100 ms by default, up to 10 s) bounds it.
4. At `Config::wake_timeout_us` with UVM still set: `Undervoltage`. With UVM clear and ACTIVE still 0: `Timeout`.
   In both cases the driver stays uninitialised, every channel off, and ENABLE is left high: ENABLE low disables the
   device (p14), and the next start would wait tEN and the wake again from the beginning. The application decides:
   retry `EnsureInitialized()` (it costs one more timeout), report a supply fault, or cycle VM, the only reset the
   chip has short of a POR (p22).
5. COMER is armed after the wake: a write rejected during the wake may set it, and nothing says a STATUS read clears
   it (p36: "Read Only"), so the driver checks it only for the writes that follow.
6. The masks, every CFG_CH, CFG_DPM and the ONCH state are written; STATUS is read back.
7. FAULT is read with every SDI bit high, which clears every flag on both parts ("Clearing the FAULT Register",
   p28-29). The logic registers keep their content until a POR (p22), and ENABLE low is not a reset (p14), so faults
   from before the start (a host reset with the chip powered) would hold the FAULT pin and look new. They are
   reported by `GetStartupFaults()` and in `GetLatchedFaults()`, and logged as a warning.
8. Every CFG_CH and CFG_DPM is read back.
9. A COMER raised by those writes (`CommunicationError`): ENABLE low for tDIS, high for tEN, and the whole procedure
   again, up to three attempts. If every attempt fails, **cycle VM**.

Transfers: 48 when the chip wakes at the first poll, 4 more for each extra poll (a poke is 2, a STATUS read 2).

## Datasheet rules the driver applies

Each has a test.

1. **CDR currents divide by 2^7 = 128** (Table 4): IHIT = HIT / 2^7 x IFS. `CurrentCode()` and `CurrentUa()` use 128.
   CFG_DPM's ISTART and IPTH divide by 127; `DpmCurrentCode()` uses that.
2. **Code 127 is not a current.** Table 4: code 127 is "HS switch off, LS switch on", the coil at full VM with no
   regulation. The driver never sends 127 in CDR (`kMaxCdrCode` = 126), and `CurrentCode()` refuses a current that
   would round to it.
3. **HIT times are exact.** Table 5: tHIT = HIT_T x 40 / fCHOP, with fCHOP = fMAIN / 4, 3, 2 or 1 (Table 1).
   `HitTimeUs()` and `HitTimeCode()` compute with 40 x divider / fMAIN, so 33.3 kHz and 26.7 kHz are not rounded.
4. **HIT_T = 255 is "continuous IHIT"** (Table 5), never the result of a rounding: `HitTimeCode()` refuses a time
   above 254 steps, and 255 is asked for by name (`kHitTimeContinuous`). `HitTimeUs()` refuses 255: it has no time.
5. **COMER is one bit of the fault byte.** The flow chart's "STATUS<7:0> = 0x04" is the COMER bit (Figure 6); at
   power-up UVM is set too (0x06), so the driver tests the bit, never the byte.
6. **The selective FAULT clear's byte order.** Figure 8 draws the SDI bit that clears OCP[4] in the fourth byte,
   the position of FAULT[31:24] in an LSB-byte-first transfer, as for a write (Figure 10). The driver sends the mask
   that way. This is a reading of a figure, **not verified on hardware**; the order is one constant,
   `reg::kFaultClearLsbByteFirst`, with tests for both, and bench step 5 decides. An all-ones clear works in either
   order.
7. **The two parts clear FAULT differently and cannot be told apart over SPI** ("Clearing the FAULT Register",
   Ordering Information). The default is `Variant::Max22200`, correct on both parts: it never asks for a selective
   clear, and `ClearFaults()` sends all ones. Declare `Variant::Max22200A` only when the bill of materials names the A
   part and bench step 5 shows a zero-SDI read leaves a latched flag set; then `ReadFaults()` keeps the flags. A
   selective clear on the MAX22200, which cannot do one, is `Unsupported`.
8. **0 % and 100 % duty are reachable in VDR.** Table 3 makes code 0 a 0 % duty and 100 a 100 % duty, and the chip
   clamps the codes in between to δMIN..δMAX by itself (Table 2). The driver sends the code as asked.
9. **SRC only below 50 kHz** ("SRC mode is only available ... for switching frequencies less than 50kHz"; Table 2 has
   no SRC row at 50 kHz). `ValidateChannel()` refuses SRC at 50 kHz and above.
10. **What the high side and VDR cannot do.** With VDRnCDR = 0 the chip ignores HSnLS, and SRC, HFS and DPM are low
    side only ("Low-Side/High-Side Operations"); the HIT check is CDR only. `ValidateChannel()` refuses each.
11. **No configuration before the wake.** Figure 6 loops until the wake-up has completed, and step 4 requires UVM
    cleared; the driver returns `Undervoltage` or `Timeout` instead of going on ([Start-up](#start-up-and-wake)).
12. **Mode and pair changes only with the channels off.** VDRnCDR and HSnLS change only with every channel off (step
    5c), CMxy only with both channels of the pair off (Register Details); the chip silently keeps the old value. The
    driver refuses them (`ChannelsActive`) and reports an ignored one with `Verify::ReadBack`.
13. **A parallel pair switches with both ONCH bits.** Table 6: SPI drives a parallel pair with "both ONCHx and ONCHy
    set logic high". The driver switches both, and `SetBridge()` refuses a pair that is not a full bridge.
14. **Shutdown reports its writes.** `Deinitialize()` reports the STATUS write that switches the channels off and
    clears ACTIVE, and still drives ENABLE low.
15. **DPM settings out of range are refused** (an IPTH above 15), never clamped: `DpmCurrentCode()` and `SetDpm()`.
16. **CFG_DPM is compared on bits 14:0 only.** Bits 31:15 are reserved, three of them read only with no reset value
    (p40-41); a full compare would fail on a real chip.
17. **No read hides a flag.** A STATUS read clears OVT and UVM ("Read Clears All", Register Details) and a FAULT read
    on the MAX22200 clears every flag, whoever makes the read. Every such read, the driver's own read-backs included,
    feeds the latch of `GetLatchedFaults()`.
18. **The SPI requirements are values, not prose:** mode 0, MSB first, tCLK, tCSPW, tCSS, tCSH and tCMS are
    `constexpr` and the ports check them with `static_assert` ([Porting](porting.md#bus-requirements)).
19. **Load diagnostics only on declared loads.** Open-load, plunger and HIT-current detection report a missing load as
    a fault (OLF, DPM, HHF) and pull the shared FAULT pin. `Config::loaded_channels` declares the loads, and a
    diagnostic on any other channel is refused (`InvalidArgument`).

## Where the datasheet contradicts itself or is silent

- **Which CFG regulates a full bridge.** Table 7 takes Forward from CFG_CHy and Reverse from CFG_CHx; the CMxy
  description says "PWM regulation control on LS according to" the lower channel's CFG. The driver writes what you
  configure for both channels; bench step 6 below decides.
- **Parallel pair ONCH.** Table 6 says both ONCH bits; the CMxy description names only the lower channel's. Setting
  both satisfies either reading.
- **What clears COMER.** STATUS marks COMER "Read Only" (p36), unlike OVT and UVM ("Read Clears All"); nothing says
  a STATUS read clears it. The driver assumes the worst: it records COMER from the first STATUS read of each start-up
  attempt and, if it is set when the wake ends, clears it with ENABLE low (or, if it stays, relies on the read-back of
  every register); only a 0 to 1 transition repeats the
  procedure, and between attempts ENABLE goes low for tDIS and high for tEN. On a running driver the first call that
  sees COMER go from 0 to 1 in any fault byte returns `CommunicationError`, and the writes since the last fault byte
  with COMER clear are in doubt (their ONCH bits count as on, `ApplyConfig()` writes their registers again). A COMER
  that stays set would hide every later miscount: the start-up clears one the wake left with ENABLE low (bounded
  tries; the model clears it so, a test covers a chip where only a power-on reset does), and while it is set on a
  running driver every switch-off is read back (2 more transfers each), so a miscounted switch-off is reported as
  `CommunicationError` or `WriteMismatch` and the copy takes what the chip holds. A miscounted switch-on or
  configuration write in that state is not seen without `Verify::ReadBack`. The model keeps COMER until ENABLE falls
  or a power-on reset, and a test covers the reading where a STATUS read clears it. Bench step 4 decides.
- **Which write a COMER blames.** The fault byte of an access shows the chip before that access's data (Figures 10
  and 12), so a miscount shows from the next access on: the call that reports it may not be the one whose write was
  ignored. The driver cannot tell which of the writes since the last clean fault byte it was, and doubts them all.
- **A power-on reset between two accesses.** The driver sees a reset from the next fault byte with ACTIVE = 0; ACTIVE
  is never written 0 while it runs. Until then it cannot know, and a reset chip drives nothing (ACTIVE = 0, p26-27).
  A one-transfer switch to a reset chip gets no fault byte back (the chip forgot the command: SDO floats in the model);
  the driver then sends the command, whose fault byte shows the reset. A reset between a read's command byte and its
  data: if the data comes back all ones (SDO floating, as in the model), one more command byte tells a reset from a
  value; if a real chip clocks out something else there, that one read returns it as data and the next call reports
  the reset. What a real chip clocks out for a data phase with no command after a reset is not given; bench step 8
  checks it.
- **No CRC on SDO.** The chip protects nothing it sends, so a persistently floating SDO (every fault byte 0xFF, no
  answer) goes unnoticed by an application that only writes: no write can see it. A periodic `ReadFaults()` shows it:
  every flag reads set (and on a part declared `Variant::Max22200`, whose FAULT read clears, `GetLatchedFaults().lost`
  is set too).
- **Flags that come and go between two accesses.** The latch keeps the summary flags of every fault byte, so an OLF
  that a switch-on clears or an HHF that a new cycle clears is kept once any access has clocked it out. A flag that is
  set and cleared with no access in between is not seen by any SPI read; the FAULT pin is the only witness.
- **CFG_DPM's reserved bits.** Bits 31:15 "must be set at zero" (p29), but p40-41 mark bits 20, 19 and 17 read only
  and give no reset value for most of them, nor for DPM_ISTART, DPM_TDEB and DPM_IPTH. The driver writes 0 there and
  compares only bits 14:0; the model reads the read-only bits as 1. `Config{}.dpm` is 0 by choice, not a known
  reset state.
- **The command after ENABLE low.** Not stated. The driver sends the command again after any ENABLE change, so the
  one-transfer ONCH write never depends on it.
- **Whether HFS scales the DPM currents.** Not stated; `DpmCurrentCode()` uses the full-scale IFS.
- **Whether an 8-bit STATUS read clears OVT and UVM.** "Read Clears All" is stated for the register; an 8-bit read
  returns only STATUS[31:24]. The driver assumes it clears nothing, and cannot latch flags it does not receive; the
  model clears them on 32-bit reads only. Bench step 10 decides.
- **SPI clock.** The SPI rows of the Electrical Characteristics give tCLK 100 ns (10 MHz), and "SPI Detailed
  Description" says the interface "can operate up to 5MHz" next to the daisy chain. `kMaxClockHz` is 10 MHz for one
  chip; the SDO pull-up's RC can set a lower limit (note 2). Bench step 1 runs at the board's clock.
- **The fault masks' position.** "Protection Circuits" says STATUS[15:8]; Table 10 and the Register Map put them at
  STATUS[23:16], which the driver uses. Thermal shutdown is said to set "one flag bit in the Fault register"; OVT is
  in STATUS.
- **An open-load flag on a channel that is switched on.** Open-load faults clear "if the corresponding output is
  turned on": the detection works while the output is off. A self-test that switches a channel on and then expects
  OLF may see it cleared.

## What the driver cannot see

- **A write the chip acknowledges and discards** (a glitch on a shared bus) leaves the chip different from the
  driver's copy. The start-up reads every register back (`WriteMismatch`); on a running driver only
  `Verify::ReadBack` proves a write landed. After a read-back that differs, the driver's copy takes the value read
  (ONCH, CFG_CH, HOLD, STATUS, CFG_DPM), and the bits that differ stay untrusted: a channel whose CFG_CH write
  failed or read back different accepts no write while a channel is on.
- **A lost ONCH write locks the drive mode.** A lost switch leaves a channel on that the driver thinks is off; a
  later drive-mode change then keeps the old mode with the new codes (step 5c), as a TRIG pin held high does (a CDR
  code 126 under VDR is a 100 % duty). Where it matters, use `Verify::ReadBack` on switches and mode changes, or
  change modes only right after `EnsureInitialized()`.

## What the model cannot show

The model is written from the datasheet, by the same reading as the driver. It cannot show:

- the SDI byte order of the selective clear (rule 6) and the command echo of a 32-bit write, which the start-up
  check relies on (Figure 10);
- what clears COMER (a STATUS read, ENABLE low, or only a power-on reset), and whether the command in force survives
  ENABLE low;
- how long a real supply takes to clear UVM, and whether the chip takes ACTIVE before it has (the tests run both
  readings: `active_needs_uvm_clear`);
- what SDO carries for a data byte sent to a chip that lost its command in a reset;
- the analog side: currents, ripple, OCP and open-load thresholds, DPM detection on a real valve, thermal limits;
- electrical timing: tCMS, tCMH (no value given), the SDO rise time through the pull-up at your clock.

## Before you depend on it

Run these on your board:

1. Board powered: `EnsureInitialized()` returns `Ok` in 48 transfers or more (4 per extra wake poll; logic analyser
   on CSB, CMD high in the command bytes and back high after every data phase), ENABLE high, 3 ms or more after
   ENABLE rose. Power VM up slowly or late: the wake loop re-pokes ACTIVE until it reads 1; with VM held under the
   UVM threshold past `Config::wake_timeout_us`: `Undervoltage`, every channel off. Measure how long the wake takes
   on your supply and set `Config::wake_timeout_us` with a margin.
2. VM off: `NoDevice`. SDI disconnected: `DeviceIdMismatch`. A 32-bit write's second SDO byte equals its command.
3. `SetChannelsOn(0x01, Verify::ReadBack)` and `SetChannelsOn(0x00, Verify::ReadBack)` 1,000 times each: `Ok`, the
   load follows. Without read-back, alternate `SetChannelsOn(0x01)` and `SetChannelsOn(0x00)`: 2 transfers for
   the first, then 1 each (one CSB low period, CMD low during it and high after), and the load still follows.
4. Write STATUS with a 3-byte transfer by hand (a miscounted write), then: the next fault byte shows COMER; note
   whether a STATUS read clears it, whether ENABLE low clears it, and that a start-up after it passes.
5. Raise an OCP on channel 4 (short OUT4 through a fuse-protected load) and a DPM fault on channel 4, then
   `ClearFaults(Faults{.overcurrent = 0x10})`: OCP[4] clears, DPM[4] stays (rule 6). On a MAX22200 the same read
   clears both. Also on a part declared `Max22200A`: a `ReadFaults()` (SDI all zero) must leave OCP[4] set.
6. A latching valve on a full-bridge pair: Forward and Reverse each regulated as configured, to settle the Table 7
   question; scope OUTx and OUTy.
7. CDR at RREF = 15 kΩ: measure the peak current at codes 32, 64 and 126 (expect 250, 500 and 984 mA within the
   accuracy table); a HIT time of code 100 at 33.3 kHz measures 120 ms.
8. Power-cycle VM while running (or pull VM below the POR level for a moment) with channel 0 on, then call
   `SetFaultMask(GetConfig().fault_mask)`: it returns `DeviceReset`, the logic analyser shows the command byte and no
   data phase after it, and the output stays off (ACTIVE = 0). Repeat with `SetChannelsOn()` twice before the dip, so
   the call after it is the one-transfer switch: record the byte SDO returns for it, and check that the driver sends
   the command and returns `DeviceReset`. Then `EnsureInitialized()` restores every channel's configuration with every
   channel off. Note whether the chip takes an ACTIVE write while UVM is still set.
9. Faults from before the start: with a load declared on channel 0 (`Config::loaded_channels`) and `open_load_detect`
   on, start, disconnect the load, wait for OLF, then call `Deinitialize()` and `EnsureInitialized()` again without
   cycling VM: the FAULT pin is released after the start, and `GetStartupFaults().open_load` has bit 0.
10. OVT and UVM in the latch: dip VM below UVLO for a moment (without a power-on reset), then call
    `SetFaultMask(mask, Verify::ReadBack)`: `GetLatchedFaults().undervoltage` is set. Note whether an 8-bit read
    (`ReadChannelsOn()`) after a second dip leaves UVM set for the next `ReadStatus()`.
11. A flag the chip clears by itself: with `open_load_detect` on channel 2 and its load disconnected while it is off,
    wait for OLF, then `SetChannel(2, true)` and `ReadFaults()`: FAULT no longer shows OLF[2] (p21), and
    `GetLatchedFaults().open_load` is set.
12. A write whose reply fails: pull SDO low through a resistor during the data phase of `SetPairMode(0,
    PairMode::FullBridge)` only if the port can report the failure; otherwise interrupt the port's transfer after the
    data has gone out. `SetChannel(0, true)` then returns `Uncertain` with no CSB activity, and `SetChannel(1, false)`
    switches both channels of pair 0 off.
11. A FAULT read with SDI all zero on the fitted part: note whether it clears a latched OLF (a MAX22200 does, a
    MAX22200A does not), and declare `Config::variant` from the result and the bill of materials.
