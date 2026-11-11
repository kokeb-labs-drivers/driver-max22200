"""Datasheet regions, generated from ``docs/datasheets.yml`` (handbook ADR-0019).

* ``<!-- generated: datasheet-card -->``: the documents, their revision and where to get them (README, home page).
* ``<!-- generated: datasheet-map -->``: every topic the driver takes from a document, with the section, the page,
  the driver API and the docs page that explains it (``docs/datasheet.md``).
* ``<!-- generated: datasheet-refs -->``: the topics a docs page relies on, as a box at the top of that page.

The YAML is the only source, so the README, the map and the pages cannot disagree about a revision or a page
number; ``kokeb-docs gen --check`` fails CI when a region is stale, and this module fails when the YAML names a
document or a docs page that does not exist.
"""

from __future__ import annotations

import re
import textwrap

import yaml

from kokeb_mkdocs_theme.paths import repo_root

SOURCE = "docs/datasheets.yml"
REQUIRED_DOC = ("id", "vendor", "title", "kind", "number", "revision", "date", "url", "sha256")
REQUIRED_TOPIC = ("topic", "doc", "section", "page", "driver", "docs")


def _load() -> tuple[dict, list]:
    data = yaml.safe_load((repo_root() / SOURCE).read_text()) or {}
    docs = {}
    for d in data.get("documents") or []:
        missing = [k for k in REQUIRED_DOC if d.get(k) in (None, "")]
        if missing:
            raise SystemExit(f"[gen] {SOURCE}: document {d.get('id', '?')!r} lacks {', '.join(missing)}")
        docs[d["id"]] = d
    if not docs:
        raise SystemExit(f"[gen] {SOURCE}: no documents")
    topics = data.get("topics") or []
    for t in topics:
        missing = [k for k in REQUIRED_TOPIC if t.get(k) in (None, "", [])]
        if missing:
            raise SystemExit(f"[gen] {SOURCE}: topic {t.get('topic', '?')!r} lacks {', '.join(missing)}")
        if t["doc"] not in docs:
            raise SystemExit(f"[gen] {SOURCE}: topic {t['topic']!r} names unknown document {t['doc']!r}")
        for page in t["docs"]:
            if not (repo_root() / "docs" / f"{page}.md").exists():
                raise SystemExit(f"[gen] {SOURCE}: topic {t['topic']!r} names missing page docs/{page}.md")
    return docs, topics


def _wrap(text: str, indent: str = "") -> str:
    """Prose wrapped at the 120 columns markdownlint allows; a long URL stays on its line."""
    return textwrap.fill(text, width=120, initial_indent=indent, subsequent_indent=indent,
                         break_long_words=False, break_on_hyphens=False)


def _title(stem: str) -> str:
    """The H1 of docs/<stem>.md, so a link reads like the page it opens."""
    text = (repo_root() / "docs" / f"{stem}.md").read_text()
    m = re.search(r"^# (.+)$", text, re.M)
    return m.group(1).strip() if m else stem


def _name(d: dict) -> str:
    return f"{d['vendor']} {d['number']} {d['revision']}"


def _page_link(d: dict, page: int) -> str:
    """`p. N`, linked to that page of the PDF when the vendor publishes a stable one."""
    if d.get("pdf"):
        return f"[p. {page}]({d['pdf']}#page={page + int(d.get('page_offset') or 0)})"
    return f"p. {page}"


def card(page) -> str:
    docs, _ = _load()
    rows = ["| Document | Revision | Get it |", "| --- | --- | --- |"]
    for d in docs.values():
        links = f"[PDF]({d['pdf']}) · [vendor page]({d['url']})" if d.get("pdf") else f"[vendor page]({d['url']})"
        rows.append(f"| {d['kind']}: {d['title']} ({d['vendor']} {d['number']}) | {d['revision']}, {d['date']} | {links} |")
    note = _wrap(
        f"Written against these revisions; the SHA-256 of each file is in [`{SOURCE}`]({page.rel(SOURCE)})."
        f" Where each part of the driver is in the datasheet: [Datasheet map]({page.rel('docs/datasheet.md')})."
    )
    return "\n".join(rows) + "\n\n" + note


def datasheet_map(page) -> str:
    docs, topics = _load()
    rows = ["| Topic | Datasheet section | Page | In the driver | Explained on |", "| --- | --- | --- | --- | --- |"]
    order = {doc_id: i for i, doc_id in enumerate(docs)}  # documents in the order the YAML lists them
    for t in sorted(topics, key=lambda x: (order[x["doc"]], int(x["page"]))):
        d = docs[t["doc"]]
        where = ", ".join(f"[{_title(p)}]({page.rel(f'docs/{p}.md')})" for p in t["docs"])
        doc = "" if len(docs) == 1 else f"{d['number']}: "
        rows.append(f"| {t['topic']} | {doc}{t['section']} | {_page_link(d, int(t['page']))} | {t['driver']} | {where} |")
    lines = []
    for d in docs.values():
        get = f"[vendor page]({d['url']})" + (f" or as a [PDF]({d['pdf']})" if d.get("pdf") else "")
        lines.append(
            _wrap(f"- **{d['kind']}: {d['title']}**, {d['vendor']} {d['number']}, revision {d['revision']}"
                  f" ({d['date']}). Get it from the {get}.").replace("\n", "\n  ")
            + f"\n  SHA-256 of the file used: `{d['sha256']}`."
        )
    return "\n".join(lines) + "\n\n" + "\n".join(rows)


def refs(page) -> str:
    docs, topics = _load()
    stem = page.path.stem
    order = {doc_id: i for i, doc_id in enumerate(docs)}
    mine = sorted((t for t in topics if stem in t["docs"]), key=lambda x: (order[x["doc"]], int(x["page"])))
    if not mine:
        raise SystemExit(f"[gen] {page.path.name}: datasheet-refs region, but no topic in {SOURCE} names {stem!r}")
    single = len({t["doc"] for t in mine}) == 1
    items = []
    if single:
        items.append(f"    From {_name(docs[mine[0]['doc']])}:\n")
    for t in mine:
        d = docs[t["doc"]]
        src = "" if single else f"{_name(d)}, "
        items.append(f"    - {t['topic']}: {src}{t['section']}, {_page_link(d, int(t['page']))}")
    return (
        '??? abstract "Datasheet sections this page relies on"\n\n'
        + "\n".join(items)
        + "\n\n"
        + _wrap(f"All of them, with the driver API for each: [Datasheet map]({page.rel('docs/datasheet.md')}).", "    ")
    )


REGIONS = {"datasheet-card": card, "datasheet-map": datasheet_map, "datasheet-refs": refs}
