# Drive modes

How the MAX22200 drives a load, and how each `ChannelConfig` field maps to it: HIT and HOLD, current or voltage
regulation, the chopping frequency, the HIT time, the side, pairs and bridges, and plunger-movement detection.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - Full-scale current constant KFS and the CDR accuracy: Electrical Characteristics (KFS0, KFS1, ITRERR), p. 9
    - The HIT and HOLD sequence, and the TRIG and SPI triggers: Functional Description - Two Levels Drive Sequence for Solenoid Valves; Figures 2 and 3, p. 14
    - Oscillator and per-channel chopping frequency: Chopping Frequency (FREQM, FRQ_CFG_); Table 1, p. 16
    - Slew-rate control and the duty-cycle limits: Slew-Rate Controlled Mode (SRC); Min and Max Duty cycle, Table 2, p. 17
    - VDR duty-cycle codes: Voltage Drive Regulation (VDR); Table 3, p. 17
    - CDR current codes, IFS from RREF and the half full-scale bit: Current Drive Regulation (CDR); Table 4; Full-Scale Current Setting; Half Full-Scale Setting, p. 18
    - HIT excitation time: HIT Excitation Time (tHIT); Table 5, p. 19
    - High side, parallel pairs and full bridges: Supported Driver Configurations; Tables 6, 7 and 8, p. 20
    - Plunger-movement detection, thermal shutdown, UVLO and the HIT check: Detection of Plunger Movement (DPM); Thermal Shutdown (TSD); Undervoltage Lockout (UVLO); HIT Current Not Reached Flag (HHF), p. 22
    - CFG_CHn: Configuration Register Description (CFG_CH_); Table 11; Register Details, CFG_CH, p. 27
    - CFG_DPM: Detection of Plunger Movement Register Description (CFG_DPM); Figure 9, p. 29

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## HIT, then HOLD

When a channel switches on (ONCH set, or its TRIG pin high with `trigger_pin`), the chip drives the HIT level for
the HIT time, then the HOLD level until the channel switches off (Figures 2 and 3). HIT pulls the plunger in; HOLD,
normally much lower, keeps it there with less heat. The HOLD time is not programmable: it lasts as long as the
channel is on.

| Field | Register bits | Meaning |
| --- | --- | --- |
| `hit` | HIT[6:0] | CDR: peak current code; VDR: duty cycle in percent |
| `hold` | HOLD[6:0] | The same, for the HOLD phase. Can change on the fly: `SetHoldCode()` |
| `hit_time` | HIT_T[7:0] | 0 = no HIT phase, 1 to 254 = steps of 40 chopping periods, 255 = HIT forever |

## Current drive (CDR)

`DriveMode::Cdr`, the reset value, regulates the peak current cycle by cycle with the chip's lossless current sense.
Low side only. IFS comes from RREF (`Config::rref_ohm`):

| Code | Peak current (Table 4) | The driver |
| --- | --- | --- |
| 0 | "HS switch on, LS switch off": no current | Accepted: off |
| 1 to 126 | code / 128 x IFS | `CurrentCode()` converts milliamps; `CurrentUa()` converts back |
| 127 | "HS switch off, LS switch on": the coil at full VM, **no regulation** | Refused everywhere (`ValidateChannel()`, `SetHoldCode()`) |

```cpp
--8<-- "tests/docs/units.cpp:currents"
```

`half_scale` (HFS) halves IFS and doubles the on-resistance, for better accuracy below 0.5 A; pass the channel's
HFS to `CurrentCode()`. The loop regulates the peak; the average current is lower by half the ripple, which falls as
the chopping frequency rises ("Fixed-Frequency Current Control").

`hit_current_check` (HHF_EN) sets the channel's HHF flag when the HIT current is not reached by the end of the HIT
time; CDR only.

## Voltage drive (VDR)

`DriveMode::Vdr` outputs a PWM voltage whose duty cycle is the code, in percent (Table 3). The current follows from
VM and the coil's resistance, so it drifts with both. Low or high side.

| Code | Duty cycle |
| --- | --- |
| 0 | 0 % |
| 1 to δMIN - 1 | δMIN (the chip raises it) |
| δMIN to δMAX | the code, in 1 % steps |
| δMAX + 1 to 99 | δMAX (the chip lowers it) |
| 100 | 100 % |

δMIN and δMAX are 4 % and 96 %, 8 % and 92 % at fMAIN (`ChopDivider::Div1`), 7 % and 93 % with SRC (Table 2,
`DutyLimitsOf()`). The chip clamps by itself, so the driver passes the code as it is: 0 % and 100 % stay
reachable. Codes 101 to 127 mean 100 % as well; the driver refuses them so that every duty has one code.

## Chopping frequency

The oscillator runs at 100 kHz or 80 kHz (`Config::clock`, STATUS.FREQM, global); each channel divides it
(`chop`, FREQ_CFG, Table 1):

| `chop` | 100 kHz | 80 kHz | Period |
| --- | --- | --- | --- |
| `Div4` (reset) | 25 kHz | 20 kHz | 40 µs / 50 µs |
| `Div3` | 33.3 kHz | 26.7 kHz | 30 µs / 37.5 µs |
| `Div2` | 50 kHz | 40 kHz | 20 µs / 25 µs |
| `Div1` | 100 kHz | 80 kHz | 10 µs / 12.5 µs |

