#!/usr/bin/env python3
"""Source layout: one source per header with the same stem, SDK code only in src/boundary/ (Python 3.9+, stdlib).

    python3 -I tools/check_layout.py                  # the repository this file is in
    python3 -I tools/check_layout.py --root DIR
    python3 -I tools/check_layout.py --selftest

Rules (the exceptions are in tools/layout.yml, each with its reason; the file is optional):
1. Every file of src/ (.cpp, .ipp, .hpp) has the stem of a public header under inc/, or is listed under `internal`.
   A source that serves two headers (a combined file) matches no header and is reported: split it, one source per
   header, named after it.
2. Every public header has a source of its stem in src/, or is header-only: a header whose stem ends in _types,
   _detail, _registers, _kconfig, _version, _interface (declarations, constants and the port contract), a
   template-only header listed under `header_only` with a reason, or a library whose src/ does not exist.
3. Every file of src/boundary/ has the stem of a public header (the SDK component it wraps), or is listed under
   `boundary_extra`: one file per SDK component, named after it.
4. Includes of an SDK (ESP-IDF, FreeRTOS, vendor HALs; not the generated sdkconfig.h, which only the
   <name>_kconfig.hpp header reads) are allowed only in src/boundary/ and examples/<platform>/;
   inc/ and the rest of src/ are platform-free.
5. Every entry of tools/layout.yml names a file that exists and has a reason.
Prints `path: message` per finding and exits 1 on one. A line that must include an SDK header carries
`// layout: sdk-ok <reason>`.
"""
from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER_SUFFIXES = {".hpp", ".h"}
SOURCE_SUFFIXES = {".cpp", ".ipp", ".hpp", ".cc", ".tpp"}
HEADER_ONLY_SUFFIXES = ("_types", "_detail", "_registers", "_kconfig", "_version", "_interface")
SDK_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"]((?:esp_|freertos/|driver/|hal/|soc/|nvs|FreeRTOS|task\.h|stm32|cmsis|core_cm|'
    r"pico/|hardware/|nrf|zephyr/|mbed|Arduino|rtos/|tx_api|bsp/)[^>\"]*)[>\"]", re.M)
TABLES = ("header_only", "internal", "boundary_extra")


def read_layout(root: Path) -> tuple[dict[str, dict[str, str]], list[str]]:
    """tools/layout.yml: three tables of `stem: reason` (a tiny YAML subset: no PyYAML needed)."""
    tables: dict[str, dict[str, str]] = {t: {} for t in TABLES}
    errors: list[str] = []
    path = root / "tools/layout.yml"
    if not path.is_file():
        return tables, errors
    current = None
    stem = None
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#") or line == "---":
            continue
        m = re.fullmatch(r"([a-z_]+):\s*(\{\})?", line)
        if m and m.group(1) in TABLES:
            current, stem = m.group(1), None
            continue
        m = re.fullmatch(r"  ([A-Za-z0-9_]+):\s*(.*)", line)
        if m and current:
            stem = m.group(1)
            reason = m.group(2).strip().lstrip(">-|").strip()
            tables[current][stem] = reason
            continue
        m = re.fullmatch(r"    (\S.*)", line)
        if m and current and stem:  # a folded continuation line of the reason
            tables[current][stem] = (tables[current][stem] + " " + m.group(1)).strip()
            continue
        errors.append(f"tools/layout.yml:{n}: not understood: {line.strip()}")
    return tables, errors


