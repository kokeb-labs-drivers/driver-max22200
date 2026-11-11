---
name: hardware-lessons-review
description: Review a driver against the handbook rules that came from testing drivers on real hardware - reply validity, stuck and floating lines, resynchronisation, retries, latches, setters, ready waits, bus requirements, an independent chip model and pin idle levels. Use when writing or reviewing a driver's protocol, start-up, setters or port, and before its first release.
---

# Hardware lessons review

Each step is one handbook rule (Rules > Architecture, C++ code, Testing, Hardware and porting). For each, find
where the driver does it, and the model test in `tests/` that proves it. A rule with no code path and no test is
a finding. Work from the chip's datasheet (the `datasheet-conformance` skill), never from another driver.

## Replies, retries and resynchronisation

1. **A reply is accepted only when the protocol proves it answers the request** (KB-ARCH-042). Every check the
   protocol offers runs before decoding: CRC or checksum, reply type and width, the echoed opcode, address or
   tag. A failed check returns `Crc` or a chip-specific mismatch, or retries per step 3; it is never decoded.
2. **A pipelined exchange completes inside one public call** (KB-ARCH-043). If the chip answers a frame late, the
   same call sends the request and collects the reply, matching them by content, never by position; the Porting
   page says no other access may come between the frames.
3. **Retries repeat only idempotent frames, are bounded and counted** (KB-ARCH-044). No retry repeats a reset
   pulse, an enable toggle or a deinitialise; an unknown write outcome re-sends only the reply request; the loop
   has a fixed bound and the count is visible to the caller or in diagnostics.
4. **A reply a stuck or floating line can produce is never data** (KB-CPP-116). All zeros, all ones, the command
   echoed back, the same word repeated: each maps to `NoDevice` or another error, and a ready or idle status a
   line could fake is confirmed by a second read. The model has a fault mode for each pattern.
5. **After a failed transfer the chip's protocol state is restored first** (KB-CPP-118): a harmless no-op, a frame
   that closes an open sequence, or the command byte again, before any new command.

## Start-up

6. **The identity check is one a dead or looped bus cannot pass** (KB-CPP-117): an echo, a bit the chip toggles,
   or a write and read-back; a partial, shifted or phantom ID is refused.
7. **Start-up waits for the chip's own ready indication** (KB-CPP-124): the datasheet minimum first, then a
   bounded poll with a deadline from `Config`; a correct ID is not readiness, and a delay never returns early.
8. **The variant is confirmed before any variant-specific write** (KB-CPP-125).
9. **Load-dependent diagnostics run only on channels `Config` declares as loaded** (KB-CPP-131), and the fault
   pin routing comes from the same declaration, before the chip is armed.

## State, shadows and latches

10. **Clear-on-read and write-1-to-clear flags collect in one driver-owned latch** (KB-CPP-119): every internal
    read ORs them in; a `Get*` accessor without I/O reads it and an explicit `Clear*` empties it.
11. **A setter updates the driver's copy only after the chip accepted the change** (KB-CPP-120), and **changes
    only what it names** (KB-CPP-121); a state-gated change outside its state returns an error and sends nothing.
12. **Control and output registers are written whole from the validated copy** (KB-CPP-122); a read-modify-write
    uses only a read validated per step 1. **Except a bit the chip changes on its own** (a channel it switches off
    for a fault, a mode it drops on a supply or watchdog fault, a status bit): the copy never sets such a bit the
    call did not ask for. Reconcile it from the chip first (the reply to the write, or a validated read), and
    believe "on" only when both the written value and the chip say so. A clear-only change (switching off) is
    written even when that read fails.

12a. **A device reset is detected, and old state is never written over it.** List every way the chip can reset or
    reload itself (power-on reset, brown-out, UVLO, a watchdog, an OTP reload) and the evidence it leaves: a
    status bit such as ACTIVE=0 or a POR/RES event, an error flag, a register back at its reset value (a sentinel),
    an invalid first conversion. On that evidence the driver marks itself uninitialised and returns its reset
    error, so the application runs `EnsureInitialized()` again; a setter must not first write cached state into
    the reset chip (check the reply of the command phase before the data phase). Setters that can turn a pin or a
    channel into an output write in the datasheet's programming order, so even an undetected reset never leaves
    an output at its reset level. A reset check that is due but cannot be read (a lost status read, a failed
    sentinel compare) stays pending, and no call returns a value as Ok until it has passed: a call that would
    succeed fails with the check's own error instead, a call that failed keeps its own error.
12b. **A fault the chip clears by itself still reaches the latch.** Status or summary bits the chip returns for
    free with every frame (status bytes, error fields) are ORed into the latch; a fault that auto-clears on
    turn-on or on a new cycle is otherwise never seen. A clear writes exactly the bits just read, never a fixed
    mask; a clear-on-read register is read once, never retried; a failed read of one records "faults may be lost".
