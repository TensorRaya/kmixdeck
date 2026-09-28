# ADR 0013 — Zellen sind Gains auf einem Bus, keine Loopbacks

Datum: 2026-09-28 · Status: akzeptiert · Löst ab: den Zellen-Teil von ADR 0002 und ADR 0009 ·
Beantwortet: ADR 0012, Konsequenz 3

## Anlass

`test_dv30c` (32 Mono-Kanäle + 4 Stereo-Mixe auf einem 32×32-Gerät) kippte auch nach ADR 0012. Zwei
Ursachen, beide gemessen (2026-09-26/27, `/var/tmp/ab-dv30c/`):

1. **Listen-Backlog.** Jede Zelle war ein eigenes `module-loopback`, und jedes `module-loopback` baut eine
   **eigene Client-Verbindung** zum PipeWire-Server auf. ~300 gleichzeitige `connect()` gegen
   `listen(fd, 128)` → `EAGAIN` → Socket verworfen → 8 ms später `Broken pipe` → das Modul zerstört sich selbst.
   Die Kante fehlt, ohne dass irgendwer einen Fehler meldet.
2. **Dateigrenze.** Ein Loopback kostet ~20 Dateien (8 memfd, 8 eventfd, 2 Sockets, Rest), 264 davon ≈ 5300,
   verteilt auf Server und Daemon. Das Soft-Limit des PipeWire-Servers ist 4096.

Die erste Reparatur war ein Drop-in, das `RLIMIT_NOFILE` des Servers hebt, plus eine Wiederholungslogik im
Daemon für verschwundene Kanten. Beides machte den Test grün und beides war Symptombehandlung: es hebt eine
Grenze an, die nur deshalb erreicht wurde, weil der Graph für jede Zelle ein komplettes Programm startet.

Terminologie, weil sie in der Diskussion falsch lief: ein PipeWire-**Link** zwischen zwei Ports kostet fast
nichts (+64 Dateien für 256 Links, gemessen). Was teuer war, ist die „Kante" im kmixdeck-Sinn — ein
vollständiges `module-loopback` mit eigener Socket-Verbindung, zwei Stream-Knoten und Shared Memory.

## Entscheidung

Eine Zelle (Kanal × Mix) ist **ein Gain-Wert**, kein Knoten.

- **Pro Kanal eine Filter-Chain** `kmixdeck.cells.<kanal>`. Sie greift den Kanal-Sink ab
  (`stream.capture.sink`, `node.target`), verteilt L/R per builtin `copy` auf je ein builtin `mixer`-Paar
  `L<mix>`/`R<mix>` pro Mix. `"Gain 1"` dieses Paars **ist** der Zellfader.
- **Ein Bus** `kmixdeck.bus`: Null-Sink mit festen 64 Kanälen `AUX0..AUX63`, `stream.dont-remix`. Mix *i*
  belegt `AUX(2i)`/`AUX(2i+1)`. Jede Kanal-Chain spielt ihre 2·M Ausgänge dorthin; PipeWire summiert.
- **Pro Mix ein Abgriff** `kmixdeck.tap.<mix>`: ein Loopback, das seine beiden AUX vom Bus-Monitor liest und
  in den Mix-Eingang spielt (bei aktivem Mix-FX in dessen Chain, sonst in den Mix-Sink).
- Verbunden wird ausschließlich über `target.object`/`node.target` in der Config. WirePlumber stellt die
  Links her; **der Graph entsteht aus der Config allein** (DV-1), ohne laufenden Daemon.

Grenze: `SPA_AUDIO_MAX_CHANNELS = 64` → 32 Mixe pro Bus. MX-1 verbietet ein festes Maximum, also bekommt
jede weitere Gruppe von 32 Mixen einen eigenen Bus `kmixdeck.bus.<k>` und jeder Kanal eine Chain
`kmixdeck.cells.<kanal>@<k>` pro Gruppe. Bus-Geometrie und die Argumente von Chain und Abgriff kommen aus
**einer** Stelle (`namespace ADR13` in `layout.h`), die Config-Renderer und Laufzeit gemeinsam benutzen — sie
können nicht auseinanderlaufen.

