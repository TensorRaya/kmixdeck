# CT-1: ein echter Tastendruck im Test, und was dabei über kglobalacceld herauskam

Stand 2026-09-22. Anlass: CT-1 stand auf 🔶 mit der Begründung „ein physischer
Tastendruck ist von hier nicht testbar". Das war zu bequem formuliert — testbar
war mehr als gedacht, nur nicht alles.

## Was vorher geprüft wurde, und was dabei übersprungen wurde

Die zwei alten CT-1-Tests prüfen:

* `test_ct1_toggle_mute_is_atomic_on_the_bus` — die Bus-Methode selbst,
* `test_ct1_kde_frontend_registers_global_shortcuts` — dass `kmixdeck-kde` seine
  Aktionen anmeldet (mitgelesen auf dem privaten Bus).

Zwischen `invokeShortcut` über D-Bus und einem Finger auf der Tastatur liegen
drei Glieder, die keiner der beiden anfasst:

1. der **Key-Grab im X-Server** (`XGrabKey` auf Keycode + Modifier),
2. **kglobalacceld**, das Keycode+Modifier auf eine Aktion auflöst,
3. das **Frontend**, das die aufgelöste Aktion tatsächlich ausführt.

`invokeShortcut` springt direkt zu Glied 3. Ein kaputter Grab oder eine falsche
Keycode-Auflösung wäre beiden alten Tests entgangen.

## Was jetzt läuft

`tests/integration/test_shortcuts.py`, ctest-Eintrag `integration-shortcuts`:

    xdotool (XTEST)  ->  Xvfb  ->  kglobalacceld 6.6.5 (XGrabKey)  ->  kmixdeck-kde
        ->  org.kmixdeck1.Channel.ToggleMute  ->  kmixdeckd  ->  PipeWire node mute

Gemessen wird am Ende der Kette, am PipeWire-Knoten: `mute=False → True`.
Drei Tests, 23 s, und drei Gegenproben, die alle rot waren, bevor ich sie als
Absicherung gezählt habe:

| Sabotage | Ergebnis |
|---|---|
| `kmixdeck-kde` nicht starten | rot — 4 XKeyPress kommen an, **keine** Wirkung |
| Grab auf F11 statt F9 legen | rot — Grab-Test und Druck-Test |
| Shortcut unter fremder Komponente | rot — Grab-Test und Druck-Test |

Die erste Zeile ist der interessante Beweis: der Tastendruck erreicht
kglobalacceld auch ohne Frontend, aber nichts passiert. Genau das trennt
„Taste kommt an" von „Aktion wird ausgeführt".

## Sind XTEST-Events echt?

Auf X-Protokollebene ja. `xev` meldet für sie `synthetic NO`, der Server
markiert sie also nicht als per `SendEvent` eingespeist — für jeden X-Client,
kglobalacceld eingeschlossen, sind sie von Hardware nicht unterscheidbar.
Gemessen mit `xev -root`: 15 Ereignisse, Keycode 75 = F9, state `0xd` =
Ctrl+Shift+Alt.

