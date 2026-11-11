"""The layers of the library: your application, the hardware-agnostic core, the port contract, your port.

Reads: Top to bottom, each arrow is a call. Your application calls one object, whose public calls are listed from
the umbrella header in inc/. The core turns each call into register reads and writes and hands them to the port
contract (the CRTP base in inc/*_interface.hpp), which forwards them, at compile time, to the hooks of the port
class you write; your port moves the bytes on the bus. The notes on the right say which layers stay the same on
every MCU and which one you write. A library without a port contract has only the first two layers.
"""

import re
from pathlib import Path

from figkit import Fig

ROOT = Path(__file__).resolve().parents[3]
SETTINGS = next((ROOT / "cmake").glob("*_build_settings.cmake"))
NAME = re.match(r"(\w+)_build_settings", SETTINGS.stem).group(1)
UMBRELLA = f"inc/{NAME}.hpp"
SKIP = {"IsInitialized", "GetConfig", "GetRevision"}


def _interface():
    found = sorted((ROOT / "inc").glob(f"{NAME}_*_interface.hpp"))
    return found[0].relative_to(ROOT).as_posix() if found else None


def data():
    text = (ROOT / UMBRELLA).read_text()
    public = text.split("public:", 1)[1].split("private:", 1)[0]
    calls = []
    for name in re.findall(r"^\s+(?:\[\[nodiscard\]\] )?Result<[^>]*> (\w+)\(", public, re.M):
        if name not in SKIP and name not in calls:
            calls.append(name)
    classes = re.findall(r"^class (\w+)\s*\{", text, re.M)
    iface = _interface()
    d = {"name": NAME, "cls": classes[0] if classes else NAME, "calls": calls, "sources": [f"{UMBRELLA}:public calls"]}
    if iface:
        itext = (ROOT / iface).read_text()
        d["bus"] = re.search(rf"{NAME}_(\w+)_interface", iface).group(1).upper()
        d["base"] = re.search(r"^class (\w+) \{", itext, re.M).group(1)
        d["hooks"] = re.findall(r"::Take\(\s*&Port::(\w+)\)", itext)
        d["sources"].append(f"{iface}:{d['base']}")
    return d


def build(d):
    port = "hooks" in d
    f = Fig(
        "driver-layers",
        "The layers, one of them yours" if port else "The layers",
        alt=(f"Layer diagram: your application calls the {d['cls']} core, which sends register reads and writes "
             f"through the {d.get('bus', '')} port contract to the port class you write, which drives the chip; "
             f"the core and the contract are the same on every MCU."
             if port else
             f"Layer diagram: your application calls the {d['cls']} library, which has no platform code."),
        width=1200,
        kicker="Architecture",
        subtitle=("Each arrow is a call. Only the port changes from one MCU to the next."
                  if port else "One header-only C++20 library, no platform code."),
        profile="overview",
        legend=[("teal", "you write"), ("brand", "this library"), ("slate", "hardware"), ("~solid", "calls")],
    )
    W = f.body_w
    side = 300
    gap, row = 40, 64
    M = W - side - gap  # main column
    nx = M + gap        # notes column

    app = f.card(0, 0, M, "Your application", ["a handler, a manager or a test"], tone="teal", accent=True)
    top = app.b + row
    inner = f.region_inner_top(top, sub_below=True)
    chips_h = f.chips(16, inner, M - 32, [c + "()" for c in d["calls"]], tone="brand", on="brand.tint")
    core = f.region(0, top, M, inner - top + chips_h + 18, "Hardware-agnostic core", tone="brand",
                    sub=f"{d['cls']}, {UMBRELLA}", sub_below=True)
    f.edge([(M / 2, app.b + 4), (M / 2, core.y - 4)], label="calls")
    if not port:
        f.note(nx, core.y, side, "No platform code: the same header builds on every MCU and on the host.",
               tone="brand", title="Portable")
        return f
    comm = f.card(0, core.b + row, M, "Port contract",
                  [f"{d['base']}<YourPort>: checked at compile time, no virtual calls"], tone="brand", accent=True)
    mine = f.card(0, comm.b + row, M, "Your port", ["  ".join(h + "()" for h in d["hooks"])],
                  tone="teal", accent=True, mono=True)
    hw = f.card(0, mine.b + row, M, d["cls"], [f"the chip, on the {d['bus']} bus"], tone="slate", accent=True)
    f.edge([(M / 2, core.b + 4), (M / 2, comm.y - 4)], label="register reads and writes")
    f.edge([(M / 2, comm.b + 4), (M / 2, mine.y - 4)], label="forwards to your hooks")
    f.edge([(M / 2, mine.b + 4), (M / 2, hw.y - 4)], label=f"{d['bus']} transfers")

    f.note(nx, core.y, side, "The core and the contract are the same code on every MCU and on the host.", tone="brand", title="Shared")
    f.note(nx, mine.y, side, "One small class per MCU or board.", tone="teal", title="Yours")
    return f
