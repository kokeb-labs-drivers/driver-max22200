#!/usr/bin/env python3
"""Stand-alone check: the repository documents only itself and names its own authors (Python 3.9+, standard
library only).

    python3 tools/check_standalone.py                      # the repository this file is in
    python3 tools/check_standalone.py --root DIR --repo NAME
    python3 tools/check_standalone.py --selftest

A driver documents its chip against the datasheet, and its copyright belongs to the repository's authors, so the
repository can move and have many owners. Over every tracked text file (`git ls-files`, else every file under the
root) it reports:

1. Another project's name: boards, products, test projects and driver repositories that are not this one;
   a private repository, a personal account (outside .github/CODEOWNERS) or a local path. Write what the chip
   does and cite the datasheet section instead.
2. Dated history: an ISO date anywhere but a CHANGELOG release heading, a datasheet revision (docs/datasheets.yml,
   or a line that names the datasheet or its revision) or a waiver expiry in .kokeb/handbook.yml. Behaviour
   learned on hardware is written as the design, with its datasheet section; a step still to check on hardware
   is a bench procedure, without a date.
3. A copyright line whose holder is not the repository's, and a LICENSE (MIT), driver.yml `authors` or
   _config/mkdocs.yml `copyright` that does not name it. The holder is `author:` in .kokeb/template.yml, else
   "The <repo> authors"; the repository name is --repo, else `repo:` in .kokeb/template.yml, else the origin
   remote, else the folder name.

Exit status 1 on a finding. CI runs it in the repository checks; the template keeps this file the same in every
repository it makes (template drift).
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Spelled in pieces so that this file passes its own check.
NAMES = [
    ("another project's name", re.compile(r"hard" r"foc", re.I)),
    ("another driver repository", re.compile(r"\bh" r"f-[a-z0-9]+", re.I)),
    ("another project's board", re.compile(r"\b(?:fl" r"ux|vor" r"tex)[ _-]?v\d", re.I)),
    ("another project's board", re.compile(r"\bport" r"enta\b", re.I)),
    ("another project's test project", re.compile(r"\bpw[-_ ]?contr" r"oller", re.I)),
    ("a reference to another driver", re.compile(r"\b(?:old" r"er|prev" r"ious) driver", re.I)),
    ("a private repository", re.compile(r"standards" r"-private", re.I)),
    ("a local path", re.compile(r"~/Doc" r"uments|(?<![\w.])/(?:Us" r"ers|ho" r"me)/[A-Za-z0-9_.-]+/")),
]
# A maintainer's account: allowed only where GitHub needs it, in .github/CODEOWNERS (repository governance).
ACCOUNTS = re.compile(r"\bn3" r"b3x\b", re.I)
ACCOUNT_FILES = {".github/CODEOWNERS"}
# --strict (the template's self-test): the board names on their own as well.
STRICT_NAMES = [("another project's board", re.compile(r"\b(?:fl" r"ux|vor" r"tex)\b", re.I))]
DATE_RE = re.compile(r"\b20\d\d-[01]\d-[0-3]\d\b")
COPYRIGHT_RE = re.compile(r"Copyright\s+(?:\(c\)|©)\s*\d{4}(?:\s*[-–,]\s*\d{4})*\s+(?P<holder>[^.\"\n]+)",
                          re.I)
REMOTE_RE = re.compile(r"(?:github\.com[:/]|^[\w.-]+@[\w.-]+:)[\w.-]+/(?P<repo>[\w.-]+?)(?:\.git)?/?$")

SKIP_DIRS = {".git", ".venv", "venv", "site", "_site", "__pycache__", "node_modules", "third_party"}
SKIP_PREFIXES = ("build",)
SKIP_PATHS = ("examples/esp32/scripts",)  # the ESP-IDF scripts submodule: another repository's files
BINARY_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".ico", ".pdf", ".bin", ".woff", ".woff2", ".ttf", ".zip"}


def record_field(root: Path, key: str) -> str | None:
    """A top-level scalar of .kokeb/template.yml, or None."""
    path = root / ".kokeb/template.yml"
    if not path.is_file():
        return None
    m = re.search(rf"^{key}: *\"?([^\"\n]*)\"?\s*$", path.read_text(encoding="utf-8"), re.M)
    return m.group(1).strip() if m and m.group(1).strip() not in ("", "null") else None


def repo_name(root: Path, given: str | None) -> str:
    if given:
        return given
    name = record_field(root, "repo")
    if name:
        return name
    try:
        url = subprocess.run(["git", "remote", "get-url", "origin"], cwd=root, capture_output=True, text=True,
                             check=True).stdout.strip()
        m = REMOTE_RE.search(url)
        if m:
            return m.group("repo")
    except (OSError, subprocess.CalledProcessError):
        pass
    return root.resolve().name


def files(root: Path) -> list[str]:
    try:
        out = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True, text=True, check=True).stdout
        rels = [line for line in out.splitlines() if line]
    except (OSError, subprocess.CalledProcessError):
        rels = [p.relative_to(root).as_posix() for p in sorted(root.rglob("*")) if p.is_file()]
    keep = []
    for rel in rels:
        parts = rel.split("/")
        if any(p in SKIP_DIRS or p.startswith(SKIP_PREFIXES) for p in parts[:-1]):
            continue
        if any(rel == s or rel.startswith(s + "/") for s in SKIP_PATHS):
            continue
        if Path(rel).suffix.lower() in BINARY_SUFFIXES or not (root / rel).is_file():
            continue
        keep.append(rel)
    return keep


def date_allowed(rel: str, line: str) -> bool:
    if rel.endswith("CHANGELOG.md") and line.startswith("## ["):
        return True
    if rel.split("/")[-1].startswith("datasheets.yml") or re.search(r"datasheet|revision", line, re.I):
        return True
    return rel == ".kokeb/handbook.yml" and re.search(r"expir", line, re.I) is not None


def check(root: Path, repo: str, strict: bool = False) -> list[str]:
    holder = record_field(root, "author") or f"The {repo} authors"
    names = NAMES + (STRICT_NAMES if strict else [])
    bad = []
    for rel in files(root):
        try:
            text = (root / rel).read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for what, pattern in names:
            if pattern.search(rel):
                bad.append(f"{rel}: {what} in the path")
        for n, line in enumerate(text.splitlines(), 1):
            for what, pattern in names:
                m = pattern.search(line)
                if m:
                    bad.append(f"{rel}:{n}: {what} ({m.group(0)!r}); document this chip against its datasheet")
            if ACCOUNTS.search(line) and rel not in ACCOUNT_FILES:
                bad.append(f"{rel}:{n}: a personal account; name the role (the maintainers) instead")
            if DATE_RE.search(line) and not date_allowed(rel, line):
                bad.append(f"{rel}:{n}: a date outside a release heading or a datasheet revision (dated history)")
            for m in COPYRIGHT_RE.finditer(line):
                if m.group("holder").strip() != holder:
                    bad.append(f"{rel}:{n}: copyright holder {m.group('holder').strip()!r}, expected {holder!r}")
    licence = root / "LICENSE"
    if licence.is_file():
        text = licence.read_text(encoding="utf-8")
        if "Permission is hereby granted" in text and not re.search(rf"Copyright \(c\) \d{{4}} {re.escape(holder)}\n",
                                                                     text):
            bad.append(f"LICENSE: no `Copyright (c) <year> {holder}` line")
    manifest = root / "driver.yml"
    if manifest.is_file() and not re.search(rf"^authors: \[\"?{re.escape(holder)}\"?\]", manifest.read_text(), re.M):
        bad.append(f'driver.yml: no `authors: ["{holder}"]`')
    site = root / "_config/mkdocs.yml"
    if site.is_file() and not re.search(rf"^copyright: .*{re.escape(holder)}$", site.read_text(), re.M):
        bad.append(f"_config/mkdocs.yml: no `copyright:` naming {holder}")
    return bad


def selftest() -> int:
    # Spelled in pieces so that this file passes its own check.
    c, day, mine = "Copy" "right (c) 2026", "20" "26-01-02", "The driver-x1 authors"
    cases = {
        "clean": ({"README.md": "# Chip\n\nReads the chip (datasheet section 7.5).\n",
                   "LICENSE": f"MIT License\n\n{c} {mine}\n\nPermission is hereby granted\n",
                   "driver.yml": f'name: x1\nauthors: ["{mine}"]\n',
                   "_config/mkdocs.yml": f"copyright: {c} {mine}\n",
                   "CHANGELOG.md": f"## [0.1.0] - {day}\n",
                   "docs/datasheets.yml": f"    date: {day}\n",
                   "inc/x1.hpp": f"// @copyright {c} {mine}. SPDX-License-Identifier: MIT\n"},
                  0),
        "names": ({"docs/a.md": "Tested on the " + "Vor" + "tex V1 board, like the " + "old" + "er driver.\n"}, 2),
        "repo": ({"docs/a.md": "See " + "h" + "f-x1-driver for the rest.\n"}, 1),
        "private": ({"docs/a.md": "Notes in N3" + "b3x/standards" + "-private and ~/Doc" + "uments/Standards.\n",
                     ".github/CODEOWNERS": "* @N3" + "b3x\n"}, 3),
        "dates": ({"docs/a.md": f"On {day} the bench saw a stuck line.\n", "CHANGELOG.md": f"- Fixed on {day}.\n"}, 2),
        "holder": ({"inc/x1.hpp": f"// {c} Acme Inc. SPDX-License-Identifier: MIT\n", "driver.yml": "name: x1\n"}, 2),
    }
    with tempfile.TemporaryDirectory(prefix="standalone-") as tmp:
        for label, (tree, want) in cases.items():
            root = Path(tmp) / label
            for rel, text in tree.items():
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_text(text, encoding="utf-8")
            got = check(root, "driver-x1")
            if len(got) != want:
                print(f"selftest {label}: {len(got)} finding(s), expected {want}:", *got, sep="\n  ")
                return 1
    if not check_strict_word():
        return 1
    print("check_standalone selftest passed")
    return 0


def check_strict_word() -> bool:
    with tempfile.TemporaryDirectory(prefix="standalone-") as tmp:
        root = Path(tmp)
        (root / "a.md").write_text("The " + "Fl" + "ux board.\n", encoding="utf-8")
        if check(root, "driver-x1") or len(check(root, "driver-x1", strict=True)) != 1:
            print("selftest strict: the board name on its own is reported only with --strict")
            return False
    return True


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1], help="repository root")
    p.add_argument("--repo", help="repository name (default: .kokeb/template.yml, the origin remote, the folder)")
    p.add_argument("--strict", action="store_true", help="also report the board names on their own")
    p.add_argument("--selftest", action="store_true", help="check the checker on small examples")
    a = p.parse_args()
    if a.selftest:
        return selftest()
    root = a.root.resolve()
    repo = repo_name(root, a.repo)
    bad = check(root, repo, a.strict)
    for line in bad:
        print(line)
    if bad:
        print(f"\nstand-alone check: {len(bad)} finding(s) in {repo}")
        return 1
    print(f"stand-alone check: {repo} names only itself and its authors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
