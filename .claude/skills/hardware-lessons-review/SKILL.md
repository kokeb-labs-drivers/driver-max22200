---
name: hardware-lessons-review
description: Review a driver against the handbook rules learnt on real hardware, as a checklist of exact questions tied to the catalogue of recurring defects - reply validity, stuck lines, resets, setters, switch-off, buffers, state machines, model independence. Use when writing or reviewing a driver's protocol, start-up, setters or port, and before its first release.
---

# Hardware lessons review

Each item is one handbook rule with the exact question to ask. For each, find the code path and the model test that
proves it; a rule with no code path and no test is a finding. Work from the datasheet (`datasheet-conformance`), never
from another driver. Ids RD-nn are in [`docs/recurring-defects.md`](../../../docs/recurring-defects.md); where each
class is caught is in [`docs/mechanization-policy.md`](../../../docs/mechanization-policy.md).

## Already enforced: do not redo by eye

Host tests, ASan/UBSan and libFuzzer (RD-30), the handbook source checks `raw-access` and `bus-requirements`
(item 16, 17), the gate (RD-19, RD-24), standalone naming. Check only that the tests below *exist*; run them, do not
re-derive them.

## Replies, retries, resynchronisation

1. **Reply validity (KB-ARCH-042, RD-02).** Every check the protocol offers runs before decoding (CRC, type, width,
   echoed opcode or tag). Ask: "Which reads decide something, and what removes each validation?" A failed check
   returns an error or retries per 3; it is never decoded.
2. **Pipelined exchange (KB-ARCH-043).** A late reply is collected inside the same public call, matched by content.
3. **Retries (KB-ARCH-044).** Only idempotent frames, bounded, counted. No retry of a reset pulse, enable toggle or
   deinitialise; an unknown write outcome re-sends only the reply request.
4. **Stuck or floating line (KB-CPP-116, RD-03).** Zeros, ones, echo, repeated word each map to an error; a ready status
   a line could fake is confirmed by a second read. Ask: "For each read path, which pattern does the CRC pass?" The
   model has one fault per pattern applied to every read function.
5. **After a failed transfer (KB-CPP-118)** the chip's protocol state is restored first (no-op, closing frame, command
   byte again).

## Start-up

6. **Identity (KB-CPP-117):** a dead or looped bus cannot pass it. 7. **Ready (KB-CPP-124):** the chip's own indication,
   minimum delay then a bounded poll. 8. **Variant (KB-CPP-125)** confirmed before variant-specific writes.
9. **Load-dependent diagnostics (KB-CPP-131)** only on channels `Config` declares loaded.

## State, shadows, latches

10. **Latches (KB-CPP-119, RD-31).** Clear-on-read and write-1-to-clear flags collect in one driver-owned latch; a
    self-check or raw read never discards one. Test: read through every diagnostic path, fault still reported.
11. **Setters (KB-CPP-120, 121, RD-04, RD-10).** The copy updates after the chip accepted; a setter changes only what it
    names; a call returning `InvalidArgument` or a state error sent nothing (frame count unchanged). Ask: "For each
    early return, what is the first line above it that touches the port?"
12. **Whole-register writes (KB-CPP-122).** Written from the validated copy; a read-modify-write uses only a validated
    read; a bit the chip changes by itself (fault shutdown, mode drop) is reconciled from the chip first and "on" is
    believed only when both the write and the chip say so.
12a. **Unseen reset (RD-01).** Ask: "List every way this chip resets or reloads; for each, which bit or sentinel shows it,
    which function reads it, and what does a setter write between the reset and that read?" On evidence the driver
    marks itself uninitialised and returns its reset error; a setter checks the command phase reply before the data
    phase; outputs are written in the datasheet's programming order; a due-but-unreadable check stays pending and no call
    returns Ok until it passes.
