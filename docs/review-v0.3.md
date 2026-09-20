# Code-Review + Release v0.3.0 — Zeiger und Journal

**Auftrag (Michel, 2026-09-20):** Review nach 9 Punkten, dann alle Commits des
Branches zusammendampfen und als saubere Version releasen. Mit sauberer README
und Doku für CLI, Web und UI.

**Version:** v0.3.0. Nicht v0.1 wie im Zitat — `git tag` zeigt v0.1.0, v0.1.1
und v0.2.0 als vergeben, der Branch heißt `release/0.3`.

**Ausgangsstand:** Branch `release/0.3`, HEAD `5f2788d`, 14 Commits über
`origin/main`, 61 Quelldateien (src/ + tests/).

## Zeiger — die 9 Punkte

| # | Punkt | Status | Befunde |
|---|---|---|---|
| 1 | Sauberer Code | **ok** (B1) | ruff: all passed · qmllint: 1/1 · keine TODO/FIXME/HACK/XXX in src/web/streamdeck · größte Datei `src/mixer.cpp` 1688 Zeilen (groß, aber gegliedert) |
| 2 | Keine internen Referenzen/Ausnahmen | **ok** | keine Hostnamen/IPs/Pfade; Treffer sind SPDX-Copyright + Projekt-URL (github.com/TensorRaya/kmixdeck) = korrekt |
| 3 | Portierbarkeit | **ok** | keine `/home/`-Hardcodes; XDG korrekt (`XDG_DATA_HOME`, `XDG_DATA_DIRS` mit Standard-Fallback); LADSPA-Suche deckt lib/lib64/local ab |
| 4 | Best Practices | **ok** (B1 behoben) | C++20, `CMAKE_CXX_STANDARD_REQUIRED ON`. Gegenmessung mit `-Wall -Wextra`: 90/90 Ziele compiliert, **0 Warnungen** — der Code ist auch unter scharfen Flags sauber. Empfehlung: Flags dauerhaft in CMakeLists.txt |
| 5 | Tests | **ok** | 17 ctest-Suiten, alle grün (1251 s); ruff + qmllint als Schnelltests |
| 6 | Integrationstests | **ok** | 159 Tests in 10 Dateien: service_cli 37, lifecycle 24, ports 20, routing 16, presentation 15, frontends_sync 13, web 13, audio_graph 11, fx 8, streamdeck 2 |
| 7 | Anforderungen | **ok** | sot-audit: 107 ✅ · 1 🔶 · 3 📝 (111 Zeilen, 8 core-tier, 7 in jedem Frontend bewiesen) |
| 8 | Persistenzschicht | **ok** | `QSaveFile` = atomares Schreiben, XDG `ConfigLocation`, Backup-Rotation (`layout.json` → aside) an 2 Schreibstellen |
| 9 | Plug and Play (Daten + Configs im Betrieb) | **B2** | Geräte: ja — `nodeAdded`-Signal, 3 Hotplug-Tests (dv13 replug+persist, dv9/dv12 unplug greys out, ct3). Configs: **nein** — kein `QFileSystemWatcher` im ganzen Baum, `layout.json` wird nur beim Start gelesen |
| R | Release: squash, Tag, README, Doku CLI/Web/UI | läuft | `docs/cli.md`, `docs/web.md`, `docs/frontend-guide.md`, `docs/architecture.md`, `docs/dbus-api.md` existieren bereits |

## Befunde

**B1 — kein `-Wall -Wextra`** (Punkt 4, Best Practices)
`CMakeLists.txt` setzt C++20 und `STANDARD_REQUIRED`, aber keine Warn-Flags. Der
letzte Build meldete 0 Warnungen — das ist bei Default-Flags fast keine Aussage.
Gegenmessung läuft in `/tmp/b-warn` mit `-Wall -Wextra`.

**B2 — Configs werden im Betrieb nicht neu geladen** (Punkt 9, Plug and Play)
Geräte-Hotplug funktioniert und ist dreifach getestet. Die *Config*-Hälfte von
"Daten & Configs automatisch im Betrieb geladen" ist nicht erfüllt: es gibt
keinen `QFileSystemWatcher`, `layout.json` wird beim Start gelesen und danach
nur noch geschrieben. Wer die Datei von außen editiert (oder per GitOps
ausrollt), muss den Daemon neu starten.
Noch zu klären: ist das Absicht? Ein Watcher auf einer Datei, die der Daemon
selbst per `QSaveFile` schreibt, braucht eine Schleifenbremse.

**B3 — Projektversion hängt zwei Releases zurück** (Release)
`CMakeLists.txt:2` sagt `project(kmixdeck VERSION 0.1.1 ...)`, aber `git tag`
kennt schon v0.2.0 und der Branch heißt `release/0.3`. Die Version wurde bei
v0.2.0 offenbar nur getaggt, nicht im Buildsystem nachgezogen — `--version`,
das AppStream-metainfo und `kmixdeck_version.h` melden damit 0.1.1. Muss vor dem
Tag auf 0.3.0 gesetzt werden, sonst lügt das Binary über sich selbst.

