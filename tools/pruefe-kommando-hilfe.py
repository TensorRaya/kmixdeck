#!/usr/bin/env python3
"""CL-3: jedes Kommando hat eine eigene Hilfe mit mindestens einem Beispiel.

Geprueft wird der GENERIERTE Text, nicht die Markdown-Quelle: nur was in
hilfe_pro_kommando() ankommt, sieht der Benutzer auch. Ein Beispiel, das im
Markdown steht aber vom Generator verschluckt wird, ist kein Beispiel.

Warum eigenstaendig und nicht in pruefe-hilfe.py: CL-9 prueft Doku GEGEN Code
(Kommandonamen), CL-3 prueft die Vollstaendigkeit der Hilfe je Kommando. Zwei
Fragen, zwei Tests — bei einer Fehlermeldung will man wissen, welche.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
GENERATOR = WURZEL / "docs" / "generiere-doku.py"


def _lade():
    spec = importlib.util.spec_from_file_location("generiere_doku", GENERATOR)
    if spec is None or spec.loader is None:
        sys.exit(f"cannot load {GENERATOR}")
    modul = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modul)
    return modul


def main() -> int:
    gd = _lade()
    teile = gd.lese()
    texte = gd.hilfe_pro_kommando(teile)
    erwartet = [n for n, _ in gd._kommandos(dict(teile).get("COMMANDS", []))]

    fehler: list[str] = []

    fehlend = [n for n in erwartet if n not in texte]
    if fehlend:
        fehler.append(f"no per-command help generated for: {fehlend}")

    # Jedes Kommando: mindestens ein Beispiel ('$ ' im generierten Block).
    ohne_beispiel = sorted(n for n, t in texte.items() if "      $ " not in t)
    if ohne_beispiel:
        fehler.append(
            f"CL-3 wants at least one worked example per command, these have none: "
            f"{ohne_beispiel}\n      -> add a '> kmixdeck …' line right below the "
            f"': …' explanation in docs/kmixdeck.md")

    # Jedes Kommando: die Synopsis muss den Kommandonamen tragen.
    ohne_synopsis = sorted(n for n, t in texte.items() if f"  {n}" not in t)
    if ohne_synopsis:
        fehler.append(f"no synopsis line naming the command: {ohne_synopsis}")

    # CL-3 verlangt, dass die TOP-LEVEL-Hilfe kurz bleibt: eine Schirmseite.
    kurz = gd.als_help(teile)
    zeilen = kurz.count("\n")
    if zeilen > 60:
        fehler.append(f"the top-level --help is {zeilen} lines — CL-3 wants one screen "
                      f"(<= 60); move detail into the per-command help")

    if fehler:
        for f in fehler:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1

    beispiele = sum(t.count("      $ ") for t in texte.values())
    print(f"CL-3 ok: {len(texte)} commands, {beispiele} worked examples, "
          f"top-level help {zeilen} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
