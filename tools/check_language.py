#!/usr/bin/env python3
"""The C++ language standard is declared once and agrees everywhere (Python 3.9+, standard library only).

    python3 -I tools/check_language.py                # the repository this file is in
    python3 -I tools/check_language.py --root DIR
    python3 -I tools/check_language.py --fix          # rewrite every site to the declared standard
    python3 -I tools/check_language.py --selftest

driver.yml carries the declaration (the `language:` block):

    language:
      standard: 20               # the minimum ISO C++ standard: a consumer needs at least this
      profile: KB-CPP20          # the handbook profile of that standard
      profile_version: "1.0"     # the handbook release that defines it
      misra: "MISRA C++:2023"    # the MISRA edition the driver is analysed against
      compilers:                 # the oldest versions the driver promises to build with, quoted
        gcc: "10"
        clang: "13"
      tested: [20]               # every standard CI compiles the headers under (includes `standard`)
      effective_from: 2026-11-15 # the date the declaration took effect

This checks the block's shape and then every place that repeats it: `cxx_std_N` and `CMAKE_CXX_STANDARD` in the CMake
files, clang-format `Standard`, `__cplusplus` assertions, `-std=c++N` and workflow `std` inputs (these may name any
standard in `tested`), the README badge, "a C++NN compiler" in the prose, and "GCC N+" / "Clang N+" floors. With
`--fix` it rewrites them, which is the mechanical part of moving to a new standard (docs/language-standard.md).
Whether the profile is known, supported or retired is decided by the organisation registry
(kokeb-labs/ci-tools, tools/standards/check_standards.py), not here. A driver.yml without the block is a note.
Prints `path:line: message` per finding; exit status 1 on one.
"""
from __future__ import annotations

import datetime
import re
import sys
import tempfile
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent
KNOWN = (20, 23)  # the standards this tool can rewrite; ci-tools' standards.yml says which are accepted
# __cplusplus for each standard: `==` for C++20; GCC 11 to 13 report 202100 for C++23, GCC 14 and Clang 17 202302.
CPLUSPLUS = {20: ("==", 202002), 23: (">=", 202100)}
CPLUSPLUS_STD = {201402: 14, 201703: 17, 202002: 20, 202100: 23, 202302: 23}
KEYS = ("standard", "profile", "profile_version", "misra", "compilers", "tested", "effective_from")
SKIP_DIRS = {".git", ".venv", "venv", "site", "_site", "node_modules", "third_party", "__pycache__"}
SUFFIXES = {".cmake", ".txt", ".yml", ".yaml", ".sh", ".py", ".cpp", ".hpp", ".ipp", ".md", ".tmpl", ""}
FLOOR_WORDS = {"gcc": "GCC", "clang": "Clang"}


def block(text: str) -> Optional[dict]:
    """The `language:` mapping of driver.yml: two-space `key: value` lines and one nested `compilers:` mapping."""
    m = re.search(r"^language:[ \t]*(?:#.*)?\n((?:[ \t]+\S.*\n?|[ \t]*#.*\n?)+)", text, re.M)
    if not m:
        return None
    fields: dict = {}
    nested: Optional[str] = None
    for line in m.group(1).splitlines():
        kv = re.fullmatch(r"(\s+)([a-z_]+):\s*(.*?)\s*(?:#.*)?", line)
        if not kv:
            continue
        indent, key, raw = len(kv.group(1)), kv.group(2), kv.group(3)
        if indent >= 4 and nested:
            fields[nested][key] = raw.strip("\"'")
            continue
        nested = None
        if raw == "":
            nested = key
            fields[key] = {}
        elif raw.startswith("["):
            fields[key] = [x.strip() for x in raw.strip("[]").split(",") if x.strip()]
        else:
            fields[key] = raw.strip("\"'")
    return fields


def version_ok(v: str) -> bool:
    return bool(re.fullmatch(r"\d+(?:\.\d+)*", v))