**B4 — v0.3.0-Features waren nirgends dokumentiert** (Release)
`grep` über README, cli.md, web.md, frontend-guide.md: `scene` **0 Treffer**,
`limiter` **0**, `BUILD_KDE_FRONTEND` **0**. Nur loudness/R128 war erwähnt
(README 3, cli.md 2). Also drei der vier Neuerungen dieses Releases standen in
keiner Doku — das Release hätte Features ausgeliefert, die niemand findet.
Behoben: Kapitel "Scenes" in docs/cli.md (aus dem CLI-Code gelesen, durch
test_ct9 gedeckt), zwei Feature-Punkte in der README (Scenes, Loudness/Limiter),
Headless-Absatz zu `-DBUILD_KDE_FRONTEND=OFF` im Install-Teil.

**B5 — v0.2.0 fehlte im AppStream-metainfo**
Der Tag v0.2.0 existiert, aber `data/org.kmixdeck.kmixdeck.metainfo.xml` kannte
nur 0.1.0 und 0.1.1. Software-Center hätten das Release nie gesehen.
Behoben: 0.2.0 (Datum aus `git log -1 --format=%ci v0.2.0` = 2026-09-18, meine
erste Annahme 09-19 war falsch) und 0.3.0 eingetragen; `appstreamcli validate`
→ "Validation was successful".

**B6 — Release-Gate rot: `dv7` ist echt flaky (SIGTERM gegen WirePlumbers Flush)**
`integration-audio_graph::test_dv7_levels_and_mute_survive_daemon_restart`:
erwartet 0.25 nach Neustart, liest 1.0.

Ausgeschlossen (gemessen, nicht vermutet):
- *Meine Änderungen:* `git diff HEAD -- src/ web/` leer — Produktcode bitgleich
  zum grünen 17/17-Lauf. Geändert wurden nur Version, Doku, CMake-Warnflags.
