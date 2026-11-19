#!/usr/bin/env python3
"""The agent skills and the agent context agree with the tree (Python 3.9+, standard library only).

    python3 -I tools/check_skills.py                  # the repository this file is in
    python3 -I tools/check_skills.py --root DIR
    python3 -I tools/check_skills.py --selftest       # check the checker on small trees

docs/agent-context.md is the page an agent reads first; it links every skill. This checks that

1. every skill linked from docs/agent-context.md exists, and every folder of .claude/skills/ is linked from it;
2. each SKILL.md starts with front matter that has `name` (the folder name) and a `description` of a sentence, and
   is at most 250 lines; each file of .claude/agents/ has front matter with `name` and `description`;
3. every `tools/...` file or folder that a skill, an agent or the context page names exists (a placeholder such as
   `tools/<name>`, a wildcard, or a template-only file in a scaffolded repository is not checked);
4. every relative Markdown link in a skill or agent resolves to a file.

Prints `path: message` per finding and exits 1 when there is one. A skill is read by agents as instructions: a
command that names a file which does not exist sends the agent to improvise, so this runs in the preflight.
"""
from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONTEXT = "docs/agent-context.md"
MAX_LINES = 250
# Present in the template, removed when a repository is scaffolded from it (scaffold.py TEMPLATE_ONLY).
TEMPLATE_ONLY = ("tools/setup.py", "tools/scaffold.py", "tools/scaffold")
# A repository's own optional file, which the checks read when it exists (the exceptions of tools/check_layout.py).
OPTIONAL = ("tools/layout.yml",)
SKILL_LINK = re.compile(r"\.claude/skills/([a-z0-9-]+)/SKILL\.md")
TOOLS = re.compile(r"(?<![\w/.-])(tools/[A-Za-z0-9_.][A-Za-z0-9_./-]*)([<*{$\[]?)")
LINK = re.compile(r"\]\(([^)\s]+)\)")
FENCE = re.compile(r"^```.*?^```", re.S | re.M)


def front_matter(text: str) -> dict[str, str] | None:
    if not text.startswith("---\n"):
        return None
    end = text.find("\n---", 4)
    if end < 0:
        return None
    fields: dict[str, str] = {}
    for line in text[4:end].splitlines():
        m = re.fullmatch(r"([a-z-]+): ?(.*)", line)
        if m:
            fields[m.group(1)] = m.group(2).strip()
    return fields


def tool_exists(root: Path, token: str) -> bool:
    token = token.rstrip(".,;:)'\"`/")
    if (root / token).exists() or token in OPTIONAL:
        return True
    in_template = (root / "tools/scaffold.py").is_file()
    return not in_template and any(token == t or token.startswith(t + "/") for t in TEMPLATE_ONLY)


def check(root: Path) -> list[str]:
    bad: list[str] = []
    skills = root / ".claude/skills"
    names = sorted(p.name for p in skills.iterdir() if p.is_dir()) if skills.is_dir() else []
    context = root / CONTEXT
    if not context.is_file():
        return [f"{CONTEXT}: missing (the page agents read first; it links every skill)"]
    ctext = context.read_text(encoding="utf-8")
    linked = sorted(set(SKILL_LINK.findall(ctext)))
    for name in linked:
        if not (skills / name / "SKILL.md").is_file():
            bad.append(f"{CONTEXT}: names skill {name} but .claude/skills/{name}/SKILL.md does not exist")
    for name in names:
        if name not in linked:
            bad.append(f"{CONTEXT}: skill {name} is not linked as .claude/skills/{name}/SKILL.md")
    files: list[Path] = [context]
    for name in names:
        path = skills / name / "SKILL.md"
        if not path.is_file():
            bad.append(f".claude/skills/{name}: no SKILL.md")
            continue
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        fm = front_matter(text)
        if fm is None:
            bad.append(f"{rel}: no front matter (--- name / description ---) at the top")
        else:
            if fm.get("name") != name:
                bad.append(f"{rel}: front matter name is {fm.get('name')!r}, the folder is {name!r}")
            if len(fm.get("description", "")) < 40:
                bad.append(f"{rel}: front matter description is missing or too short to say when to use the skill")
        if len(text.splitlines()) > MAX_LINES:
            bad.append(f"{rel}: {len(text.splitlines())} lines, the limit is {MAX_LINES}")
        files.append(path)
    agents = root / ".claude/agents"
    for path in sorted(agents.glob("*.md")) if agents.is_dir() else []:
        fm = front_matter(path.read_text(encoding="utf-8"))
        rel = path.relative_to(root).as_posix()
        if fm is None or not fm.get("name") or not fm.get("description"):
            bad.append(f"{rel}: front matter needs name and description")
        files.append(path)
    for path in files:
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        for m in TOOLS.finditer(text):
            token, after = m.groups()
            if after:  # tools/<name>, tools/*.py, tools/${x}: a pattern, not a file
                continue
            if not tool_exists(root, token):
                line = text.count("\n", 0, m.start()) + 1
                bad.append(f"{rel}:{line}: names {token.rstrip('.,;:)')}, which does not exist")
        if path == context:
            continue
        prose = FENCE.sub("", text)
        for m in LINK.finditer(prose):
            target = m.group(1).split("#", 1)[0]
            if not target or re.match(r"[a-z]+:", target):
                continue
            if not (path.parent / target).resolve().exists():
                bad.append(f"{rel}: link to {target} does not resolve")
    return bad


