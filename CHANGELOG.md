# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Changed

- The repository moved to kokeb-labs-drivers; URLs follow.

## [0.1.0] - 2026-11-10

### Breaking

- Every I/O call of a running driver can return `Error::DeviceReset` (was `ReadStatus()` only): a fault byte or a
  STATUS value with ACTIVE = 0 means a power-on reset. `ReadRegister(reg::kStatus)` no longer returns such a STATUS
  as read.
- Every I/O call of a running driver can return `Error::CommunicationError` when it sees COMER go from 0 to 1 (was
  only with `Verify::ReadBack`): an earlier write was miscounted.
- New `Error::Uncertain` (23): `SetChannelsOn()`, `SetChannel()` and `SetBridge()` refuse a switch-on in a pair whose
  mode a write with a failed reply left unknown; that pair is switched off instead (the rest of a `SetChannelsOn()` mask
  still goes out, and a channel a failed write may have switched on is switched off when asked, even if the copy
  already says off).
  A switch-off there is never refused: it switches both channels of the pair off, the partner too.
- `SetChannel(ch, false)` in a full-bridge pair sets HiZ (both channels off) instead of returning `InvalidArgument`.
- `LatchedFaults` has new fields (`overcurrent`, `open_load`, `hit_not_reached`, `plunger`, `lost`), and `Any()`
  counts them.
- `FullScaleUa()` returns `Result<uint32_t>` (`InvalidArgument` for an RREF out of range, `kNoRref` included) instead
  of 0, and `HitTimeUs()` returns `Result<uint32_t>` (`InvalidArgument` for `kHitTimeContinuous`) instead of
  `kForever`, which is removed: no public helper returns a sentinel (handbook KB-CPP-126, KB-CPP-127).
- A load-dependent diagnostic (`open_load_detect`, `plunger_detect`, `hit_current_check`) needs its channel in the new
  `Config::loaded_channels`; `Validate()`, `ApplyConfig()`, `EnsureInitialized()` and `ConfigureChannel()` refuse it
  otherwise with `InvalidArgument` (KB-CPP-131).
- The port concept checks the required hooks exactly: each parameter of `EnsureInitialized()`, `Transfer()` and
  `GpioSet()` must have exactly the documented type, and each hook must be `noexcept` and return `bool`
  (KB-CPP20-006). `GpioSet()` is part of `SpiPort`. A `DelayUs()`, `GpioRead()` or `Log()` declared with another
  signature is a compile error (KB-CPP20-007).
- `reg::kMaxSpiHz` is replaced by `kMaxClockHz` with the other bus requirements in `max22200_spi_interface.hpp`.
- `Error` is `[[nodiscard]]`; `GetConfig()` is `const&`-qualified and deleted on a temporary driver.
- `EnsureInitialized()` takes 48 transfers (was 46): it reads and clears FAULT.

### Added

- Bus requirements as `constexpr` (KB-CPP-123): `kSpiMode`, `kMsbFirst`, `kMaxClockHz`, `kMinCsHighNs`, `kCsSetupNs`,
  `kCsHoldNs`, `kContinuousWord`, `kCmdSetupNs`, `kTransferBytes`, `kSdoOpenDrain`, with datasheet references; the host
  and ESP32 ports and the documented port check their settings with `static_assert`.
- A driver-owned latch of clear-on-read flags (KB-CPP-119): every 32-bit STATUS read ORs OVT and UVM into it, every
  FAULT read the flags it clears; `GetLatchedFaults()` and `ClearLatchedFaults()`, no I/O. Only the clear empties
  it (a restart does not), and a reply a floating SDO produces is never latched (KB-CPP-116).
- The start-up reads and clears FAULT and reports what was latched before it in `GetStartupFaults()` (KB-CPP-058).
- `ReadRegister()`, a single-shot raw 32-bit read with no retry (KB-CPP-129).
- `DecodePairMode()`, `ValidLoadDiagnostics()`, `Faults::Or()` and `Faults::And()`, `LatchedFaults`.
- Strict ISO C++20 everywhere (KB-CPP20-001): `CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors` and the C++20
  deprecations as errors; a header self-check per public header on GCC, Clang, the Cortex-M build and inside the
  ESP-IDF build (`max22200_cxx20_check.cpp`).
- Compile-fail tests for a narrowed `Transfer()` length, a `Transfer()` without `noexcept`, a `DelayUs(uint16_t)`, a
  `void GpioRead()`, a `GpioRead()` taking the signal by value and a misdeclared `Log()`; `tests/test_latch.cpp`;
  compile-time round trips of the register encoders.
