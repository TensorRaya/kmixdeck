# SPDX-License-Identifier: GPL-3.0-or-later
"""Kein Testlauf darf Daemons hinterlassen — auch nicht, wenn er hart stirbt.

🔴 Gemessen 2026-09-21: drei Sandbox-Daemons (pipewire, wireplumber, kmixdeckd) liefen
30.856 s — 8,5 Stunden — nach dem Ende ihres Laufs weiter, einer bei 6,9 % CPU auf einer
4-Kern-Maschine; dazu fuenf verwaiste /tmp/kmixdeck-pw-*-Verzeichnisse. Die Grundlast
der Maschine lag dadurch bei 9–10 OHNE laufenden Test.

Warum das die Testergebnisse verfaelscht: ein PipeWire-Graph ist soft-realtime. Verpasst
er die Deadline fuer einen Periodenpuffer, liefert er Stille statt Signal. Genau so sehen
die Fehlschlaege aus, die bisher als "nur bei -j2" und "geteilte PipeWire-Ressourcen"
abgehakt wurden:

    last reading -inf dB     — im Aufnahmefenster kam gar kein Signal an
    last reading -40.33 dB   — Signal kam, aber ein Teil des Puffers war Stille
    did not happen within 5s — die Property-Aenderung des Daemons kam zu spaet

Nicht die Parallelitaet war die Ursache, sondern die Leichen der vorherigen Laeufe. Der
Unterschied ist wichtig: bei Parallelitaet waere ein RESOURCE_LOCK die Loesung, bei
Leichen ist es Aufraeumen. Das eine kostet Laufzeit, das andere nicht.

Warum PR_SET_PDEATHSIG und nicht ein atexit-Handler oder try/finally: die Sandbox hat
bereits close() mit _stop_procs(), und das ist korrekt — es laeuft nur nicht, wenn pytest
selbst per SIGKILL oder ctest-TIMEOUT wegstirbt. PR_SET_PDEATHSIG legt die Aufgabe in den
Kernel: der schickt dem Kind ein Signal, sobald der Elternprozess endet, egal wie. Kein
Python-Code kann das fuer den SIGKILL-Fall leisten.

Diese Datei setzt es fuer JEDEN Popen der Integrationssuite durch, nicht nur fuer die
Sandbox — kmixdeckd, dbus-daemon, chrome, pw-loopback und busctl-monitor werden an ueber
einem Dutzend Stellen gestartet, und jede einzeln zu patchen bricht beim naechsten neuen
Test wieder.
"""
from __future__ import annotations

import ctypes
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

PR_SET_PDEATHSIG = 1


def _stirb_mit_eltern() -> None:
    """Im Kind, nach fork und vor exec: SIGKILL, sobald der Elternprozess endet."""
    ctypes.CDLL("libc.so.6", use_errno=True).prctl(PR_SET_PDEATHSIG, 9, 0, 0, 0)


_ECHTER_POPEN = subprocess.Popen


class _PopenMitTotmannschalter(_ECHTER_POPEN):  # type: ignore[misc,valid-type]
    """subprocess.Popen, aber jedes Kind stirbt mit diesem Prozess.

    preexec_fn laeuft im Kind zwischen fork und exec. Setzt ein Test selbst ein
    preexec_fn, rufen wir beides auf — sonst wuerde unser Schalter das fremde
    verdraengen (oder umgekehrt).
    """

    def __init__(self, *args, **kwargs):
        fremd = kwargs.get("preexec_fn")
        if fremd is None:
            kwargs["preexec_fn"] = _stirb_mit_eltern
        else:
            def beides() -> None:
                _stirb_mit_eltern()
                fremd()
            kwargs["preexec_fn"] = beides
        super().__init__(*args, **kwargs)


@pytest.fixture(scope="session", autouse=True)
def _kein_daemon_ueberlebt_den_lauf():
    """Totmannschalter fuer alle Kindprozesse der Suite, plus Aufraeumen am Ende."""
    subprocess.Popen = _PopenMitTotmannschalter  # type: ignore[misc]
    vorher = set(Path(tempfile.gettempdir()).glob("kmixdeck-pw-*"))
    # Verzeichnisse aus HART abgebrochenen Laeufen: deren Prozesse sind durch
    # PR_SET_PDEATHSIG tot, aber das rmtree im finally-Block lief nie. Gemessen
    # 2026-09-21: nach einem SIGKILL auf pytest blieb genau ein solches Verzeichnis
    # zurueck. Ohne diesen Schritt sammeln sie sich unbegrenzt (fuenf Stueck waren es,
    # bevor ich hingesehen habe). Nur Verzeichnisse ohne lebenden Prozess anfassen,
    # damit ein parallel laufender Testlauf nichts unter den Fuessen verliert.
    for d in vorher:
        if not _hat_lebenden_prozess(d):
            shutil.rmtree(d, ignore_errors=True)
    try:
        yield
    finally:
        subprocess.Popen = _ECHTER_POPEN  # type: ignore[misc]
        for d in set(Path(tempfile.gettempdir()).glob("kmixdeck-pw-*")) - vorher:
            shutil.rmtree(d, ignore_errors=True)


def _hat_lebenden_prozess(verzeichnis: Path) -> bool:
    """Laeuft noch ein Prozess mit diesem XDG_RUNTIME_DIR? (Dann Finger weg.)"""
    for eintrag in Path("/proc").iterdir():
        if not eintrag.name.isdigit():
            continue
        try:
            umgebung = (eintrag / "environ").read_bytes().decode("utf-8", "replace")
        except OSError:
            continue        # Prozess ist zwischen iterdir und read weggegangen, oder fremd
        if f"XDG_RUNTIME_DIR={verzeichnis}\0" in umgebung:
            return True
    return False


def pytest_report_header(config) -> str:
    """Im Kopf jedes Laufs sichtbar: wieviel Last schon da war, bevor gemessen wird.

    Bei einem Fehlschlag in einer Audiomessung ist das die erste Frage — und ohne
    diese Zeile steht sie in keinem Log. 1,0 pro Kern ist die Grenze, ab der ein
    soft-realtime-Graph seine Deadlines reisst.
    """
    last = os.getloadavg()[0]
    kerne = os.cpu_count() or 1
    warnung = "  🔴 ueberbucht, Audiomessungen sind unzuverlaessig" if last > kerne else ""
    return f"host load {last:.2f} on {kerne} cores{warnung}"