def shape(fields: dict) -> List[str]:
    bad = []
    for k in KEYS:
        if k not in fields:
            bad.append(f"language.{k} is missing")
    for k in set(fields) - set(KEYS):
        bad.append(f"language.{k} is not a known key")
    if bad:
        return bad
    std = fields["standard"]
    if not str(std).isdigit() or int(std) not in KNOWN:
        bad.append(f"language.standard is {std!r}, this tool knows {', '.join(map(str, KNOWN))}")
    elif fields["profile"] != f"KB-CPP{std}":
        bad.append(f"language.profile is {fields['profile']!r}, expected KB-CPP{std}")
    for k in ("profile_version", "misra"):
        if not fields[k]:
            bad.append(f"language.{k} is empty")
    comp = fields["compilers"]
    if not isinstance(comp, dict) or not comp or not all(version_ok(v) for v in comp.values()):
        bad.append("language.compilers must map a compiler key to a dotted version, e.g. gcc: \"10\"")
    tested = fields["tested"]
    if not isinstance(tested, list) or not tested or not all(t.isdigit() for t in tested):
        bad.append("language.tested must be a list of standards, e.g. [20]")
    elif str(std) not in tested:
        bad.append(f"language.tested {tested} must include the declared standard {std}")
    elif any(int(t) < int(std) for t in tested):
        bad.append(f"language.tested {tested} names a standard below the declared minimum {std}")
    try:
        datetime.date.fromisoformat(str(fields["effective_from"]))
    except ValueError:
        bad.append(f"language.effective_from is {fields['effective_from']!r}, expected YYYY-MM-DD")
    return bad


class Site:
    """One kind of place that repeats the standard: where it applies, what is wrong, and the rewrite."""

    def __init__(self, label: str, pattern: str, applies: Callable[[str], bool], bad: Callable[[re.Match], bool],
                 repl: Callable[[re.Match], str], code_only: bool = False) -> None:
        self.label, self.rx, self.applies, self.bad, self.repl = label, re.compile(pattern), applies, bad, repl
        self.code_only = code_only  # a `#` comment of a CMake, YAML, shell or Python file is not read


def hash_comments(rel: str) -> bool:
    return rel.endswith((".cmake", ".yml", ".yaml", ".sh", ".py")) or "CMakeLists.txt" in rel