- The review skills `cpp20-profile`, `misra-review` and `hardware-lessons-review` and the `safety-reviewer` agent,
  from the template.

- First version of the driver for the Analog Devices MAX22200 and MAX22200A octal solenoid and motor drivers,
  written on the Kokeb Labs driver template from datasheet 19-100531 Rev 1, every rule checked against the
  datasheet.
- `max22200::MAX22200<Port>` with the configuration in the constructor and one start-up call, `EnsureInitialized()`:
  CMD high, ENABLE and tEN, the programming procedure of Figure 6 (STATUS read; a wake loop of bare ACTIVE = 1 writes,
  tWU and STATUS reads until ACTIVE reads 1 with UVM clear, bounded by `Config::wake_timeout_us`, 100 ms by default;
  STATUS write, every CFG_CHn and CFG_DPM), and a read-back of every register and of each write's command echo. A
  wake that runs out returns `Undervoltage` (UVM set) or `Timeout`, uninitialised with every channel off. COMER
  repeats the procedure (`kStartAttempts`). `ApplyConfig()` writes only what changed; `Deinitialize()` switches every
  channel off, clears ACTIVE and drives ENABLE low.
- Switching from the driver's copy of ONCH, one 8-bit write each: `SetChannelsOn()`, `SetChannel()` (parallel pairs
  switch as one), `SetBridge()` (Table 7); `ReadChannelsOn()`. The Command Register stays in force until rewritten
  (p25), so an ONCH write right after another is one transfer (the data byte); any other access, a failure, an ENABLE
  change, COMER or a read-back sends the command again. The data byte's STATUS[7:0] updates `GetLastStatusByte()`.
- Settings: `ConfigureChannel()`, `SetHoldCode()` (8-bit HOLD update on the fly), `SetPairMode()`, `SetFaultMask()`,
  `SetDpm()`; `Verify::ReadBack` per call reads the register back and checks COMER. A drive-mode, side or pair-mode
  change while a channel concerned is on is refused (`Error::ChannelsActive`).
- Diagnostics: `ReadStatus()` (power-on reset reported as `Error::DeviceReset`), `ReadFaults()` and `ClearFaults()`
  for both parts (`Config::variant`, `Variant::Max22200` by default: correct on both parts, a selective clear needs
  `Max22200A` declared per the BOM and a bench check), `ReadFaultPin()` through an optional `GpioRead()` hook,
  `GetLastStatusByte()`.
- After a read-back that differs the driver's copy takes the value read (ONCH, CFG_CH, HOLD, STATUS, CFG_DPM); the
  ONCH bits that differ stay untrusted, and a channel whose CFG_CH write failed or read back different accepts no write
  while a channel is on.
- `ChannelConfig` and `Config` as the registers' codes, `Config{}` equal to the reset state, `Validate()` and
  `ValidateChannel()` with every datasheet rule; exact conversions `CurrentCode()`, `CurrentUa()` (IFS / 128),
  `HitTimeCode()`, `HitTimeUs()`, `DpmCurrentCode()` (IFS / 127), `DebounceUs()`, `ChopPeriodNs()`,
  `DutyLimitsOf()`; Kconfig options for the part, the oscillator and RREF.
- Errors `NoDevice`, `Pin`, `CommunicationError`, `WriteMismatch`, `Undervoltage`, `ChannelsActive`,
  `DeviceReset`.
- Host tests over a protocol model of both parts, written from the datasheet independently of the driver's register
  header and cross-checked against it, with the bus and board faults the port contract allows, every transfer and pin write
  of every operation failed in turn; a libFuzzer target with its corpus replayed as a CTest; the host and ESP32
  examples; the Drive modes and Status and limitations pages. Not verified on hardware.

**Datasheet rules the first version applies.** Details on the Status and limitations page:

- CDR currents use IFS / 128 (Table 4); no current becomes code 127, the unregulated full-on state.
- HIT times use the exact chopping frequency (33.3 kHz, 26.7 kHz, Tables 1 and 5); a HIT time beyond 254 steps is
  refused instead of becoming "continuous IHIT".
- COMER is tested as a bit of the fault byte (Figure 6), never by comparing the whole byte.
- The selective FAULT clear sends its mask LSB byte first, where Figure 8 draws it; not verified on hardware: it is
  one constant, `reg::kFaultClearLsbByteFirst`, and a bench step decides. A FAULT read on the MAX22200A clears only
  what SDI selects.