### Der Daemon bleibt die zentrale API

Nichts an der Rolle ändert sich (Michel, 2026-09-27): Die D-Bus-API, `cell set/mute/get`, Undo, Szenen,
Export/Import, MX-7-Verknüpfungen — alles unverändert. Geändert hat sich nur, **was hinter** `writeCell`
passiert: statt `setVolume` auf einem Loopback-Knoten ein `setControl` auf zwei Controls der Kanal-Chain.

### Wo der Zellzustand lebt

WirePlumber sichert Stream-Lautstärken (`state-stream.lua`), aber **keine Filter-Controls**. Deshalb:

- Lautstärke und Stumm jeder Zelle stehen in `layout.json` (`"cells"`), Quelle der Wahrheit ist das Layout.
- Die Config rendert sie als `control = { "Gain 1" = <wert> }` → nach einem PipeWire-/WirePlumber-Neustart
  ohne Daemon kommt jede Zelle mit ihrem Wert zurück (gemessen, `test_dv7_levels_and_mute_survive_daemon_restart`).
- Stumm = Gain 0; der Faderwert bleibt im Layout erhalten.
- Schreibt ein Fremder (`pw-cli`) einen Zell-Gain, sieht der Daemon das am Echo (`Props.params`) und stellt den
  Layout-Wert wieder her (MX-2, `test_mx2_cell_state_survives_a_foreign_writer`).
- Kommt ein Mix hinzu oder fällt weg, ändert sich die Signatur der Chain (`node.description` trägt die
  Mix-Liste); der Daemon baut veraltete Chains neu. Der Bus bleibt stehen, weil er fest 64 Kanäle hat.

### Zellpegel (UX-13)

Es gibt keinen Knoten pro Zelle mehr, an dem ein Meter hängen könnte. UX-13 verlangt einen Post-Fader-Pegel,
also rechnet der Daemon `cell/<k>/<m> = Kanalpeak × Zellgain`. Mathematisch identisch, null Streams extra.

## Gemessen

Pult über die CLI aufgebaut, privater PipeWire, **Standard-Dateigrenze 4096, kein Drop-in**. Reproduzierbar mit
`tools/measure-dv30c.py` (ein Lauf) bzw. `tools/measure-dv30c.py F --runs 10 --load 3` (die Reihe unten).

32 × 4 (Vergleich mit ADR 0012, dort gemessen): Loopback-Matrix **556 Knoten, 3846–3933 fds** →
ADR 0013 **75 Knoten, 1314 Server-Dateien, 39 Client-Verbindungen, 264 Links**.

32 × 32 (dv30c) — Loopback-Matrix: rot (Listen-Backlog 128 bei ~300 gleichzeitigen Verbindungen,
Dateigrenze 4096). ADR 0013, Zehnerreihe F1–F10 **unter Last** (3 × `nice 19`-Dauerschleife, Last 6–9 auf
4 Kernen), Stand nach den drei Fixes unten:

| | F1–F10 |
|---|---|
| Knoten | **219** (alle 10 Läufe identisch) |
| Server-Dateien | **1711** |
| Client-Verbindungen | **90** |
| Links | **680** |
| Aufbau über die CLI | **9,0–15,2 s** unter Last |
| fehlende Zellen / LastError | **0 / leer**, 10 von 10 |
| Zelle d1→r0 auf 0,25 | 0,25 in r0, 1,0 in r1 |

Vorher unter derselben Last: 1 von 5 Läufen rot (`cells.d13` fehlte), 1 von 12 mit 221 statt 219 Knoten.
Restliche Core-Fehler in F1–F10 (0–30 je Lauf, „no global N“ nach dem Umhängen der Mix-Ausgänge) kamen aus
`destroyOurNodes`, das beide Hälften eines Moduls zerstörte — dort nachgezogen, danach im Kurzlauf 0.

