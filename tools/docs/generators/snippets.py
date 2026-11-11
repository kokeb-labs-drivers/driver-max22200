"""``<!-- generated: quickstart-code -->``: the Quickstart example, copied from the compiled snippet file.

The README cannot include files the way the site does, so this region carries a copy of the ``quickstart``
section of ``tests/docs/quickstart.cpp`` (built by the host CI). ``kokeb-docs gen --check`` fails when the copy
and the compiled file differ.
"""

from __future__ import annotations

import re

from kokeb_mkdocs_theme.paths import repo_root


def _section(path: str, name: str) -> str:
    text = (repo_root() / path).read_text()
    m = re.search(rf"--8<-- \[start:{name}\]\n(.*?)\n[^\n]*--8<-- \[end:{name}\]", text, re.S)
    if not m:
        raise SystemExit(f"[gen] no section {name!r} in {path}")
    return m.group(1)


def quickstart_code() -> str:
    return "```cpp\n" + _section("tests/docs/quickstart.cpp", "quickstart") + "\n```\n"


REGIONS = {"quickstart-code": quickstart_code}