- VDR duty codes are sent as asked, so 0 % and 100 % are reachable (Table 3); the chip applies δMIN and δMAX itself.
- SRC at 50 kHz, CDR, SRC, HFS or DPM on the high side, and HHF in VDR are refused.
- The start-up wakes the chip with bare ACTIVE writes until ACTIVE reads 1 with UVM clear (Figure 6), bounded by
  `Config::wake_timeout_us`. COMER is not assumed to clear on a STATUS read (p36: "Read Only"): it is armed once the
  wake is done, only a 0 to 1 transition repeats the procedure, and ENABLE goes low for tDIS and high for tEN between
  attempts.
- CMD idles high: it is driven high again after every data phase, the one-transfer ONCH write included (p25-26).
- `Config::rref_ohm` is not given by default (`kNoRref`): the code-level API works without it, and `CurrentCode()`,
  `CurrentUa()` and `DpmCurrentCode()` return `InvalidArgument` until it is set.
- CFG_DPM is compared on bits 14:0 only: bits 31:15 are reserved, three of them read only with no reset value
  (p40-41).
- Mode and pair-mode changes are refused while the channels concerned run (step 5c, Register Details); parallel pairs
  switch both ONCH bits (Table 6); out-of-range DPM settings are refused instead of clamped; shutdown reports its
  writes.

### Changed

- Hardware page: the pinout, the block diagram and the application circuit are now figures drawn from
  `docs/hardware.yml`, which also generates the pin table (replacing the hand-written one) and the parts table.
  Every pin, block and part cites the datasheet section and page; values the datasheet does not give are marked as
  not confirmed. `kokeb-docs hardware --kicad-netlist` exports the circuit.
- Synced with the driver template: `tools/preflight.sh` runs clang-tidy over every file CI lints, as CI does, and
  the hardware-lessons review adds the switch-off lessons. The fault-handling example in the API guide clears the
  overcurrent channels with unsigned bit operations, which that clang-tidy run requires.
- The `misra-review`, `cpp20-profile` and `new-driver-from-template` skills and `docs/repo-setup.md` follow the
  template: reading the MISRA report, the known tool misfires, enumeration ranges without a sentinel, and the
  settings tool.
- `tools/preflight.sh` and the `verify` and `hardware-lessons-review` skills follow the template: every check CI runs
  that a workstation can run, in one command, and the rules for device resets, faults the chip clears itself, dropped
  writes, switch-offs that no uncertainty refuses, channels in doubt and a model that changes state on its own.
- Follows the driver template's portable ownership: copyright belongs to `The driver-max22200 authors` (`LICENSE`, every
  source header, `driver.yml` `authors`, the docs footer). `tools/check_standalone.py` runs in the *Handbook source
  checks* job and keeps the code and docs about this chip only. `AGENTS.md` follows the template's structure; the
  issue and PR templates ask for the board, MCU and port in one field.
- The port concepts match each hook's address against exactly one `noexcept` signature with
  `detail::ExactHook<Port, Sig>::Take(&Port::Hook)` instead of probing it with a converting `detail::Exactly<T>`:
  no conversion operator is left. A misdeclared hook is still a compile error. `kAlwaysFalse` takes a class and
  the enumeration helpers an enumeration (`ClassType`, `Enumeration`), each with a compile-fail test.
  `HasGpioRead` matches `bool GpioRead(CtrlPin, GpioSignal&) noexcept` the same way, so a hook that takes the
  signal by value or as `const&` still does not match.
- Include guards instead of `#pragma once` in every header (KB-CPP-019).
- Every narrow operand of a shift or bitwise operator is widened to `uint32_t` first, and `UL` literals and
  `unsigned` casts are fixed-width (KB-CPP20-020, -021); enumerations are range-checked with `detail::IsEnumerator()`
  (KB-CPP20-025); `ToString()` is a switch over every enumerator.
- The ESP32 port sets CMD high and ENABLE low before it enables their outputs (KB-HW-018); the Porting page lists each
  control pin's idle level and polarity.
- The data phases go through fixed-extent `std::span`s (KB-CPP20-042).
- MISRA C++:2023 conformance: integer constant arguments have exactly the parameter's type through the `consteval`
  literals `detail::operator""_u8` and `operator""_u32` (`EncodeStatus(config_, 0_u8, true)`,
  `FaultClearSdi(0xFFFF'FFFF_u32)`; an out-of-range literal does not compile), and a narrow ONCH bit is widened
  explicitly (Rule 7.0.6); the layout checks compare against `U` literals (Rule 7.0.5); operands of `&&`, `||`, `?:`
  and comparisons are parenthesised (Rule 8.0.1); every switch over an enumeration ends with `default: break;`, and
  the host tests build with `-Wswitch-enum` so a new enumerator still warns (Rule 9.4.2); the five defaulted
  `operator==` are hidden friends (Rule 16.6.1); the `.ipp` include at the end of `max22200.hpp` carries the
  deviation marker of MD-CPP-009 (Rule 19.0.3). Behaviour is unchanged.
