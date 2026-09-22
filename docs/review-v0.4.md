# Code-Review v0.4 — Zeiger und Journal

**Auftrag (Michel, 2026-09-22):** „Dann das nochmal ganzheitlich. Aber nicht die
Commits squashen. Guck nur, dass die Commits sauber, atomar und immer eine Sache
behandeln."

Das ist die Wiederholung des Reviews von v0.3 (`docs/review-v0.3.md`, Stand
`5f2788d`) auf dem heutigen Stand — plus ein zehnter Punkt, der beim letzten Mal
nicht dabei war: **Atomarität der Commit-Historie**. Kein Squash diesmal.

**Ausgangsstand (gemessen 2026-09-22):**

- Branch `release/0.3`, HEAD `8262f94` = `origin/release/0.3`, Arbeitsbaum sauber
- **18 Commits** seit Tag `v0.3.0` (`1552844`), 34 seit `v0.2.0`
- 78 Quelldateien, 21.284 Zeilen: C++ 9.778 · Test-Python 7.465 · QML 4.287 · Web 1.946
- `./build/bin/kmixdeck --version` → `kmixdeck 0.3.0`
- Letztes Vollgate 15:18 Uhr: 27/27 grün, GATE=0, 1.556 s

**Offen übernommen aus v0.3:** B2 — Configs werden im Betrieb nicht neu geladen
(kein `QFileSystemWatcher`), damals als `ops-avsc6` vertagt.

---

## Zeiger — die 10 Punkte

| # | Punkt | Status | Befunde |
|---|---|---|---|
| 1 | Sauberer Code | **ok** | ruff all passed · qmllint 1/1 (5,5 s) · 0× TODO/FIXME/HACK/XXX · 0 Debug-Reste (`qDebug`/`console.log`) · `src/mixer.cpp` 2.018 Zeilen aber 171 Methoden, längste 102 (`Mixer::onNode`) — gegliedert, kein Monolith |
| 2 | Keine internen Referenzen / Ausnahmen | **ok** | Produktivcode (src, web, streamdeck, interfaces, data, tools, po): 0 Hostnamen, 0 Personen/Profilnamen, 0 IPs, 0 `/home/`. Keine Geräte-Sonderfälle im Code. Treffer nur in `docs/review/*` (Altdokument, das genau deren Entfernung protokolliert) und als Quellenangabe „Michel 2026-09-xx" in `requirements.md` — korrekt und gewollt |
| 3 | Portierbarkeit | **ok** | XDG durchgehend (`QStandardPaths` an 8 Stellen, `ConfigLocation`/`GenericDataLocation`/`PicturesLocation`); keine `/home/`-Hardcodes; `LADSPA_PATH` wird **zuerst** gelesen, dann lib/lib64/local + Debian-Triplet aus dem Compiler (`KMIXDECK_MULTIARCH`), nicht geraten; Mindestversionen gesetzt (cmake 3.20, Qt 6.6, KF 6.0, libpipewire ≥ 1.0) |
| 4 | Best Practices | läuft | C++20 + `STANDARD_REQUIRED`; `-Wall -Wextra` dauerhaft in CMakeLists (B1-Fix aus v0.3, 56× in build.ninja nachweisbar). Scharfe Gegenmessung mit `-Wshadow -Wconversion -Wpedantic` → **C1** |
| 5 | Tests | **ok** | 27 ctest-Suiten, 200 Tests gesammelt; 12 Unit-Tests für die Persistenz; **0 Skips im Vollgate** (`grep -ic skip` → 0) |
| 6 | Integrationstests | **ok** | 189 Integrationstests in 12 Dateien: service_cli 44 · lifecycle 24 · ports 20 · frontends_sync 19 · web 16 · routing 16 · presentation 15 · audio_graph 11 · soundboard 10 · fx 9 · shortcuts 3 · streamdeck 2. 18 Skip-Bedingungen vorhanden, aber **keine greift hier** — alle Abhängigkeiten da (Chrome, LADSPA in `/usr/lib/ladspa`, websockets 15.0.1, gi, pulsectl, kglobalacceld, Xvfb) |
| 7 | Anforderungen | **ok** | sot-audit: **121 ✅ · 1 🔶 · 0 📝** (122 Zeilen, 8 core-tier, 8 in jedem Frontend bewiesen). Offen nur CT-1 (physischer Tastendruck) |
| 8 | Persistenzschicht | **ok** | `QSaveFile` + `commit()` an 2 Schreibstellen = atomar; Schema-`version` (aktuell 2); v1-Migration (`outputDevice`-String); Datei aus neuerer Version wird **beiseitegelegt statt heruntergestuft**; korrupte Datei wird abgelehnt ohne den Zustand anzufassen. 12 Unit-Tests deckeln das ab, inkl. Golden-Conf |
| 9 | Plug and Play (Daten + Configs im Betrieb) | **B2 behoben** | Geräte: ja — `nodeAdded`-Signal, Hotplug dreifach getestet (`test_dv13_hotplug_new_multiport_device_is_listed_wired_replugged_and_persisted` mit gemessenem Audio über alle sechs Schritte, `test_dv9_dv12_unplug_greys_out_and_replug_restores`, CT-3). Configs: **jetzt auch ja** — `QFileSystemWatcher` auf `layout.json` und sein Verzeichnis, 300 ms Entprellung, Schleifenbremse über die zuletzt geschriebenen Bytes; externe Änderung in **0,50 s** übernommen, eigene Schreibvorgänge lösen 0 Reloads aus, kaputte Datei wird abgewiesen. Neu als **DV-32** in `requirements.md` mit drei Tests |
| 10 | **Commit-Historie atomar** (neu) | **C3, C4** | 18 Commits, Median 8 Dateien, größter 30. 7/18 mit Conventional-Präfix, 11 mit Anforderungs-ID oder freiem Text (C3). Vier Commits bündeln mehrere Anforderungen, `dd64d25` allein vier (C4). CONTRIBUTING sagt nichts über Commit-Form — das ist die Ursache |
| 11 | Doku CLI / Web / UI | **C5, C6, C7 behoben** | CLI vorbildlich: `docs/cli.md` ist absichtlich nur ein Zeiger, die Referenz steht in `kmixdeck.md` (eine Quelle → man/`--help`/Web), **alle 26 Kommandos** darin enthalten. Frontend-Dokus haben Lücken: loudness/LUFS, soundboard/sample und scene stehen in je 3 QML- und 2–4 Web-Dateien im Code, in `window.md` und `web.md` aber **0×** — jetzt in beiden Dokus plus README ergänzt, gegen den Code verifiziert (die Ziel-Liste ist −14/−16/−18/−23/−9 LUFS, nicht die fünf Werte, die ich zuerst hingeschrieben hatte). Dazu C6 (toter Link) und C7 (erfundene `kmixdeckrc`) |