12b. **Self-clearing faults** reach the latch: free status bytes are ORed in; a clear writes exactly the bits just read;
    a clear-on-read register is read once; a failed read records "faults may be lost".
12c. **Uncertain writes and switch-off (RD-05, RD-02).** A write whose reply failed may have taken effect: each cached
    value a guard trusts takes the more dangerous of old and new until a validated read. **No uncertainty ever refuses a
    switch-off, a release to input or `Deinitialize()`**; they always send, in the smallest atomic write, never as the tail
    of a burst, never one bit of a bridge. Ask: "Name each flag that can make a call return early; show the test where it
    is set and Off still sends." A channel in doubt counts as on; a doubt is moved, never dropped, until the chip vouches
    for the settling write.
12d. **No integrity check, no state change.** A critical-fault frame, reset indication or mode change without CRC is
    confirmed by a second frame, then outputs are made safe.
12e. **Complete guards (RD-10).** Ask: "Draw the states from the datasheet (reset, asleep, powered off, pending timer, in
    doubt); for each command and state, where is the guard and which test removes it?" A start-up that leaves a pending
    timer or delay unchecked must not energise; a power gate starts closed when the port has the hook.

## Values and diagnostics

13. **Range (KB-CPP-126):** checked, never silently clamped. 14. **Non-measurement (KB-CPP-127):** settling sentinel,
    frozen feedback, first conversion, sensor not fitted are errors, not numbers. 15. **Dependent conversion
    (KB-CPP-128)** uses the chip's actual setting. 16. **Raw access (KB-CPP-129):** single-shot, `GetLast*` without I/O.

## Bus and port

17. **Requirements (KB-CPP-123)** are `constexpr` in the bus header; the example ports `static_assert` them.
18. **I2C (KB-CPP-130):** NACK-by-design documented, no empty probe, repeated START where needed.
19. **Idle levels (KB-HW-018):** reset, enable, chip-select, write-protect inactive before enable and first frame.
20. **Shared bus (KB-HW-019, RD-06).** Chip-selects idle before any start; UART flushed before transmit; I2C bus clear before
    a retried start-up; timeouts everywhere. Ask: "Where does the memory of each buffer passed to the port live?" It is a
    member of the driver object, not a local or a function-local static; the mock port checks the address range.
21. **Example starts (RD-21).** The ESP32 example with default pins and `Config` reaches the first measurement; an optional
    hook returns `true` when its pin is absent.

## Tests and models

22. **Stand-alone model (KB-TST-030, RD-07).** Its own register map with datasheet references, no driver include,
    `static_assert` cross-check, golden frames from the datasheet, virtual clock.
23. **The model changes state on its own (RD-01, RD-02, RD-03).** Power-on reset or brown-out mid-session, fault shutdowns,
    self-clearing faults, a dropped or miscounted write, one flipped reply at call k, both readings of an ambiguous
    datasheet (the safety tests run under each), separate delays per status flag.
24. **Guards are killed (RD-25).** One-edit mutants (negate, drop, swap constant) of every guard on a write, state or
    value are killed by a unit test; surviving mutants are findings.
25. **Fuzz asserts the safety rules (RD-26).** No write after a reset is seen, no switch-on in doubt, no frame after an
    argument error; a mutant of each invariant fails the target.
26. **Independent review (RD-07).** Someone who wrote neither driver nor model checks both against the datasheet; green
    tests cannot show what both got wrong.

## Report

Review-round format, ranked: `BLOCKER` (a wrong value returned as data, a switch-off refused, a write over an unseen
reset, out-of-bounds), `MAJOR` (a missing guard or test, a survivor that matters), `minor`. Each: catalogue id and rule
number, `file:line` (or the missing test), the failing scenario (what the hardware would do), the fix with the test
that proves it. List documented ambiguities with knobs, and mark what only a bench can confirm **not verified on
hardware** with the bench steps. End with one line: `NO BLOCKER` or `BLOCKERS REMAIN`.
