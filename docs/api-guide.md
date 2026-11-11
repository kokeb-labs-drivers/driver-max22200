# API guide

Every task the driver supports, with the calls that do it. The member list is in the
[API reference](api-reference.md); what each call promises is on the [Contract](contract.md) page.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - ENABLE and the 0.5 ms to SPI ready: Enable Logic Input, p. 14
    - The HIT and HOLD sequence, and the TRIG and SPI triggers: Functional Description - Two Levels Drive Sequence for Solenoid Valves; Figures 2 and 3, p. 14
    - High side, parallel pairs and full bridges: Supported Driver Configurations; Tables 6, 7 and 8, p. 20
    - Overcurrent and open-load protection: Protection Circuits; Overcurrent Protection (OCP); Open-Load Detection (OL), p. 21
    - Plunger-movement detection, thermal shutdown, UVLO and the HIT check: Detection of Plunger Movement (DPM); Thermal Shutdown (TSD); Undervoltage Lockout (UVLO); HIT Current Not Reached Flag (HHF), p. 22
    - The programming procedure: Programming Procedure; Figure 6, p. 23
    - STATUS: Status Register Description (STATUS); Table 10; Register Details, STATUS (0x00), p. 26
    - FAULT and its two clear-on-read behaviours: Fault Register Description (FAULT); Table 12; Clearing the FAULT Register; Figure 8, p. 28

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The shape of the API

- One object per chip: `max22200::MAX22200<YourPort> chip(port, config);`. The constructor stores the
  configuration and does no I/O.
- `EnsureInitialized()` is the only call that starts the chip. Every other call that touches the chip returns
  `Error::NotInitialized` until it has succeeded.
- Every fallible call returns `Result<T>`: check `ok()` (or the `bool` conversion) before reading `value`; on
  failure `value` is `T{}` and `error` says why.
- A call sends only what the datasheet needs. Calls that can read back take a trailing `Verify verify =
  Verify::None`; `Verify::ReadBack` reads the register back and compares it, and its cost is stated per call.
- The driver keeps a copy of every register it wrote. `GetConfig()` and `GetChannelsOn()` cost nothing.

| Call | Transfers | With `Verify::ReadBack` |
| --- | --- | --- |
| `SetChannelsOn()`, `SetChannel()`, `SetBridge()` | 1 right after an ONCH write with nothing in between, else 2 | 3 or 4 |
| `SetHoldCode()` | 2 | 4 |
| `ConfigureChannel()`, `SetPairMode()`, `SetFaultMask()`, `SetDpm()` | 2 | 4 |
| `ReadStatus()`, `ReadFaults()`, `ClearFaults()`, `ReadChannelConfig()`, `ReadChannelsOn()` | 2 | |
| `EnsureInitialized()` | 42 (+2 per extra wake-up poll) | |
| `ApplyConfig()` | 2 per register that changes | |
| `Deinitialize()` | 2 | |

A transfer is one CSB-low period: the command byte, or the 1 or 4 data bytes after it.

## Start

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

`EnsureInitialized()` drives CMD high (its idle level) and ENABLE high, waits tEN (0.5 ms), then runs the programming
procedure of Figure 6: it reads STATUS (which clears UVM), then wakes the chip ("wake-up completed?"): a bare ACTIVE = 1
write, tWU (2.5 ms), a STATUS read, repeated every `kWakePollUs` (10 ms) until ACTIVE reads 1 with UVM clear, for at
most `Config::wake_timeout_us` (100 ms by default). Then it writes STATUS (fault masks, FREQM, pair modes, every channel
off, ACTIVE = 1), CFG_CH0 to CFG_CH7 and CFG_DPM. Then it checks what it wrote: STATUS must hold it and every 32-bit
write must have echoed its command byte (Figure 10). Next it reads FAULT with every SDI bit high, which clears every
flag on both parts: the chip may keep FAULT across an ENABLE cycle (the datasheet does not say ENABLE low clears it), so
flags from before the start would hold the FAULT pin and look new. They are reported by `GetStartupFaults()`. Last,
every configuration register read back must equal what was written (CFG_DPM: bits 14:0; bits 31:15 are reserved, some
read only with no reset value). COMER is read only and the datasheet does not say what clears it, and a wake write the
chip rejected can set it: if it is set when the wake ends, ENABLE goes low for tDIS and high for tEN and STATUS is read
again, `kStartAttempts` times at most (if it stays set, the attempt relies on the read-back and the session reads every
switch-off back); a COMER that appears later repeats the procedure, after ENABLE low for tDIS and high again for tEN, up
to `kStartAttempts` times; if every attempt fails, cycle VM. At the wake timeout it returns `Undervoltage` (UVM still
set) or `Timeout` (UVM clear, ACTIVE still 0), uninitialised, every channel off, ENABLE left high so a retry continues
the wake. On any other failure it drives ENABLE low again. It takes 48 transfers when the chip wakes at the first poll,
4 more per extra poll.