## Befunde

**C1 — schärfere Warn-Flags: ECMs `-Werror` verhindert die Messung** (Punkt 1, 4)

`-Wall -Wextra` stehen seit v0.3 in CMakeLists und melden 0 Warnungen. Die
Gegenmessung mit `-Wshadow -Wconversion -Wpedantic` findet aber echte Stellen:

    src/fx.cpp:297     declaration of 'n' shadows a previous local   [-Wshadow]
    src/layout.h:42,44 conversion from 'qsizetype' to 'int'          [-Wconversion]

Zwei Fallen dabei selbst erlebt, beide gemessen:

1. KDEs ECM (`KDECompilerSettings`) hängt eigene Flags **nach** `CMAKE_CXX_FLAGS`
   an — darunter `-Werror=return-type`, `-Werror=init-self`, `-Werror=undef` und
   ein generelles `-Werror`. Ein `-Wno-error` in `CMAKE_CXX_FLAGS` wird davon
   überschrieben, der Bau bricht beim ersten Fund ab und man sieht **eine**
   Warnung statt aller.
2. `ninja` ohne `-k 0` hört beim ersten Fehler auf. Die Kombination beider Fallen
   sah aus wie „nur 3 Warnungen", war aber „Abbruch nach der dritten Datei".

Bewertung nach Durchsicht aller 29 Stellen:

| Art | Stellen | Urteil |
|---|---|---|
| `-Wconversion` `qsizetype → int` | 15 | **Rauschen.** `indexOf`/`size()` geben in Qt 6 `qsizetype` (64 bit), die umgebenden Qt-APIs nehmen `int`. Kein Fix — das wegzukonvertieren macht den Code unleserlicher, nicht sicherer |
| `-Wconversion` sonstige (`Promoted<int,longlong>`, `QMap::size_type`) | 2 | dito |
| `-Wshadow` lokales `p` verdeckt `Cli::p` | 4 | **echter Befund → C2.** `Cli::p` ist der `QCommandLineParser` (main.cpp:812). Wer in so einem Block `p.value(...)` schreibt, greift am Parser vorbei — und das compiliert je nach Typ sogar |
| `-Wshadow` lokales `sub`/`o` verdeckt `Cli`-Member | 5 | wie C2, gleiche Ursache, gleiche Stelle im Code |
| `-Wshadow` Lambda-Parameter verdeckt äußeres Local | 3 | `mixerclient.cpp:218/229` (`w` = derselbe Watcher, heute harmlos), `fx.cpp:297` (`n`). Kosmetik, aber genau das Muster, das beim nächsten Umbau still das Falsche trifft |

**C2 — 9 lokale Variablen verdecken Member der `Cli`-Struktur** (Punkt 1, 4)

`src/cli/main.cpp` hat eine `Cli`-Struktur mit den Membern `app`, `p`
(`QCommandLineParser&`), `a` (Argumente). An 9 Stellen deklarieren Kommando-Zweige
lokale `p`, `sub`, `o` mit demselben Namen. Heute funktioniert alles; die Gefahr
ist der nächste Umbau: `p.value("…")` in einem dieser Blöcke geht an den Parser
vorbei, und bei `QString p` compiliert es klaglos.

Umbenennung der 9 Locals ist ein reiner Namensfix ohne Verhaltensänderung —
nachweisbar über ein grünes Gate mit identischem Verhalten.

**C3 — zwei Commit-Stile in einer Historie** (Punkt 10)

18 Commits seit `v0.3.0`, davon **7 mit Conventional-Präfix** (`feat(ux18):`,
`test(ct1):`, `fix(fx):`, `docs:`) und 11 mit Anforderungs-ID als Präfix (`CL-7:`,
`FX-8:`, `fx9:`) oder ganz freiem Text (`Gate-Tempo:`, `Gate-Label:`). Beide Stile
sind für sich lesbar, die Mischung macht `git log --grep` unzuverlässig.

`CONTRIBUTING.md` sagt **nichts** über Commit-Form — deshalb driftet es. Das ist
die Ursache, nicht die Symptome.

**C4 — vier Commits bündeln mehrere Anforderungen** (Punkt 10)

| Commit | Dateien | Bündelt |
|---|---|---|
| `dd64d25` | 10 | CL-1, CL-2, CL-4, CL-9 — **vier** Anforderungen |
| `3fb0419` | 11 | CL-3 + CL-8 |
| `7205f7b` | 4 | Gate-Label-Umbenennung + drei Tests nachregistriert |
| `83e6db5` | 6 | Gate-Beschleunigung + Lock-Ausbau |

Nachträglich aufspalten geht nicht ohne Historie umzuschreiben — und Michel hat
Squash ausdrücklich ausgeschlossen, Rebase wäre derselbe Eingriff in
veröffentlichte Commits. Die Konsequenz gehört also nach vorne: Regel in
CONTRIBUTING, damit die nächsten 18 Commits atomar sind.

Der Rest der Historie ist in Ordnung: Median 8 Dateien pro Commit, jeder Betreff
nennt ein Thema plus die dabei gefundenen Fehler — das ist kein Bündeln, sondern
Ehrlichkeit über den Fund.

**B2 — Konfigs wurden nur beim Start gelesen** (Punkt 9, *behoben*)

Michels Kriterium heißt „Daten und Konfigs automatisch im Betrieb geladen".
`layout.json` wurde **genau einmal** gelesen, im Service-Konstruktor. Eine
Änderung von außen — per Hand, per Config-Management, per synchronisierter Kopie
von einem anderen Rechner — brauchte einen Neustart. Vertagt seit v0.3 als
`ops-avsc6`, jetzt umgesetzt: `QFileSystemWatcher` auf Datei **und**
Verzeichnis, 300 ms Entprellung, Schleifenbremse über die zuletzt geschriebenen
Bytes. Neu: **DV-32** in `requirements.md`, drei Tests, externe Änderung wird in
**0,50 s** übernommen.