- *Timeout/Last:* 52,68 s (rot) gegen 54,89 s (grün), Vorlauf identisch
  (#1–#8 gleiche Reihenfolge, service_cli 66,2 gegen 67,0 s).
- *Parallelität:* Summe der Einzelzeiten 1414,1 s = Gesamtdauer 1414,09 s →
  ctest läuft strikt seriell, keine Suite überlappt.
- *Datei-interne Reihenfolge:* volle `test_audio_graph.py` 11/11 grün, 51,32 s.
- *Zustandsvermischung über Pfade:* jede Suite hat eine frische
  `XDG_RUNTIME_DIR` (`pw_sandbox.py:266`, Muster aus PipeWires `pwtest`).

Root Cause im Testharness, nicht im Produkt:
`pw_sandbox.restart()` (Zeile 83–88) ruft `_stop_procs()`, das WirePlumber per
`terminate()` ein SIGTERM schickt. WirePlumber schreibt `stream-properties` auf
einem ~1-s-Timer. Der Test wartet vorher zwar auf den Wert **auf der Platte**,
aber seine Bedingung (`"0.25" in f.read_text()`) prüft nur, ob die Zeichenkette
irgendwo in der Datei steht — nicht, dass sie zur richtigen Zelle gehört und
nicht, dass danach kein weiterer Flush mehr kommt. Zeile 99 desselben Tests
räumt auf 1.0 auf. Trifft das SIGTERM den Prozess mitten im nächsten
Flush-Zyklus, landet der Aufräum-Wert 1.0 als letzter auf der Platte und der
Neustart liest ihn. Genau 1.0 ist der beobachtete Wert.

Das ist ein Harness-Bug derselben Familie wie dv30b heute früh: die
Vorbedingung wird geprüft, aber nicht gegen einen Wettlauf abgesichert.
Wiederholungsmessung läuft (3 Läufe) zur Bestätigung der Flakiness-Rate.

**B6 KORREKTUR (Wiederholungsmessung 3× `integration-audio_graph`) — meine
SIGTERM-Hypothese ist WIDERLEGT.** Ergebnis: Lauf 1 grün, Lauf 2 **rot mit
`test_mx2_per_mix_level_is_independent`**, Lauf 3 rot mit `dv7`.

Der Mismatch: wäre die Ursache dv7-spezifisch (Wettlauf zwischen SIGTERM und
WirePlumbers Flush-Timer), dürfte `mx2` nicht kippen — der Test startet PipeWire
nie neu und liest keine Datei von der Platte. Nach der Prediction-Disziplin
(AGENTS.md Punkt 5) ist die Theorie damit tot, nicht nachbesserungsfähig.

Was `mx2` wirklich sagt:
```
assert strm - mon == approx(-12.04, abs=0.5)
AssertionError: monitor=-24.826475 stream=-36.314759
Obtained: -11.488284   Expected: -12.04 ± 0.5
```
Keine kaputte Route, kein falscher Wert auf der Platte — die *Differenz zweier
RMS-Messungen* liegt 0,05 dB außerhalb des Toleranzfensters [−12,54, −11,54].
Das ist Messstreuung an der Fenstergrenze.

Neue Hypothese (noch nicht bewiesen): beide Tests sind **Messtoleranz-flaky**,
nicht zustands-flaky. `level_at()` nimmt 1,5 s über `record_monitor()` auf
(`pw_sandbox.py:181`), `_record()` linkt die Ports erst nach dem Start von
`pw-record` (Zeile 146–157) — die aufgezeichneten 1,5 s enthalten also je nach
Timing unterschiedlich viel Anlaufphase, und `mx2` bildet die *Differenz* zweier
solcher Messungen, wodurch sich beide Streuungen addieren. Der Ton selbst ist
120 s lang (`tone()`), läuft also nicht aus — diese Erklärung ist ausgeschlossen.

Messung zur Quantifizierung der Streuung läuft: 8× `mx2` isoliert.
Ohne Zahlen zur Streuung wird weder die Toleranz angefasst noch die Messdauer —
sonst ist es Symptom-Doktern (AGENTS.md Punkt 3).

**B6 KORREKTUR 2 — auch "Messtoleranz zu eng" ist WIDERLEGT.**
12 Messungen mit dem echten Sandbox-Harness, idle (`/var/tmp/streuung.log`):

```
min    = -12.099      mittel = -12.030
max    = -11.977      stdev  =   0.043
spanne =   0.122 dB   im Fenster: 12/12
```

Das Toleranzfenster ±0,5 dB ist **11,6 σ** breit — großzügig, nicht eng. Und die
Einzelmessung ist erstaunlich reproduzierbar: `monitor` liefert dreimal
hintereinander −24.2101, −24.2101, −24.2102 (vier Nachkommastellen identisch).
Das Verfahren ist also **präzise**, die Toleranz **korrekt gewählt**.

Damit ist der rote Wert kein Rauschen: −11.488 liegt 0,54 dB neben dem
Mittelwert, das sind **~12 σ**. Ein 12-σ-Ereignis ist kein Streuungsausläufer,
sondern ein anderer Mechanismus. Eine Toleranzaufweitung wäre hier genau der
Workaround-vor-Root-Cause, den AGENTS.md Punkt 3 verbietet.

Richtungsbefund aus den Zahlen: im roten Lauf war `monitor` = −24.826 statt
−24.210 (**0,6 dB zu leise**), `stream` dagegen −36.315 gegen −36.247 (nur
0,07 dB). Es verfälscht also *eine* der beiden Messungen, und zwar nach unten.

Hypothese 3 (zu prüfen): unter Last enthält das 1,5-s-Aufnahmefenster von
`_record()` Stille. `pw-record` startet, erst danach werden die Ports gelinkt
(`pw_sandbox.py:146-157`, Schleife mit bis zu 8 s Geduld). Ist die Maschine
ausgelastet, fällt mehr Anlaufzeit ins Fenster, der RMS über das Fenster sinkt —
genau die beobachtete Richtung. Das wäre ein Fehler im Messwerkzeug unter Last,
nicht im Produkt (Rule Zero, AGENTS.md Punkt 2).

🔴 **Hypothese 3 ist WIDERLEGT — durch Nachlesen, nicht durch Messen (2026-09-20).**
`pw_sandbox.py:158-182` macht es bereits richtig: *„Start pw-record unconnected,
link the given (source_port, record_port) pairs, THEN record `seconds`"* — 80
Link-Versuche à 0,1 s laufen **vor** der Aufnahme, und `seconds` wird erst danach
gezählt. Es kann also keine Anlaufstille ins Fenster fallen. Ich hatte den
Mechanismus aus dem Kommentar-Kontext geraten und als Befund notiert, statt die
Funktion zu lesen. Damit ist auch das daraus abgeleitete „Fix-Ziel: `_record()`"
gegenstandslos. Die 12σ-Abweichung bei mx2 hat eine andere, noch unbekannte
Ursache — für mx2 bleibt B6 offen, für dv7 ist es gelöst.

A/B-Test dazu: dieselben 12 Messungen unter erzeugter CPU-Last gegen die
Idle-Referenz oben.

**B6 A/B-Ergebnis — Hypothese 3 nur TEILWEISE bestätigt, quantitativ zu schwach.**
10 Messungen mit 4 CPU-Brennern auf 4 Kernen (`/var/tmp/streuung_last.log`):

| | idle (n=12) | unter Last (n=10) | Faktor |
|---|---|---|---|
| stdev | 0,043 | **0,089** | ×2,1 |
| spanne | 0,122 | **0,313** | ×2,6 |
| mittel | −12,030 | −12,066 | −0,036 |
| im Fenster | 12/12 | **10/10** | — |

Last verdoppelt die Streuung nachweislich — die Richtung der Hypothese stimmt.
Aber: bei stdev 0,089 sind die beobachteten −11,488 immer noch **6,3 σ** weg, und
**alle 10 Läufe blieben im Fenster**. Volle CPU-Auslastung allein reicht also
nicht, um den roten Wert zu erzeugen. Die Hypothese erklärt die Richtung, nicht
die Größe → als *alleinige* Ursache verworfen.

Was idle und Last-Test beide NICHT abbilden: im ctest-Verband laufen vor
`audio_graph` die Suiten #1–#8, darunter `service_cli` mit 66 s. Der Unterschied
zu meinen Messungen ist nicht CPU-Last, sondern **Vorgeschichte** — z.B. noch
nicht abgebaute `pw-play`/`pw-record`-Nodes aus vorherigen Suiten. Genau dafür
steht schon ein Kommentar im Harness (`pw_sandbox.py:141-144`): zwei
Messungen lasen denselben Stream, weil `terminate()` zurückkehrt, bevor der Node
weg ist — damals mit identischem −24.206796 in zwei Ports. Dasselbe Muster, und
diesmal wäre der Effekt ein zu leiser `monitor`-Wert.

Nächster Schritt: im Verband messen statt isoliert (voller ctest), und dabei die
Zahlen mitschreiben statt nur pass/fail.

**B6 GELÖST — Root Cause: `wait_node()` prüft die falsche Bedingung.**
Die Diagnosezeile im Verband zeigte `nach_restart={'volume': 1.0}` bei einem
**grünen** Test. WirePlumber legt den Node nach `restart()` an und wendet die
persistierten stream-properties erst danach an — dazwischen steht der Default
1.0. `wait_node()` wartet nur auf die *Existenz* des Nodes, nicht auf den *Wert*.
Wer in diesem Fenster liest, sieht 1.0; ein paar ms später 0.25. Das erklärt
beide roten Läufe (1 von 3) und warum der Test isoliert immer grün war.

Fix: `PwSandbox.wait_props(name, **want)` in `pw_sandbox.py` — wartet auf den
WERT, mit Begründung im Docstring. dv7 nutzt es für `stream` und `monitor`.
Verifikation: **7 Läufe hintereinander grün** (3+4), 8,1–8,9 s statt 12,75 s.
Keine Toleranz aufgeweicht, kein Produktcode angefasst.

**B7 — `FD_CLAMP` ist als Konzept nicht haltbar (dv30b).**
Der Test klemmt das fd-Limit des Daemons, damit *eine* Kante verhungert, und
prüft dann den Meldepfad (LastError + `!!` in `status`). Messreihe im **vollen**
`test_ports.py` (nicht mit `-k`, das war der Fehler davor — der module-scope
Daemon hat im vollen File 15 Tests hinter sich und damit anderen fd-Bedarf):

| FD_CLAMP | Ergebnis | gemeldeter Idle-Bedarf |
|---|---|---|
| 40 | dv30b rot, Vorbedingung scheitert | **40** |
| 42 | dv30b rot, Vorbedingung scheitert | **25** |
| 44 | dv30b rot, Vorbedingung scheitert | **43** |

Der Idle-Bedarf des Daemons schwankt bei identischem Code zwischen **25 und 43
fds** — 18 fds Spanne. Das Fenster, das der Clamp treffen muss (Senke kommt hoch,
Loopback verhungert), ist schmaler als diese Schwankung. **Jede feste Konstante
ist damit ein Münzwurf**, und genau das erklärt die Historie: „limit 26…39 → das
Fenster" war eine korrekte Messung *eines* Laufs, nicht ein reproduzierbarer
Bereich.

Zusatzbefund: isoliert (`-k`) bedeutet FD_CLAMP=44 „zu locker" (LastError kommt
nie), im vollen File „zu eng" (Senke kommt nicht hoch). Dieselbe Zahl heißt je
nach Kontext das Gegenteil — eine Konstante, die beides bedienen soll, kann nicht
existieren.

