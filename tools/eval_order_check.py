#!/usr/bin/env python3
"""Unspecified evaluation order in a test or fuzz check (docs/recurring-defects.md, RD-34).

The arguments of a call are evaluated in an unspecified order: GCC on x86-64 evaluates them right to left, Clang and
GCC on aarch64 left to right. A call that takes, as separate arguments, both a driver operation (`d.Op(...)`) and a
read of the state that operation changes (`sent()`, `Transfers()`, `pending()`, `++`) passes on one compiler and fails,
or checks nothing, on the other. Compute the result into a local first (`const auto r = d.Op(); Check(r, sent());`).
The same holds for two reads of the fuzzer's input (`in.Byte()`, `FuzzConfig(in)`) in different arguments of one call.
Braced-init lists, `&&`, `||`, `?:` and `,` are sequenced and are not reported.

    python3 -I tools/eval_order_check.py                    # the repository this file is in: tests/ examples/
    python3 -I tools/eval_order_check.py --root DIR         # another tree
    python3 -I tools/eval_order_check.py FILE...            # just these files
    python3 -I tools/eval_order_check.py --selftest         # check the checker on small examples

Prints `file:line: error eval-order: <call>` per finding and exits 1 when there is one. A deliberate exception carries
`// eval-order: ignore` on the line of the call. Standard library only; a token scan without types, so it reports what
it can decide and a clean run does not replace review.
"""
import re
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUFFIXES = {".cpp", ".hpp", ".ipp", ".h", ".tmpl"}
SCAN_DIRS = ("tests", "examples", "tools/scaffold")
PROBE = re.compile(r"\b(?:sent|pending|ensure_failed|Transfers|Sent|Pending|Calls|Writes|Count|count)\s*\(\s*\)"
                   r"|\+\+|--")
OP = re.compile(r"\b(?:d|drv|driver|dev|dut|client|writer)\s*(?:\.|->)\s*[A-Z]\w*\s*\(")
# A read that advances the fuzzer's input: two of them in different arguments decode the same bytes differently
# per compiler.
INPUT = re.compile(r"\bin_?\s*(?:\.|->)\s*(?:Byte|Word)\s*\(\s*\)|\b(?:Fuzz|Next|Read)\w*\s*\(\s*in\s*\)")
SKIP = {"if", "while", "for", "switch", "return", "sizeof", "static_assert", "catch", "decltype"}
IGNORE = "eval-order: ignore"


def blank(match: re.Match[str]) -> str:
    """The comment as spaces, keeping the line breaks, so line numbers stay right."""
    return re.sub(r"[^\n]", " ", match.group())


def split_args(text: str) -> list[str]:
    out: list[str] = []
    depth = 0
    cur = ""
    for ch in text:
        depth += ch in "([{"
        depth -= ch in ")]}"
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    return [*out, cur]


def scan_text(raw: str) -> Iterator[tuple[int, str]]:
    """(line, call) for each call that mixes an operation and a probe read in different arguments."""
    lines = raw.splitlines()
    text = re.sub(r"/\*.*?\*/|//[^\n]*", blank, raw, flags=re.S)
    for m in re.finditer(r"\b(\w+)\s*\(", text):
        if m.group(1) in SKIP:
            continue
        j, depth = m.end(), 1
        while j < len(text) and depth:
            depth += (text[j] == "(") - (text[j] == ")")
            j += 1
        args = split_args(text[m.end():j - 1])
        mixed = any(OP.search(a) for a in args) and any(PROBE.search(a) and not OP.search(a) for a in args)
        two_reads = sum(1 for a in args if INPUT.search(a)) > 1
        if len(args) > 1 and (mixed or two_reads):
            line = text.count("\n", 0, m.start()) + 1
            if IGNORE not in lines[line - 1]:
                yield line, " ".join(text[m.start():j].split())


def files_under(root: Path) -> list[Path]:
    return sorted(p for d in SCAN_DIRS for p in (root / d).rglob("*") if p.is_file() and p.suffix in SUFFIXES)


def selftest() -> int:
    bad = "void f() { Check(d.Op(1), was_up, sent()); }\n"
    good = ("void f() { const auto r = d.Op(1); Check(r, was_up, sent()); Check(d.Op(2), was_up, probe);\n"
            "  Check(d.Op(3) && sent() == 0); for (int i = 0; i < 3; ++i) { d.Op(i); } }\n"
            "// Check(d.Op(4), sent());\n")
    two_in = "void f() { d.Set(in.Byte(), in.Byte() % 3U); d.Set(FuzzChannel(in), 1); d.Set(x, in.Byte()); }\n"
    marked = "void f() { Check(d.Op(1), sent());  // eval-order: ignore\n}\n"
    split = "void f() {\n  Check(d.Op(1),\n        was_up, sent());\n}\n"
    ok = (list(scan_text(bad)) == [(1, "Check(d.Op(1), was_up, sent())")] and not list(scan_text(good))
          and not list(scan_text(marked)) and [n for n, _ in scan_text(two_in)] == [1]
          and [n for n, _ in scan_text(split)] == [2])
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "tests/fuzz").mkdir(parents=True)
        (Path(tmp) / "tests/fuzz/f.cpp").write_text(bad, encoding="utf-8")
        ok = ok and [p.name for p in files_under(Path(tmp))] == ["f.cpp"]
    print("eval_order_check selftest " + ("passed" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    quiet = "--quiet" in argv
    if "--root" in argv:
        files = files_under(Path(argv[argv.index("--root") + 1]))
    else:
        files = [Path(a) for a in argv if not a.startswith("--")] or files_under(ROOT)
    found = 0
    for path in files:
        for line, call in scan_text(path.read_text(encoding="utf-8", errors="replace")):
            print(f"{path}:{line}: error eval-order: {call[:140]}")
            found += 1
    if not quiet or found:
        print(f"eval_order_check: {len(files)} file(s), {found} finding(s)")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