**B2, zweiter Schaden: gleichzeitiges Speichern zerstörte die fremde Änderung** (DV-33)

Der erste Fix machte den Test grün — **einmal**. Im Vollgate fiel er wieder um, und dann
6 von 6 Läufen unter Last, während er einzeln nie umfiel. Das war der Hinweis: nicht der
Watcher, sondern ein Wettlauf.

Gemessen im selben Lauf, zwei Zahlen nebeneinander: der Test schrieb **802 Bytes** mit dem
neuen Mix, der Handler las 300 ms später **1128 Bytes** — unsere eigenen. Dazwischen hatte
der Daemon selbst gespeichert (ein Retarget ruft `saveLayout`) und die Änderung von Hand
überschrieben. Die Schleifenbremse verglich danach unsere frischen Bytes mit unserem eigenen
Merker, fand sie gleich und schwieg. Die Änderung war weg, ohne ein Wort im Log.

Das ist kein Testartefakt, das ist Datenverlust: wer `layout.json` von Hand editiert, während
der Daemon aus irgendeinem Grund speichert, verliert seine Arbeit stillschweigend. Unter Last
jedes Mal, im Leerlauf fast nie — deshalb sah es nach Flackern aus.

Zwei Änderungen, weil es zwei Ursachen sind: der Schnappschuss wird jetzt **beim Signal**
genommen statt 300 ms später (`m_layoutFremdeAenderung`), und `saveLayout()` schreibt nicht
über eine Datei, die weder unsere noch unverändert ist. `Layout::loadFromJson` beurteilt die
festgehaltenen Bytes statt erneut von der Platte zu lesen.

Messung: **6 von 6 Läufen rot** vor dem Fix, **6 von 6 grün** danach, jeweils voller
Dateilauf unter Last. Fünf Hypothesen waren vorher widerlegt: fehlendes Verzeichnis,
Vorgänger-Datei, Löschung durch DV-1, `files()` nach rename, Reihenfolge von `saveLayout`
und `watchLayoutFile`.

Was ich mir selbst ankreide: ich habe „B2 ist durch, 27/27" gemeldet, nachdem ich **einen**
grünen Lauf gesehen hatte. Der Test war zu dem Zeitpunkt schon unzuverlässig. Ein einzelner
grüner Lauf ist kein Beweis — bei einem Zeitfehler ist er reine Glückssache.

**C6 — toter Link in der README**

`docs/cli.md#scenes` zeigte auf einen Abschnitt, den es seit CL-1 nicht mehr
gibt (`cli.md` ist seitdem nur noch ein Zeiger auf `kmixdeck.md`). Ein Link, den
niemand prüft, verrottet lautlos. Konsequenz nach vorne: `tools/pruefe-doku-links.py`
prüft **28 interne Links über 34 Dateien** gegen Datei *und* Anker, läuft im
`fast`-Gate (16/16 grün). Gegenprobe: mit dem alten Link wieder rot.

**C7 — die Manpage nannte eine Datei, die es nie gab**

`# FILES` führte `~/.config/kmixdeckrc` — geerbt aus einer Recherche-Notiz von
*vor* der Implementierung (`docs/research/pipewire-kde-technical-notes.md:122`).
Echt sind `~/.config/kmixdeck/layout.json` und das generierte
`pipewire.conf.d/90-kmixdeck.conf`. Beide jetzt korrekt dokumentiert, inklusive
des neuen Live-Reload-Verhaltens.

---

## Journal

Regeln für dieses Journal, aus der Debugging-Reihenfolge:

- **FAKT** = Kommando-Ausgabe, append-only, wird nie umgeschrieben
- **HYPOTHESE** = Vermutung; wird explizit als bestätigt oder widerlegt markiert
- **AKTION** = Eingriff, mit dem Ergebnis daneben
- Eine Prediction vor jedem Fix, danach verifiziert. Ein Mismatch = Plan verwerfen.

### 2026-09-22 — Ausgangsstand