`ChopPeriodNs()` gives the period exactly. A higher frequency lowers the ripple and raises the switching losses.
`slew_rate_control` (SRC) slows the edges to about 200 ns for lower emissions, doubles the OCP deglitch time, and is
allowed only on the low side below 50 kHz: `Div4` or `Div3` at 100 kHz, `Div4` to `Div2` at 80 kHz
("Slew-Rate Controlled Mode (SRC)"; `ValidateChannel()` enforces it). The chopping frequency and SRC can change while
the channel runs.

## HIT time

tHIT = HIT_T x 40 / fCHOP (Table 5), so a step is 40 chopping periods: 1.6 ms at 25 kHz, 2 ms at 20 kHz (the
datasheet's example: 0 to 508 ms).

```cpp
--8<-- "tests/docs/units.cpp:hit-time"
```

`HitTimeCode()` never returns 255: a time beyond 254 steps is refused, because 255 is a HIT phase that never ends,
which a rounding must not produce. Ask for it with `kHitTimeContinuous`. A non-zero time below half a step becomes
1, never 0 ("no HIT").

## Low side and high side

`Side::Low` (the reset value) is a load from OUTn to VM; `Side::High` a load from OUTn to ground. The high side runs
VDR only and has no SRC, HFS or DPM ("Low-Side/High-Side Operations (HSnLS)"); `ValidateChannel()` refuses each.
With VDRnCDR = 0 the chip ignores HSnLS altogether, so CDR on the high side is refused too.

## Pairs: parallel and full bridge

Channels 0-1, 2-3, 4-5 and 6-7 form pairs (`Config::pairs`, STATUS.CMxy). The chip takes a pair mode only while
both channels of the pair are off; the driver returns `ChannelsActive` otherwise.

**Parallel** (`PairMode::Parallel`): OUTx and OUTy tied together for twice the current; the lower channel's CFG_CH
drives both, the upper one's is ignored. Table 6 says SPI drives the pair "both ONCHx and ONCHy set logic high";
the register description names only the lower ONCH. The driver switches both bits together, which satisfies both
readings: `SetChannel()` on either channel switches the pair, and `SetChannelsOn()` refuses a mask that splits it.

**Full bridge** (`PairMode::FullBridge`): an H-bridge for a latching valve or a brushed DC motor. ONCHy and ONCHx
choose the state (Table 7), set with `SetBridge()`:

| `BridgeState` | ONCHy, ONCHx | OUTx | OUTy |
| --- | --- | --- | --- |
| `HiZ` | 0, 0 | Hi-Z | Hi-Z |
| `Forward` | 0, 1 | VM | regulated with CFG_CHy |
| `Reverse` | 1, 0 | regulated with CFG_CHx | VM |
| `Brake` | 1, 1 | ground | ground |

HSnLS is ignored in a bridge. The register description of CMxy says "PWM regulation control on LS according to"
the lower channel's CFG, while Table 7 takes Forward from CFG_CHy and Reverse from CFG_CHx; the driver writes
whatever you configure for each channel and follows Table 7 in its documentation ([Status and
limitations](limitations.md)). In both modes the TRGnSPI bits of the two channels together decide SPI or TRIG
control (Tables 6 and 8).

## Plunger-movement detection (DPM)

A moving plunger makes the current dip during HIT (back EMF); a stuck one does not. With `plunger_detect` (DPM_EN,
low side) the chip watches the current from ISTART until IHIT or the end of tHIT, and sets the channel's DPM flag
when no dip deeper than the threshold and longer than the debounce time came (Figures 4 and 9). The settings are
global (`Config::dpm`, CFG_DPM, `SetDpm()`):

| Field | Code | Value |
| --- | --- | --- |
| `start` | DPM_ISTART, 0 to 127 | code x IFS / 127 (`DpmCurrentCode(ma, rref, 127)`) |
| `threshold` | DPM_IPTH, 0 to 15 | code x IFS / 127 (`DpmCurrentCode(ma, rref, 15)`) |
| `debounce` | DPM_TDEB, 0 to 15 | code chopping periods (`DebounceUs()`) |

The DPM currents divide by 127, the CDR currents by 128 (the datasheet uses both). Set ISTART just below the
current at which the dip is expected; the current above ISTART must rise slower than 700 mA/ms for the detection to
work. The datasheet does not say whether HFS scales the DPM currents; `DpmCurrentCode()` uses the full-scale IFS.

## Diagnostics per channel

| Field | Flag | When |
| --- | --- | --- |
| `open_load_detect` (OL_EN) | OLF | A disconnected load, sensed while the channel is off (below 2 V low side, above VM - 2 V high side). Clears when the channel switches on or FAULT is read |
| `hit_current_check` (HHF_EN) | HHF | CDR: IHIT not reached by the end of tHIT. Clears on a FAULT read or a new cycle |
| `plunger_detect` (DPM_EN) | DPM | No valid dip during HIT |
| always on | OCP | Over 1.1 A: the channel is switched off. It stays off until it is switched off and on again |

Read them with `ReadFaults()` and `ClearFaults()` ([API guide](api-guide.md#read-and-clear-faults)).