**Was damit weiterhin NICHT geprüft ist:** alles unterhalb von X — USB/HID,
evdev, libinput — und der Wayland-Pfad durch KWin. Den Wayland-Pfad schließt der
Abschnitt [Wayland: KWin hält die Taste selbst](#wayland-kwin-hält-die-taste-selbst);
USB/HID, evdev und libinput bleiben außerhalb jedes automatischen Tests.

## Befund: kglobalacceld 6.6.5 stürzt in setShortcutKeys ab

Der naheliegende Weg — Tastenkombination über D-Bus setzen — ist nicht
benutzbar. `setShortcutKeys` und `setForeignShortcutKeys` **töten den Daemon**:

    kf.globalaccel.kglobalacceld: QKeySequence("Ctrl+Alt+Shift+F9")
    KCrash: Application 'kglobalacceld' crashing... crashRecursionCounter = 2

Reproduziert in einer Differentialkette, ein Aufruf pro Schritt:

| Aufruf | Daemon lebt danach |
|---|---|
| `doRegister` | ja |
| `shortcutKeys` (lesen) | ja |
| `setShortcutKeys` flags=4 | **nein** |
| `setForeignShortcutKeys` | **nein** |

Unabhängig von: Taste (F9 wie M), D-Bus-Client (`busctl` wie `gdbus` mit
korrekt typisierten Argumenten), laufendem Frontend (mit wie ohne).

Zwei Sackgassen auf dem Weg dorthin, beide meine:

* **`ai 0` als Rückgabe** hielt ich zuerst für einen Parameterfehler und habe
  acht Flag-Kombinationen durchprobiert. Die Antwort stand im Quelltext
  (`kglobalacceld/src/kglobalacceld.cpp`, Zeile 491–498): bei Autoloading und
  einem Shortcut, der nicht `isFresh` ist, kehrt die Funktion sofort zurück und
  liefert die vorhandenen — leeren — Keys. Lesen schlägt Raten.
* **„The X11 connection broke. Did the X11 server die?"** las ich als Xvfb-Problem.
  Rule Zero dagegen gehalten: ein minimaler `XGrabKey`-Client (python-xlib) greift
  im selben Xvfb einwandfrei und empfängt den Keycode 75 mit state `0xd`. Xvfb war
  in Ordnung; die abgerissene Verbindung war die **Folge** des Daemon-Absturzes,
  nicht die Ursache. Außerdem hatte ich eine eigene Xvfb-Leiche auf `:96` liegen
  lassen, die einen Lauf zusätzlich verfälschte.

## Der Weg, der funktioniert

Die Kombination **vor** dem Start in `kglobalshortcutsrc` legen:

    [kmixdeck]
    _k_friendly_name=kmixdeck
    mute-channel-game=Ctrl+Shift+Alt+F9,none,Mute channel: Game

kglobalacceld greift die Taste beim Laden und betritt die abstürzende Codebahn
nie. Das ist zugleich näher an der Realität als der D-Bus-Weg: genau dieser
Ladepfad läuft in einer echten Sitzung nach jedem Neustart, und es ist das, was
System Settings auf Platte hinterlässt.

Nachweis, dass der Grab wirklich sitzt, statt nur die Datei zu glauben:

    getGlobalShortcutsByKey(251658296)
      -> "mute-channel-game" "Mute channel: Game" "kmixdeck" "kmixdeck" "default"

## Werkzeuge

`Xvfb`, `xdotool`, `kglobalacceld` (Paket gleichen Namens; die Binary liegt unter
`/usr/lib/<triplet>/libexec/`, nicht auf `$PATH`). Fehlt eines, überspringt sich
die Datei — und ein Lauf, der „passed" sagt, hat dann nichts geprüft. Deshalb
steht in CONTRIBUTING.md der einmalige Blick auf `ctest -R shortcuts -V`:
`3 passed`, nicht `3 skipped`.

## Wayland: KWin hält die Taste selbst

Nachtrag 2026-09-22, abends. CT-1 fordert ausdrücklich Wayland, und dort gibt es
kein separates kglobalacceld: `kwin_wayland` bringt `libkglobalacceld0` mit und
übernimmt `org.kde.kglobalaccel` selbst. Gemessen mit `busctl --user list`, die
PID hinter dem Namen ist die von `kwin_wayland`. Der X11-Test oben prüft also eine
Komponente, die es in einer Wayland-Sitzung gar nicht gibt.

`tests/integration/test_shortcuts_wayland.py`, ctest-Eintrag
`integration-shortcuts_wayland`:

    kwin-fake-key  ->  KWin 6.6.6 (org_kde_kwin_fake_input)  ->  Eingabeumleitung
        ->  Shortcut-Filter  ->  kmixdeck-kde (Wayland-Client)
        ->  org.kmixdeck1.Channel.ToggleMute  ->  kmixdeckd  ->  PipeWire node mute

KWin läuft privat mit `--virtual` (ohne GPU, ohne Sitzung), liest dasselbe
`kglobalshortcutsrc` und meldet im Log, was es tut:

    Loading group  "kmixdeck"
    Registering key "Ctrl+Alt+Shift+F9" for "kmixdeck" : "mute-channel-game"
    Processed key "Ctrl+Alt+Shift+F9" , current sequence "Ctrl+Alt+Shift+F9" = "mute-channel-game"

Drei Tests, ≈ 10 s, 3 von 3 Läufen grün. Zwei Gegenproben, beide rot, bevor ich
den Test gezählt habe:

| Sabotage | Ergebnis |
|---|---|
| Positivtest drückt F10 statt F9 | rot — `PipeWire mute changing from False did not happen within 5s` |
| Gegentest drückt F9 statt F10 | rot — `an unassigned key changed the mute state` |

### Drei Wege, die nicht gingen

| Weg | Ergebnis | Warum |
|---|---|---|
| `ydotool` über `/dev/uinput` | `rc=0`, keine Wirkung, KWin loggt nichts | `--virtual` hat kein libinput; uinput-Geräte sieht es nie |
| `wtype` | `Compositor does not support the virtual keyboard protocol` | KWin 6.6 bietet `zwp_virtual_keyboard_v1` nicht an |
| `fake_input` ohne Freigabe | `compositor offers no org_kde_kwin_fake_input` | KWin zeigt eingeschränkte Schnittstellen nur Clients, deren `.desktop`-Datei sie in `X-KDE-Wayland-Interfaces` nennt |

Den dritten Fall hatte ich in der Sondierung nicht gesehen, weil dort — ohne dass
es mir auffiel — `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` gesetzt war. Der Test im
Repo lief ohne und war 3 von 3 rot, in 4 s. Jetzt setzt ihn der Test für seinen
privaten Compositor ausdrücklich, mit Begründung im Code; die Sitzung des
Benutzers berührt das nicht.

`org_kde_kwin_fake_input` ist KWins eigener Weg für synthetische Eingabe. Die Tasten gehen in
dieselbe Eingabeumleitung wie die einer Tastatur; der Shortcut-Filter kann sie
nicht unterscheiden. Der Helfer `tests/tools/kwin-fake-key.c` (≈ 60 Zeilen) wird
aus `plasma-wayland-protocols/fake-input.xml` per `wayland-scanner` gebaut und nie
installiert.

### Was jetzt noch nicht getestet ist

USB/HID, evdev und libinput — die Kernel- und Treiberschichten **unter** dem
Compositor. kmixdeck enthält und konfiguriert keine davon; sie gehören zum System,
nicht zur Software unter Test. Mit KWins `--virtual`-Backend sind sie nicht
erreichbar, und ein DRM-Backend mit echtem Seat bräuchte eine grafische Sitzung.
CT-1 steht damit auf ✅: die Anforderung „MUST work on Wayland, via KGlobalAccel"
ist bis zur Grenze des Compositors bewiesen.

## Nebenbefund, schwerer als CT-1 selbst: eine Suite lief in keinem Gate

Auf die zweite Hälfte der Frage („und Tests ausbauen?") habe ich nicht neue Tests
geschrieben, sondern zuerst gefragt, wo die vorhandenen **nicht greifen**. Dazu
`ls tests/integration/test_*.py` gegen `ctest -N` abgeglichen:

    Dateien   : 12
    im ctest  : 11
    NICHT im ctest: ['soundboard']

`test_soundboard.py` — 10 Tests für CT-8, am selben Tag committet und von mir als
fertig gemeldet — stand nicht in der `foreach`-Liste in `tests/CMakeLists.txt`.
Im Vollgate-Log: **0 Treffer** für „soundboard". Alle acht Vollgates dieser Runde
meldeten „100% tests passed, 0 tests failed out of 24" — über eine Suite, die nie
gestartet wurde. Ein grünes Gate über nicht gelaufene Tests ist schlimmer als ein
rotes: es beendet die Suche.

Behoben, und zwar beides:

* `soundboard` in die Liste eingetragen — ctest hat jetzt **27** Einträge statt 24
  (dazu `integration-shortcuts` und der Wächter selbst).
* `tools/pruefe-suiten.py` als `fast`-Test `suiten-vollstaendig`: vergleicht die
  Dateien auf Platte gegen die Namen in CMakeLists.txt, in Millisekunden, ohne
  Daemon. Gegenprobe gemacht — `soundboard` wieder herausgenommen: rot mit
  `test_soundboard.py (10 Tests)` und dem Hinweis, wo es einzutragen ist.

Das ist die Schwester zum bestehenden `gate-label-vollstaendig`: der prüft, dass
jeder *registrierte* Test in ein Gate fällt, der neue, dass jede Datei überhaupt
*registriert* ist.

## Zweiter Nebenbefund: zwei Statuszeilen logen

Beim Durchsehen aller 121 Zeilen nach „✅ mit Einschränkung im Text":

* **UX-18** (Loudness/EBU R128) stand auf ✅ und gab im eigenen Text zu, der
  UI-Teil fehle — „the ✅ was premature". Nachgemessen:
  `grep -ril 'loudness|lufs|r128' src/qml src/frontend web` → 0 Treffer, während
  Daemon, Bus, CLI und Test vollständig sind. Die Anforderung verlangt
  ausdrücklich „**show** … next to its peak/RMS meter" und „the target line MUST
  be **drawn**". Status auf 🔶 korrigiert.
* **CT-4** behauptete zweimal „level indication still missing" und im selben Feld
  „live meters per mix and channel". Der Tray *hat* Pegelanzeige:
  `TrayOverview.qml` trägt pro Mix und Kanal einen `LevelMeter` mit Peak, RMS und
  Clip, und `test_ch7_rms_and_clip_are_metered_in_the_daemon_and_shown_everywhere`
  misst sie am echten Tray (Sinus: Peak/RMS ≈ 3 dB, Vollpegel setzt
  `trayChannelMeter/game.clip`, Freigabe nach ≈ 1,5 s). Die veralteten Notizen
  sind raus.

Audit danach: **120 ✅ · 2 🔶 · 0 📝** bei 121 Zeilen. Eine Anforderung mehr auf
🔶 als vorher — das ist keine Verschlechterung, sondern das Ende einer falschen
Meldung.