12c. **A write the chip may have dropped is not Ok.** A frame-count or command error the chip reports later makes
    the bits of the last unverified write uncertain, and the next call returns an error until they are read back.
    The converse holds too: **a write whose reply failed may have taken effect.** Every cached value a guard trusts
    without I/O (a setpoint, an enable, a mode) then takes the more dangerous of the old and new values, or is
    marked unknown until a validated read or a successful write; a guard that depends on it refuses and sends
    nothing. **No uncertainty ever refuses a switch-off or a release to input**: an off request on a channel, a
    pair or a bridge in doubt switches off everything it could be (both channels of a pair; never one bit of a
    bridge, which can turn Brake into Reverse), and releasing a pin to input is the cheapest, most robust write the
    driver has. Each uncertainty flag gets a test that switch-off still works with it set. A call that changes several
    channels at once (a mask, a bank write) still applies every switch-off when it refuses a switch-on, and an off
    request in any mode the driver knows (a bridge, a pair) is never an invalid argument. Prefer the simplest
    conservative rule that can be proven (for example "an unknown register back at its power-up value is a reset")
    over logic that tries to explain an uncertain state away. A channel in doubt counts as on: a "nothing changed,
    send nothing" shortcut compares the request against every bit that may be on, not only the copy, and a refused
    switch-on in a group whose mode is unknown leaves that group off (the one state that means the same in every
    mode). A failed read-back of a write keeps the written channels in doubt; it never confirms the copy.
    A switch-off is sent first as the smallest write that is stored whole or not at all (one data byte, one
    frame), never as the tail of a multi-byte burst: when the chip applies the bytes it accepted before a NACK, a
    half-written "off" can leave a channel more active than before. A switch-off, a release and `Deinitialize()`
    are never refused because the driver is uninitialised after a reset report: once the driver has started, they
    always send their writes, and `Deinitialize()` never returns Ok without having done its work.
    Doubt is moved, never dropped: a write that settles a doubt (a channel that may be on, a mode that may be
    either) keeps it as "unverified" until the chip vouches for that write, because the settling write itself
    can be the one the chip ignored. Once nothing can vouch for a write any more (a sticky error flag already
    set), configuration changes are read back or stay in doubt; a configuration write stops when its own reply
    first shows the error, but a switch-off is never stopped.
12d. **A frame with no integrity check never decides a state change alone.** A critical-fault frame, a reset
    indication or a mode change without a CRC is confirmed by a second frame before the driver acts on it; acting
    means making the outputs safe (EN low, driver uninitialised).

## Values and diagnostics

13. **Values are checked against the operating range, never silently clamped** (KB-CPP-126); a quantised value
    reports what was programmed.
14. **A state that is not a measurement is an error, never a number** (KB-CPP-127): settling sentinels, frozen
    feedback, the first conversion after reset, a sensor not fitted; a multi-item read reports validity per item.
15. **A conversion that depends on another setting uses the chip's actual setting** (KB-CPP-128).
16. **Single-shot raw access and the last raw reply** (KB-CPP-129): a raw access with no retry and a `GetLast*`
    accessor with no I/O; side effects of diagnostic reads are documented. The source check `raw-access` checks
    the declarations.

## The bus and the port

17. **The bus requirements are `constexpr` in the bus interface header** (KB-CPP-123): SPI mode, bit order,
    clock, CS timing and continuous-word need; I2C address, clock and repeated START; UART baud rate and
    turnaround. The example ports `static_assert` their settings against them; the source check
    `bus-requirements` checks the declarations.
18. **I2C** (KB-CPP-130): NACK-by-design transfers are documented and not reported as bus errors, no empty or
    zero-length probe is ever sent, and a combined write-read is used wherever the chip needs a repeated START.
19. **Control outputs reach their inactive level before they are enabled** (KB-HW-018): the port drives reset,
    enable, soft chip-select and write-protect to INACTIVE before enabling the output and before the first frame;
    the Porting page states each pin's idle level and polarity.
20. **Port conformance covers the shared-bus faults** (KB-HW-019): the first frame after a mode or clock change is
    intact, chip-selects idle before any driver starts, UART input is flushed before a transmit and never between
    transmit and receive, an I2C bus clear runs before a retried start-up, every operation has a timeout, and
    transfer buffers live in memory the bus and its DMA reach (owned by the driver object, never on a caller's
    stack or in a function-local static).

## Tests

21. **Hardware findings become model faults and tests; the model stands alone** (KB-TST-030). Each behaviour
    found on hardware is a fault mode of `tests/mock_max22200.hpp` and a regression test that fails on the code
    before the fix, citing the datasheet. The model carries its own register map with datasheet references (it
    does not include the driver's register header), a test cross-checks the two maps with `static_assert`, and
    a chip with timing has a virtual clock. Evidence from a bench or a customer stays out of the repository.
22. **The model changes state on its own, as the datasheet says the chip does.** A model that changes a register only
    when the driver writes it cannot catch rules 12, 12a or 12b. It offers a power-on reset or brown-out mid session
    (registers back to reset values, the datasheet's reset flags set), fault shutdowns and the bits they clear, faults
    the chip clears by itself, a dropped or miscounted write, and a clock that advances with every frame (so watchdogs
    and timeouts are real). Where the datasheet is ambiguous (for example whether a write's reply shows the register
    before or after the write), the model offers both readings and the safety tests run under each. Every timing the
    datasheet leaves open gets its own knob: each status flag updates on its own delay, not tied to another one (a mode
    shown at once, its "valid" flag later), and a low-power mode the datasheet does not say accepts writes is modelled
    dropping them; a check read once is polled to its deadline before it counts as a refusal. A fuzz target asserts the
    safety rules too (no switch-on while a pair is in doubt, no write after a reset is seen); when a fix changes what a
    call may send, replace its invariant with the weaker true one, never delete it. Skip an invariant only for the call
    whose own fault the driver cannot see (a miscount armed for that call, a dropped write, a deaf line), and check it
    again after a successful `EnsureInitialized()`: a skip that, once set, lasts for the rest of the input leaves
    exactly the fault paths unfuzzed.
23. **An independent review before the first release.** Someone who did not write the driver or its model checks
    both against the datasheet (the `safety-reviewer` agent): a model written by the same author shares the
    author's assumptions, and green tests cannot show what both got wrong.

## Report

One line per finding: rule id, `file:line` (or the missing test), what the hardware would do, and the fix with
the test that proves it, most severe first (a value returned as data when it is not, then lost or repeated
frames, then missing diagnostics). Mark anything only a bench can confirm as **not verified on hardware**, with
the bench steps.