## Befund unter Last (2026-09-28)

Ruhig: 15/15 und 40/40 grün. Unter CPU-Kontention (3 × `nice 19`-Schleifen, Last 5–7,5 auf 4 Kernen) fehlte
in Lauf L5 `kmixdeck.cells.d13` nach 180 s (vorher in Reihe 1: `out.r1`/`tap.r1`), und je ein Lauf hatte
zwei Knoten zu viel (221 statt 219). Mit Trace jeder Anforderung/Zerstörung (`KMX_TRACE_GRAPH`) gemessen,
drei Fehler im Daemon, keiner in PipeWire:

1. **MX-2 wertete das Echo einer frischen Chain.** `filter-graph.c` meldet `control_data[0]`, das bis zum
   ersten Aufsetzen des Graphen 0,0 ist (`pw-mon`: bis zu 9 Echos mit 0,0 vor dem ersten richtigen Wert).
   Der Wächter hielt das für einen Fremdschreiber: **2578 Rückschreibungen** in einem 32×32-Lauf, 72 schon
   beim Anlegen eines Mixes auf einem 3-Kanal-Pult. Jetzt: eine Chain wird erst bewertet, nachdem sie das
   Layout einmal zurückgemeldet hat. Nach dem Fix: 0 beim Mix-Anlegen, der Fremdschreiber-Test bleibt scharf.
2. **Doppelte Zerstörung.** Beide Streams eines Moduls wurden zerstört, obwohl einer das ganze Modul
   entlädt, und Abgleichläufe vor der Rückmeldung zerstörten dieselbe ID noch einmal: 157× „no global N“,
   244× „unknown resource N“ (Steuerwerte an sterbende Knoten) im 3-Kanal-Lauf. Jetzt eine Zerstörung pro
   Modul und keine Schreibzugriffe auf Knoten im Abbau: **0 Core-Fehler**.
3. **Anforderungs-Merker.** `onNode` löschte den Merker bei *jedem* Param-Echo; die alte Chain gleichen
   Namens echote bis zu ihrer Entfernung und gab so die noch ladende neue frei → zweites Modul (221 Knoten).
   Und ein Knoten, der nie kam, wurde nie neu angefordert, obwohl der Kommentar es versprach → d13 fehlte
   für immer. Jetzt: nur das erste Auftauchen erfüllt die Anforderung; „nie erschienen“ gibt sie frei und
   fordert neu an; Zwillinge werden beim Abgleich auf den älteren reduziert.

## Was entfernt wurde

- `data/pipewire-50-kmixdeck-nofile.conf` und die CMake-Zeile dazu — der Graph passt wieder in die
  Standardgrenze, ein gehobenes Limit ist keine Voraussetzung mehr.
- Die zeitgesteuerten Wiederholungen aus der Bandage (`m_edgeRetries`) — der Massenausfall, den sie
  überdeckte, entsteht nicht mehr, weil es die ~300 gleichzeitigen Verbindungen nicht mehr gibt.
- `kmixdeck.link.<kanal>.<mix>`-Knoten, `m_cells`, `Names::cellNode`.

## Folgen

- Die Pegelmessung pro Zelle ist berechnet, nicht abgegriffen. Ein Fehler **innerhalb** der Chain (z. B. ein
  Gain, der nicht ankommt) zeigt sich nicht im Zellmeter, sondern erst am Mix. Das Echo aus `Props.params`
  deckt genau diesen Fall ab.
- Ein Mix mehr oder weniger baut alle Kanal-Chains neu. Das ist ein kurzer Aussetzer (Modul-Neuladen) auf
  jedem Kanal — akzeptiert, weil Mixe selten angelegt werden und Kanäle/Zellen die häufige Operation sind.
- Import älterer Exporte mit `kmixdeck.link.<k>.<m>`-Pegeln wird weiter verstanden und auf Zellen abgebildet.