- Synced with driver-template: `_config/.clang-tidy` (the handbook's high-level profile), the repository checks with
  the handbook's source checks, the `verify`, `add-driver-feature` and `new-driver-from-template` skills.

### Fixed

- A `SetChannelsOn()` that returns `Uncertain` no longer skips its write while a switch-off is unverified: a switch-off
  that no fault byte with COMER clear has vouched for yet (the chip may have miscounted it, and the next fault byte
  may have been lost on the bus) counts as possibly on, so a mask that asks that channel off goes out again.
- Doubt that a write settles is no longer lost when the chip miscounts that very write (p34, p36): a switch-off, a
  STATUS write that carries ONCH, CMxy or FREQM moves the doubt it clears into the unverified sets, so the COMER that
  follows restores it; with COMER already set a STATUS write keeps the doubt until a read-back, and a read-back that
  matches proves every switch.
- A configuration write (CFG_CHn, HOLD, STATUS, CFG_DPM) whose command byte shows a new COMER stops before its data
  phase and returns `CommunicationError`: the channels the miscount left in doubt may be on, so a drive-mode change
  would be ignored by the chip (Programming Procedure 5c). ONCH writes are never stopped, so a switch-off always goes
  out.
- While COMER stays set (detection degraded), `ConfigureChannel()` reads its write back (2 more transfers), and a CFG_CH
  or STATUS write without a read-back leaves the channel's mode, or the pair modes and FREQM it changes, in doubt: a
  miscounted configuration write cannot show then, so it is never trusted.
- `tools/preflight.sh` follows the template: clang-tidy runs as CI runs it. A signed-bitwise finding it then reported in
  the fault-handling example of the API guide is fixed.
- A power-on reset (UVLO, p22) is detected by every call from the fault byte of its command, before the data phase:
  no write reaches the reset chip, so no setter re-activates it with the cached ONCH over reset CFG_CHn values. The
  driver marks itself uninitialised with every channel off and returns `DeviceReset`. A STATUS value with
  ACTIVE = 0 does the same. A one-transfer ONCH write that gets no fault byte back (0xFF) sends its command again so
  the reset shows.
- Flags the chip clears by itself (OLF on a switch-on, p21; HHF on a new cycle, p23) are no longer lost: the summary
  flags of every fault byte (OVT, UVM, OCP, OLF, HHF, DPM) go into `GetLatchedFaults()` at no extra I/O.
- A miscounted write (COMER, p34) is no longer reported as `Ok` for ever: the first call that sees COMER go from 0
  to 1 returns `CommunicationError`; the ONCH bits the writes since the last clean fault byte changed count as on for
  `ChannelsActive`, their channels as of either drive mode, and `ApplyConfig()` writes their registers again.
- A write whose reply failed may have taken effect: a pair mode or FREQM it changed is unknown until a STATUS write
  (pair off) or read-back settles it, a switch in that pair returns `Uncertain`, `ConfigureChannel()` accepts only a
  channel valid at both clocks, and `ApplyConfig()` writes the register again even when the copy matches. A channel
  that may run either drive mode is written, its own cached value included, only with every channel off, so the
  write settles the mode.
- A COMER the wake leaves is cleared at start-up with ENABLE low, and while COMER stays set on a running driver every
  switch-off is read back (2 more transfers each), so miscount detection never stays off silently;
  `IsMiscountDetectionDegraded()` reports that state. A switch-off miscounted in the very call that first shows the
  COMER keeps its channels in doubt (they count as on and are switched off again when asked), and so does a switch-off
  whose forced read-back fails.
- Read data of all ones is followed by one more command byte, so a reset between a read's command byte and its data
  returns `DeviceReset` instead of the floating value.
- A STATUS read, or a FAULT read that clears, that fails or gets a floating reply sets `LatchedFaults::lost`: the
  flags it cleared are unknown. A genuine FAULT of 0xFFFFFFFF with a 0xFF fault byte is treated the same, never used
  as data.
- A STATUS read-back (`SetPairMode()`, `SetFaultMask()` with `Verify::ReadBack`) cleared OVT and UVM on the chip and
  dropped them; they are kept in the latch.
- Faults latched before the start held the FAULT pin after a clean start; the start-up now clears them.
- A STATUS read-back with the reserved CMxy value 3 stored it as a `PairMode`; it is refused and the copy keeps its
  mode.
- `ApplyConfig()` on a running driver dropped a new `wake_timeout_us`; it is kept, as is the new `loaded_channels`.