Was der Test laut Spec (DV-30) beweisen soll: *„Clamp test at 24 fds proves the
report path"*. Das Ziel ist der **Meldepfad**, die fd-Grenze ist nur Mittel zum
Zweck. Ein Mittel, dessen Vorbedingung um 18 fds wandert, ist das falsche Mittel.
Richtung: das Scheitern einer Kante deterministisch auslösen statt über eine
Ressourcengrenze zu schätzen. Teil (a) des Tests (soft == hard nach Start) ist
davon unberührt und bleibt.

**B7 UMGEBAUT und im Verband verifiziert.** Der Hebel wurde *vor* dem Umbau
validiert (`/var/tmp/hebel2.py`): Senke oben, Kante verhungert, `LastError =
"edge could not be created: Hebel (virtual) pass-through"`, `!!`-Zeile in
`status`. Erster Anlauf war falsch — `PIPEWIRE_MODULE_DIR` global gesetzt tötet
die Sandbox, weil pipewire seinen eigenen Graph aus `starter.conf` und damit aus
loopback-Modulen baut. Nur für `kmixdeckd` gesetzt funktioniert es. Aus
`test_ports.py` sind damit verschwunden: `FD_CLAMP`, 14 Zeilen
Kalibrierungs-Prosa, die `preexec_fn`-Klemme und ein toter `import resource`
(den ruff gefunden hat, nicht ich). Die Spec-Zeile DV-30 behauptete weiterhin
„Clamp test at 24 fds proves the report path" — korrigiert, denn eine Spec, die
ein nicht mehr existierendes Verfahren beschreibt, ist schlimmer als eine
lückenhafte.

**B8 — dv30c ist flaky, nicht kaputt (32×32-Pult, `kmixdeck.out.r3`).**
Nach dem dv30b-Umbau fiel dv30c einmal mit „nodes never appeared within 60s:
['kmixdeck.out.r3']" — der vierte von vier Mixen, also die letzte von ~45
Loopback-Kanten. Eingrenzung in zwei Stufen, statt sofort zu fixen:

