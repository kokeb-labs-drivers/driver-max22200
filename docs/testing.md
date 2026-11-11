# Testing and CI

How the driver is checked: host tests against a model of the chip, a fuzzer that corrupts what the chip sends
back, compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis and measured quality
gates, on every pull request.

## Host tests

The tests build the real driver against a protocol model of the chip (`tests/mock_max22200.hpp`). The model is written
from the datasheet on its own: it has its own register map with page numbers and does not include the driver's
`max22200_registers.hpp`; `tests/test_model.cpp` cross-checks the two maps at compile time. It runs the chip transfer
by transfer: the Command Register written with CMD high and its fault byte, the command staying in force until
rewritten (p25), 32-bit writes LSB byte first with the command echo, 32-bit reads MSB byte first, 8-bit accesses of
bits 31:24 with the STATUS[7:0] byte of an 8-bit write, the clock-count rule of COMER (the first 32 clocks of a 32-bit
write, then every 8), COMER kept until ENABLE falls or a power-on reset (the worst reading of "Read Only"; the other
is an option), STATUS reads that clear OVT and UVM, FAULT reads that clear selectively (MAX22200A) or wholly
(MAX22200), CFG_DPM's read-only reserved bits reading 1, VDRnCDR and HSnLS locked while a channel is on or a TRIG pin
high, CMxy locked while the pair is on, OLF and HHF cleared when a channel switches on and HHF on a new cycle, an
OCP channel kept off until its ONCH goes off and on again, tEN after ENABLE and tWU after ACTIVE, a power-on reset
that restores every reset value and forgets the command, and ACTIVE refused while UVM is set or (the other reading)
taken. The faults are the bus and board conditions the
port contract allows or the datasheet names: SDO floating high, stuck low or noise; SDI not reaching the chip; a
power-on reset; VM below UVLO, or still rising; miscounted writes; writes acknowledged and lost (a shared-bus glitch);
a TRIG pin held high; channel faults; failed transfers (before and after the chip took them), lost and replaced
transfers; failed pin writes. They build with the firmware's flags, `-fno-exceptions -fno-rtti`, and warnings as
errors, on GCC and Clang, as ISO C++20 (`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`, the C++20 deprecations as
errors) with the standard library's bounds checks on. `max22200_host_tests` runs 168 test cases; with the Kconfig,
version, install, Doxygen, host-example, compile-fail and fuzz-corpus tests CTest runs 23 tests.

The strict-dialect header checks (`max22200_header_checks`) compile each public header alone, first in its own
translation unit, and assert `__cplusplus == 202002L`: on GCC and Clang with the host tests, with Arm GCC in the
Cortex-M build (`max22200_optimised_build`), and with the ESP-IDF toolchain through
`examples/esp32/main/max22200_cxx20_check.cpp`, compiled at `-std=c++20 -pedantic-errors` whatever dialect ESP-IDF
uses for the rest (handbook KB-CPP20-001).

