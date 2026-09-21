#!/usr/bin/env python3
"""CL-9: der Hilfetext wird gegen die Wirklichkeit geprueft.

Zwei Richtungen, beide muessen stimmen:

  1. Jedes Kommando, das die CLI kennt (Dispatch-Tabelle in src/cli/main.cpp),
     steht in docs/kmixdeck.md.
  2. Jedes Kommando, das die Doku nennt, existiert in der Dispatch-Tabelle.

Warum das ein eigener Test ist: `scene` ging in v0.3.0 heraus, ohne im
handgepflegten Hilfetext zu stehen — die Funktion war da, niemand konnte sie
finden. Umgekehrt ist genauso schlimm: Doku, die ein Kommando verspricht, das
`unknown command` liefert.

Die Dispatch-Tabelle ist hier die Wahrheit, nicht die Doku. Sie entscheidet zur
Laufzeit, was das Programm tut; alles andere ist Beschreibung.
"""
from __future__ import annotations

import importlib.util
import re
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
GENERATOR = WURZEL / "docs" / "generiere-doku.py"
MAIN_CPP = WURZEL / "src" / "cli" / "main.cpp"


def _lade_generator():
    spec = importlib.util.spec_from_file_location("generiere_doku", GENERATOR)
    if spec is None or spec.loader is None:
        sys.exit(f"cannot load {GENERATOR}")
    modul = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modul)
    return modul


def kommandos_aus_code() -> set[str]:
    """Die QMap<QString, Fn>-Tabelle in Cli::run() — die einzige Wahrheit."""
    quelle = MAIN_CPP.read_text(encoding="utf-8")
    treffer = re.search(r"static const QMap<QString, Fn> table = \{(.*?)\n        \};",
                        quelle, re.S)
    if not treffer:
        sys.exit("dispatch table not found in src/cli/main.cpp — has Cli::run() been "
                 "restructured? This test reads the table, adjust the regex here.")
    namen = set(re.findall(r'QStringLiteral\("([a-z][a-z-]*)"\)', treffer.group(1)))
    if len(namen) < 10:
        sys.exit(f"dispatch table yielded only {len(namen)} commands — the regex is "
                 f"probably matching the wrong block: {sorted(namen)}")
    return namen


def main() -> int:
    gd = _lade_generator()
    teile = dict(gd.lese())
    doku = {name for name, _ in gd._kommandos(teile.get("COMMANDS", []))}
    code = kommandos_aus_code()

    fehlt_in_doku = sorted(code - doku)
    fehlt_im_code = sorted(doku - code)

    if fehlt_in_doku:
        print(f"FAIL: the CLI knows these commands, the documentation does not "
              f"mention them: {fehlt_in_doku}\n"
              f"      -> add them to docs/kmixdeck.md, section COMMANDS, as a "
              f"`name …` definition line with a ':' explanation.", file=sys.stderr)
    if fehlt_im_code:
        print(f"FAIL: the documentation promises these commands, the CLI does not "
              f"have them: {fehlt_im_code}\n"
              f"      -> either implement them or remove them from "
              f"docs/kmixdeck.md.", file=sys.stderr)
    if fehlt_in_doku or fehlt_im_code:
        return 1

    print(f"CL-9 ok: {len(code)} commands, documentation and dispatch table agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
