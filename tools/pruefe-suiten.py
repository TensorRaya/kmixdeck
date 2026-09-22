#!/usr/bin/env python3
"""Jede Integrations-Testdatei MUSS einen ctest-Eintrag haben.

Anlass (2026-09-22): `tests/integration/test_soundboard.py` existierte, war gruen und
committet — stand aber nicht in der foreach-Liste in tests/CMakeLists.txt. Damit liefen
10 CT-8-Tests in KEINEM Vollgate, und das Gate meldete trotzdem "100% tests passed".
Ein gruenes Gate ueber Tests, die nie liefen, ist schlimmer als ein rotes: es beendet
die Suche.

Dieser Test vergleicht die Dateien auf Platte gegen die Namen in CMakeLists.txt. Er
braucht kein ctest und keinen Daemon, laeuft in Millisekunden und traegt darum das
Label `fast`.

Aufruf: pruefe-suiten.py <tests-quellverzeichnis>
"""
import pathlib
import re
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Aufruf: {sys.argv[0]} <tests-quellverzeichnis>", file=sys.stderr)
        return 2
    tests = pathlib.Path(sys.argv[1])
    cml = tests / "CMakeLists.txt"
    if not cml.exists():
        print(f"FEHLER: {cml} nicht gefunden", file=sys.stderr)
        return 2

    dateien = {p.stem[len("test_"):] for p in (tests / "integration").glob("test_*.py")}
    text = cml.read_text()

    # (a) Namen aus der foreach-Liste
    eingetragen = set()
    m = re.search(r"foreach\(suite\s+([^)]+)\)", text)
    if m:
        eingetragen |= set(m.group(1).split())
    # (b) einzeln angelegte Suiten: add_test(NAME integration-<name> ... test_<name>.py
    for name in re.findall(r"integration/test_([a-z0-9_]+)\.py", text):
        eingetragen.add(name)

    fehlen = sorted(dateien - eingetragen)
    verwaist = sorted(n for n in (eingetragen - dateien) if n)

    if fehlen:
        print("FEHLER: diese Integrations-Suiten haben KEINEN ctest-Eintrag und laufen in "
              "keinem Vollgate:", file=sys.stderr)
        for n in fehlen:
            anzahl = len(re.findall(r"^def test_", (tests / "integration" / f"test_{n}.py").read_text(),
                                    re.M))
            print(f"  test_{n}.py  ({anzahl} Tests)", file=sys.stderr)
        print("\nEintragen in tests/CMakeLists.txt (foreach-Liste oder eigener add_test-Block).",
              file=sys.stderr)
    if verwaist:
        print(f"FEHLER: ctest verweist auf Dateien, die es nicht gibt: {verwaist}", file=sys.stderr)

    if fehlen or verwaist:
        return 1
    print(f"ok: {len(dateien)} Integrations-Suiten, alle im ctest eingetragen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