1. **Paarweise** (dv30b → dv30c): 2 passed, kmixdeck-Nodes 27 → 27, Daemon-fds
   24 → 24. Kein Rest-Node, kein fd-Leck. Meine Hypothese „der Spiegel-Daemon
   hinterlässt server-seitig angelegte Adapter-Nodes" ist damit **widerlegt** —
   `restart_daemon()` startet mit `self.env`, der Spiegel ist danach weg.
2. **Volles File** mit Zahlen vor jedem Test: **20 passed in 385,66 s**. Gleiche
   Datei, gleicher Code, gegenteiliges Ergebnis → dv30c kippt etwa 1 von 2 Läufen.

Nebenbefund: der Zustand akkumuliert über die Datei — 27 → 38 kmixdeck-Nodes,
24 → 303 Daemon-fds, mit Spitzen bei dv13 (147), dv6 (286), dv30 (303). Vor
dv30c standen im grünen Lauf 34 Nodes / 117 fds. Kein Leck im engeren Sinn, aber
der geteilte Daemon am Ende der Datei ist nicht der vom Anfang.

Kandidat, ausdrücklich **noch nicht belegt**: `Mixer::checkPendingEdges`
(`src/mixer.cpp:179-190`) gibt einer Kante **3 s**, dann setzt es `LastError`.
dv30c baut ~45 Kanten auf einmal, wartet 60 s auf den letzten Node und prüft
danach, dass `LastError` leer ist. Braucht die letzte Kante mehr als 3 s, meldet
der Daemon einen Fehler, obwohl die Kante noch kommt — 3 s Gnadenfrist gegen 60 s
Wartezeit ist ein Widerspruch in den Annahmen. Hypothese aus dem Code, keine
Messung. Wird geprüft, bevor daran etwas geändert wird.

🔴 **Die 3-s-Frist-Hypothese ist WIDERLEGT (`/var/tmp/b8_frist.py`, 5 Läufe).**
Vorhersage war: die Kanten brauchen länger als 3 s **und** `LastError` ist
gesetzt. Gemessen, 5 von 5 grün:

| Lauf | letzter Node | Zeit | LastError unterwegs | danach |
|---|---|---|---|---|
| 1 | `kmixdeck.out.r3` | 5,68 s | `''` | `''` |
| 2 | `kmixdeck.out.r3` | 6,32 s | `''` | `''` |
| 3 | `kmixdeck.out.r3` | 5,59 s | `''` | `''` |
| 4 | `kmixdeck.out.r3` | 7,43 s | `''` | `''` |
| 5 | `kmixdeck.out.r3` | 6,85 s | `''` | `''` |

Die erste Hälfte der Vorhersage trifft zu (5,6–7,4 s, klar über 3 s), die zweite
**nicht**: `LastError` bleibt leer, auch während des Aufbaus abgefragt. Der
Edge-Watcher schlägt hier also nicht zu — die 3 s gelten offenbar nicht für die
Mix-Output-Nodes, die dv30c erwartet. Ein Mismatch = Plan verwerfen (AGENTS.md
Punkt 5), kein zweiter Fix auf der widerlegten Theorie.

Bestätigt ist dabei: `kmixdeck.out.r3` ist **immer** der Nachzügler, in allen 5
Läufen. Das ist kein Zufallsknoten, sondern reproduzierbar der letzte. Und
isoliert reicht die Zeit mit Faktor 8 (7,4 s gegen 60 s Timeout) — die Flakiness
braucht also den Zustand der vollen Datei, nicht den Test allein.

Nebenbefund, noch unerklärt: der Daemon hält in diesem Aufbau **3771–3950 fds**.
Im vollen Lauf standen vor dv30c 117 fds und der Test verlangt lediglich `>500`.
Die Größenordnung passt zu „47 Kanten ≈ 913 fds" aus dem Ursprungsbefund nicht —
hier ist es viermal so viel. Das ist die nächste Spur, aber es ist eine
Beobachtung, keine Diagnose.

**B8 TREFFER — der rote Lauf eingefangen, mit Zahlen (Jäger, Lauf 2 von 2).**
Ein Jäger-Job (`/var/tmp/b8_jaeger.py`, entkoppelt, Abbruch beim ersten Roten)
hat den Fehlerfall reproduziert. Der reparierte `wait_nodes()` liefert jetzt das
Entscheidende:

```
AssertionError: nodes never appeared within 60s: 1 of 36 missing: ['kmixdeck.out.r3']
  graph has 563 nodes, 555 of them kmixdeck.*
```

**555 kmixdeck-Nodes.** Der Test-Kommentar rechnet mit *„32 channels + 4 mixes ≈
45 loopback clients"* — real sind es über 500. Genau **eine** Kante fehlt
(`out.r3`), alle anderen 35 stehen. Das ist keine Ressourcengrenze (fds 159,
`LastError` leer), sondern: PipeWire legt unter 555 Nodes den letzten Node nicht
binnen 60 s an.