def sites(fields: dict) -> List[Site]:
    std = int(fields["standard"])
    tested = {int(t) for t in fields["tested"]}
    cmake = lambda r: "CMakeLists.txt" in r or r.endswith(".cmake")  # noqa: E731
    doc = lambda r: r.endswith((".md", ".tmpl"))  # noqa: E731
    text = lambda r: r.endswith((".md", ".tmpl", ".cmake", ".cpp", ".hpp", ".ipp")) or "CMakeLists.txt" in r  # noqa: E731
    source = lambda r: not r.endswith(".md")  # noqa: E731
    workflow = lambda r: r.startswith(".github/workflows/") or (r.startswith("tools/scaffold/") and ".yml" in r)  # noqa: E731

    def swap(m: re.Match) -> str:
        return m.group(0)[:m.start("n") - m.start()] + str(std) + m.group(0)[m.end("n") - m.start():]

    def not_std(m: re.Match) -> bool:
        return int(m.group("n")) != std

    def not_tested(m: re.Match) -> bool:
        return int(m.group("n")) not in tested

    def cpp_bad(m: re.Match) -> bool:
        return CPLUSPLUS_STD.get(int(m.group("v"))) != std

    def cpp_fix(m: re.Match) -> str:
        op, value = CPLUSPLUS[std]
        return f"__cplusplus {op} {value}L"

    out = [
        Site("cxx_std_N", r"\bcxx_std_(?P<n>\d\d)\b", cmake, not_std, swap, True),
        Site("CMAKE_CXX_STANDARD", r"\bCMAKE_CXX_STANDARD\s+(?P<n>\d\d)\b", cmake, not_std, swap, True),
        Site("clang-format Standard", r"^\s*Standard:\s*c\+\+(?P<n>\d\d)\b", lambda r: r.endswith(".clang-format"), not_std, swap),
        Site("__cplusplus", r"__cplusplus\s*(?P<op>==|>=)\s*(?P<v>\d{6})L", source, cpp_bad, cpp_fix, True),
        Site("-std flag", r"(?<![\w-])-{1,2}std=(?:gnu|c)\+\+(?P<n>\d\d)\b", source, not_tested, swap, True),
        Site("workflow std", r"^\s*(?:cppcheck_std|std|cxx_std|cpp_std)\s*:\s*[\"']?c?(?:\+\+)?(?P<n>\d\d)[\"']?\s*$",
             workflow, not_tested, swap, True),
        Site("README badge", r"img\.shields\.io/badge/C%2B%2B-(?P<n>\d\d)", doc, not_std, swap),
        Site("README badge text", r"\[!\[C\+\+(?P<n>\d\d)\]", doc, not_std, swap),
        Site("prose", r"(?:\b(?:[Aa]|an|requires|needs|Header-only|ISO)\s+|brings the headers and )C\+\+(?P<n>\d\d)\b",
             text, not_std, swap),
        Site("prose", r"(?:^|(?<=\s))C\+\+(?P<n>\d\d) (?:compiler|driver|library)\b", text, not_std, swap),
    ]
    for key, word in FLOOR_WORDS.items():
        have = fields["compilers"].get(key)
        if have is None:
            continue

        def floor_bad(m: re.Match, have: str = have) -> bool:
            return m.group("n") != have

        def floor_fix(m: re.Match, have: str = have) -> str:
            return m.group(0)[:m.start("n") - m.start()] + have + m.group(0)[m.end("n") - m.start():]

        out.append(Site(f"{word} floor", rf"\b{word} (?P<n>\d+(?:\.\d+)*)\+", doc, floor_bad, floor_fix))
    return out


def scan(root: Path, fields: dict, fix: bool) -> Tuple[List[str], int]:
    """Findings (path:line: message) and the number of lines rewritten (with fix)."""
    rules = sites(fields)
    std = int(fields["standard"])
    found: List[str] = []
    changed = 0
    requires = False
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if any(p in SKIP_DIRS or p.startswith(("build", "cmake-build")) for p in rel.split("/")[:-1]):
            continue
        if path.suffix not in SUFFIXES and path.name not in ("CMakeLists.txt", ".clang-format"):
            continue
        if rel in ("tools/check_language.py", "CHANGELOG.md") or rel.startswith(("docs/adr/", "tools/scaffold/license/")):
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        except (OSError, UnicodeDecodeError):
            continue
        dirty = False
        for i, raw in enumerate(lines):
            line = raw
            for rule in rules:
                if not rule.applies(rel) or (rule.label == "prose" and "KB-CPP" in line):
                    continue  # a line about a profile's own rules keeps the profile's standard
                limit = len(line.split("#", 1)[0]) if rule.code_only and hash_comments(rel) else len(line)
                hits = [m for m in rule.rx.finditer(line) if m.end() <= limit]
                if rule.label == "cxx_std_N" and (rel == "CMakeLists.txt" or rel.startswith("cmake/")):
                    requires = requires or any(int(m.group("n")) == std for m in hits)
                for m in reversed(hits):  # right to left: a rewrite does not move the matches before it
                    if not rule.bad(m):
                        continue
                    extra = f", tested is {sorted(int(t) for t in fields['tested'])}" if rule.label in (
                        "-std flag", "workflow std") else ""
                    found.append(f"{rel}:{i + 1}: {m.group(0).strip()!r} ({rule.label}), language.standard is {std}{extra}")
                    if fix:
                        line = line[:m.start()] + rule.repl(m) + line[m.end():]
            if line != raw:
                lines[i] = line
                dirty = True
                changed += 1
        if dirty:
            path.write_text("".join(lines), encoding="utf-8")
    if (root / "CMakeLists.txt").is_file() and not requires:
        found.append(f"CMakeLists.txt: no cxx_std_{std} in CMakeLists.txt or cmake/ (the target must require the "
                     "declared standard)")
    return found, changed


