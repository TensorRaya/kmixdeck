# CT-8 (Soundboard) — Entscheidungen und Messprotokoll

Append-only. Fakten sind Kommando-Ausgaben, Hypothesen stehen getrennt und werden
als widerlegt markiert, sobald eine Messung dagegen spricht.

## Was gefordert ist

Aus `docs/spec/requirements.md`, CT-8 (owner 2026-09-18):

- Ein Soundboard ist ein **Kanal der Art `soundboard`**, der registrierte Samples
  auf Abruf in den Graph spielt (wav/flac/ogg/mp3 über die Decoder der Distro).
- Bus: `Mixer.PlaySample(name)`, `StopSample`, `Samples` (`a{sv}`: name, path,
  length, gain). CLI: `kmixdeck sample play <name>`.
- Samples laufen durch die **FX-Kette des Kanals und die Mix-Fader** wie jede
  andere Quelle — „nur Mikro"-Mixe können sie damit ausschließen.
- Abspielen läuft **im Daemon** (`pw_stream`), nicht in einem gestarteten Prozess.
- Von jedem CT-2-Client auslösbar (Home Assistant, Stream Deck über CT-3).
- **Die Kanalart ist optional**: kein Soundboard-Kanal existiert, bis der Nutzer
  einen anlegt (Opt-in-Regel, `docs/spec/requirements.md` Zeile 13).

## Entscheidung: Decoder

Kein Eigenbau. Geprüft wurde, was auf dem Zielsystem **installiert** ist
(`pkg-config`, 2026-09-22):

| Kandidat | Status | Urteil |
|---|---|---|
| libsndfile | nicht installiert | fällt weg (kein mp3/ogg ohne Zusatz) |
| Qt6Multimedia | nicht installiert | fällt weg — zöge QtGui in den headless Daemon |
| vorbisfile + libmpg123 + opusfile | nicht installiert | drei Abhängigkeiten statt einer |
| **libavformat/libavcodec/libswresample/libavutil** | **62.11 / 6.1 / 60.8** | **gewählt** |

ffmpeg ist da, deckt alle vier geforderten Formate in EINER API ab und bringt das
Resampling mit, das wir ohnehin brauchen (Sample-Rate ≠ Graph-Rate).

Belegte Decoder des installierten ffmpeg (`ffmpeg -decoders`):

    A....D pcm_s16le    PCM signed 16-bit little-endian
    AF...D flac         FLAC (Free Lossless Audio Codec)
    A....D vorbis       Vorbis
    A....D mp3          MP3 (MPEG audio layer 3)
    A....D opus         Opus

Opus ist damit gratis dabei, obwohl CT-8 es nicht verlangt.

## Entscheidung: wie das Audio in den Graph kommt

Vorbild ist `src/pipewire/meters.cpp` — der Daemon betreibt dort bereits
`pw_stream`, nur in Aufnahme-Richtung (Loudness, UX-18). Der Sampler ist die
Gegenrichtung: `PW_DIRECTION_OUTPUT` auf den Kanal-Sink des Soundboard-Kanals.

Zwei Lehren aus `meters.cpp` werden übernommen, nicht neu erlitten:

1. **Keine Rate erzwingen.** `info.rate = 0` heißt „die Rate des Graphen". Ein
   hart gesetztes 48 kHz zwang in einer 44,1-kHz-Session einen Resampler in den
   Pfad und brach den Vertrag „folgt der Session, erzwingt nichts" (DV-4).
   Der Sampler resampelt **selbst** mit libswresample von der Datei-Rate auf die
   Rate, die der Graph aushandelt — bekannt erst in `param_changed`.
2. **Kein `node.latency` setzen**, wo es nicht gebraucht wird.

## Warum nicht `pw-play` starten