Vorhersage zum `no global`-Verdacht war: häufen sich die Fehler **vor** dem
Timeout, ist es die Ursache; sind sie gleichverteilt, ist es Rauschen. Gemessen:
367 Vorkommen, **erstes bei 14 %, letztes bei 92 %** des Logs — gleichmäßig über
den ganzen Lauf. Damit ist `no global` als Ursache **widerlegt**, es ist
Begleitrauschen des geteilten Daemons.

Offene Frage, die die Richtung bestimmt: sind 555 Nodes für ein 32×32-Pult
*normal* (dann ist die Zahl im Test-Kommentar seit je falsch und der Test
braucht mehr Geduld oder weniger Pult), oder akkumulieren sie im Verband (dann
räumt ein Vortest nicht ab). Das wird als Nächstes isoliert gegen den Verband
gemessen — nicht geraten.

Werkzeugfehler in dieser Runde, zum Mitschreiben: (1) der `makereport`-Hook lief
nach dem `finally` des Tests, seine „36 von 36 fehlen" ist Aufräumung; die echten
Zahlen stehen in der Assertion. (2) „501 no global" aus dem ersten Lauf war meine
Fehlinterpretation — es waren 501 Filtertreffer über *alle* Muster, gedruckt
wurden nur die letzten 25. (3) Die Regex im Jäger war `(\\d+)` statt `(\d+)`,
also lief die ID-Statistik auf eine leere Liste und in ein `ValueError` —
nachdem der Treffer gesichert war, deshalb ohne Schaden.

## Nachtrag — Anforderung CLI-Tool (Michel, 2026-09-20)

**B9 — `wait_level()` hat jahrelang gewürfelt statt gemessen.**
Gefunden beim zweiten Gate-Durchlauf: dv14 rot mit
`AssertionError: -12 dB output trim, got -4.1 dB`. Das sah nach einem
Produktfehler aus, war aber der vierte Fall derselben Harness-Krankheit.

```python
def wait_level(fn, pred, tries=6):
    for _ in range(tries):
        if pred(v): return v
        time.sleep(0.4)
    return v          # ← Prädikat nie erfüllt, trotzdem Wert zurück
```

Nach 6 Versuchen gab die Funktion den letzten Messwert zurück, **auch wenn
die Bedingung nie eintrat**. Aus einem Timeout wurde damit eine falsche Zahl —
die schlimmste Sorte Testfehler, weil sie wie ein echter Produktfehler aussieht.

🔴 **Selbstkorrektur, gleicher Tag — die Zahlen in diesem Abschnitt waren falsch,
und der Fehler war meiner.** Ich hatte geschrieben, das Wartefenster sei
„6 × 0,4 s = 2,4 s" und eine Einschwingzeit von 3,84–4,03 s gemessen. Beides ist
widerlegt:

1. **Fenster falsch gerechnet.** Jeder `fn()`-Aufruf ist eine **1,5-s-Aufnahme**.
   Das echte alte Fenster war `6 × (1,5 + 0,4) = 11,4 s`, nicht 2,4 s — ich hatte
   den `sleep` für die ganze Zeit gehalten und die Messdauer vergessen.
2. **„Einschwingzeit" war meine eigene Stoppuhr.** In `/var/tmp/trim_zeit.py`
   kostet jede Runde 0,2 s Pause + 1,5 s Aufnahme = 1,7 s. Die „3,86 s" sind also
   **zwei Messungen**, keine Produkteigenschaft.
3. **Es gibt überhaupt keine Blende.** `graph.cpp:369` setzt `channelVolumes`
   hart über `pw_node_set_param`; im ganzen `src/` steht kein `fade`, `ramp` oder
   `slew`. Die Erzählung „Aufnahme läuft über die Blende" hatte keine Grundlage
   im Code — ich habe sie nicht überprüft, bevor ich sie aufgeschrieben habe.

**Was wirklich gemessen ist** (`/var/tmp/trim_wahrheit.py`, 12 Kurzaufnahmen à
0,25 s direkt nach dem Befehl):

```
CLI kehrte nach 26 ms zurueck
t=0,03s  -36,92 dB     <- schon im Ziel (Basis -24,46 - 12 = -36,46)
t=5,09s  -36,92 dB     <- unveraendert, Streuung 0,3 dB ueber 5 s
```

**Der Trim sitzt nach 26 ms.** Es gibt nichts abzuwarten. Damit ist auch mein
„Fix" `tries=6→20` falsch und schädlich: 20 × 1,9 s = **38 s pro fehlschlagender
Messung**, und genau das hat `integration-ports` im zweiten Gate-Durchlauf über
die 600-s-Grenze von `tests/CMakeLists.txt:49` geschoben (600,31 s, Timeout).
`tries` steht wieder auf 6 (≈ 11,4 s), `settled_level` auf 12.

**Was von B9 bleibt** — und zwar bestätigt: die Wurf-Änderung. Ein Helfer, der
ohne erfülltes Prädikat einen Wert zurückgibt, verwandelt einen Timeout in eine
falsche Zahl. Der Beweis kam sofort: nach dem Fix wurden **dv25 und dv28 rot mit
`last reading -inf dB`** — zwei echte Fehler, die vorher hinter Meldungen wie
„L=-inf R=-inf" lagen und niemandem auffielen. Warum dv14 ursprünglich −4,1 dB
sah, ist damit **noch offen**; die Ungeduld-Erklärung ist erledigt.

