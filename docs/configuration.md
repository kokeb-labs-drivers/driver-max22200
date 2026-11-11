# Configuration

Every field of `max22200::Config` and `max22200::ChannelConfig`, how a configuration is checked, and the optional
Kconfig layer. What the settings do to the load is on [Drive modes](drive-modes.md).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Analog Devices 19-100531 1:

    - The two parts and how each clears the FAULT register: General Description; Ordering Information, p. 1
    - Oscillator and per-channel chopping frequency: Chopping Frequency (FREQM, FRQ_CFG_); Table 1, p. 16
    - VDR duty-cycle codes: Voltage Drive Regulation (VDR); Table 3, p. 17
    - CDR current codes, IFS from RREF and the half full-scale bit: Current Drive Regulation (CDR); Table 4; Full-Scale Current Setting; Half Full-Scale Setting, p. 18
    - STATUS: Status Register Description (STATUS); Table 10; Register Details, STATUS (0x00), p. 26
    - CFG_CHn: Configuration Register Description (CFG_CH_); Table 11; Register Details, CFG_CH, p. 27
    - CFG_DPM: Detection of Plunger Movement Register Description (CFG_DPM); Figure 9, p. 29
    - Register map and reset values: Register Map; Register Details, p. 35

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

`Config{}` holds the reset values of STATUS and every CFG_CHn (Register Details, p36, p38-39), for a part declared as
`Max22200` (correct on both parts) whose RREF is not given: every channel CDR, low side, codes 0 (no current), 25 kHz,
every diagnostic off; COMER masked, the other faults signalled on FAULT; 100 kHz; every pair independent. The datasheet
gives no reset values for CFG_DPM (p40-41): `dpm` is 0, the value written at start-up.

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

## Config

| Field | Type | Reset value | Register | Notes |
| --- | --- | --- | --- | --- |
| `variant` | `Variant` | `Max22200` | none | How a FAULT read clears; the default suits both parts, `Max22200A` only per the BOM ([Hardware](hardware.md#parts)) |
| `rref_ohm` | `uint32_t` | `kNoRref` (0) | none | Not given by default: codes work, `CurrentCode()`, `CurrentUa()` and `DpmCurrentCode()` return `InvalidArgument`. 15000 to 100000 (15 kΩ: IFS = 1 A, KFS / RREF); never sent |
| `wake_timeout_us` | `uint32_t` | 100000 | none | How long the wake loop of `EnsureInitialized()` may take: 2500 (one tWU) to 10000000. tWU is 2.5 ms, but UVM clears only once VM is above UVLO (p22), so the wake also waits for the supply ramp; measure yours (bench step 1) |
| `loaded_channels` | `uint8_t` | 0 | none | The channels with a load fitted, bit n for channel n; never sent. Only these may turn on `open_load_detect`, `plunger_detect` or `hit_current_check`: on an unloaded channel they report the missing load as a fault and pull the shared FAULT pin (handbook KB-CPP-131) |
| `clock` | `MasterClock` | `Khz100` | STATUS.FREQM | Global: every channel's chopping frequency and HIT time follow it |
| `fault_mask` | `FaultMask` | COMER masked | STATUS[23:17] | A masked fault does not pull FAULT low; its flag is still set |
| `pairs[4]` | `PairMode` | `Independent` | STATUS.CMxy | Changes only with both channels of the pair off |
| `channels[8]` | `ChannelConfig` | all zero | CFG_CH0 to CFG_CH7 | Below |
| `dpm` | `DpmConfig` | none given (0 written) | CFG_DPM | `start` 0 to 127, `debounce` and `threshold` 0 to 15 |

## ChannelConfig

| Field | Reset value | Register bits | Range and rules |
| --- | --- | --- | --- |
| `drive` | `Cdr` | VDRnCDR (7) | Changes only with every channel off and TRIGA, TRIGB low |
| `side` | `Low` | HSnLS (6) | The same; `High` needs `Vdr` |
| `hit` | 0 | HIT[6:0] (22:16) | CDR 0 to 126 (`CurrentCode()`); VDR 0 to 100 (percent) |
| `hold` | 0 | HOLD[6:0] (30:24) | The same; on the fly with `SetHoldCode()` |
| `hit_time` | 0 | HIT_T[7:0] (15:8) | 0 none, 1 to 254 (`HitTimeCode()`), 255 forever (`kHitTimeContinuous`) |
| `chop` | `Div4` | FREQ_CFG (5:4) | fMAIN / 4, 3, 2, 1 |
| `half_scale` | false | HFS (31) | IFS halved; low side only |
| `trigger_pin` | false | TRGnSPI (23) | TRIGA (even channels) or TRIGB (odd) switches the channel instead of ONCH |
| `slew_rate_control` | false | SRC (3) | Low side; below 50 kHz only |
| `open_load_detect` | false | OL_EN (2) | Only on a channel in `Config::loaded_channels` |
| `plunger_detect` | false | DPM_EN (1) | Low side only; settings in `Config::dpm`; only on a loaded channel |
| `hit_current_check` | false | HHF_EN (0) | CDR only; only on a loaded channel |

`EncodeChannel()` and `DecodeChannel()` convert to and from the 32-bit register; `EncodeStatus()`, `DecodeStatus()`,
`EncodeDpm()`, `DecodeDpm()` and `DecodeFaults()` do the same for the others.

## Validation

`max22200::Validate(config)` checks a configuration without touching the chip; `EnsureInitialized()` and
`ApplyConfig()` call it before any I/O, `ConfigureChannel()` calls `ValidateChannel()`. They return
`Error::InvalidArgument` for:

- an enum holding a value that is not an enumerator, or the reserved pair mode 3;
- an RREF that is neither `kNoRref` nor 15 kΩ to 100 kΩ (`kNoRref` passes: only the milliamp conversions need it);
- a wake timeout outside 2.5 ms to 10 s;
- a CDR code above 126 (127 is "LS on", the coil at full VM with no regulation, Table 4) or a VDR code above 100;
- CDR on the high side (the chip ignores HSnLS in CDR), or SRC, HFS or DPM on the high side;
- SRC at 50 kHz or above ("SRC mode is only available ... for switching frequencies less than 50kHz");
- HHF in VDR (the HIT check is CDR only);
- a DPM field wider than its bits.

The runtime checks that need the chip's state return `ChannelsActive` instead: a drive-mode or side change while a
channel is on, a pair-mode change while one of the pair is on.

## Kconfig

Under ESP-IDF, `idf.py menuconfig` shows **MAX22200 solenoid driver** when the driver is a component of the project
([Installation](installation.md)); the menu is [`_config/Kconfig`](../_config/Kconfig). The options reach the code
only through [`inc/max22200_kconfig.hpp`](../inc/max22200_kconfig.hpp), never through a driver signature:

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | `Config` field |
| --- | --- | --- |
| `CONFIG_MAX22200_VARIANT_PLAIN` / `CONFIG_MAX22200_VARIANT_A` | MAX22200 | `variant` |
| `CONFIG_MAX22200_CLOCK_80KHZ` | n (100 kHz) | `clock` |
| `CONFIG_MAX22200_RREF_OHM` | 0 (not given) | `rref_ohm` |

The channel settings are not Kconfig options: set them in code. Without Kconfig (plain CMake, other platforms)
`KconfigDefaults()` returns `Config{}`. Both cases are host tests.

Next: [Porting](porting.md).
