# FX-9 (Ducking) — Messprotokoll und Root Cause

Append-only. Fakten sind Kommando-Ausgaben, Hypothesen stehen getrennt und werden
als widerlegt markiert, sobald eine Messung dagegen spricht.

## Entscheidung: welches Plugin

`analyseplugin` über alle installierten LADSPA-Kompressoren (2026-09-21):

| Plugin | Sidechain | Kanäle | Urteil |
|---|---|---|---|
| sc1_1425 | nein | mono | untauglich |
| sc2_1426 | nein | mono | untauglich |
| **sc3_1427** | **ja** | **stereo** | **gewählt** |
| sc4_1882 | nein | stereo | kein externer Trigger |
| sc4m_1916 | nein | mono | kein externer Trigger |

Ducking braucht Stereo für die Musik plus einen getrennten Trigger-Eingang.
Nur SC3 hat beides.

### SC3-Grenzen (aus `analyseplugin`, nicht geraten)

    Attack time (ms)       1.5 … 400
    Release time (ms)        2 … 800
    Threshold level (dB)     0 … -30      ← Vorsicht: Maximum ist 0, Minimum -30
    Ratio (1:n)              1 … 10
    Knee radius (dB)         1 … 10
    Makeup gain (dB)         0 … +24
    Chain balance            0 … 1

`Chain balance` laut swh-Quelle (sc3_1427.xml): **0 = Left+right in, 1 = Sidechain.**
Für Ducking muss der Wert 1 sein — nachgelesen, nicht durch Ausprobieren bestimmt.

Audio-Port-Reihenfolge im Plugin: `Sidechain`, `Left input`, `Right input`,
`Left output`, `Right output`. Die Reihenfolge ist irrelevant, solange `inputs`/
`outputs` die Ports **namentlich** benennen — was filter-chain unterstützt.

## Gemessene Fakten

1. **filter-chain veröffentlicht nur EINGANGS-Controls.** Alle 7 Props-Namen des
   SC3-Nodes geprüft: `Attack time (ms)`, `Release time (ms)`,
   `Threshold level (dB)`, `Ratio (1:n)`, `Knee radius (dB)`,
   `Makeup gain (dB)`, `Chain balance`. SC3s Control-*Ausgang*
   „Gain reduction (dB)" ist **nicht** darunter.
   → Folge: Das Badge kann die Reduktion nicht am Plugin ablesen. Sie wird aus
   dem Pegelvergleich der zwei Ducker-Seiten gebildet (peak in vs. peak out),
   also aus dem, was der Nutzer hört, statt aus dem, was das Plugin behauptet.

2. **`pw_context_load_module("libpipewire-module-link-factory")` erzeugt keinen
   Link.** Gab ein gültiges Modul-Handle zurück, Registry blieb ohne Link.
   `pw_core_create_object(core, "link-factory", …)` — was `pw-link` selbst
   aufruft — verlinkt sofort. Bestätigt mit `pw-link -l`.

3. **Ein Capture-Stream kann nur EIN Ziel targeten.** Der Trigger kann deshalb
   nicht über `node.target` in die Chain kommen; AUX0 wird per `linkPorts()`
   verdrahtet. Verifiziert: `kmixdeck.channel.voice:monitor_FL → input_AUX0`,
   `channel.game:monitor_FL/FR → input_FL/FR`, alle Links `state=active`.

