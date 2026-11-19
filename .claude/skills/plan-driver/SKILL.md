---
name: plan-driver
description: Decide, before anything is scaffolded, whether a chip needs a new driver, an extension of an existing driver family, or nothing (a duplicate), and fix the public API shape. Surveys the organisation's drivers and comparable open-source drivers read-only, and writes placement.md with a verdict (NEW, EXTEND:<driver>, DUPLICATE:<driver> or BLOCKED) that the next step consumes. Use first, before new-driver-from-template.
---

# Plan a driver

Run this once per requested chip, before `new-driver-from-template`. It produces one file, `placement.md`, and
changes no repository. A planner reads, compares and decides; it does not scaffold, download a datasheet PDF into a
repository, or copy code.

## 1. Pin the request

Write down, from the request and the vendor's product page: the exact part number and every orderable variant, the
vendor, the bus (`spi`, `i2c`, `uart`, `adc`, `none`, ...), the chip's function in one sentence, and whether it
drives anything that can hurt (a motor, a heater, a valve, a high voltage: those drivers get the `safety` rules in
`hardware-lessons-review` and the strictest API shape, section 4). Find the datasheet (vendor site, no login, no
bypass of a 403: if the vendor hides it, the verdict is BLOCKED) and record vendor, document number, revision and
the URL. Do not download the PDF into a repository. Reading it in a scratch folder is fine.

## 2. Survey what exists (read-only, cited)

Cite every source as a URL or `org/repo@commit` and note its licence. Never copy code, register tables or prose from
a source: take facts from the datasheet and write the driver from them (the `datasheet-conformance` skill).

1. **The organisation's drivers.**
   `gh repo list kokeb-labs-drivers --limit 200 --json name,description,visibility,repositoryTopics` lists them.
   For each candidate read `driver.yml` (`gh api repos/kokeb-labs-drivers/<repo>/contents/driver.yml --jq .content |
   base64 -d`), the README's chip summary and `docs/datasheets.yml` (which datasheet and which topics it covers).
   Also search by part-number stem and vendor: `gh search code --owner kokeb-labs-drivers '<stem>'`. A private
   repository you cannot read is not a gap in the survey: say "unreadable" and put the question to the owner.
2. **The shared layer.** `kokeb-labs/kb-core` holds the concepts (port contracts, handlers, pins) and its
   catalogue lists which drivers exist and which buses they use. Read its `catalog/` and `docs/` to learn which
   concept your bus uses and whether a handler for that class of device exists.
3. **Comparable open-source drivers.** `gh search repos '<part number>' --limit 20 --json fullName,description,license,pushedAt,stargazersCount`
   and the same with the family stem. Add the vendor's own reference driver and the major embedded frameworks'
   drivers if the vendor links them. For each one note: licence (`gh api repos/<o>/<r>/license --jq .license.spdx_id`),
   last activity, language, the shape of its public API (class and method names, how the bus is injected, sync or
   async), which chip features it covers and which it misses, and known open issues about timing or errata. An
   unlicensed or copyleft source is read for its public API shape only; do not read its function bodies.
4. **Errata and vendor notes.** The vendor's errata sheet, application notes and software guide for the part; they
   change the design more often than another driver does.

## 3. Decide the placement

| Verdict | When | What the factory does next |
| --- | --- | --- |
| `NEW` | No driver of the organisation covers this part, or the chip's command set, state machine or protocol differs enough that sharing code would make both worse. | Scaffolds `driver-<chip>` from the template. |
| `EXTEND:<driver>` | The part belongs to the family of an existing driver: the same register map or protocol with variants (suffixes, option bits, a superset), and the existing public API can express the variant without a breaking change. Rule of thumb: the datasheet is the same document or a family document and at least four fifths of the register map and the start-up sequence match. | Opens work on that driver with `add-driver-feature` (a variant selector or new calls); no new repository. |
| `DUPLICATE:<driver>` | An existing driver already supports this exact part (same part number or same datasheet), or the request is the same device under another name. | Stops; links the existing driver; no work. |
| `BLOCKED` | The datasheet cannot be obtained without a login or a bypassed 403, the licence of a needed document is unclear, the part is end-of-life with no documentation, a safety question needs the owner, or a dependency (a missing kb-core concept) is not ready. | Stops and reports `blocked_on` to the owner. |

