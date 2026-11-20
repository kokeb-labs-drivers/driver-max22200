#!/usr/bin/env python3
"""The hardware validation level and the safety class are stated once and agree everywhere (Python 3.9+, standard library only).

    python3 -I tools/check_hardware.py                # the repository this file is in
    python3 -I tools/check_hardware.py --root DIR
    python3 -I tools/check_hardware.py --selftest

The `hardware-validation` skill defines the levels H0 (host model only), H1 (bench rig exercised, logs kept) and
H2 (verified by a hardware engineer). driver.yml carries `hardware: {level, rig, evidence, reviewed}`; this checks

1. a driver with a driver.yml has a `hardware.level` of H0, H1 or H2 (a missing block is reported as a note, the level
   is then taken as H0, so a driver not yet updated still passes);
2. the README shows exactly one hardware badge, for that level;
3. H1 and H2 name a rig document that exists, with the headings the skill requires, and an evidence entry;
4. H2 also names the review it applies to (`reviewed`).
5. `safety.class` is passive, sensing or actuating (a missing block is a note): what the chip path can do to the
   physical world. passive: nothing a safety decision rests on (memory, display, communication). sensing: a measurement
   one may rest on, so a wrong value is the hazard. actuating: it can move, energise, heat or switch something, so a
   wrong command is the hazard. It is the driver's own class; the product that uses it classifies itself (KB-ARCH-030).
A library has no driver.yml and is not checked. Prints `path: message` per finding; exit status 1 on one.
"""
from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEVELS = ("H0", "H1", "H2")
BADGE = re.compile(r"img\.shields\.io/badge/hardware-(H\d)")
SAFETY_CLASSES = ("passive", "sensing", "actuating")
RIG_HEADINGS = ("Purpose", "Equipment", "Wiring", "Bus settings", "Firmware", "Steps and results",
                "Discrepancies with the model", "Not covered")


def block(text: str, name: str = "hardware") -> dict[str, str] | None:
    """The `name:` mapping of driver.yml: indented `key: value` lines under it."""
    m = re.search(rf"^{name}:[ \t]*(?:#.*)?\n((?:[ \t]+\S.*\n?)+)", text, re.M)
    if not m:
        return None
    fields: dict[str, str] = {}
    for line in m.group(1).splitlines():
        kv = re.fullmatch(r"\s+([a-z_]+):\s*(.*?)\s*(?:#.*)?", line)
        if kv:
            value = kv.group(2).strip().strip("\"'")
            fields[kv.group(1)] = "" if value in ("null", "~") else value
    return fields


def check(root: Path) -> tuple[list[str], list[str]]:
    manifest = root / "driver.yml"
    if not manifest.is_file():
        return [], []
    bad: list[str] = []
    notes: list[str] = []
    text = manifest.read_text(encoding="utf-8")
    safety = block(text, "safety")
    if safety is None:
        notes.append("driver.yml: no safety block (add `safety: {class: passive | sensing | actuating}`, "
                     "hardware-validation skill)")
    elif safety.get("class") not in SAFETY_CLASSES:
        bad.append(f"driver.yml: safety.class is {safety.get('class')!r}, expected one of {', '.join(SAFETY_CLASSES)}")
    fields = block(text)
    level = "H0"
    if fields is None:
        notes.append("driver.yml: no hardware block; H0 assumed (add `hardware: {level: H0}`, hardware-validation skill)")
    else:
        level = fields.get("level", "")
        if level not in LEVELS:
            return bad + [f"driver.yml: hardware.level is {level!r}, expected one of {', '.join(LEVELS)}"], notes
    readme = root / "README.md"
    badges = BADGE.findall(readme.read_text(encoding="utf-8")) if readme.is_file() else []
    if fields is not None:
        if badges != [level]:
            bad.append(f"README.md: hardware badge {badges or 'missing'}, driver.yml says {level} "
                       "(one badge, for that level; see the hardware-validation skill)")
    if level in ("H1", "H2"):
        rig = fields.get("rig", "")
        rig_path = root / rig if rig else None
        if not rig_path or not rig_path.is_file():
            bad.append(f"driver.yml: hardware.rig must name an existing rig document at {level} (got {rig!r})")
        else:
            text = rig_path.read_text(encoding="utf-8")
            for heading in RIG_HEADINGS:
                if not re.search(rf"^##\s+{re.escape(heading)}\s*$", text, re.M):
                    bad.append(f"{rig}: no `## {heading}` section")
        if not fields.get("evidence"):
            bad.append(f"driver.yml: hardware.evidence is empty at {level} (the logs of the run)")
    if level == "H2" and not fields.get("reviewed"):
        bad.append("driver.yml: hardware.reviewed is empty at H2 (the engineer's review and the release it covers)")
    return bad, notes