4. **Validierung greift und nennt den Grund.** `threshold = -45` wird abgelehnt:
   „threshold must be 0..-30 dB (SC3's range), got -45" — auf dem Bus und im
   Log. Ohne Prüfung des Rückgabewerts sieht ein Aufrufer nur „keine Wirkung".

5. 🔴 **ROOT CAUSE (vorbestehender Bug, nicht FX-9):** Der generische
   `ladspa`-Zweig in `fx.cpp` schreibt feste Portnamen:

       inputs = [ "gamesc3:In" ]   outputs = [ "gamesc3:Out" ]

   SC3 hat keine Ports `In`/`Out` (sie heißen `Left input` / `Right input` /
   `Left output` / `Right output`). filter-chain findet die Ports nicht,
   verwirft den Graphen still und leitet durch. Messbare Folgen:
   - **alle** Controls des Nodes stehen in den Props auf `0.0`, obwohl der
     Daemon die Werte korrekt gespeichert hat (`FxChain` liest sie zurück),
   - SC3 als reiner Kompressor mit Ratio 20:1 und Schwelle −30 dB auf einem
     −21-dBFS-Ton reduziert **0.0 dB**,
   - der Ducker reduziert ebenfalls 0.0 dB.

   Reichweite: **jedes** mehrkanalige LADSPA-Plugin, das per `fx set` eingetragen
   wird, läuft still ins Leere — `rc 0`, keine Warnung, keine Wirkung. FX-9 hat
   den Bug nur aufgedeckt; er betrifft den bestehenden `ladspa`-Effekttyp.

## Verworfene Hypothesen

- „`Chain balance = 1` ist falsch" — **widerlegt** durch die swh-Quelle: 1 *ist*
  Sidechain.
- „`inputs` mappen positionell, die Musik landet auf dem Sidechain" —
  **widerlegt**: das Mapping ist namentlich, und die Links sind korrekt.
- „Die Musik erreicht den Ducker nicht" — **widerlegt**: `duck-out` führte
  0.0884, sobald die Chain überhaupt gebaut wurde. Der Nullwert in einem Lauf
  kam von Fakt 4 (Validierung hatte die Chain abgelehnt).
- „SC3 braucht mehr Einlaufzeit (`count % 4`)" — **widerlegt**: auch nach 8 s
  keine Reduktion, weil die Controls auf 0 standen.

## Fix und Verifikation (2026-09-21)

`ladspaPorts()` liest die Audio-Portnamen per `dlopen` aus dem Plugin
(`ladspa_descriptor`), `renderFilterChainArgs()` setzt alle Ports qualifiziert in
`inputs`/`outputs`. Ein `Sidechain`-Eingang wird im normalen FX-Zweig
ausgesortiert: er ist kein Kanal der Kette, sondern wird separat gespeist —
sonst landete FL im Sidechain (SC3 listet ihn als ERSTEN Port).

**Vorher → nachher, gleiche Kette (sc3_1427, Threshold −30 dB, Ratio 10:1):**

| Beobachtung | vorher | nachher |
|---|---|---|
| `inputs` in den args | `"gamesc3:In"` | `"gamesc3:Left input" "gamesc3:Right input"` |
| Controls in den Props | alle `0.0` | `-30.0` / `10.0` / `5.0` / `200.0` |
| `kmixdeck.fx.game.out` | `suspended` | `running` |
| Kompression am Kanal | **0.0 dB** | **−4.6 dB** |

Vorhergesagt waren ~5 dB: Ton −21 dBFS Peak / −24 dBFS RMS, also 6 dB über der
Schwelle, bei 10:1 ≈ 5 dB Reduktion. Gemessen −4.6 dB — Prediction erfüllt.

⚠️ **Messfalle:** `pw-play` in die Kanal-SENKE spielen geht an der Kette VORBEI
(`fx.game.out → channel.game:playback`, aber nichts speist `fx.game`). Wer so
messt, sieht 0 dB und verdächtigt das Plugin. Es muss in den KETTENEINGANG
(`kmixdeck.fx.game`) gespielt werden. Dieselbe Falle hat hier mehrere Läufe
gekostet.

## Offen

Der Ducker selbst reduziert weiterhin **0.0 dB**, obwohl:
- alle Controls korrekt in den Props stehen (`Chain balance = 1.0`, Threshold
  −30, Ratio 4, Attack 5, Release 200),
- beide Nodes `running` sind,
- Musik durch ihn fließt (`duck-out` führt 0.0442),
- der Trigger-Link aktiv ist (`channel.voice:monitor_FL → input_AUX0`),
- und auch ein Signal **direkt** auf `input_AUX0` nichts ändert.

Der letzte Punkt entlastet den Link: der Fehler sitzt in der Ducker-Chain selbst.
Nächster Verdacht (noch NICHT belegt): bei `audio.channels = 3` /
`audio.position = [ FL FR AUX0 ]` verbindet filter-chain den dritten Kanal
möglicherweise nicht mit dem `Sidechain`-Port — in diesem Aufbau ist der
Sidechain der DRITTE Eintrag in `inputs`, das Plugin selbst listet ihn aber als
ERSTEN Port. Zu prüfen ist, ob die Zuordnung namentlich oder positionell erfolgt;
die Doku belegt das für diesen Fall nicht.

## Zweiter Befund: Kanal-Index 2 des Capture-Streams erreicht den Graphen nicht

A/B, identischer Aufbau, eine Variable (welcher Kanal-Index auf den einzigen
Graph-Eingang gelegt wird, `inputs = [ null null "n:Left input" ]` gegen
`[ "n:Left input" null null ]`):

| verbundener Kanal-Index | Pegel am Ducker-Ausgang |
|---|---|
| 0 (FL) | 0.0442 |
| 2 (AUX0) | **0.0000** |

Gegenprobe gegen "der letzte Kanal ist tot": mit `audio.channels = 4`
(`[FL FR AUX0 AUX1]`) liegt der Sidechain auf Index 2, ist aber nicht mehr der
letzte — Reduktion bleibt 0.0 dB. Also **Index 2 generell**, nicht die Position
am Ende.

Ebenfalls gemessen: `duck.game.out:output_FL` = 0.0884, `output_AUX0` = 0.0.

Widerlegt (jeweils gemessen, nicht vermutet):
- `Chain balance` falsch — nein, 1 ist korrekt (swh-Quelle: `lev_in =
  (1-bal)*(L+R)*0.5 + bal*sidechain`, bei bal=1 hoert der Detektor nur den Sidechain).
- Positionelles statt namentliches Port-Mapping — filter-graph.c 1895 fuellt
  `graph->input[]` in der Reihenfolge der `inputs`-Liste, Ports namentlich gesucht.
- `null` in der `outputs`-Liste — entfernt, keine Aenderung.
- `node.target` + `stream.capture.sink` erzwingen ein 2-kanaliges Format —
  beide entfernt, keine Aenderung.
- Sidechain-Port des Plugins defekt — nein: Musik auf den Sidechain gelegt
  ergibt 0.0263 (deutliche Kompression).
- Mein `linkPorts`-Trigger liefert nichts — nein: am `monitor_AUX0` des
  Ducker-Eingangs liegen 0.0884 Peak / 0.0621 RMS.
- Ducker-Graph rechnet nicht — nein: mit `Chain balance = 0` komprimiert er
  0.0884 auf 0.0527 (Prediction ~0.0519).

Offen: warum Index 2 im Prozesspfad leer bleibt, obwohl Format (3 Kanaele,
`[FL FR AUX0]`) und Links (`active`) korrekt sind. Kandidat aus der Quelle:
`module-filter-chain.c:1317` fuellt `cin[]` mit NULL auf, wenn der
Capture-Buffer weniger Datas fuehrt als der Graph Inputs hat; `filter-graph.c:331`
(`if (port->desc && in[i])`) ueberspringt dann den `connect_port`, und der Port
bleibt auf dem `silence_data` aus dem Setup (`filter-graph.c:1650`). Zu messen an
`in->buffer->n_datas` — dafuer fehlt noch ein Trace-Log des filter-chain-Moduls
im kmixdeck-Prozess (`PIPEWIRE_DEBUG=spa.filter-graph:4` griff nicht).

## Rule Zero: der Referenzaufbau zeigt denselben Fehler (2026-09-21)

Eigenes Werkzeug verdaechtigt, also das Muster OHNE kmixdeck-Code nachgebaut —
filter-chain per `pw-cli load-module` in der Sandbox, SC3, Sidechain auf dem
dritten Capture-Kanal:

| Aufbau | ohne Trigger | mit Trigger |
|---|---|---|
| eigener Ducker (kmixdeck) | 0.0884 | 0.0884 |
| Referenz, capture 3ch / playback 3ch + `null` | 0.0884 | 0.0884 |
| Referenz nach Doku-Muster, capture 3ch / playback 2ch, kein `null` | 0.0884 | 0.0884 |
| Referenz, Doku-Muster, **Trigger mit Vollpegel** | 0.0884 | **0.0884** |

Der letzte Lauf ist der Beweis: `r3.in:monitor_AUX0` liegt bei **1.0** (Vollausschlag,
~30 dB ueber der Schwelle von -30 dB). Bei Ratio 10:1 muesste SC3 die Musik um
rund 27 dB druecken. Gemessen: 0.0 dB. Das Signal ist nachweislich im Node, im
richtigen Kanal, und erreicht den Detektor des Plugins nicht.

Damit ist **mein Code entlastet**: das Muster "Sidechain als zusaetzlicher
Capture-Kanal einer filter-chain" traegt in PipeWire 1.6.2 kein Signal zum
LADSPA-Sidechain-Port. Das Doku-Muster (Dolby-Surround-Beispiel: asymmetrische
Kanalzahlen, `audio.channels` pro Stream, kein `null` im `outputs`) aendert daran
nichts.

Belegt aus den Quellen, warum das konsistent ist:
- `sc3_1427.so` per `dlopen` ausgelesen: `Sidechain` ist Audio-Port **7**,
  `Left input` 8, `Right input` 9 — der Sidechain steht VOR den Audio-Eingaengen.
- `filter-graph.c:1885` fuellt `graph->input[]` als `desc->input[j]`, also in
  Plugin-Port-Reihenfolge; mit `inputs`-Liste dagegen namentlich (`:1895`).
- `module-filter-chain.c:1317` fuellt `cin[]` mit NULL auf; `filter-graph.c:331`
  (`if (port->desc && in[i])`) ueberspringt dann `connect_port`, der Port behaelt
  das `silence_data` aus `filter-graph.c:1650`.

Nebenbefund, unabhaengig davon behoben: der Ducker setzte `Knee radius (dB)` und
`Makeup gain (dB)` nie, beide standen damit auf 0. `Knee = 0` ist in SC3s Formel
(`-(threshold - knee - lin2db(env)) / knee`) eine Division durch Null. Jetzt
Knee = 3, Makeup = 0. Die Prediction "damit wird die Reduktion sichtbar" wurde
gemessen WIDERLEGT — die Aenderung bleibt trotzdem, weil ungesetzte Controls
sonst auf 0 stehen.

## Konsequenz fuer FX-9

Der Sidechain-Weg ueber einen zusaetzlichen Capture-Kanal ist eine Sackgasse,
belegt am Referenzaufbau. Naechster Kandidat, ohne Sidechain-Port: die Reduktion
per Control-Port fahren — der Trigger-Pegel wird ohnehin schon im Daemon gemessen
(`m_lastPeaks`), und `filter.graph`-Controls sind zur Laufzeit ueber
`Props`/`params` setzbar. Damit braucht das Ducking kein SC3 mit externem
Sidechain, sondern einen simplen Gain, den der Daemon nach dem gemessenen
Trigger-Pegel faehrt. Das ist auch fachlich naeher an dem, was ein Streamer
"Ducking" nennt (feste Absenkung um N dB, nicht Kompressor-Kennlinie).

## FX-9 funktioniert (2026-09-21)

Umbau auf zwei builtin-`linear`-Gains, die der Daemon auf jedem Meter-Tick
(25/s) ueber Props fuehrt. Vorab am Referenzaufbau geprueft: `set-param Props
{ params = [ "g:Mult" 0.25 ] }` aenderte den Pegel von 0.0884 auf 0.0221 —
vorhergesagt 0.0221 (ein Viertel = -12 dB), also exakt getroffen. Erst danach
gebaut.

End-to-End im eigenen Code, Ducker-Ausgang gemessen, `DuckReduction` ueber D-Bus:

| depth | ohne Trigger | mit Trigger | gemessen | DuckReduction | Prediction |
|---|---|---|---|---|---|
| -12 dB | 0.0884 | 0.0222 | **-12.0 dB** | -12 | -12 dB ✓ |
| -24 dB | 0.0884 | 0.0056 | **-24.0 dB** | -24 | -24 dB ✓ |

Die Gegenprobe ist die zweite Zeile: eine andere Tiefe ergibt einen anderen,
vorher berechneten Pegel — der Wert folgt der Einstellung, nicht dem Zufall.

### Drei Fehler auf dem Weg, alle gemessen statt geraten

1. **Eigene Validierung blockte.** `threshold` war auf SC3s Kompressorbereich
   (0..-30) begrenzt. Die Schwelle ist jetzt der Trigger-Pegel in dBFS, Bereich
   0..-60; Default -40. Fehlermeldung entsprechend ohne "SC3's range".
2. **Falscher Meter-Schluessel.** `Meters::peaks` ist nach `node.name` gekeyt
   (`kmixdeck.channel.voice`); die Form `channel/voice` entsteht erst in
   `LevelsAdaptor::publicKey` fuer D-Bus. Erst auf `channel/<slug>` gesetzt,
   im Log als `trigger-peak -1` gesehen, dann auf `Names::channelNode` korrigiert.
3. **Meter liefen nur mit UI-Abonnent.** `syncTargets()` baute die Ziel-Liste
   komplett innerhalb von `if (!m_subscribers.isEmpty())`. Ducking haette damit
   nur funktioniert, solange irgendein Fenster ein Meter offen hat. Die
   Trigger-Kanaele stehen jetzt ausserhalb dieser Bedingung, und
   `Mixer::channelChanged` loest `syncTargets` aus (queued — inline nimmt das
   waehrend eines PipeWire-Reconnects den Bus-Namen mit, CH-4).

### Was dabei wegfiel

`linkDuckTriggerWhenPresent` und der AUX0-Link sind geloescht: der Trigger-Pegel
steht im Daemon schon zur Verfuegung. `duckReduction` liest jetzt den gefahrenen
Multiplikator statt zwei Peaks zu vergleichen — die alte Rechnung hat auch
Volumenaenderungen des Kanals als "Reduktion" gesehen.

Attack/Release sind die Rampe: pro Tick darf der Multiplikator um
`1 / (ms / 40)` wandern, also erreicht er das Ziel genau nach der eingestellten
Zeit.

### Offen

Das CLI hat noch kein `duck`-Kommando (getestet: `kmixdeck duck ...` existiert
nicht, D-Bus `Ducking` funktioniert). Dreifach-Paritaet ist damit fuer FX-9 noch
nicht erfuellt — CLI und beide UIs fehlen.
