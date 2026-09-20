# ADR 0012 — Was ein 32×32-Pult den Graph kostet (gemessen)

Datum: 2026-09-20 · Status: akzeptiert (Befund), Konsequenz offen

## Anlass

`test_dv30c` kippte in 1 von 6 vollen Läufen, nie allein. Bei der Ursachensuche
fiel auf, dass niemand wusste, wie groß das größte unterstützte Layout im Graph
wirklich ist. Der Testkommentar rechnete seit einem Jahr mit *„32 channels +
4 mixes ≈ 45 loopback clients"*.

## Messung

Drei isolierte Läufe, `/var/tmp/b8_nodes.py`, privater PipeWire, frischer Daemon,
Zeitreihe während des Aufbaus:

| Zustand | Nodes (gesamt / kmixdeck) |
|---|---|
| Leerer Daemon | 29 / 27 |
| + virtuelles 32×32-Gerät | 32 / 30 |
| + 8 Kanäle | 88 / 86 |
| + 16 Kanäle | 144 / 142 |
| + 24 Kanäle | 200 / 198 |
| + 32 Kanäle | 256 / 254 |
| + Mix r0 | 331 / 329 |
| + Mix r1 | 406 / 404 |
| + Mix r2 | 481 / 479 |
| + Mix r3 | **556 / 554** |

Reproduzierbarkeit: 554 in allen drei Läufen, exakt. fds am Ende: 3933 / 3846 /
3861. Aufbauzeit 19,9–23,1 s (CLI-Aufrufe), danach sind alle Nodes binnen
**2,1–2,4 s** sichtbar. Nach dem Abräumen: wieder 29 / 27 — **kein Leck**.

## Was das heißt

Der Graph wächst linear und gut vorhersagbar:

- **+7 Nodes pro Kanal** (56 je 8 Kanäle)
- **+75 Nodes pro Mix** — ein Mix kostet mehr als zehn Kanäle

Die 75 pro Mix sind der Posten, der die Größe macht: 4 Mixe = 300 Nodes, mehr als
die Hälfte des Ganzen. Ursache ist die Architektur aus ADR 0009: jede
Kanal-×-Mix-Zelle ist ein eigener Loopback, also 32 × 4 = 128 Zellen plus deren
Ports und Monitor-Nodes.

**Betriebsfolge:** ~3900 fds für ein Pult, das ein Ui24R-Nutzer realistisch
aufbaut. Das Standard-`RLIMIT_NOFILE` von 1024 reicht für **weniger als ein
Viertel** davon. Deshalb hebt `daemon/main.cpp:23` das soft- auf das hard-Limit,
und deshalb war der EMFILE-Ausfall vom 2026-09-18 keine Panne, sondern die
vorhersagbare Folge. Die systemd-Unit setzt `LimitNOFILE=65536`; wer kmixdeckd
von Hand in einer Shell mit `ulimit -n 1024` startet, läuft in dieselbe Wand.

## Konsequenz

1. `test_dv30c`: Timeout 60 s → 180 s. Begründet, nicht gepolstert — der
   isolierte Wert ist 2,4 s, der Faktor deckt die Last von 19 Vortests auf dem
   geteilten Daemon. Der Kommentar trägt jetzt die Messwerte statt der Fantasie-45.
2. Die fd-Anforderung gehört in die Doku für Anwender, nicht nur in einen Test:
   wer ein großes Pult fährt, braucht ein gehobenes Limit. → offen
3. **Offene Architekturfrage:** 75 Nodes pro Mix ist viel. Ob sich Zellen
   zusammenfassen lassen (ein Loopback pro Kanal statt pro Zelle, Mischung im
   Filter-Graph) ist nicht untersucht. Das wäre eine Änderung an ADR 0009 und
   braucht eine eigene Messung — **nicht** im Rahmen von v0.3.0 entscheiden.

## Was hier ausdrücklich NICHT stand

Vor der Messung liefen drei Hypothesen, alle widerlegt:

- FD-Clamp als Testhebel (Idle-Bedarf schwankt 25–43, Konstante untauglich)
- liegengebliebene Nodes aus dem Vortest (27 → 27, kein Rest)
- `no global`-Fehler des Daemons als Ursache (367 Vorkommen, gleichverteilt über
  14 %–92 % des Logs → Begleitrauschen)

Die Einzelheiten stehen in `docs/review-v0.3.md`, B6–B8.

## Nachtrag 2026-09-21 — was am Messwerkzeug falsch war

Am Tag nach dieser Messung fiel auf, dass der Diagnoseblock des Jäger-Skripts
(`/var/tmp/b8_jaeger.py`) seine Zahlen **nach** dem `finally`-Zweig des
fehlgeschlagenen Tests erhob — und `test_dv31` startet dort den Daemon neu.
Die „159 fds / 38 Nodes beim Fehlschlag", die dabei herauskamen, sind also der
Zustand eines frisch gestarteten Daemons, nicht der Zustand zum Fehlerzeitpunkt.

**Für diese ADR ändert das nichts:** die Tabelle oben stammt aus drei isolierten
Läufen mit eigenem Daemon, ohne fremdes `finally` dazwischen, und 554 Nodes
reproduzierten exakt. Die Zahlen sind gültig.

Was daraus zu lernen ist, gehört trotzdem hierher, weil der nächste Mensch dieses
Werkzeug wieder benutzen wird: **ein Diagnoseblock muss messen, bevor
Aufräumcode läuft.** Sonst beschreibt er den reparierten Zustand und liest sich
wie ein Befund. Dieselbe Klasse Fehler wie das alte `wait_level`, das einen
unbestätigten Wert zurückgab (siehe CONTRIBUTING): das Werkzeug liefert eine
Zahl, die wie eine Messung aussieht und keine ist.

Ebenfalls am 2026-09-21 widerlegt: eine angebliche „Einschwingzeit von 3,86 s"
beim Setzen eines Trims. Gemessen (`/var/tmp/trim_wahrheit.py`, CLI-Rückkehr bis
erster Pegel): der Wert sitzt nach **26 ms** und bleibt über 5 s innerhalb von
0,3 dB. Es gibt keine Blende — `pw_node_set_param` setzt hart. Die 3,86 s waren
die Laufzeit der Messschleife selbst, weil jede Pegelmessung 1,5 s Aufnahme
braucht. Details in `docs/review-v0.3.md`, B9.
