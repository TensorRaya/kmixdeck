#!/usr/bin/env python3
"""Jeder interne Link in der Doku MUSS auf eine existierende Datei und eine existierende
Ueberschrift zeigen.

Anlass (2026-09-22, Review v0.4): README verwies auf `docs/cli.md#scenes`. Die Datei gab es,
den Abschnitt nicht — `cli.md` ist seit CL-1 nur noch ein Zeiger auf `kmixdeck.md`, und der
Anker war beim Umzug mitgenommen worden, ohne zu pruefen ob er am Ziel ankommt. Ein toter
Anker faellt keinem auf: der Browser springt an den Dateianfang und die Seite sieht richtig
aus. Gefunden wurde er nur, weil ich im Review JEDEN Link einzeln nachgeschlagen habe — also
genau die Handarbeit, die ein Waechter ersetzt.

Geprueft wird:
  * relative Links `[text](pfad)` — Datei muss existieren
  * Anker `[text](pfad#anker)` — die Zieldatei muss eine Ueberschrift mit diesem Slug haben
  * reine Anker `[text](#anker)` — Ueberschrift in derselben Datei
Nicht geprueft: http(s)-Links (kein Netz im Gate), `mailto:`, Bilder ausserhalb des Repos.

Die Slug-Regel ist die von GitHub/GitLab: klein, Leerzeichen zu `-`, alles ausser
Buchstaben/Ziffern/`-`/`_` faellt weg. `## Loudness (EBU R128)` wird `loudness-ebu-r128`.

Braucht kein ctest, keinen Daemon, kein Netz → Label `fast`.

Aufruf: pruefe-doku-links.py <repo-wurzel>
"""
import pathlib
import re
import sys

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+?)(#[^)\s]*)?\)")
UEBERSCHRIFT = re.compile(r"^(#{1,6})\s+(.*?)\s*$", re.MULTILINE)
UEBERSPRINGEN = ("http://", "https://", "mailto:", "ftp://", "tel:")


def slug(text: str) -> str:
    """GitHub-Ankerregel: klein, Leerzeichen zu -, Sonderzeichen weg."""
    text = re.sub(r"`|\*|_{1,2}", "", text)          # Code- und Kursiv-Marker zaehlen nicht mit
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # Links in Ueberschriften: nur der Text
    text = text.strip().lower().replace(" ", "-")
    return re.sub(r"[^a-z0-9\-_]", "", text)


def anker_von(datei: pathlib.Path) -> set[str]:
    if not datei.exists():
        return set()
    text = datei.read_text(encoding="utf-8", errors="replace")
    return {slug(t) for _, t in UEBERSCHRIFT.findall(text)}


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Aufruf: {sys.argv[0]} <repo-wurzel>", file=sys.stderr)
        return 2
    wurzel = pathlib.Path(sys.argv[1]).resolve()
    if not wurzel.is_dir():
        print(f"FEHLER: {wurzel} ist kein Verzeichnis", file=sys.stderr)
        return 2

    dateien = sorted(
        p for p in [*wurzel.glob("*.md"), *wurzel.glob("docs/**/*.md")]
        if "/build/" not in str(p)
    )
    if not dateien:
        print("FEHLER: keine Markdown-Dateien gefunden", file=sys.stderr)
        return 2

    anker_cache: dict[pathlib.Path, set[str]] = {}
    fehler: list[str] = []
    geprueft = 0

    for datei in dateien:
        text = datei.read_text(encoding="utf-8", errors="replace")
        for zeilennr, zeile in enumerate(text.splitlines(), 1):
            for ziel, anker in LINK.findall(zeile):
                if ziel.startswith(UEBERSPRINGEN):
                    continue
                geprueft += 1
                if ziel:
                    pfad = (datei.parent / ziel).resolve()
                    if not pfad.exists():
                        rel = datei.relative_to(wurzel)
                        fehler.append(f"{rel}:{zeilennr}: Datei fehlt: {ziel}")
                        continue
                else:
                    pfad = datei            # reiner Anker, gleiche Datei
                if not anker:
                    continue
                if pfad.suffix != ".md":    # Anker in Nicht-Markdown pruefen wir nicht
                    continue
                if pfad not in anker_cache:
                    anker_cache[pfad] = anker_von(pfad)
                gesucht = anker[1:].lower()
                if gesucht and gesucht not in anker_cache[pfad]:
                    rel = datei.relative_to(wurzel)
                    zielrel = pfad.relative_to(wurzel) if wurzel in pfad.parents or pfad == wurzel else pfad
                    fehler.append(
                        f"{rel}:{zeilennr}: Abschnitt '#{gesucht}' gibt es in {zielrel} nicht"
                    )

    if fehler:
        print(f"FEHLER: {len(fehler)} toter Doku-Link:", file=sys.stderr)
        for f in fehler:
            print(f"  {f}", file=sys.stderr)
        print(
            "\nEntweder den Link korrigieren oder die Ueberschrift anlegen. Ein Anker, der\n"
            "ins Leere zeigt, springt im Browser stumm an den Dateianfang — niemand merkt es.",
            file=sys.stderr,
        )
        return 1

    print(f"{geprueft} interne Doku-Links in {len(dateien)} Dateien, alle Ziele und Anker vorhanden")
    return 0


if __name__ == "__main__":
    sys.exit(main())