Decide on evidence, in this order: the part number and datasheet (exact match is DUPLICATE), the register map and
protocol (shared is EXTEND), the differences in start-up, timing or error model (large is NEW). When the evidence
is mixed (for example half the register map shared), choose NEW and list the shared pieces under "Share"
(section 5): two small drivers beat one with a mode flag for each variant. Never choose EXTEND when it would change
an existing driver's public API incompatibly; that is NEW with a note.

## 4. Decide the public API shape

Decide these before the first line of code, and write each with its reason (the architect stage and the reviewers
read this):

- **Bus and port.** Use the template's standard hooks for the bus. List any extra hook (a reset line, a ready pin,
  a chip-select group) and say which are optional. The port is a concept, the driver holds no SDK type.
- **Object model.** One class per chip instance, constructed with the port and a `Config` that holds the datasheet's
  reset values; no I/O in the constructor; `EnsureInitialized()` is the only start-up call. A chip with several
  independent channels or functions is one class with channel arguments, not several classes sharing a bus.
- **Calls.** Every fallible call returns `Result<T>`; units are suffixes (`Mv`, `Ms`, `C`); a read-back or compare is
  a trailing `Verify` argument (KB-ARCH-041). List the calls by chip feature, covering all of the chip, then mark
  which are v0.1.0 and which are later; a feature left out is stated in the docs, never silent.
- **Timing and concurrency.** Which waits are blocking, which have a Start/Poll form, whether the chip needs a
  periodic call, and whether the bus can be shared. The driver never owns a thread, lock or clock.
- **Errors and safety.** Chip-specific `Error` values start at 16. Anything that drives an actuator or a high voltage
  defaults to off, needs an explicit enable call, and has a defined fail-safe state when the bus fails.
- **Model knobs.** The datasheet ambiguities that the host model must make switchable.
- **Examples and platforms.** Host always; ESP32 when the chip is used there; the platforms in `docs/platforms.md`.

## 5. What to share via kb-core

kb-core takes concepts and generic services, never chip code. Mark each item as `use` (exists, cite it), `propose`
(the concept is missing: write a one-paragraph proposal for the kb-core maintainers) or `none`:

- the **port concept** for the bus, and any new hook that more than one driver would need;
- a **handler** (owner task with snapshots, or the single-thread interface) for the device class, if the chip needs
  periodic service or async completion;
- **pins** and shared-line rules (reset, enable, interrupt) the chip needs from the board;
- DMA or timing requirements the bus has (a minimum inter-frame gap, a clock-stretch allowance);
- the catalogue metadata the configurator reads: `driver.yml` fields (`bus`, `mcus`, `peripheral`).

Do not move chip code into kb-core to share it between two drivers; extract the common part into a third small
library only when three drivers need it.

## 6. Write placement.md

Put it where the factory says (default: next to the request, outside every repository). Do not commit it to a
driver. Use exactly this shape; the factory reads the first line and the front matter.

```markdown
---
verdict: NEW            # NEW | EXTEND:<driver> | DUPLICATE:<driver> | BLOCKED
chip: <part number>
repo: driver-<chip>     # NEW only; the extended or duplicate driver otherwise
bus: [spi]
safety: false           # true when it can damage hardware or cause unsafe motion, heat or pressure
blocked_on: null        # BLOCKED only: one sentence, and who can unblock it
---
# Placement: <part number>

## Chip
Part, variants, vendor, one-sentence function. Datasheet: vendor, number, revision, URL (no PDF stored).

## Survey
| Source | Kind | Licence | Covers | Misses | Used for |
| --- | --- | --- | --- | --- | --- |
(organisation drivers, kb-core, open-source drivers, vendor notes; every row cited)

## Verdict
The verdict, the rule from section 3 that applies and the evidence in three to six lines.

## Public API
The decisions of section 4, as a short list with reasons, and the call list by feature (v0.1.0 or later).

## Share
`use`, `propose` or `none` for each item of section 5.

## Open questions
Anything the owner must answer; each as one sentence.
```

Check before handing over: the front matter parses (`python3 -c "import yaml,sys;
yaml.safe_load(open(sys.argv[1]).read().split('---')[1])" placement.md`), every survey row has a source and a
licence, and no row quotes code. A planner that cannot reach a verdict says BLOCKED with the reason; it does not
guess.
