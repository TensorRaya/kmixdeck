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