- FAKT: `git log --oneline v0.3.0..HEAD | wc -l` → 18
- FAKT: `git status --porcelain` → leer; `git status -sb` → `## release/0.3...origin/release/0.3`
- FAKT: 78 Quelldateien; `wc -l` gesamt 21.284
- FAKT: `./build/bin/kmixdeck --version` → `kmixdeck 0.3.0`, `CMakeLists.txt:2` → `VERSION 0.3.0` (konsistent)
- FAKT: größte Dateien: `src/mixer.cpp` 2.018 · `src/cli/main.cpp` 1.726 ·
  `tests/integration/test_service_cli.py` 1.417 · `test_ports.py` 1.028 ·
  `test_frontends_sync.py` 991 · `src/frontend/mixerclient.cpp` 819 · `src/daemon/service.cpp` 727
- FAKT: Vollgate 2026-09-22 15:18 (`/var/tmp/gate-20260922-145239.log`): 27/27, GATE=0, 1.556 s

### 2026-09-22 — B2: der Watcher, der ins Leere zeigte

Drei Hypothesen, drei Mal widerlegt — protokolliert, weil die Reihenfolge der
Irrtümer die eigentliche Lehre ist.

- FAKT: Einzellauf `-k b2_` → 1 failed, 2 passed. Voller Dateilauf → dasselbe,
  1 failed / 26 passed. Diagnoseskript außerhalb pytest → **grün, 0,50 s**.
- HYPOTHESE 1: Der Watcher greift nie, weil Datei und Verzeichnis beim
  Konstruktor fehlen (`addPath` gibt still `false` zurück).
  → **bestätigt als echter Fehler**, aber nicht die Ursache des Testfehlschlags:
  nach `mkpath` + geprüftem Rückgabewert war der Einzellauf grün, die Suite rot.
- HYPOTHESE 2: Die Datei existiert in der Suite schon beim Start (der Test davor
  schreibt sie im `finally`). → **widerlegt**, Nachbau grün.
- HYPOTHESE 3: Ein Test löscht `layout.json` (Zeile 227, DV-1), danach zeigt der
  Watch ins Leere. → **widerlegt**, Nachbau mit Löschung grün, erkannt in 0,25 s.
- FAKT (die Messung, die es entschied): Log des Suite-Laufs zeigt
  `applying without restart` **0×**, `does not parse` **1×** (das ist der dritte
  B2-Test), `no layout yet` **2×**. Datei im Moment des Fehlschlags: 802 Bytes,
  `version=2`, `mixes=['monitor','stream','extra','vonhand']`, Daemon lebt.
  Also: Datei korrekt, Daemon wach, Handler feuert **gar nicht**.
- AKTION (Rule Zero, auf die Qt-Annahme angewandt): 40-Zeilen-Reproduktion in
  reinem Qt6, ohne kmixdeck (`/var/tmp/qtwatch.cpp`). Ergebnis:

      addPath(dir) = true
      addPath(datei) = false          (Datei fehlt)
      -- erzeuge die Datei
        >> directoryChanged           ← nur die ERZEUGUNG
      -- ändere den Inhalt
        (kein Signal!)                ← files() ist leer
      -- trage Datei-Watch nach: true
        >> fileChanged                ← erst jetzt

- **ROOT CAUSE**: `directoryChanged` feuert bei Erzeugen und Löschen, **nie** bei
  einer Inhaltsänderung. Mein Kommentar behauptete das Gegenteil. Und da der
  Daemon das Starter-Layout **nach** `watchLayoutFile()` schreibt, wurde der
  Datei-Watch nie gesetzt — der Nachtrag lag im Entprellungs-Handler, der ohne
  Signal nie läuft. Zirkulär.
- AKTION: Re-Arm dorthin, wo die Datei entsteht — in `saveLayout()`, nach jedem
  eigenen Schreiben. Deckt zugleich den Inode-Wechsel durch `QSaveFile` ab.
- PREDICTION: Suite 27/27, `applying without restart` ≥ 1.
  → **getroffen**: 27 passed in 35,77 s.
- FAKT (Gegenprobe): Re-Arm-Zeile ausgeknipst → derselbe Test rot
  (1 failed / 26 passed), Zeile zurück → 27/27. Der Test hängt am Fix.

Lehre: `addPath` gibt einen `bool` zurück, den niemand liest — und eine Annahme
über Fremdverhalten („das Verzeichnis fängt das mit ab") gehört gemessen, nicht
kommentiert. Vier Läufe hätte ich mir gespart, wenn die 40 Zeilen Qt-Reproduktion
am Anfang gestanden hätten statt am Ende.