```bash
cmake -S . -B build -DMAX22200_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_max22200.cpp`](../tests/test_max22200.cpp) | The lifecycle (no I/O before the start, the transfers of the start in Figure 6's order, tEN and tWU, the wake loop and its timeout, a retried start, shutdown and restart), `Validate()` and `ValidateChannel()` against each datasheet rule, switching with and without `Verify::ReadBack`, parallel pairs and full bridges, channel settings and the mode lock, HOLD on the fly, pair modes, masks and DPM, STATUS and `DeviceReset`, both FAULT clear behaviours (the selective clear's byte order), the FAULT pin, the encodings and every unit conversion |
| [`tests/test_fast_path.cpp`](../tests/test_fast_path.cpp) | The one-transfer ONCH write and every way it is invalidated, the STATUS byte it returns, the driver's copy after each kind of read-back mismatch, COMER at start-up (already set, cleared on read, new), CFG_DPM's reserved bits |
| [`tests/test_latch.cpp`](../tests/test_latch.cpp) | The latch of clear-on-read flags fed by every STATUS and FAULT read (a STATUS read-back keeps the OVT it clears), the faults latched before the start cleared and reported, the raw `ReadRegister()`, a CMxy of 3 refused, load diagnostics only on declared loads |
| [`tests/test_reset.cpp`](../tests/test_reset.cpp) | What the chip changes on its own: a power-on reset caught from the fault byte by every call before any data phase (with ACTIVE refused or taken while UVM is set), the restart after it; OLF and HHF the chip clears latched from the fault byte; the OCP latch; a miscounted write reported by the next call, its bits in doubt and its register written again; a failed reply: pair modes and FREQM unknown, `Uncertain`, registers written again; clear-on-read flags lost |
| [`tests/test_model.cpp`](../tests/test_model.cpp) | The model's register map against the driver's, and the model's own rules (command persistence, the clock count, COMER, OLF/HHF on switch-on, HHF on a new cycle, the OCP latch, CFG_DPM's read-only bits, the power-on reset, ACTIVE with UVM set) |
| [`tests/test_bus_faults.cpp`](../tests/test_bus_faults.cpp) | Every transfer of every operation, `EnsureInitialized()` included, fails in turn, before and after the chip took it; each returns `Bus`, CMD is left low and the next call works. Every pin write fails in turn; each returns `Pin` and a command without CMD high is never sent |
| [`tests/fuzz/`](../tests/fuzz/fuzz_max22200.cpp) | The fuzz target; CTest replays its corpus (`max22200_fuzz_max22200_corpus`), see below |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | Every chip-specific value (16 to 23) and `Unsupported` are returned by a real call |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/compile_fail/`](../tests/compile_fail/port_without_delay.cpp) | Code that must not compile, each for its `EXPECT:` diagnostic: a port without `DelayUs()`, without `GpioSet()`, with a `void GpioSet()`, a `Transfer()` with a narrower length or without `noexcept`, a `DelayUs(uint16_t)`, a `void GpioRead()`, a `GpioRead()` that takes the signal by value, a `Log()` with another signature, a class that is not a port |
| [`tests/mock_max22200.hpp`](../tests/mock_max22200.hpp) | The chip model; the mock ports over it are in `mock_max22200_bus.hpp` |
| [`tests/check_version_copy.cmake`](../tests/check_version_copy.cmake) | `_config/Doxyfile` carries the CMake version |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`max22200_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(max22200)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

The model does not reproduce the chip's analog or electrical behaviour, and the points the datasheet leaves open are
modelled one way and listed: see [Status and limitations](limitations.md).

## Fuzz tests

Everything the driver receives comes from SDO and from the port's return values, so that is what a noisy cable, an
unplugged board or a broken chip controls. [`tests/fuzz/fuzz_max22200.cpp`](../tests/fuzz/fuzz_max22200.cpp) lets
libFuzzer choose the part and a configuration (sometimes for the other part, sometimes invalid), then up to 64 driver
calls, and on every transfer what the bus does: pass the model's reply, fail the transfer (before or after the chip
took it), lose it (the chip never sees it, SDO floats), flip bits of the reply or replace it; on every pin write
whether it fails. Between calls it changes the board: SDO floating, stuck low or noisy, SDI not reaching the chip, a
power-on reset, VM below UVLO, a miscounted or a lost write, a failed reply followed by a miscount of the next write,
a TRIG pin held high, channel faults. After every call
the target checks the driver's promises and aborts, naming the line, when one breaks:

- a failed call returns an `Error` the enum defines (never one documented as not returned) and `T{}`; every transfer
  is 1 or 4 bytes with both buffers, and a 4-byte transfer never has CMD high;
- before `EnsureInitialized()` no call makes a transfer or a pin write; `InvalidArgument`, `ChannelsActive` and
  `Unsupported` mean nothing was sent;
- **a reset chip is never re-activated** without a new start, under both readings of whether ACTIVE is taken while
  UVM is set;