def selftest() -> int:
    rig = "# Bench rig\n" + "".join(f"\n## {h}\ntext\n" for h in RIG_HEADINGS)

    def tree(tmp: Path, level: str, badge: str, extra: str = "", rig_text: str | None = None) -> Path:
        (tmp / "driver.yml").write_text(f"name: x\nhardware:\n  level: {level}   # note\n{extra}", encoding="utf-8")
        (tmp / "README.md").write_text(f"[![Hardware](https://img.shields.io/badge/hardware-{badge}%20x-grey)](a)\n"
                                       if badge else "no badge\n", encoding="utf-8")
        if rig_text is not None:
            (tmp / "docs").mkdir(exist_ok=True)
            (tmp / "docs/bench-rig.md").write_text(rig_text, encoding="utf-8")
        return tmp

    h1 = "  rig: docs/bench-rig.md\n  evidence: bench-logs-v0.1.0-rc.2.txt\n"
    cases = [
        ("H0 good", ("H0", "H0"), None),
        ("badge for another level", ("H0", "H1"), "hardware badge"),
        ("no badge", ("H0", ""), "hardware badge"),
        ("invalid level", ("H3", "H3"), "expected one of"),
        ("H1 without a rig", ("H1", "H1"), "hardware.rig"),
        ("H1 good", ("H1", "H1", h1, rig), None),
        ("H1 rig missing a section", ("H1", "H1", h1, rig.replace("## Wiring", "## Wires")), "Wiring"),
        ("H1 without evidence", ("H1", "H1", "  rig: docs/bench-rig.md\n", rig), "evidence"),
        ("H2 without a review", ("H2", "H2", h1, rig), "reviewed"),
        ("H2 good", ("H2", "H2", h1 + "  reviewed: hardware engineer review of v0.1.0-rc.3\n", rig), None),
    ]
    ok = True
    for label, args, want in cases:
        with tempfile.TemporaryDirectory() as tmp:
            bad, _ = check(tree(Path(tmp), *args))
        if bool(bad) if want is None else not any(want in b for b in bad):
            ok = False
            print(f"selftest FAILED: {label}: expected {want!r}, got {bad}")
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "driver.yml").write_text("name: x\n", encoding="utf-8")
        bad, notes = check(Path(tmp))
        if bad or not notes:
            ok = False
            print("selftest FAILED: a driver.yml without the block is a note, not an error")
    for label, extra, want in (("safety good", "safety:\n  class: sensing   # note\n", None),
                               ("safety invalid", "safety:\n  class: dangerous\n", "safety.class"),
                               ("safety empty", "safety:\n  class:\n", "safety.class")):
        with tempfile.TemporaryDirectory() as tmp:
            root = tree(Path(tmp), "H0", "H0")
            (root / "driver.yml").write_text((root / "driver.yml").read_text(encoding="utf-8") + extra, encoding="utf-8")
            bad, notes = check(root)
        if bool(bad) if want is None else not any(want in b for b in bad):
            ok = False
            print(f"selftest FAILED: {label}: expected {want!r}, got {bad}")
        if want is None and any("safety" in n for n in notes):
            ok = False
            print("selftest FAILED: a safety block is not noted as missing")
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "driver.yml").write_text("name: x\nhardware:\n  level: H0\n", encoding="utf-8")
        (Path(tmp) / "README.md").write_text("[![Hardware](https://img.shields.io/badge/hardware-H0%20x-grey)](a)\n")
        bad, notes = check(Path(tmp))
        if bad or not any("safety" in n for n in notes):
            ok = False
            print("selftest FAILED: a missing safety block is a note, not an error")
    with tempfile.TemporaryDirectory() as tmp:
        if check(Path(tmp)) != ([], []):
            ok = False
            print("selftest FAILED: a library (no driver.yml) is not checked")
    print("check_hardware selftest " + ("passed" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    root = Path(argv[argv.index("--root") + 1]).resolve() if "--root" in argv else ROOT
    bad, notes = check(root)
    for line in notes:
        print(f"note: {line}")
    for line in bad:
        print(line)
    if bad:
        print(f"check_hardware: {len(bad)} finding(s)")
        return 1
    if "--quiet" not in argv:
        print("check_hardware: the hardware level, the badge, the evidence and the safety class agree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
