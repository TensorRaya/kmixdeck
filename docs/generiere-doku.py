#!/usr/bin/env python3
"""Erzeugt aus docs/kmixdeck.md die man page UND den --help-Text.

WARUM Eigenbau statt pandoc (2026-09-20): pandoc kann das, wiegt aber
201 MB installiert und muesste in jeden CI-Lauf und jeden Build-Container.
Gebraucht wird hier eine Teilmenge von Markdown (Ueberschriften,
Definitionslisten, eingerueckte Codebloecke, **fett**/*kursiv*) - das sind
rund 120 Zeilen Konvertierung. scdoc waere mit 46 KB die schlanke
Alternative, verlangt aber ein eigenes Quellformat; Markdown war die
Vorgabe, damit die Quelle auch im GitLab lesbar gerendert wird.

Aufruf:
    generiere-doku.py man   > kmixdeck.1      (groff, man-pages(7))
    generiere-doku.py help                   (Kurzfassung fuer --help)
    generiere-doku.py pruefen                (Abschnitte gegen man-pages(7))

Konventionen, die hier durchgesetzt werden:
  - man-pages(7): Abschnittsreihenfolge NAME, SYNOPSIS, DESCRIPTION,
    OPTIONS, ..., EXIT STATUS, FILES, SEE ALSO, NOTES; Zeilen <= 75 Zeichen.
  - clig.dev: --help ist die KURZE Stufe (eine Bildschirmseite), Beispiele
    stehen vor den Optionen, am Ende ein Zeiger auf die Vollreferenz.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

QUELLE = Path(__file__).resolve().parent / "kmixdeck.md"

# Reihenfolge laut man-pages(7). Abschnitte, die wir nicht fuehren, fehlen
# einfach; unbekannte Namen sind ein Fehler, damit sich keine Eigenkreation
# einschleicht.
ERLAUBT = [
    "NAME", "SYNOPSIS", "DESCRIPTION", "EXAMPLES", "OPTIONS",
    "COMMANDS", "REFERENCES", "EXIT STATUS", "FILES", "SEE ALSO", "NOTES",
]
# In --help kommt nur diese Auswahl - eine Seite, nicht die Vollreferenz.
FUER_HELP = ["NAME", "SYNOPSIS", "EXAMPLES", "OPTIONS"]


def lese() -> list[tuple[str, list[str]]]:
    text = QUELLE.read_text(encoding="utf-8")
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    teile: list[tuple[str, list[str]]] = []
    name = None
    puffer: list[str] = []
    for zeile in text.splitlines():
        m = re.match(r"^#\s+(.+?)\s*$", zeile)
        if m:
            if name:
                teile.append((name, puffer))
            name, puffer = m.group(1), []
        elif name:
            puffer.append(zeile)
    if name:
        teile.append((name, puffer))
    return teile


def _fuege(teile: list[str]) -> str:
    """Fortsetzungszeilen zusammensetzen.

    Ein Bindestrich am Zeilenende der QUELLE ist Teil des Wortes
    ("Eingabe-\nkanal" = "Eingabekanal"), kein Trennzeichen mit Leerraum.
    Naives " ".join() erzeugte daraus "Eingabe- kanal".
    """
    aus = ""
    for t in (x.strip() for x in teile if x.strip()):
        if not aus:
            aus = t
        elif aus.endswith("-"):
            aus += t
        else:
            aus += " " + t
    return aus


def _esc(s: str) -> str:
    """groff-Sonderzeichen entschaerfen, dann Auszeichnung uebersetzen."""
    s = s.replace("\\", "\\e")
    # UTF-8-Typografie -> groff-Escapes. Ohne das meldet `groff -ww -z`
    # "invalid input character code 128/148/151" und die Zeichen fehlen oder
    # erscheinen als Muell in der Ausgabe (gemessen 2026-09-21: 20 Warnungen).
    for zeichen, ersatz in (("\u2014", "\\(em"), ("\u2013", "\\(en"),
                            ("\u201c", "\\(lq"), ("\u201d", "\\(rq"),
                            ("\u2018", "\\(oq"), ("\u2019", "\\(cq"),
                            ("\u2026", "\\&..."), ("\u2192", "\\(->"),
                            ("\u00d7", "\\(mu"), ("\u2264", "\\(<="),
                            ("\u2265", "\\(>="), ("\u2248", "\\(~="),
                            ("\u00b7", "\\(bu"), ("\u00a0", "\\ "),
                            ("\u2190", "\\(<-"), ("\u2212", "\\-")):
        s = s.replace(zeichen, ersatz)
    # Akzentbuchstaben (im Slug-Beispiel "Ünïcode Name!") als groff-Composite:
    # \[u00DC] o.ae. braucht -Tutf8; \(:U ist das portable Umlaut-Composite und
    # rendert auch auf -Tascii lesbar.
    for zeichen, ersatz in (("\u00dc", "\\(:U"), ("\u00fc", "\\(:u"),
                            ("\u00c4", "\\(:A"), ("\u00e4", "\\(:a"),
                            ("\u00d6", "\\(:O"), ("\u00f6", "\\(:o"),
                            ("\u00df", "\\(ss"), ("\u00ef", "\\(:i"),
                            ("\u00e9", "\\('e"), ("\u00e8", "\\(`e")):
        s = s.replace(zeichen, ersatz)
    s = re.sub(r"\*\*(.+?)\*\*", r"\\fB\1\\fR", s)
    s = re.sub(r"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)", r"\\fI\1\\fR", s)
    if s.startswith("."):
        s = "\\&" + s
    return s


def als_man(teile: list[tuple[str, list[str]]], version: str) -> str:
    aus = [
        f'.TH KMIXDECK 1 {date.today():%Y-%m-%d} "kmixdeck {version}" '
        '"kmixdeck manual"',
        ".nh",          # keine Silbentrennung
        ".ad l",        # linksbuendig, kein Blocksatz
    ]
    for name, zeilen in teile:
        aus.append(f".SH {name}")
        # SYNOPSIS: jede Aufrufvariante auf eigene Zeile. Ohne .br fuellt
        # groff sie zu einem Absatz zusammen ("kmixdeck channel add kmixdeck
        # --watch ..."), was unlesbar ist.
        if name == "SYNOPSIS":
            erste = True
            for z in zeilen:
                if not z.strip():
                    continue
                if not erste:
                    aus.append(".br")
                aus.append(_esc(z.strip()))
                erste = False
            continue
        i = 0
        offen_tp = False
        while i < len(zeilen):
            z = zeilen[i]
            nackt = z.strip()
            if not nackt:
                i += 1
                continue
            # Definitionsliste:  Begriff \n :   Erklaerung
            if i + 1 < len(zeilen) and zeilen[i + 1].lstrip().startswith(":"):
                aus.append(".TP")
                aus.append(_esc(nackt))
                erkl = [zeilen[i + 1].lstrip()[1:].strip()]
                i += 2
                while i < len(zeilen) and zeilen[i].startswith("    ") \
                        and not zeilen[i].lstrip().startswith(":"):
                    erkl.append(zeilen[i].strip())
                    i += 1
                aus.append(_esc(_fuege(erkl)))
                offen_tp = True
                continue
            # Unterabschnitt '## Titel' -> .SS. Ohne diesen Zweig landet die
            # Markdown-Zeile als Text mitten im Absatz ("## Port references
            # <ref> names hardware ports") — gemessen 2026-09-21 in REFERENCES.
            if nackt.startswith("## "):
                if offen_tp:
                    aus.append(".PP")
                    offen_tp = False
                aus.append(".SS " + _esc(nackt[3:].strip()))
                i += 1
                continue
            # Fenced Codeblock ```…``` (Markdown-Norm in kmixdecks docs/).
            # Ohne diesen Zweig landen die Zaunzeilen als Text in der man page
            # und groff fliesst die Befehle zu einem Absatz zusammen
            # ("```sh kmixdeck cell set game stream -12dB ```") — gemessen
            # 2026-09-21 in EXAMPLES, unlesbar.
            if nackt.startswith("```"):
                i += 1
                if offen_tp:
                    aus.append(".RS")
                else:
                    aus.append(".PP")
                    aus.append(".RS 4")
                aus.append(".EX")
                while i < len(zeilen) and not zeilen[i].strip().startswith("```"):
                    aus.append(_esc(zeilen[i]) if zeilen[i].strip() else "")
                    i += 1
                i += 1     # schliessender Zaun
                aus.append(".EE")
                aus.append(".RE")
                aus.append(".PP")
                continue
            # Eingerueckter Codeblock
            if z.startswith("    "):
                if offen_tp:
                    aus.append(".RS")
                else:
                    # Leerzeile vor dem Block, sonst klebt das Beispiel am
                    # erklaerenden Satz davor.
                    aus.append(".PP")
                    aus.append(".RS 4")
                aus.append(".EX")
                while i < len(zeilen) and (zeilen[i].startswith("    ")
                                          or not zeilen[i].strip()):
                    if zeilen[i].strip():
                        aus.append(_esc(zeilen[i][4:]))
                    i += 1
                aus.append(".EE")
                aus.append(".RE")
                # Absatz NACH dem Block, sonst klebt der naechste
                # Erklaerungssatz direkt an der letzten Codezeile.
                aus.append(".PP")
                continue
            if offen_tp:
                aus.append(".PP")
                offen_tp = False
            aus.append(_esc(nackt))
            i += 1
    return "\n".join(aus) + "\n"


def _kommandos(zeilen: list[str]) -> list[tuple[str, str]]:
    """Kommandonamen + Kurztext aus dem COMMANDS-Abschnitt (CL-3, CL-9).

    Die Definitionszeilen sehen so aus:

        `mix list` · `mix add <name>` · `mix remove <slug>`
        : As for channels.

    Genommen wird das ERSTE Wort jeder Backtick-Marke — das ist das Kommando,
    wie es die Dispatch-Tabelle in src/cli/main.cpp kennt (`mix`, `cell`, …).
    Unterkommandos (`add`, `remove`) gehoeren in die Pro-Kommando-Hilfe (CL-3),
    nicht in die Uebersicht. Reihenfolge: erstes Auftreten, damit `status` oben
    steht und nicht alphabetisch zwischen `scene` und `streamdeck` verschwindet.
    """
    erklaerung: dict[str, str] = {}
    folge: list[str] = []
    i = 0
    while i < len(zeilen):
        z = zeilen[i]
        marken = re.findall(r"`([^`]+)`", z)
        # Nur Zeilen, die wirklich Kommandos definieren: es folgt eine ':'-Zeile.
        if marken and i + 1 < len(zeilen) and zeilen[i + 1].lstrip().startswith(":"):
            text = _klartext(zeilen[i + 1].lstrip()[1:].strip())
            kurz = re.split(r"(?<=\.)\s", text)[0].rstrip(".")
            for m in marken:
                name = m.split()[0].strip("<>[]|")
                if not name or not name[0].isalpha():
                    continue
                if name not in erklaerung:
                    erklaerung[name] = kurz
                    folge.append(name)
            i += 2
            continue
        i += 1
    return [(n, erklaerung[n]) for n in folge]


def _unterabschnitte(zeilen: list[str]) -> list[tuple[str, list[str]]]:
    """'## name'-Bloecke eines Abschnitts aufteilen -> [(name, inhalt), …].

    Gemeinsam genutzt von als_help (Kommandoliste) und CL-9 (Abgleich gegen die
    Dispatch-Tabelle in src/cli/main.cpp). Eine Quelle fuer die Kommandonamen.
    """
    raus: list[tuple[str, list[str]]] = []
    name: str | None = None
    inhalt: list[str] = []
    for z in zeilen:
        if z.startswith("## "):
            if name is not None:
                raus.append((name, inhalt))
            name = z[3:].strip()
            inhalt = []
        elif name is not None:
            inhalt.append(z)
    if name is not None:
        raus.append((name, inhalt))
    return raus


def als_help(teile: list[tuple[str, list[str]]]) -> str:
    """docs/kmixdeck.md -> der `--help`-Text (CL-3: eine Schirmseite).

    Reihenfolge NAME · USAGE · COMMANDS (ein Zeile je Kommando) · OPTIONS ·
    EXAMPLES · Zeiger auf `man kmixdeck`. Ueberschriften englisch, weil das
    Programm englisch ist — die deutschen Titel kamen aus dem tbload-Vorbild
    und waren hier ein Fremdkoerper.
    """
    d = dict(teile)
    aus: list[str] = []
    kopf = " ".join(x.strip() for x in d.get("NAME", []) if x.strip())
    aus.append(kopf.replace(" - ", " — ", 1))
    aus.append("")
    aus.append("USAGE")
    for z in d.get("SYNOPSIS", []):
        if z.strip() and not z.strip().startswith("```"):
            aus.append("  " + _klartext(z.strip()))

    # COMMANDS: eine Zeile je Kommando. Die '## '-Titel sind Themengruppen
    # ("Overview and state", "Mixes") — richtig fuer die man page, unbrauchbar
    # als Kommandoliste. Die echten Namen stehen in den `…`-Definitionszeilen,
    # also erste Marke jeder Zeile nehmen und nach erstem Wort gruppieren.
    # CL-9 prueft diese Menge gegen die Dispatch-Tabelle in src/cli/main.cpp.
    befehle = _kommandos(d.get("COMMANDS", []))
    if befehle:
        aus.append("")
        aus.append("COMMANDS")
        for name, erkl in befehle:
            aus.append(f"  {name:11} {erkl}"[:94].rstrip())

    for name, titel in (("OPTIONS", "OPTIONS"), ("EXAMPLES", "EXAMPLES")):
        zeilen = d.get(name, [])
        if not zeilen:
            continue
        aus.append("")
        aus.append(titel)
        i = 0
        while i < len(zeilen):
            z = zeilen[i]
            if not z.strip():
                i += 1
                continue
            if i + 1 < len(zeilen) and zeilen[i + 1].lstrip().startswith(":"):
                begriff = _klartext(z.strip())
                erkl = [zeilen[i + 1].lstrip()[1:].strip()]
                i += 2
                while i < len(zeilen) and zeilen[i].startswith("    ") \
                        and not zeilen[i].lstrip().startswith(":"):
                    erkl.append(zeilen[i].strip())
                    i += 1
                text = _klartext(_fuege(erkl))
                kurz = re.split(r"(?<=\.)\s", text)[0]
                aus.append(f"  {begriff:22} {kurz}")
                continue
            if z.strip().startswith("```"):
                i += 1
                continue
            # 🔴 Fortsetzungszeilen eines Absatzes anhaengen statt als eigene
            # Zeile ausgeben: sonst reisst ein umgebrochener Satz mitten im Wort
            # auseinander und liest sich wie ein eigener Eintrag (gemessen
            # 2026-09-21: "`interfaces/*.xml` and are stable." stand allein da).
            if z.startswith("    "):
                aus.append("      " + _klartext(z.strip()))
            elif aus and aus[-1].startswith("  ") and not aus[-1].strip().startswith("kmixdeck") \
                    and len(aus[-1]) + len(z.strip()) < 94 and not z.startswith("`"):
                aus[-1] = aus[-1].rstrip() + " " + _klartext(z.strip())
            else:
                aus.append("  " + _klartext(z.strip()))
            i += 1
    aus.append("")
    aus.append("Full reference, including what the numbers mean:")
    aus.append("  man kmixdeck")
    return "\n".join(aus) + "\n"


def _klartext(s: str) -> str:
    s = re.sub(r"\*\*(.+?)\*\*", r"\1", s)
    s = re.sub(r"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)", r"\1", s)
    return s


def pruefen(teile: list[tuple[str, list[str]]]) -> int:
    namen = [n for n, _ in teile]
    fehler = []
    unbekannt = [n for n in namen if n not in ERLAUBT]
    if unbekannt:
        fehler.append(f"Abschnitt nicht in man-pages(7)-Liste: {unbekannt}")
    for pflicht in ("NAME", "SYNOPSIS", "DESCRIPTION"):
        if pflicht not in namen:
            fehler.append(f"Pflichtabschnitt fehlt: {pflicht}")
    stelle = [ERLAUBT.index(n) for n in namen if n in ERLAUBT]
    if stelle != sorted(stelle):
        fehler.append(f"Reihenfolge weicht von man-pages(7) ab: {namen}")
    # NAME muss "name - beschreibung" sein, sonst findet apropos/whatis nichts.
    nz = " ".join(x.strip() for x in dict(teile).get("NAME", []) if x.strip())
    if not re.match(r"^\S+ - \S", nz):
        fehler.append(f"NAME nicht als 'befehl - beschreibung': {nz!r}")
    for f in fehler:
        print(f"FEHLER: {f}", file=sys.stderr)
    print("Abschnitte ok" if not fehler else f"{len(fehler)} Fehler",
          file=sys.stderr)
    return 1 if fehler else 0


def _version() -> str:
    """Version fuer die .TH-Zeile der man page.

    Reihenfolge: KMIXDECK_VERSION aus der Umgebung (so setzen CMake und
    die Paketbauer sie, die kennen die echte Paketversion) -> Git-Tag ->
    Platzhalter. Im Build-Container gibt es weder Git-Historie noch Tags,
    deshalb reicht "git describe" allein nicht.
    """
    aus_umgebung = os.environ.get("KMIXDECK_VERSION", "").strip()
    if aus_umgebung:
        return aus_umgebung.lstrip("v")
    try:
        r = subprocess.run(["git", "describe", "--tags", "--abbrev=0"],
                           capture_output=True, text=True, timeout=5,
                           cwd=QUELLE.parent)
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip().lstrip("v")
    except (OSError, subprocess.SubprocessError):
        pass
    return "0"


def als_header(teile: list[tuple[str, list[str]]]) -> str:
    """docs/kmixdeck.md -> C++-Header mit dem --help-Text (CL-1).

    Warum ueberhaupt ein Header: bis 2026-09-21 stand in src/cli/main.cpp ein
    38-zeiliger, handgepflegter Hilfetext in setApplicationDescription() —
    genau das Duplikat, das CL-1 verbietet. Der Drift war schon messbar: dem
    Handtext fehlten `devices ports`, `devices virtual`, `channel wire`,
    `mix wire`, `mix get` und `fx clear`. Jetzt kommt der Text aus derselben
    Quelle wie man page und docs/cli.md, und CL-9 prueft die Kommandonamen
    gegen die Dispatch-Tabelle.
    """
    text = als_help(teile)
    aus = [
        "// CL-1: GENERIERT aus docs/kmixdeck.md — nicht von Hand aendern.",
        "// Erzeugt von docs/generiere-doku.py (Ziel `header`), eingehaengt in",
        "// src/CMakeLists.txt. Aenderungen gehoeren in docs/kmixdeck.md.",
        "#pragma once",
        "",
        "namespace kmixdeck {",
        "/// Der --help-Text, wortgleich mit `man kmixdeck` (dort vollstaendig).",
        "inline constexpr const char *HILFE_TEXT =",
    ]
    for zeile in text.rstrip("\n").split("\n"):
        # C-String-Literal: Backslash und Anfuehrungszeichen entschaerfen.
        sicher = zeile.replace("\\", "\\\\").replace('"', '\\"')
        aus.append(f'    "{sicher}\\n"')
    aus.append("    ;")
    aus.append("}   // namespace kmixdeck")
    aus.append("")
    return "\n".join(aus)


def main() -> int:
    was = sys.argv[1] if len(sys.argv) > 1 else "man"
    teile = lese()
    if was == "man":
        sys.stdout.write(als_man(teile, _version()))
    elif was == "help":
        sys.stdout.write(als_help(teile))
    elif was == "header":
        sys.stdout.write(als_header(teile))
    elif was == "pruefen":
        return pruefen(teile)
    else:
        print(f"unbekannt: {was} (man | help | header | pruefen)", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