def check(root: Path, fix: bool = False) -> Tuple[List[str], List[str]]:
    manifest = root / "driver.yml"
    if not manifest.is_file():
        return [], []
    fields = block(manifest.read_text(encoding="utf-8"))
    if fields is None:
        return [], ["driver.yml: no language block; C++20 is assumed (add `language:`, docs/language-standard.md)"]
    bad = [f"driver.yml: {m}" for m in shape(fields)]
    if bad:
        return bad, []
    found, changed = scan(root, fields, fix)
    notes = []
    if fix:
        notes.append(f"rewrote {changed} line(s) to C++{fields['standard']}")
        found, _ = scan(root, fields, False)  # what --fix cannot rewrite is still reported
    return found, notes


def selftest() -> int:
    DAY = "20" "26-11-15"  # in pieces: the stand-alone check reads dates outside driver.yml as history
    driver = ("name: x\nlanguage:\n  standard: 20\n  profile: KB-CPP20\n  profile_version: \"1.0\"\n"
              "  misra: \"MISRA C++:2023\"\n  compilers:\n    gcc: \"10\"\n    clang: \"13\"\n  tested: [20]\n"
              f"  effective_from: {DAY}\n")
    files = {
        "CMakeLists.txt": "target_compile_features(x INTERFACE cxx_std_20)  # cxx_std_17 in a comment\n",
        "tests/CMakeLists.txt": "target_compile_features(t PRIVATE cxx_std_20)\n"
                                'file(WRITE a "static_assert(__cplusplus == 202002L, \\"c++20\\");")\n',
        "_config/.clang-format": "BasedOnStyle: Google\nStandard: c++20\n",
        "tools/preflight.sh": "cppcheck --std=c++20 x\n",
        ".github/workflows/ci.yml": "with:\n  cppcheck_std: \"c++20\"\n",
        "README.md": "# X\n\n[![C++20](https://img.shields.io/badge/C%2B%2B-20-informational)](a)\n\n"
                     "C++20 driver for X. Header-only C++20. You need a C++20 compiler (GCC 10+, Clang 13+).\n",
    }

    def tree(tmp: Path, drv: str = driver, **over: str) -> Path:
        (tmp / "driver.yml").write_text(drv, encoding="utf-8")
        for rel, text in {**files, **over}.items():
            p = tmp / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding="utf-8")
        return tmp

    ok = True

    def expect(label: str, bad: List[str], want: Optional[str]) -> None:
        nonlocal ok
        if (bool(bad) if want is None else not any(want in b for b in bad)):
            ok = False
            print(f"selftest FAILED: {label}: expected {want!r}, got {bad}")

    with tempfile.TemporaryDirectory() as t:
        expect("consistent tree", check(tree(Path(t)))[0], None)
    cases = [
        ("cmake names another standard", {"tests/CMakeLists.txt": "target_compile_features(t PRIVATE cxx_std_23)\n"},
         "cxx_std_23"),
        ("cplusplus assertion", {"tests/CMakeLists.txt": "cxx_std_20 static_assert(__cplusplus == 201703L);\n"},
         "__cplusplus"),
        ("clang-format", {"_config/.clang-format": "Standard: c++17\n"}, "clang-format"),
        ("std flag outside tested", {"tools/preflight.sh": "cppcheck --std=c++17 x\n"}, "-std flag"),
        ("workflow input", {".github/workflows/ci.yml": "with:\n  cppcheck_std: \"c++17\"\n"}, "workflow std"),
        ("badge", {"README.md": files["README.md"].replace("-20-", "-17-")}, "README badge"),
        ("prose", {"README.md": files["README.md"].replace("a C++20 compiler", "a C++17 compiler")}, "prose"),
        ("floor", {"README.md": files["README.md"].replace("GCC 10+", "GCC 12+")}, "GCC floor"),
        ("library does not require the standard", {"CMakeLists.txt": "add_library(x INTERFACE)\n"}, "no cxx_std_20"),
    ]
    for label, over, want in cases:
        with tempfile.TemporaryDirectory() as t:
            expect(label, check(tree(Path(t), **over))[0], want)
    shapes = [
        ("missing key", driver.replace("  misra: \"MISRA C++:2023\"\n", ""), "language.misra is missing"),
        ("unknown standard", driver.replace("standard: 20", "standard: 26"), "this tool knows"),
        ("profile of another standard", driver.replace("KB-CPP20", "KB-CPP23"), "expected KB-CPP20"),
        ("tested lacks the standard", driver.replace("tested: [20]", "tested: [23]"), "must include"),
        ("tested below the minimum", driver.replace("standard: 20", "standard: 23").replace("KB-CPP20", "KB-CPP23")
         .replace("tested: [20]", "tested: [20, 23]"), "below the declared minimum"),
        ("tested above the minimum is fine", driver.replace("tested: [20]", "tested: [20, 23]"), None),
        ("bad date", driver.replace(DAY, "soon"), "effective_from"),
        ("bad compiler", driver.replace('gcc: "10"', "gcc: ten"), "compilers"),
    ]
    for label, drv, want in shapes:
        with tempfile.TemporaryDirectory() as t:
            expect(label, check(tree(Path(t), drv))[0], want)
    with tempfile.TemporaryDirectory() as t:
        bad, notes = check(tree(Path(t), "name: x\n"))
        if bad or not notes:
            ok = False
            print("selftest FAILED: a driver.yml without the block is a note, not an error")
    with tempfile.TemporaryDirectory() as t:
        if check(Path(t)) != ([], []):
            ok = False
            print("selftest FAILED: a library (no driver.yml) is not checked")
    # --fix: raise the floor of a consistent C++20 tree to C++23; every site follows.
    with tempfile.TemporaryDirectory() as t:
        root = tree(Path(t), driver.replace("standard: 20", "standard: 23").replace("KB-CPP20", "KB-CPP23")
                    .replace("tested: [20]", "tested: [23]").replace('gcc: "10"', 'gcc: "13"'))
        bad, _ = check(root)
        if len(bad) < 7:
            ok = False
            print(f"selftest FAILED: a C++20 tree declared as C++23 should list its sites, got {bad}")
        bad, notes = check(root, fix=True)
        if bad:
            ok = False
            print(f"selftest FAILED: --fix left findings: {bad}")
        text = (root / "README.md").read_text(encoding="utf-8")
        cm = (root / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        if "C++23 compiler" not in text or "-23-" not in text or "GCC 13+" not in text or "cxx_std_23" not in cm \
                or "__cplusplus >= 202100L" not in cm or "--std=c++23" not in (root / "tools/preflight.sh").read_text():
            ok = False
            print("selftest FAILED: --fix did not rewrite the sites as expected")
        if "cxx_std_17" not in (root / "CMakeLists.txt").read_text(encoding="utf-8"):
            ok = False
            print("selftest FAILED: --fix touched a CMake comment")
    print("check_language selftest " + ("passed" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv: List[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    root = Path(argv[argv.index("--root") + 1]).resolve() if "--root" in argv else ROOT
    bad, notes = check(root, fix="--fix" in argv)
    for line in notes:
        print(f"note: {line}")
    for line in bad:
        print(line)
    if bad:
        print(f"check_language: {len(bad)} finding(s)")
        return 1
    if "--quiet" not in argv:
        print("check_language: the declared standard and every place that repeats it agree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