def selftest() -> int:
    skill = ("---\nname: {n}\ndescription: Use this skill when something needs doing, one sentence long enough.\n---\n"
             "# {n}\nRun `tools/preflight.sh` and read [the page](../../../docs/agent-context.md).\n")
    ctx = "# Context\n" + "".join(f"- [{n}](../.claude/skills/{n}/SKILL.md)\n" for n in ("verify", "release"))

    def tree(tmp: Path, **change: str | None) -> Path:
        files = {"docs/agent-context.md": ctx, "tools/preflight.sh": "",
                 ".claude/skills/verify/SKILL.md": skill.format(n="verify"),
                 ".claude/skills/release/SKILL.md": skill.format(n="release")}
        for key, value in change.items():
            files[key.replace("__", "/")] = value
        for rel, text in files.items():
            if text is not None:
                (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
                (tmp / rel).write_text(text, encoding="utf-8")
        return tmp

    cases = [
        ("good tree passes", {}, None),
        ("missing context", {"docs__agent-context.md": None}, "missing"),
        ("skill file missing", {".claude__skills__release__SKILL.md": None}, "does not exist"),
        ("skill not linked", {".claude__skills__extra__SKILL.md": skill.format(n="extra")}, "extra is not linked"),
        ("no front matter", {".claude__skills__verify__SKILL.md": "# verify\n"}, "no front matter"),
        ("wrong name", {".claude__skills__verify__SKILL.md": skill.format(n="other")}, "front matter name"),
        ("short description", {".claude__skills__verify__SKILL.md": "---\nname: verify\ndescription: x\n---\n"},
         "description"),
        ("tool missing", {".claude__skills__verify__SKILL.md": skill.format(n="verify") + "Run tools/nope.py.\n"},
         "tools/nope.py"),
        ("tool placeholder is fine", {".claude__skills__verify__SKILL.md": skill.format(n="verify")
                                      + "Run tools/<name>.py and tools/*.sh.\n"}, None),
        ("broken link", {".claude__skills__verify__SKILL.md": skill.format(n="verify") + "[x](../../gone.md)\n"},
         "does not resolve"),
        ("too long", {".claude__skills__verify__SKILL.md": skill.format(n="verify") + "x\n" * 260}, "lines"),
        ("agent without front matter", {".claude__agents__a.md": "no"}, "front matter"),
    ]
    ok = True
    for label, change, want in cases:
        with tempfile.TemporaryDirectory() as tmp:
            bad = check(tree(Path(tmp), **change))
        hit = (not bad) if want is None else any(want in b for b in bad)
        if not hit:
            ok = False
            print(f"selftest FAILED: {label}: expected {want!r}, got {bad}")
    with tempfile.TemporaryDirectory() as tmp:  # a template-only tool is allowed once the template files are gone
        root = tree(Path(tmp), **{".claude__skills__verify__SKILL.md": skill.format(n="verify")
                                  + "Run tools/setup.py.\n"})
        if check(root):
            ok = False
            print("selftest FAILED: tools/setup.py in a scaffolded repository")
        (root / "tools/scaffold.py").write_text("", encoding="utf-8")
        if not any("tools/setup.py" in b for b in check(root)):
            ok = False
            print("selftest FAILED: tools/setup.py missing in the template itself")
    print("check_skills selftest " + ("passed" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    root = Path(argv[argv.index("--root") + 1]).resolve() if "--root" in argv else ROOT
    bad = check(root)
    for line in bad:
        print(line)
    if bad:
        print(f"check_skills: {len(bad)} finding(s)")
        return 1
    if "--quiet" not in argv:
        print("check_skills: every skill is linked from the agent context, has front matter, and names files that exist")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