| Error | Meaning |
| --- | --- |
| `InvalidArgument` | The `Config` fails `Validate()`; nothing was sent |
| `Bus`, `Pin` | The port failed a transfer or a pin write |
| `NoDevice` | SDO read 0xFF throughout: nothing drives it |
| `DeviceIdMismatch` | STATUS read back differs, or a write did not echo its command: SDI, CMD or CSB broken |
| `WriteMismatch` | A configuration register did not take its write |
| `Undervoltage` | UVM still set at `Config::wake_timeout_us`: VM below UVLO, slow, or UVM stuck (cycle VM) |
| `Timeout` | ACTIVE never read 1 within `Config::wake_timeout_us`, UVM clear: cycle VM |
| `CommunicationError` | A new COMER in every attempt: the chip miscounts the clocks |

## Switch channels

```cpp
--8<-- "tests/docs/api_tasks.cpp:switching"
```

`SetChannelsOn(mask)` writes ONCH, STATUS[31:24], in one 8-bit write (Programming Procedure, step 5a).
`SetChannel(channel, on)` computes the new mask from the driver's copy and sends the same single write: no STATUS
read first. `GetChannelsOn()` returns the copy. A channel with `trigger_pin` follows its TRIG pin instead.

The Command Register stays in force until it is rewritten (p25), so a switch right after a switch sends only its data
byte: **1 transfer**. The first switch after anything else sends the command again (2 transfers): after any other
register access, a failed transfer or pin write, an ENABLE change (`Deinitialize()`, a restart), a COMER in a fault
byte, or a `Verify::ReadBack` (its read changes the command). The data byte's SDO carries STATUS[7:0] (Figure 12),
kept in `GetLastStatusByte()`: watching the fault flags costs nothing. If that byte is 0xFF (no answer: a chip that
lost the command in a reset clocks nothing out), the write goes out again with its command, so the command byte shows
whether the chip was reset. A miscounted write shows as COMER from the next byte on: the call that sees COMER go from
0 to 1 returns `CommunicationError` (see [What every call checks](#what-every-call-checks)); only `Verify::ReadBack`
proves that a given switch landed.

With `Verify::ReadBack` the driver reads ONCH back (8 bits) and compares it: `WriteMismatch` when the chip did not take
it (the copy then takes the value read, and the channels that differ are not trusted for the `ChannelsActive` check),
`CommunicationError` when the read's fault byte shows COMER (the write had the wrong clock count). The copy changes only
when the write went out; after `Bus` the channels the write would have changed count as on for the `ChannelsActive`
check until a write succeeds. `ReadChannelsOn()` reads ONCH from the chip without changing the copy. A switch-on in a
pair whose mode a failed write left unknown returns `Uncertain` and sends nothing (see [A write whose reply
failed](#a-write-whose-reply-failed)). A switch-off is never refused: in such a pair it switches both channels off, the
partner too, because ONCHx = ONCHy = 0 is off in every mode (Tables 6 and 7) and one bit alone could turn a full-bridge
Brake into Forward or Reverse. `SetBridge(pair, BridgeState::HiZ)` does the same. A `SetChannelsOn()` mask that changes
such a pair switches it off; if it asked for a switch-on there, the rest of the mask, every switch-off in it, still goes
out, then the call returns `Uncertain`. The pair goes off rather than keep the copy's bits: after a miscount the copy
may show a channel on that the chip has off. A channel that may still be on (a failed write may have switched it on, or
no clean fault byte has vouched for its switch-off yet) is always switched off when the mask asks it off, even when the
copy already says off. In a full-bridge pair `SetChannel(ch, false)` sets HiZ (both bits 0, Table 7): the partner goes
off too.

In a parallel pair both ONCH bits move together; in a full-bridge pair use `SetBridge(pair, state)` (Table 7):

| `BridgeState` | OUTx | OUTy |
| --- | --- | --- |
| `HiZ` | Hi-Z | Hi-Z |
| `Forward` | VM | regulated with CFG_CHy |
| `Reverse` | regulated with CFG_CHx | VM |
| `Brake` | ground | ground |

## Change a channel's settings

`ConfigureChannel(channel, config)` writes the whole CFG_CHn (32 bits). Everything but the drive mode and the side
can change while the channel runs (step 5c): the currents, the HIT time, the chopping frequency, SRC, the
diagnostics. A change of drive mode or side while any channel is on returns `ChannelsActive` and sends nothing.

```cpp
--8<-- "tests/docs/api_tasks.cpp:hold"
```

`SetHoldCode(channel, code)` writes only CFG_CHn[31:24], HFS and HOLD, in one 8-bit write (step 5b), for a HOLD
current controlled on the fly; HFS is kept. The code is checked against the channel's drive mode (`ValidCode()`).

`SetPairMode()`, `SetFaultMask()` and `SetDpm()` write STATUS or CFG_DPM; a pair mode changes only with both channels
of the pair off. `ApplyConfig()` writes every register of a new `Config` that differs from the current one.

## Read and clear faults

```cpp
--8<-- "tests/docs/api_tasks.cpp:faults"
```

`ReadStatus()` returns ONCH, the seven flags and ACTIVE. The read clears OVT and UVM on the chip (Register Details:
"Read Clears All"); the four channel flags (OCP, OLF, HHF, DPM) say only that some channel has one: the FAULT
register says which.
If ACTIVE reads 0 on a running driver, a power-on reset cleared every register: the driver marks itself
uninitialised and returns `DeviceReset`; call `EnsureInitialized()` to configure the chip again. Every other call
does the same from its command byte (see [What every call checks](#what-every-call-checks)).

`Config::variant` is `Variant::Max22200` by default, which is correct on both parts. Declare `Variant::Max22200A`
only when the bill of materials says that part is fitted: a selective clear sent to a MAX22200 clears every flag.

| Call | Declared MAX22200A | Declared MAX22200 (the default) |
| --- | --- | --- |
| `ReadFaults()` | Reads FAULT, clears nothing | Reads FAULT; a MAX22200 clears every flag (unavoidable), a MAX22200A none |
| `ClearFaults()` | Reads FAULT, clears every flag | The same |
| `ClearFaults(which)` | Clears only the flags in `which` (Figure 8) | `Unsupported`, nothing sent |

Both return the flags as they were before the clear. An OCP channel was switched off by the chip; after the flag is
cleared it resumes only once it is switched off in ONCH and on again ("Overcurrent Protection (OCP)"). OLF also
clears when the channel switches on, HHF when it starts a new cycle.

`ReadFaultPin()` reads the FAULT pin through the optional `GpioRead()` hook, with no SPI: use it in the deferred part
of a FAULT interrupt, then `ReadFaults()` for the details. `GetLastStatusByte()` returns STATUS[7:0] as the chip
clocked it out during the last command byte, for free.

### The latch of clear-on-read flags

A STATUS read clears OVT and UVM, and a FAULT read on the MAX22200 clears every flag, whoever makes the read: your
`ReadStatus()`, but also the read-back of `SetPairMode()` or `SetFaultMask()` with `Verify::ReadBack`, a raw
`ReadRegister()` or the start-up's own reads. So that no internal read hides a flag, the driver ORs what each read
took from the chip into one latch (handbook KB-CPP-119):

| Call | I/O | What it does |
| --- | --- | --- |
| `GetLatchedFaults()` | none | `LatchedFaults`: OVT and UVM seen by any 32-bit STATUS read, the summary flags of every fault byte, the FAULT flags any FAULT read cleared, and `lost` when a read that clears flags did not deliver them, since the last clear |
| `ClearLatchedFaults()` | none | Empties the latch; the chip is not touched |
| `GetStartupFaults()` | none | The FAULT flags the last start-up found and cleared |

Only `ClearLatchedFaults()` empties the latch; a restart does not, so the UVM of a `DeviceReset` survives the
`EnsureInitialized()` that follows. The start-up's STATUS reads feed it too, except for UVM before the wake has
completed (Figure 6 reads STATUS to clear the power-on UVM). The chip clears some flags by itself: OLF when the channel
switches on (p21), HHF when it starts a new cycle (p23). So that such a flag is not lost before a FAULT read sees it,
the summary flags (OVT, UVM, OCP, OLF, HHF, DPM) of every fault byte any access clocks out, the one-transfer switch
included, go into the latch as `overtemperature`, `undervoltage`, `overcurrent`, `open_load`, `hit_not_reached` and
`plunger`, at no extra I/O; the channel is known only if a FAULT read caught it in `faults`. A reply that a floating SDO
produces (all ones with a 0xFF fault byte) is not data and is never latched as flags; a STATUS read, or a FAULT read
that clears, that gets one or fails sets `lost`: what the chip held is unknown (a genuine FAULT of all ones with a 0xFF
fault byte reads the same way, and is treated the same). An 8-bit read (ONCH, HOLD) returns neither OVT nor UVM, and the
datasheet does not say it clears them: the driver assumes it does not. On a part declared `Variant::Max22200A`,
`ReadFaults()` clears nothing and latches nothing; `ClearFaults(which)` latches the flags in `which`.

### Raw register access

`ReadRegister(bank)` reads one register once: one 32-bit read, 2 transfers, no retry, no decoding and no check
(handbook KB-CPP-129), for bring-up and diagnostics. Its side effects are the chip's own: reading STATUS clears OVT
and UVM, reading FAULT clears every flag on the MAX22200; both go to the latch. `GetLastStatusByte()` holds the
fault byte of the last access.

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| Error | Returned by | What to do |
| --- | --- | --- |
| `NotInitialized` | Every I/O call before the start, after `Deinitialize()` or `DeviceReset` | Call `EnsureInitialized()` |
| `InvalidArgument` | Any call with an argument out of range; nothing was sent | Fix the argument |
| `Bus` | Any I/O call whose transfer failed | Retry; the chip may or may not hold the write |
| `Pin` | Any I/O call whose CMD or ENABLE write failed; `ReadFaultPin()` | Check the port's GPIO |
| `NoDevice`, `DeviceIdMismatch` | `EnsureInitialized()` | Check the wiring ([Troubleshooting](troubleshooting.md)) |
| `WriteMismatch` | `EnsureInitialized()`; setters with `Verify::ReadBack` | Retry the write; check the TRIG pins for a mode change |
| `CommunicationError` | `EnsureInitialized()`; any call on a running driver that sees COMER go from 0 to 1 | Check SCK and CSB; call `ApplyConfig()` or the setter again: a write may not have landed |
| `Undervoltage`, `Timeout` | `EnsureInitialized()` | Check VM; retry, raise `Config::wake_timeout_us`, or cycle VM |
| `ChannelsActive` | `ConfigureChannel()`, `SetPairMode()`, `ApplyConfig()` | Switch the channels off first |
| `DeviceReset` | Any I/O call on a running driver | Call `EnsureInitialized()`: the chip lost its configuration |
| `Uncertain` | A switch-on with `SetChannelsOn()`, `SetChannel()` or `SetBridge()` in a pair whose mode a failed write left unknown (a switch-off always goes out) | Write the pair mode again, or read it back with `Verify::ReadBack` |
| `Unsupported` | `ReadFaultPin()` without `GpioRead()`; `ClearFaults(which)` on the MAX22200 | Add the hook; clear all |

### What every call checks

Every command byte clocks out the fault byte STATUS[7:0] (Figure 7), and so does an 8-bit write's data byte
(Figure 12). On a running driver each one is checked at no extra I/O:

- **A power-on reset.** ACTIVE is only ever written 1 while running, so a fault byte with ACTIVE = 0 (other than
  0xFF, what a floating SDO reads) means a UVLO reset every register to its default: ACTIVE = 0, every channel off,
  every CFG_CHn in CDR on the low side ("Undervoltage Lockout (UVLO)", p22; p26, p38-39). The access stops before its
  data phase, so nothing from the driver's copy goes to the reset chip: no setter re-activates it with the old ONCH
  over reset channel settings. The driver marks itself uninitialised, every channel off, and returns `DeviceReset`.
  A STATUS value with ACTIVE = 0 does the same. An SDO stuck low reads as such a byte: the driver stops, the safe side.
- **A miscounted write.** COMER going from 0 to 1 (p34) means a write since the last fault byte with COMER clear was
  ignored. The call that sees it returns `CommunicationError`, once (a call that fails for another reason leaves it
  for the next). The ONCH bits those writes changed count as on for the `ChannelsActive` check, the channels they
  configured as of either drive mode, and `ApplyConfig()` writes their registers again. COMER is read only and may
  stay set (p36), which would hide every later miscount. So the start-up clears a COMER the wake left by taking
  ENABLE low for tDIS and high for tEN (the registers survive it, p14; 2 more transfers and the waits, only then),
  and while COMER is set on a running driver every switch-off is read back: 2 more transfers per switch-off, only in
  that state. A switch-off is never `Ok` without proof that it landed. A configuration write (CFG_CHn, HOLD, STATUS,
  CFG_DPM) whose command byte shows a new COMER stops before its data phase and returns `CommunicationError`: the
  channels in doubt may be on, and the chip would ignore a drive-mode change (5c). An ONCH write is never stopped.
  Doubt a write settles (a switch-off, a STATUS write) stays recorded until a clean fault byte or a read-back proves
  that write, so a miscount of that very write brings it back. While COMER stays set, `ConfigureChannel()` reads its
  write back too (2 more transfers), and the other configuration writes leave what they changed in doubt.
  `IsMiscountDetectionDegraded()` (no I/O) says when the driver is in that state.
- **All-ones read data.** It carries no fault byte. A reset between a read's command byte and its data leaves the
  chip without a command and SDO floating, so the driver sends one more command byte (an 8-bit ONCH read, 2
  transfers, only after all-ones data): its fault byte tells a reset from a value.
- **Flags the chip clears by itself** go to the latch (above).

### A write whose reply failed

A transfer that fails may still have reached the chip, so after `Bus` or `Pin` from a write the chip may hold the old
value or the new one. The driver keeps the old value in its copy (it is what the next write is computed from) and
treats each cached field so that no decision rests on a guess:

| Cached field | After a failed write | Settled by |
| --- | --- | --- |
| ONCH (`GetChannelsOn()`) | The copy keeps the old mask; the bits the write would have changed count as on for `ChannelsActive` | A successful ONCH or STATUS write |
| CMxy (pair modes) | A changed pair's mode is unknown: a switch-on in that pair returns `Uncertain` with no I/O, a switch-off clears both of its bits (only "both off" means the same in every mode, Tables 6 and 7), and a pair-mode change waits for the pair to be off | A STATUS write with the pair off, or a STATUS read-back |
| FREQM | Unknown: `ConfigureChannel()` accepts only a channel valid at both master clocks | A STATUS write or read-back |
| Fault masks | No decision depends on them; STATUS is written again by the next `ApplyConfig()` | A STATUS write |
| CFG_CHn (drive mode, side, HFS, HOLD) | The channel may run either mode: any write to it, the copy's own value included, waits for every channel to be off (only then does the chip take the mode bits, step 5c), HOLD is checked against both modes, and `ApplyConfig()` writes it again | A `ConfigureChannel()` or `ApplyConfig()` with every channel off |
| CFG_DPM | No decision depends on it; written again by the next `ApplyConfig()` | A successful `SetDpm()` |

The same handling applies to the writes a new COMER puts in doubt.

`Crc`, `Busy` and `DeviceFault` belong to the shared base set and are never returned: the chip sends no CRC, it is
never busy to SPI, and its faults are data, never a failed call.

## Shut down

```cpp
--8<-- "tests/docs/api_tasks.cpp:deinit"
```

`Deinitialize()` writes STATUS with every channel off and ACTIVE = 0 (outputs Hi-Z), then drives ENABLE low (sleep,
under 11 µA). The driver is uninitialised afterwards even if a write failed, and ENABLE goes low whatever happened.