def check(root: Path) -> list[str]:
    tables, bad = read_layout(root)
    for name, table in tables.items():
        for stem, reason in table.items():
            if not reason:
                bad.append(f"tools/layout.yml: {name}.{stem} has no reason")
    inc, src = root / "inc", root / "src"
    headers = {p.stem for p in inc.rglob("*") if p.suffix in HEADER_SUFFIXES} if inc.is_dir() else set()
    # `x.h.in` (a generated header) has the stem of the header it makes.
    headers |= {Path(p.stem).stem for p in inc.rglob("*.in")} if inc.is_dir() else set()
    sources = {p.stem for p in src.glob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES} if src.is_dir() else set()
    boundary = ({p.stem for p in (src / "boundary").glob("*") if p.suffix in SOURCE_SUFFIXES}
                if (src / "boundary").is_dir() else set())
    if src.is_dir():
        for stem in sorted(headers):
            if stem in sources or stem in tables["header_only"] or stem.endswith(HEADER_ONLY_SUFFIXES):
                continue
            bad.append(f"inc: {stem} has no src/{stem}.cpp (or .ipp); add it, or list {stem} under header_only in "
                       "tools/layout.yml with the reason it needs no source")
        for stem in sorted(sources):
            if stem not in headers and stem not in tables["internal"]:
                bad.append(f"src/{stem}.*: matches no public header and is not listed under internal in "
                           "tools/layout.yml (one source per header, named after it: split a combined file)")
    for stem in sorted(boundary):
        if stem not in headers and stem not in tables["boundary_extra"]:
            bad.append(f"src/boundary/{stem}.*: matches no public header and is not listed under boundary_extra "
                       "(one file per SDK component, named after it)")
    for stem in sorted(tables["header_only"]):
        if stem not in headers:
            bad.append(f"tools/layout.yml: header_only.{stem} names no public header")
        elif stem in sources:
            bad.append(f"tools/layout.yml: header_only.{stem} has a source now; remove the entry")
    for stem in sorted(tables["internal"]):
        if stem not in sources:
            bad.append(f"tools/layout.yml: internal.{stem} names no file of src/")
    for stem in sorted(tables["boundary_extra"]):
        if stem not in boundary:
            bad.append(f"tools/layout.yml: boundary_extra.{stem} names no file of src/boundary/")
    for folder in (inc, src):
        for path in sorted(folder.rglob("*")) if folder.is_dir() else []:
            if not path.is_file() or "boundary" in path.relative_to(root).parts:
                continue
            for m in SDK_INCLUDE.finditer(path.read_text(encoding="utf-8", errors="replace")):
                line = path.read_text(encoding="utf-8", errors="replace").count("\n", 0, m.start()) + 1
                text = path.read_text(encoding="utf-8", errors="replace").splitlines()[line - 1]
                if "layout: sdk-ok" in text:
                    continue
                bad.append(f"{path.relative_to(root).as_posix()}:{line}: includes the SDK header {m.group(1)}; SDK "
                           "code goes in src/boundary/<component>.cpp (one file per component)")
    return bad


def selftest() -> int:
    layout = "header_only:\n  chip_extra: constants only\ninternal:\n  shared: the seam\nboundary_extra: {}\n"
    base = {"inc/chip.hpp": "", "inc/chip_types.hpp": "", "inc/chip_spi_interface.hpp": "", "inc/chip_extra.hpp": "",
            "inc/chip_version.h.in": "", "src/chip.ipp": "", "src/shared.hpp": "", "tools/layout.yml": layout}

    def tree(tmp: Path, add: dict[str, str] | None = None, drop: tuple[str, ...] = ()) -> Path:
        files = {**base, **(add or {})}
        for rel in drop:
            files.pop(rel)
        for rel, text in files.items():
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
            (tmp / rel).write_text(text, encoding="utf-8")
        return tmp

    cases = [
        ("good", {}, (), None),
        ("header without a source", {"inc/chip_more.hpp": ""}, (), "chip_more has no src"),
        ("combined source", {"src/chip_and_more.cpp": ""}, (), "matches no public header"),
        ("source without header", {}, ("inc/chip.hpp",), "src/chip.*"),
        ("boundary file not named after a header", {"src/boundary/two_things.cpp": ""}, (), "one file per SDK"),
        ("boundary file named after a header", {"src/boundary/chip.cpp": ""}, (), None),
        ("sdk include in src", {"src/chip.ipp": '#include "esp_log.h"\n'}, (), "esp_log.h"),
        ("sdk include in inc", {"inc/chip_types.hpp": "#include <freertos/FreeRTOS.h>\n"}, (), "freertos"),
        ("sdk include allowed in boundary", {"src/boundary/chip.cpp": '#include "esp_log.h"\n'}, (), None),
        ("sdk include with the marker", {"src/chip.ipp": '#include "esp_log.h"  // layout: sdk-ok\n'}, (), None),
        ("stale header_only entry", {"src/chip_extra.cpp": ""}, (), "has a source now"),
        ("stale internal entry", {}, ("src/shared.hpp",), "names no file of src"),
        ("entry without a reason", {"tools/layout.yml": layout.replace("constants only", "")}, (), "no reason"),
    ]
    ok = True
    for label, add, drop, want in cases:
        with tempfile.TemporaryDirectory() as tmp:
            bad = check(tree(Path(tmp), add, drop))
        if bool(bad) if want is None else not any(want in b for b in bad):
            ok = False
            print(f"selftest FAILED: {label}: expected {want!r}, got {bad}")
    with tempfile.TemporaryDirectory() as tmp:  # a header-only library: no src/, no layout.yml
        (Path(tmp) / "inc/lib").mkdir(parents=True)
        (Path(tmp) / "inc/lib/Thing.h").write_text("", encoding="utf-8")
        if check(Path(tmp)):
            ok = False
            print("selftest FAILED: a library without src/")
    print("check_layout selftest " + ("passed" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    root = Path(argv[argv.index("--root") + 1]).resolve() if "--root" in argv else ROOT
    bad = check(root)
    for line in bad:
        print(line)
    if bad:
        print(f"check_layout: {len(bad)} finding(s)")
        return 1
    if "--quiet" not in argv:
        print("check_layout: one source per header, SDK code only in src/boundary/")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