**Lehre für mich, nicht fürs Projekt:** ich habe eine plausible Geschichte
gebaut, sie mit einer Zahl aus meiner eigenen Messapparatur belegt und in zwei
Dauerdokumente geschrieben (Journal, CONTRIBUTING), bevor ich in den Produktcode
geschaut habe. Michel musste zweimal „verstehe das Problem nicht" sagen, bis ich
gemessen habe. Die 26 ms hätten in fünf Minuten auf dem Tisch gelegen.

**Das Muster, vier Fälle an einem Tag:** dv7 (`wait_node` prüfte Existenz statt
Wert), mx2 (`terminate()` kehrt zurück, bevor der Node weg ist), dv30c (Timeout
für eine erfundene Graph-Größe), dv14 (`wait_level` liefert unbestätigte Werte).
Immer dasselbe: **es wird auf Werte assertiert, auf die niemand gewartet hat.**
Das ist keine Sammlung von Einzelfällen, sondern eine Systematik im Harness —
jede Warte-Hilfsfunktion muss entweder den Zielzustand bestätigen oder werfen.
Ein Rückgabewert ohne erfüllte Bedingung ist ein Fehler, kein Messergebnis.

Rückwirkend erklärt das auch die mx2-Streuung (12σ) von heute früh: dort wurden
zwei Messungen mit identischem Wert (−24,206796 dB) gelesen, weil ein Node nach
`terminate()` noch stand. Gleiche Familie.

Michel hat während des Reviews eine Anforderung für die **nächste** Version
nachgeschoben (nicht v0.3.0): die CLI soll handwerklich auf dem Niveau von
`tbload` sein — Manual, Parameter, Help, Patching über die Konsole, aktuelle
Konfiguration als Tree-View.

Wichtige Unterscheidung: **AR-3 ist schon ✅** und fordert, dass die CLI 100 %
der API abdeckt. Das ist erfüllt. Michels Anforderung betrifft nicht die
Abdeckung, sondern die *Bedienbarkeit* — deshalb eigene Sektion, keine
Umschreibung von AR-3.

Gemessener Ausgangszustand (`kmixdeck` 0.3.0, 2026-09-20):

| geprüft | Ergebnis |
|---|---|
| man page | **keine** (kein `*.1` im Repo) |
| Shell-Completion | **keine** |
| Tree-View | **keine** |
| `scene` in `--help` | fehlt, obwohl das Kommando existiert |
| `Usage:`-Zeile | zeigt `argv[0]` → `./build/bin/kmixdeck` |
| `kmixdeck help` | scheitert mit "service not reachable" statt Hilfe zu zeigen |
| `--help`-Länge | 49 Zeilen, handgepflegt |
| `--version` | ok (`kmixdeck 0.3.0`) |
| Exit-Code unbekanntes Kommando | ok (2) |

Vorbild `~/repos/tbload` (vom Auftraggeber benannt), übernommene Prinzipien:
- `docs/generiere-doku.py` erzeugt aus EINER Markdown-Quelle die man page UND
  den `--help`-Text → kein Drift zwischen beiden möglich
- `help` ohne Striche zeigt die Vollreferenz (wie `npm help ls`), weil `man-db`
  auf headless Nodes oft fehlt
- langer Text durch `$PAGER`, nur wenn stdout ein TTY ist

Abgelegt als **Sektion 5b, CL-1 bis CL-9** in `docs/spec/requirements.md`,
Status durchgehend 📝. `tools/sot-audit.py --write` akzeptiert: 111 → 120 Zeilen
(107 ✅ · 1 🔶 · 12 📝).

Korrektur an mir selbst: in CL-8 hatte ich die Exit-Code-Tabelle aus dem Kopf
geschrieben (0/1=not found/2=usage/…). `src/cli/main.cpp:39` sagt
`Ok=0, Usage=1, NoService=2, NotFound=3, Rejected=4` — auf die echten Werte
korrigiert, bevor die falsche Tabelle in der Spec stehenbleibt.

Status-Werte: `offen` · `läuft` · `ok` · `Befund` (mit Nummer) · `behoben`

## Journal

Append-only. Fakten (Kommando + Ausgabe) strikt getrennt von Hypothesen.

### 2026-09-20 — Start

- FAKT: `git branch --show-current` → `release/0.3`
- FAKT: `git tag` → v0.1.0, v0.1.1, v0.2.0 (also ist v0.3.0 der nächste)
- FAKT: `git log --oneline origin/main..HEAD | wc -l` → 14
- FAKT: 61 Dateien in src/ + tests/ (*.cpp, *.h, *.py, *.qml)
- FAKT: Toplevel hat README.md, CONTRIBUTING.md, LICENSE, docs/, interfaces/,
  po/, ruff.toml, streamdeck/, web/, tools/, data/

### 2026-09-20 — Punkte 1–9 vermessen