- **no switch-off is held back**: after a `SetChannelsOn()` that returns `Uncertain`, each pair on the chip ends at the
  request or only loses bits, and every channel asked off that the driver could know was on is off; a
  `SetChannel(ch, false)` that returns `Ok` leaves the channel off. Both are checked while the bus delivered the call
  as sent and no write was lost unseen since the last start (a dropped write, deaf SDI, a TRIG pin high, a lost
  transfer: a successful start clears this). A miscount does not stop the checks: an earlier one the driver has not
  seen yet only narrows "could know" to its copy, and one armed for the call itself skips them for that call, except
  the `SetChannel()` check while detection is already degraded (every switch-off is then read back); a miscount the
  driver is blind to (SDO not carrying the chip's fault bytes while COMER is set) and a forged reply count as hidden
  state;
- **a drive-mode change that returns `Ok` is in force**: after a `ConfigureChannel()` that returns `Ok` on a bus that
  delivered it, the chip holds the requested drive mode and side (it takes them only with every channel off, 5c);
- CMD is high (its idle level) after every call that drove pins without a failure; ENABLE is low after a failed start
  that drove it (a wake timeout excepted) and after `Deinitialize()`;
- **the chip never holds a channel setting the driver would refuse** (a CDR code of 127, CDR on the high side, a VDR
  code above 100) nor reserved bits, unless a transfer was lost while reported as done, a TRIG pin was held high, or
  the board dropped or miscounted a write (the chip then keeps a mode the driver cannot see);
- a lone ONCH data byte (the one-transfer write) goes out only while the chip's command in force is the 8-bit STATUS
  write, unless the board reset the chip, SDI was deaf or a transfer was lost since the last command;
- **`EnsureInitialized()` never returns `Ok` on a dead bus** (SDO floating or stuck, SDI not reaching the chip) unless
  a transfer of the call was tampered with; after it, tWU has passed;
- while every call since the start was trusted and the board did nothing behind the driver's back, the chip holds
  exactly what the driver says it wrote: ONCH, STATUS, every CFG_CHn and CFG_DPM.

The fuzzer's first runs broke three promises. Two were promises stated too strongly (a call that drives no pin keeps
CMD as an earlier failed pin write left it; a start that fails before it drives ENABLE leaves ENABLE as it was), kept
as `regress-cmd-left-high-by-earlier-pin-failure` and `regress-failed-bus-up-keeps-enable`. The third,
`regress-trig-high-keeps-old-drive-mode`, is a limit of the chip: with a TRIG pin high the chip keeps the old drive
mode and takes the new codes, so a CDR code of 126 written with a change to VDR runs as a 100 % duty. The driver cannot
see the TRIG pins; it is documented ([Hardware](hardware.md#timing)), and `Verify::ReadBack` reports it. Reviewing
the same path found that a failed ONCH write that reached the chip left the mode-change check blind to the channel it
switched on; the driver now counts such channels as on (unit test "After a failed ONCH write ...").

After the second review's changes it broke two more. `regress-garbage-readback-trusted`: a read-back through a
stuck-low SDO differed, the driver took the garbage as the chip's ONCH, and a later drive-mode change landed on a
channel that was really on; now the bits that differ stay untrusted, and a channel whose CFG_CH write failed or read
back different accepts no write while a channel is on. `regress-lost-onch-write-locks-drive-mode`: the board drops an
ONCH write the driver believes, so a channel stays on and locks the drive mode, as a TRIG pin does; it is a board
fault only a read-back can see, documented on [Status and limitations](limitations.md).

After the wake loop it broke one: `regress-forged-wake-read`, a STATUS read replaced on the bus showed ACTIVE before
the chip had it, so the start finished less than tWU after ACTIVE rose. A transfer the port reports as good but the
chip never saw is invisible to the driver; the promise excuses it, and every output is still off at that point.

With the switch-off checks running through miscounts it broke one: `regress-failed-readback-drops-doubt`. While COMER
stayed set, a switch-off was miscounted and its forced read-back failed; the driver then trusted the copy ("off"), and
a later refused mask sent nothing while the channel still drove. A failed read-back now leaves the channels asked off
in doubt (unit test "COMER: a degraded switch-off whose read-back fails stays in doubt").

CI's fuzzing then found `regress-unverified-switch-off-held-back`: a switch-off was miscounted, the next access's
fault bytes were lost on the bus (0xFF), so the COMER went unseen, and a `SetChannelsOn()` refused with `Uncertain`
skipped its write because the copy already matched, leaving the channel on. A switch-off that no clean fault byte has
vouched for now counts as possibly on, so the mask goes out again (unit test "Off: a refused mask re-sends a switch-off
no fault byte has vouched for").

A new board event, a failed reply followed by a miscount of the next write, and a new promise (a `ConfigureChannel()`
that returns `Ok` changed the drive mode and side on the chip) found `regress-miscounted-settling-write-1` to `-4`: a
write that settled the doubt a failed reply left was itself miscounted, and the doubt was lost (a switch-off held back,
a switch-on in a pair of unknown mode, a drive-mode change ignored by the chip while a channel drove). The doubt a
write settles now stays recorded until a clean fault byte or a read-back proves it, and a configuration write stops
before its data at a new COMER (unit tests "COMER: doubt a miscounted settling write cleared comes back" and "COMER: a
configuration write stops before its data at a new COMER"). Running on, it found `regress-degraded-config-write-1` to
`-4`: with COMER already set, a miscounted CFG_CH write was trusted; `ConfigureChannel()` now reads back in that state
(unit test "COMER: while degraded, ConfigureChannel reads back"). `regress-blind-miscount-sdo-floating` and
`regress-forged-reply-vouches-miscount` were gaps in the promise, not the driver: with SDO floating the driver cannot
see a COMER at all, and a reply forged on the bus can show a clean fault byte that vouches for a write the chip
ignored (the chip sends no CRC). Both now count as hidden state until the next start.

`regress-deaf-sdi-loses-onch-write`: SDI that does not reach the chip loses every write, an ONCH write too, so a
channel stays on and locks the drive mode, as a dropped write does; the promise excuses it like the other board faults
only a read-back can see.

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM and its libc++)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DMAX22200_BUILD_TESTS=ON -DMAX22200_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_max22200
./build-fuzz/tests/fuzz/fuzz_max22200 -max_total_time=300 tests/fuzz/corpus/fuzz_max22200
```

The same file also builds without libFuzzer as `fuzz_replay_fuzz_max22200`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_max22200/`](../tests/fuzz/corpus/fuzz_max22200) on GCC and Clang: the seeds, and every input
that ever broke a promise (`regress-*`), so a fixed bug stays fixed. CI fuzzes for 120 s on every pull request (job
`Host quality / fuzz`). A local run of 4 minutes (12.2 million runs) broke no promise; on macOS the libFuzzer build
uses UBSan's recover mode with `UBSAN_OPTIONS=halt_on_error=1` and `-fno-sanitize=function`, because Apple's linker
rejects that object otherwise (CI builds on Linux with the CMake flags).

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` | This driver (local) |
| --- | --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 | highest 12 (`Validate()`, `ValidateChannel()`); 11 in the driver class (`initialize()`) |
| Function length | lizard, lines of code per function | at most 60 | within the limit |
| Parameters | lizard, per function | at most 6 | within the limit |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % | 94.6 % (846 of 894) |
| Branch coverage | same run | at least 75 % | 87.5 % (555 of 634) |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) | measured in CI |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report | clean locally (Apple Clang) |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise | clean locally (4 minutes, 12.2 million runs) |

The local figures are GCC 15 and gcovr 8.7 with the CI's options; CI uses GCC 14. The reports are in the run's
`host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DMAX22200_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DMAX22200_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `max22200_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

**Not verified on hardware.** The ESP32 example ([Examples](examples.md)) is the bench test: it switches channel 0
every half second and logs ONCH, the STATUS flags and the FAULT bytes. The steps to run before depending on the
driver are on [Status and limitations](limitations.md#before-you-depend-on-it); record the board and the result in
the pull request's bench-status section.
