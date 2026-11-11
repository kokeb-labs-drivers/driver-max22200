# Datasheet map

The driver follows the MAX22200 datasheet. This page says which revision, where to get it, and where each part of
the driver is described in it, so you can check any behaviour against the source. Section names and page numbers
are the datasheet's own (the printed page numbers).


<!-- generated: datasheet-map -->
- **Datasheet: MAX22200/MAX22200A 36V, 1A Octal Integrated Serial-Controlled Solenoid and Motor Driver**, Analog Devices
  19-100531, revision 1 (2025-03). Get it from the [vendor page](https://www.analog.com/en/products/max22200.html).
  SHA-256 of the file used: `374c60d9f0aba445ef9689fafa48080d1f612bdb1f1cbc7e7baabe6de5f35818`.

| Topic | Datasheet section | Page | In the driver | Explained on |
| --- | --- | --- | --- | --- |
| The two parts and how each clears the FAULT register | General Description; Ordering Information | p. 1 | `Variant`, `Config::variant` | [Hardware](hardware.md), [Configuration](configuration.md) |
| Supply, logic levels, on-resistance, OCP and open-load thresholds | Absolute Maximum Ratings; Electrical Characteristics | p. 7 | none: board design (documented limits) | [Hardware](hardware.md) |
| Full-scale current constant KFS and the CDR accuracy | Electrical Characteristics (KFS0, KFS1, ITRERR) | p. 9 | `kKfs`, `FullScaleUa()` | [Drive modes](drive-modes.md), [Hardware](hardware.md) |
| Enable, wake-up and disable times; SPI timing | Electrical Characteristics (tEN, tWU, tDIS, SPI); Figure 1 | p. 10 | `reg::kEnableUs`, `reg::kWakeUpUs`, `reg::kDisableUs`, `kMaxClockHz`, `kMinCsHighNs`, `kCsSetupNs`, `kCsHoldNs`, `kCmdSetupNs` | [Hardware](hardware.md), [Porting](porting.md), [Contract](contract.md) |
| Pins, CMD, ENABLE, FAULT and the TRIG inputs | Pin Configuration; Pin Description | p. 12 | `CtrlPin`, `SpiInterface`, `kSdoOpenDrain` | [Hardware](hardware.md), [Porting](porting.md), [Quickstart](quickstart.md) |
| ENABLE and the 0.5 ms to SPI ready | Enable Logic Input | p. 14 | `EnsureInitialized()`, `Deinitialize()` | [Contract](contract.md), [API guide](api-guide.md) |
| The HIT and HOLD sequence, and the TRIG and SPI triggers | Functional Description - Two Levels Drive Sequence for Solenoid Valves; Figures 2 and 3 | p. 14 | `ChannelConfig`, `SetChannelsOn()` | [Drive modes](drive-modes.md), [API guide](api-guide.md) |
| Oscillator and per-channel chopping frequency | Chopping Frequency (FREQM, FRQ_CFG_); Table 1 | p. 16 | `MasterClock`, `ChopDivider`, `ChopPeriodNs()` | [Drive modes](drive-modes.md), [Configuration](configuration.md) |
| Slew-rate control and the duty-cycle limits | Slew-Rate Controlled Mode (SRC); Min and Max Duty cycle, Table 2 | p. 17 | `ValidateChannel()`, `DutyLimitsOf()` | [Drive modes](drive-modes.md) |
| VDR duty-cycle codes | Voltage Drive Regulation (VDR); Table 3 | p. 17 | `kMaxVdrCode`, `ValidCode()` | [Drive modes](drive-modes.md), [Configuration](configuration.md) |
| CDR current codes, IFS from RREF and the half full-scale bit | Current Drive Regulation (CDR); Table 4; Full-Scale Current Setting; Half Full-Scale Setting | p. 18 | `CurrentCode()`, `CurrentUa()`, `kMaxCdrCode`, `Config::rref_ohm` | [Drive modes](drive-modes.md), [Configuration](configuration.md) |
| HIT excitation time | HIT Excitation Time (tHIT); Table 5 | p. 19 | `HitTimeCode()`, `HitTimeUs()`, `kHitTimeContinuous` | [Drive modes](drive-modes.md) |
| High side, parallel pairs and full bridges | Supported Driver Configurations; Tables 6, 7 and 8 | p. 20 | `Side`, `PairMode`, `BridgeState`, `SetChannel()`, `SetBridge()`, `SetPairMode()` | [Drive modes](drive-modes.md), [API guide](api-guide.md), [Status and limitations](limitations.md) |
| Overcurrent and open-load protection | Protection Circuits; Overcurrent Protection (OCP); Open-Load Detection (OL) | p. 21 | `Faults`, `FaultMask`, `ClearFaults()` | [API guide](api-guide.md), [Troubleshooting](troubleshooting.md) |
| Plunger-movement detection, thermal shutdown, UVLO and the HIT check | Detection of Plunger Movement (DPM); Thermal Shutdown (TSD); Undervoltage Lockout (UVLO); HIT Current Not Reached Flag (HHF) | p. 22 | `DpmConfig`, `ReadStatus()`, `Error::DeviceReset`, `Error::Undervoltage`, `LatchedFaults` | [Drive modes](drive-modes.md), [API guide](api-guide.md), [Troubleshooting](troubleshooting.md) |
| The SPI, the 8-bit accesses and the daisy chain | SPI Detailed Description; Figure 5 | p. 23 | `detail::RegisterBus`, `kSpiMode`, `kMsbFirst`, `SetChannelsOn()`, `SetHoldCode()` | [Architecture](architecture.md), [Status and limitations](limitations.md), [Porting](porting.md) |
| The programming procedure | Programming Procedure; Figure 6 | p. 23 | `EnsureInitialized()`, `kStartAttempts`, `Config::wake_timeout_us`, `ConfigureChannel()` | [Contract](contract.md), [Architecture](architecture.md), [API guide](api-guide.md) |
| The Command Register and the fault byte | Command Register Description (COMMAND); Table 9; Figure 7 | p. 25 | `reg::Command()`, `GetLastStatusByte()` | [Architecture](architecture.md), [Porting](porting.md) |
| STATUS | Status Register Description (STATUS); Table 10; Register Details, STATUS (0x00) | p. 26 | `Status`, `ReadStatus()`, `GetLatchedFaults()`, `FaultMask`, `EncodeStatus()`, `DecodePairMode()` | [API guide](api-guide.md), [Configuration](configuration.md) |
| CFG_CHn | Configuration Register Description (CFG_CH_); Table 11; Register Details, CFG_CH | p. 27 | `ChannelConfig`, `EncodeChannel()`, `ConfigureChannel()` | [Configuration](configuration.md), [Drive modes](drive-modes.md) |
| FAULT and its two clear-on-read behaviours | Fault Register Description (FAULT); Table 12; Clearing the FAULT Register; Figure 8 | p. 28 | `ReadFaults()`, `ClearFaults()`, `GetLatchedFaults()`, `GetStartupFaults()`, `Error::Unsupported` | [API guide](api-guide.md), [Status and limitations](limitations.md) |
| CFG_DPM | Detection of Plunger Movement Register Description (CFG_DPM); Figure 9 | p. 29 | `DpmConfig`, `SetDpm()`, `DpmCurrentCode()`, `DebounceUs()` | [Drive modes](drive-modes.md), [Configuration](configuration.md) |
| 32-bit and 8-bit transfers and the command echo | Reading and Writing Operations of Registers; Figures 10 to 13 | p. 30 | `detail::RegisterBus`, `kTransferBytes`, `Verify::ReadBack` | [Architecture](architecture.md), [Porting](porting.md), [Contract](contract.md) |
| Communication error detection | Communication Error Detection (COMF) | p. 34 | `Error::CommunicationError`, `kContinuousWord` | [Troubleshooting](troubleshooting.md), [Contract](contract.md) |
| Register map and reset values | Register Map; Register Details | p. 35 | `reg::` constants, `Config{}` | [Configuration](configuration.md), [Architecture](architecture.md) |
| Current rating and safe operating area | Applications Information; Current Rating; Figures 14 to 17 | p. 42 | none: board and thermal design | [Hardware](hardware.md) |
| The application diagram | Typical Application Circuits; Application Diagram | p. 44 | none: board design | [Hardware](hardware.md) |
<!-- /generated -->

## When the datasheet and the driver differ

The driver is written against the revision above. If you have a newer revision, compare its revision history with
the topics above; a change to a pin, a register, a timing or a value the driver uses is a bug report for this
repository. Where the driver deliberately goes beyond the datasheet, the [Contract](contract.md) says so.

The repository never contains the PDF itself: vendors own their documents. Download it from the vendor, and compare
its SHA-256 with the one above to confirm you have the same file.