- FAKT: `grep` nach Hostnamen/IPs/`/home/` in src,web,streamdeck,interfaces,data,
  tools,README,CONTRIBUTING,docs → nur SPDX-Copyright + `github.com/TensorRaya/kmixdeck`
- FAKT: `grep -c TODO|FIXME|HACK|XXX` in src/web/streamdeck → keine Treffer
- FAKT: `python3 -m ruff check .` → All checks passed!
- FAKT: `ctest -R qmllint` → 1/1 passed, 4,03 s
- FAKT: 159 `def test_` in tests/integration/ (10 Dateien); `ctest -N` → 17 Suiten
- FAKT: `tools/sot-audit.py` → 107 ✅ · 1 🔶 · 3 📝 (111 rows, 8 core-tier)
- FAKT: `src/layout.cpp` nutzt `QSaveFile` (2 Stellen) + `QStandardPaths::ConfigLocation`
  + Backup-Rotation (`QFile::rename(path, aside)`)
- FAKT: `grep -rn QFileSystemWatcher src/ web/` → **kein Treffer**
- FAKT: 3 Hotplug-Tests: dv13 (replug+persist), dv9/dv12 (unplug greys out), ct3
- HYPOTHESE → widerlegt: erster Warn-Build meldete "0 warnings" bei `ninja=1`.
  Ursache: `-G Ninja` fehlte, cmake schrieb Makefiles, ninja fand keine
  build.ninja. Die 0 war Abwesenheit von Messung, nicht von Warnungen.
  Zweiter Fehler im selben Aufbau: `/tmp` ist tmpfs (169 MB, RAM) — falscher
  Ort für einen Debug-Build auf einem Host mit 8 GB ohne Swap.
- FAKT: Neuer Lauf in `/var/tmp/b-warn` mit `-G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra"`:
  `cmake=0`, `ninja=0`, 90/90 Ziele compiliert, Flags in flags.make nachweisbar,
  **0 Warnungen** → B1 ist Werkzeug-, kein Codebefund
- AKTION: `-Wall -Wextra` dauerhaft in CMakeLists.txt (GNU|Clang, kein -Werror);
  `cmake -S . -B build` + `ninja` → 11 Ziele neu, 0 Warnungen, `-Wall` in build.ninja
- FAKT: `CMakeLists.txt:2` → `project(kmixdeck VERSION 0.1.1)`;
  `./build/bin/kmixdeck --version` → `kmixdeck 0.1.1` → **B3 am Binary belegt**

### 2026-09-20 — Release-Teil

- AKTION: `ops-avsc6` (P2) für B2 angelegt — Config-Watcher nicht in v0.3.0,
  braucht eine Schleifenbremse gegen die eigenen QSaveFile-Schreibvorgänge
- AKTION: Version 0.1.1 → 0.3.0 in CMakeLists.txt, README-Badge, docs/dbus-api.md
- FAKT: nach Neubau `./build/bin/kmixdeck --version` → `kmixdeck 0.3.0` (B3 behoben)
- FAKT: Doku-Lücke gemessen (grep über README, cli.md, web.md, frontend-guide.md):
  `scene` 0 · `limiter` 0 · `BUILD_KDE_FRONTEND` 0 · loudness/R128 5 → **B4**
- FAKT: `scene save|recall|list|delete` existiert in src/cli/main.cpp:368/782,
  erscheint aber nicht in `kmixdeck --help`
- FAKT: CT-9 ist durch test_ct9_scene_roundtrip... (test_service_cli.py:722)
  und 22 scene-Treffer in test_frontends_sync.py gedeckt
- AKTION: Kapitel "Scenes" in docs/cli.md (--add-Verhalten aus main.cpp:355-360),
  2 Feature-Punkte in README, Headless-Absatz im Install-Teil
- HYPOTHESE → widerlegt: Im Kapitel stand `kmixdeck-cli scene …`. Das Target heißt
  `kmixdeck-cli`, aber `OUTPUT_NAME` ist `kmixdeck` (src/CMakeLists.txt:30) — die
  restliche Doku nennt es korrekt `kmixdeck`. Korrigiert, 0 Treffer `kmixdeck-cli`.
- FAKT: metainfo kannte nur 0.1.0/0.1.1 → **B5**; 0.2.0 (Datum 2026-09-18 aus
  `git log -1 --format=%ci v0.2.0`, meine Annahme 09-19 war falsch) + 0.3.0 ergänzt;
  `appstreamcli validate` → "Validation was successful: infos: 1"
- FAKT: Headless-Build `-DBUILD_KDE_FRONTEND=OFF` in /var/tmp/b-headless:
  `cmake=0`, `ninja=0`, 0 Warnungen. Binaries: kmixdeck (CLI), kmixdeckd, plus
  Unit-Tests — **kein** kmixdeck-kde. Voller Build hat kmixdeck-kde zusätzlich.
- FAKT: `kmixdeck-web` ist ein Python-Script, `install(PROGRAMS)` bei
  CMakeLists.txt:64 steht AUSSERHALB von `if(BUILD_KDE_FRONTEND)` → wird auch
  headless installiert, der Doku-Satz "service, CLI und web bridge" stimmt