Das Anforderungsdokument verlangt es ausdrücklich im Daemon. Gründe, die die
Tests dieses Repos belegen: ein gestarteter Prozess erscheint als eigener Knoten
im Graph, heißt bei mehreren Samples gleich (`pw-play`), und das Verlinken über
den **Namen** ist genau der Fehler, der `test_ports.py` monatelang instabil
gemacht hat (siehe `CONTRIBUTING.md`, „Check the return code of every command you
fire at the graph"). Ein `pw_stream` im Daemon hat eine ID, die wir kennen.

## Gemessene Fakten

1. **`Sampler::probe` liest alle vier geforderten Formate** (2026-09-22, gegen die
   echte Klasse gelinkt, nicht gegen eine Nachbildung):

       probe.wav    len=1.000 rate=44100 ch=1 err=-
       probe.flac   len=1.000 rate=44100 ch=1 err=-
       probe.ogg    len=1.000 rate=44100 ch=1 err=-
       probe.mp3    len=1.000 rate=44100 ch=1 err=-

   Und die Fehlerfälle melden sich sprechend statt still mit Nullen:

       gibtsnicht.wav  err=no such file: /var/tmp/ct8/gibtsnicht.wav
       /etc/hostname   err=cannot open '/etc/hostname' (unsupported format or unreadable)

   Letzteres ist Absicht: ein Soundboard, das eine unlesbare Datei stumm
   akzeptiert, lässt den Nutzer im Stream auf einen toten Knopf drücken.

2. **ffmpeg ist tatsächlich gelinkt** (`ldd build/bin/kmixdeckd`): libavformat.so.62,
   libavcodec.so.62, libswresample.so.6, libavutil.so.60 — geprüft, weil ein
   `pkg_check_modules`, das nur in der Konfiguration steht, nichts beweist.

## Hypothesen (offen)

- H1: Mehrere Samples gleichzeitig brauchen je einen eigenen `pw_stream`, weil
  ein Stream genau ein Format aushandelt. Zu messen.

## FX-Kette: gemessener Signalweg (2026-09-22)

`pw-link -l` am laufenden Graphen, Board mit aktiver Kette:

```
kmixdeck.sample -> kmixdeck.fx.board -> kmixdeck.fx.board.out -> kmixdeck.channel.board -> Zelle -> Mix
```

Die Kanal-Kette sitzt **vor** dem Plain-Sink (`mixer.cpp`: "in front of the channel sink").
Folge: der Kanal-Sink ist in JEDEM Fall post-chain — ein Pegel dort kann nicht zeigen, ob
ein Stream die Kette durchlaufen oder umgangen hat. Beide Wege enden am selben Knoten.

**Bug 1 — falsches Ziel.** `playSample` nahm fest `Names::channelNode()`, also den Sink
hinter der Kette. Gemessen ohne Fix: `kmixdeck.sample:output_FL -> kmixdeck.channel.board`,
mit Fix `-> kmixdeck.fx.board`. Behoben durch `Mixer::fxTarget()`.

**Bug 2 — laufende Samples.** Wird die Kette eingeschaltet, waehrend ein Sample klingt,
kann der Stream nicht umgehaengt werden: er entsteht mit `node.dont-reconnect` +
`node.dont-fallback`, und die PipeWire-Doku sagt dazu *"The node is initially linked to
target.object ... If the target is removed, the node is destroyed"*. `moveStream()` schreibt
nur WirePlumber-Metadaten (`target.object`) und bleibt daher wirkungslos. Behoben durch
`Mixer::restartSoundingSamples()` — mit Warten auf den asynchron erscheinenden fx-Knoten
(ohne das Warten zielte der Neustart wieder auf den Plain-Sink, im Log belegt).

Position wird beim Neustart nicht erhalten. Bewusst: ein Jingle ist 2-8 s, ein hoerbarer
Neustart ist ehrlich, ein stilles Umgehen der Kette nicht.

**Testlehre.** Drei Fassungen dieses Tests waren gruen, obwohl der Fix zurueckgedreht war —
alle drei messten Pegel am Kanal-Sink. Erst die Pruefung der echten Verlinkung
(`pw_sandbox.sink_of()`) wird rot. Gegenprobe pro Teilfix einzeln gefahren: Zielwahl raus =>
rot mit "linked to 'kmixdeck.channel.board'", Neustart raus => rot mit "never held in 6s;
last value 'kmixdeck.channel.board'".

## Zwei Fehler, die nur die echten Frontends zeigten (2026-09-22)

**Bug 3 — manueller Stop wurde nicht angesagt.** `Sampler::stop()` zerstoerte die Voice ohne
`finished()` zu feuern; nur das natuerliche Auslaufen (reaper, Zeile 232) meldete sich. Folge: der
Daemon war still, `PropertiesChanged` blieb aus, und alle drei Frontends zeigten nach einem Stop
weiter "playing" — gemessen: CLI `sounding=false`, Web-Pad trug weiter die Klasse `sounding`.
Fix: `stop()` sammelt die getroffenen Voices und feuert `finished()` ausserhalb beider Sperren,
genau wie der reaper. Absicherung: `busctl monitor` auf `PropertiesChanged` (die Property allein
genuegt NICHT — sie wird bei jedem Lesen neu berechnet und ist auch ohne Signal korrekt).

**Bug 4 — `#soundboard` sprang auf den Mixer.** `boardTab()` laeuft in jedem Render; beim ersten
Render ist der Zustand noch leer, also war `hasBoard()` false und die Ruecksprung-Zeile warf jeden
Direktaufruf von `#soundboard` (und jedes Lesezeichen) zurueck auf `#mixer`. Gemessen:
`location.hash` las `#mixer` einen Frame nach dem Laden von `#soundboard`. Fix: nur zurueckfallen,
wenn der Zustand tatsaechlich angekommen ist (`connected` + nicht-leere `objects`).

**Frontend-Beweise.** KDE: `--open soundboard --probe soundboardPanel.visible` = true,
`samplePad/board:jingle.highlighted` false -> true beim Abspielen. Web: echter Klick auf das Pad,
danach Klasse `sounding` und `-24 dB` am Kanal, zweiter Klick raeumt beides auf. CLI: `sample list`
als JSON. Alle drei in `test_frontends_sync.py` gegen denselben Zustand gestellt.

**Nebenfund:** `--probe` gibt eine Zeile pro Probe zurueck, ein Label mit `\n` kommt abgeschnitten
an. Der Pad-Text ist deshalb nur der Name; den Zustand zeigen `highlighted` + Icon.

## Nebenbefunde aus der CT-8-Verifizierung (2026-09-22)

Drei Sachen, die nicht CT-8 waren, aber beim dreifachen Nachweis auffielen:

1. **`test_ct7_export_import…` war seit ≥ 2026-09-21 im Verbund rot**, allein
   gruen (`/var/tmp/gate6_1.log` vom 21.09., 08:48, identische Meldung
   `picture() == before did not happen within 12.0s`). Zwei Verursacher, nicht
   einer — der zweite zeigte sich erst, nachdem der erste behoben war:
   **(1)** Die Core-Zeile „MX-10 mix mute" liess `stream` stumm; die modulweite
   `stack`-Fixture traegt das neun Tests weiter, CT-7 exportiert dann einen
   stummen Mix und misst `-inf dB` gegen erwartete `-33 dB` (nachgemessen:
   Kanal −24,21 dB, stream-Mix −inf dB).
   **(2)** Die CT-8-Zeile selbst — mein eigener Eintrag — liess ein 120-s-Sample
   auf dem Board-Kanal laufen, der mit 1,0 in jeden Mix speist. CT-7 las den
   stream-Mix daraufhin bei −24,9 dB und den music-Kanal bei −29,5 dB, also
   **+4,6 dB ueber** statt −9 dB unter dem Kanal. Isoliert messe ich −8,94 dB,
   mit laufender Fake-App −9,06 dB — die Abweichung kam allein vom Board.
   Einzelproben fanden (2) nicht: je eine Nachbarzeile + CT-7 war immer gruen,
   erst alle neun zusammen zeigten es. Gefunden, indem der Assert im Fehlerfall
   den Zustand nennt (Kanaele, Mutes, Zellen) statt nur die Zahl — `board` stand
   sofort in der Liste.
   Fix: jede CORE-Zeile, die Zustand setzt, nennt ihr Undo als 10. Tabellenfeld;
   vier Zeilen hatten das Problem (MX-6 Master, MX-10 Mute, CH-7 Mute, CH-7
   Trim) plus die CT-8-Zeile (`sample stop` + `channel remove board`). CT-7s ✅
   war damit bis heute unbelegt.
2. **RQ-3 (100 % dokumentierter D-Bus-Vertrag) war still verletzt**: 17 von 121
   Mitgliedern ohne Beschreibung (CT-8 8, CT-9 5, FX-9 4). Das Werkzeug
   `tools/dbus-doc.py` existierte und meldet das mit exit 1 — nur rief es kein
   Test auf. Jetzt als `fast`-Test verdrahtet, 121/121.
3. **Der deutsche Katalog hatte 5 falsch geratene Fuzzy-Eintraege**, darunter
   `Choose an audio file` → „Bild auswählen" und `Remove this sample` →
   „Kanal entfernen". `msgfmt` zaehlt Fuzzy als uebersetzt; nur `--statistics`
   nennt sie. 323/323 jetzt echt uebersetzt, 0 fuzzy.
