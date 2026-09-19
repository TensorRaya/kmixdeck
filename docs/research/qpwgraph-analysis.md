# qpwgraph — Analyse für kmixdeck

Stand des Untersuchungsgegenstands: upstream `rncbc/qpwgraph` v1.0.4 (2026-08-26), 17.7k Zeilen Qt/C++ in 39 Dateien. Analysiert am 2026-09-19.

## Kurzportrait

qpwgraph ist ein **direkter PipeWire-Client mit eigener GUI**: ein QMainWindow mit QGraphicsScene-Canvas, eine Registry-Subscribtion, alles Leben im einen Prozess. Kein Daemon, keine IPC-Schicht — die GUI *ist* der Zustandsträger. Das ist für uns nur in Punkten interessant, die unabhängig von dieser Architektur funktionieren ( unten, Spalte „Übernehmen"). Für die Grundfrage — Daemon mit D-Bus, Frontends austauschbar (AR-1..AR-9) — ist das Gegenteil ihres Aufbaus die Bestätigung unseres Modells: qpwgraph kann ohne sichtendes Fenster nichts patchen, und sein Zustand hängt an einer QSettings-Datei des GUI-Prozesses.

Ihr Herzstück ist das **Patchbay-Profil**: XML-Datei mit Verbindungspaaren, beim Start gescannt und in den Live-Graph gematcht. Das ist dem Layout-JSON von kmixdeck verwandt, aber mit zwei Mechanismen, die wir nicht haben und brauchen könnten.

## Tabelle — was wie und warum übernehmen

| # | Punkt | qpwgraph 1.0.4 | kmixdeck heute | Übernehmen? | Warum |
|---|---|---|---|---|---|
| 1 | **Kanten ohne Gain als plain link** | Link-Erzeugung über `link-factory` mit `LINK_OUTPUT_NODE/PORT` + `LINK_INPUT_NODE/PORT`, dazu `OBJECT_LINGER=true` und `LINK_PASSIVE` aus der Env. Kein eigener Client pro Kante. | Jede Kante ist ein Modul-Load: ein PipeWire-Client pro Kante, **gemessen 20–21 fds** (idle-Daemon 14 fds; ein Mix-Output-Edge +21, eine Zelle +20, ein virtuelles Gerät +20). | **Nein — nachgemessen, trägt nicht.** Siehe Messung unten. | Die Prämisse („die Gain-freien Kanten sind die Masse") ist falsch: in einem realistischen Setup sind **76 % der Kanten Zellen**, und die *müssen* Loopback bleiben, weil die Playback-Volume dort der Fader ist. Gain-frei sind nur 19 %. Dazu kann laut D-Bus-API **jede** Wire Trim tragen (`Channel.SetWireTrim`, `Mix.SetWireTrim`) — ein plain link müsste beim ersten Trim-Dreh live in einen Loopback umgebaut werden, also Umpatchen bei laufendem Audio. Aufwand und Risiko gegen 19 % einer Größe, die nach DV-30 (`LimitNOFILE=65536`) kein Limit mehr hat. |
| 2 | **Exklusiv vs. additiv beim Laden eines Profils** | Zwei Schalter: *Activated* (Profil anwenden) und *Exclusive*. Exclusiv: Verbindungen, die nicht im Profil stehen, werden getrennt. Nicht-exclusiv: Profil ergänzt nur. Beides auch als CLI: `-d/--deactivated`, `-n/--nonexclusive`. | CT-9 (Szenen) ist 📝: Recall schreibt die gespeicherte Teilmenge ins Layout, ohne Aussage über fremde Kanten. | **Ja,** als Default für `scene recall`: exklusiv. Additiv als Flag `--add`. | Ohne Exclusivity-Regel ist ein Szenenwechsel nicht deterministisch — eine App, die zwischenzeitlich WirePlumber-Standard-Routing bekommen hat, überlebt die Szene. Exclusivity ist exakt das, was „Szene" von „Sammelbecken" unterscheidet. |
| 3 | **Auto-Pin neuer Kanten** | Schalter „Auto pin": jede frisch gelegte Verbindung wird automatisch ins aktive Profil geschrieben. Manuell getrennte werden *nicht* zurückgepinnt. |existent Szenen-Logik (noch nicht gebaut) würde bei jedem Recall alles über die Köpfe der laufenden Sitzung schreiben. | Bei CT-9 mitnehmen: Recall = Sollzustand; was der Nutzer *nach* dem Recall ändert, wird Teil der Szene, wenn Auto-Pin an ist. | Das ist die einzige sinnvolle Merge-Regel für „Szene laden, dann weiterarbeiten". Manuell-getrennt-nicht-zurück heißt: die Absicht des Nutzers schlägt die Profil-Liste. |
| 4 | **Merger-Liste (Regex pro Node-Name)** | Node-Namen, die mehrfach auftauchen (Browser!), werden für Patchbay-Matching zu einem logischen Knoten zusammengelegt. Eigene Optionsseite mit Regex-Liste; Matching läuft über `nodeNameEx` statt `nodeName`. | Apps sammeln wir unter `appKey = application.name` (CH-6), aber die Patchbay-Karten (DV-24) zeigen pro PipeWire-*Node* — ein Chromium mit vier Streams ergibt vier Kartenreihen. | **Ja,** abgeschwächt: gleiche `application.name` → eine Karte, Positionen zusammengeführt. Keine Regex-Liste nötig, der Key ist schon da. | Bei echten Setups (Browser + Discord + OBS) bläht sonst Spalte 1 der Patchbay auf das Zwei-, Dreifache. Das Merger-Konzept löst genau das; die Regex-Liste ihrer Umsetzung ist Ballast, weil wir den Gruppierungsschlüssel schon haben. |
| 5 | **Knotenidentität über Name, nicht id** | `NodeNameKey(name, mode, type)` als Hash-Key; ChangeLog seit 0.9.0: kurze Nodes mit wiederverwendeten ids, same-name-different-id explizit behandelt. | Layout und App-Zuordnung hängen an `application.name`/`node.name`, die Live-Registry in `graph.cpp` an der `uint32 id` — als *Live*-Cache korrekt. | Bestätigt, nichts zu ändern. | Die id-only-Falle ist bei uns on-disk schon vermieden (CH-6); ihr ChangeLog belegt, dass andere sie hatten. |
| 6 | **Version im Profil-Root** | `version`-Attribut am Root-Element, Migration beim Laden (`< "0.5.0"` → Legacy-Names bereinigen). | Layout.json hat `version` + Sidecar `.vN-from-newer-kmixdeck` für Dateien aus neuerer Version. | Gleichartig, nichts übernehmen. | Beide Seiten lösen dasselbe Problem; unseres ist für zwei Frontends (JSON statt XML) besser geeignet. |
| 7 | **Kürzlich-used Profile als Tray-Menü** | System-Tray-Menü „Presets" mit Recently-used-Profilpfaden; eines anklicken = anwenden. | UX-17-Tray-Popover zeigt Overview, keine Szenen. | Klein: letzter-Absatz im Tray-Popover „Zuletzt geladen: …" mit Direkt-Recall. | Zwei Klicks auf den häufigsten Fall (Rechner aufgewacht, Szene holen). Rest des Tray-Layouts bleibt unserer. |
| 8 | **Node-Farbtypen (audio/video/midi/midi2/other) mit editierbaren Farben** | Fünf Porttyp-Farbkarten im Optionsdialog, Persistenz über `ColorsGroup` mit hex-Keys; MIDI 2 (UMP) als eigener Typ. | Web-Patchbay sortiert nach Richtung, Farben nur für pegel- vs. stille Kanten. | Nein. | Bei uns trägt die Kante ihren Zweck im Ref (`>L`, `>R`, Position); Farbklassen nach Media-Class sind Information zweiter Hand. |
| 9 | **Freier Canvas mit Toposort („Arrange Nodes“)** | Rank = Abstand zur Quelle, Spalten, danach Spalten vertikal auf Mittelwerte der Ziel-Y ausgerückt; Repel Overlapping Nodes als Extra. | Patchbay ist bewusst fest-spaltig (Sources → Channels → Outputs → Monitors, DV-24/DV-27). | Nein. | Ihr Algorithmus optimiert Drähte auf einem wilden Graphen — bei uns ist die Struktur die Semantik (eine Spalte = eine Stufe im Signalweg). Wegsortieren würde die Lesbarkeit kosten, die das Layout ausmacht. |
| 10 | Thumb-View-Ecke, Zoom-Range, Fullscreen, Pinch-Zoom | QGraphicsView-Spielereien mit Eckenpositionen und Settings-Keys. | Web-UI ist responsive ohne eigene Zoom-Infrastruktur. | Nein. | Fenstermanagement, kein Fachverhalten. |
| 11 | GUI besitzt den Graph (kein Daemon) | Everything-Process: kein Patchen ohne Fenster, Zustand in QSettings des GUI. | AR-1: Daemon trägt alles, Frontends sind Blätter. | Nein — Gegenreferenz. | Ihr Modell ist der Grund, warum ihre Patches beim Schließen des Fensters neu gescannt werden müssen; unseres nicht. Gehört in ADR-0010 als Kontrast. |

## Messung zu Punkt 1 (2026-09-19, Sandbox)

Ich hatte plain links als ersten Umsetzungspunkt vorgeschlagen und danach die Prämisse gemessen. Sie hält nicht.

Kosten pro Kante, gemessen am fd-Zähler des Daemons (`/proc/<pid>/fd`), Sandbox-PipeWire:

| Schritt | fds | Δ |
|---|---|---|
| idle, Starter-Layout (2 Mixes) | 14 | — |
| + 2 Fake-Geräte (kein kmixdeck-Objekt) | 14 | 0 |
| + 1 Mix→Gerät-Kante | 35 | **+21** |
| + 1 Kanal (2 Zellen) + 1 Geräte-Eingang | 95 | +60 |
| + 4 Kanäle (8 Zellen) | 255 | +160 (**20 pro Zelle**) |
| + virtuelles Gerät 8×8 | 275 | **+20** |

Kanten-Inventar für ein realistisches Setup (6 Kanäle × 4 Mixes, 2 Geräte-Eingänge, 2 Mix-Ausgänge), 84 Kanten:

| Kantentyp | Anzahl | Anteil | Gain? |
|---|---|---|---|
| Zelle (`kmixdeck.link.*`) | 64 | **76 %** | ja — Playback-Volume IST der Fader (ADR 0002) |
| Mix-Ausgang (`kmixdeck.out.*`) | 8 | 9 % | DV-14-Trim möglich |
| Mix-Capture (`kmixdeck.source.*`) | 8 | 9 % | DV-14-Trim möglich |
| Geräte-Eingang (`kmixdeck.in.*`) | 4 | 5 % | DV-14-Trim möglich |

**Ergebnis:** Gain-frei *im Moment der Anlage* sind 19 % der Kanten, und auch die können jederzeit Trim bekommen (`Channel.SetWireTrim` / `Mix.SetWireTrim`). Ein plain link müsste beim ersten Trim-Dreh live zum Loopback umgebaut werden — Umpatchen bei laufendem Audio, für eine Ersparnis, die nach `LimitNOFILE=65536` (DV-30) niemanden mehr drückt. **Verworfen**, nicht umgesetzt.

Nebenbefund für #2/#3: Zellen dominieren die Kantenzahl so deutlich, dass eine Szenen-Implementierung (CT-9) auf keinen Fall Kanten anlegen/abreißen sollte, sondern nur Volumes setzen — was die Spec ohnehin so festlegt („nicht die Verdrahtung").



## Was davon konkret in v0.3 fällt

**#1 ist verworfen** (Messung oben). Bleibt: **#2/#3** zusammen mit CT-9 (Szenen), weil dort die Merge-Regel ohnehin definiert werden muss — exklusiv als Default, `--add` als Flag, Auto-Pin für „Szene laden und weiterarbeiten". **#4** (Apps mit gleichem `application.name` = eine Patchbay-Karte) ist eine kleine Änderung in `patchbay.js` plus Gruppierung in `overview()`. **#7** (zuletzt geladene Szene im Tray-Popover) fällt mit CT-9 ab. #5/#6 sind Bestätigungen ohne Arbeit, #8–#11 begründete Nein.

Ehrlicher Ertrag dieser Analyse: **zwei** übernehmbare Mechanismen (Exclusivity-Regel, Merger-Idee) und eine widerlegte eigene Hypothese. Für 17,7k Zeilen Fremdcode ist das wenig — aber die Exclusivity-Regel hätte ich mir bei CT-9 sonst selbst ausdenken müssen, und ihre ChangeLog-Historie (0.9.0: „nodes with reused ids", 0.9.3: „players power-cycling their client on a whim") ist der Beweis, dass unsere Namens-statt-id-Entscheidung aus CH-6 die richtige war.

