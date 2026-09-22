# Contributing to kmixdeck

## The two rules (project owner, 2026-09-17 — ADR 0010)

1. **Backend and API first.** A feature exists when it is on the D-Bus tree with its interface XML and the CLI can
   drive it. UI work starts after that, never instead of it.
2. **A feature is done only when every frontend we develop has it** — CLI, the KDE window, the tray (and the web UI
   once it ships), according to the row's tier (core / full / api). One view model in `MixerClient`, N renderers;
   `tools/sot-audit.py` refuses a ✅ that lacks a proof per frontend.

## The golden rule (AR-10)

A feature or setting is **done** when one integration test drives it through its whole life cycle:

1. **Appears from outside** — a device (or app) the daemon has never seen shows up in PipeWire → it is listed with
   all its connectors and can be wired immediately.
2. **Unplug** — the configuration (wires, volumes, names) is *kept*, the entity is greyed out, nothing is deleted.
3. **Replug** — the configuration is restored live, without user action, and **audio is measured** on the path
   (not "the property says present").
4. **Daemon restart, device present** — same wires, same audio.
5. **Daemon restart, device absent** — configuration still in `layout.json`, greyed.
6. **Plug in after that restart** — audio again.

Template: `tests/integration/test_ports.py::test_dv13_hotplug_new_multiport_device_is_listed_wired_replugged_and_persisted`.

Rules for such tests:

- **Wait for the observable, never for a clock.** WirePlumber writes state on a timer, PipeWire links a stream a tick
  later under load, a daemon exports objects after the bus name — every one of those bit us on 2026-09-16. Poll the
  value you actually need (file content, node present, level above threshold).
- **A waiting helper must confirm or raise — never return an unconfirmed value.** On 2026-09-20 seven helpers
  ended in `return v` / `return cur` / `return pred()` after their last attempt, handing back whatever was
  measured even when the condition never held. That turns a timeout into a WRONG NUMBER that looks exactly
  like a product bug: `wait_prop` reported `assert 'fake.mic' == ''` for a property that had simply never
  arrived, and dv14 reported `-12 dB output trim, got -4.1 dB`. Fixing the helpers immediately surfaced two
  genuinely broken tests (dv25, dv28) that had been hiding behind nonsense messages. If the predicate does
  not hold, raise and name what was last seen; never hand the caller a value nobody waited for. `waiting.py`
  is the reference.
- **Check the return code of every command you fire at the graph.** `pw-link` exits 255 with "failed to link
  ports: No such file or directory" when the target port does not exist *yet*, and two helpers threw that away:
  `play_into_port` and `play_into` slept a fixed 0.6 s and called `subprocess.run(..., capture_output=True)`
  without looking at the result. Measured 2026-09-22: the node `pw-play` is in the graph within 0.6 s but its
  port `output_FL` frequently is not — 3 of 4 attempts failed silently, perfect correlation between "port
  absent", "rc=255" and "-inf dB". That made dv28 red in 4 of 5 runs (2 of 5 even before that day's changes)
  and it looked like a broken 32×32 audio path for months. A swallowed return code is the same bug as a
  waiting helper that returns an unconfirmed value: infrastructure noise arrives dressed as a product bug.
- **A node in the graph does not mean its PORTS are there.** Same lesson one level down, and it bit twice on
  2026-09-22: `wait_node` returned for a 32×32 device whose ports were still being registered, so `dv30c` asked
  the daemon for `AUX1` and got "has no port 'AUX1' — name it as PipeWire does ()" with an EMPTY port list. Use
  `wait_ports(name, count)` whenever the next step addresses a port; of 20 tests in `test_ports.py` exactly one
  did that before. `pw-play` has the same two-step: node first, `output_FL` later.
- **Count the MEASUREMENT, not the sleep, when you size a level wait.** Each `wait_level` attempt records
  ~1.5 s, so `tries=6` is ~11.4 s of waiting and `tries=20` is 38 s — enough to push `integration-ports`
  past its 600 s ctest limit (done on 2026-09-20, by me, while "fixing" a timeout that was not one). And
  patience is rarely the answer: a wire trim is fully applied **26 ms** after the CLI returns and holds
  within 0.3 dB (measured), because the product sets `channelVolumes` hard — there is no fade anywhere in
  it. A level that is `-inf` after three recordings is a routing fault, not a slow one.
- **Measure audio where the requirement is audio.** `pw_sandbox.level_at_port()` / `record_monitor()`; "property is
  true" is not proof that anything is audible (UX-12 headphone bug, 2026-09-16).
- **One test per feature, all six steps in it.** Spreading the life cycle over several tests hides the ordering
  bugs (replug after restart while absent is where things break).
- **Two gates, and the full one goes LAST.** `ctest -L fast` is 13 tests in **12 s**: unit tests, the label
  check, sot-audit, the QML smoke tests, the doc and CLI-help checks. The full gate takes **~25 min per run**,
  because 96 % of the runtime sits in nine PipeWire suites (`integration-frontends_sync` 460 s,
  `integration-ports` 453 s, `integration-routing` 210 s) — a factor of **~120x**.
  Working order, not negotiable: build → `-L fast` → next change. The full gate runs **once, at the end**, when
  the feature is otherwise finished and you are about to commit. Never between two edits of the same feature,
  never after a comment or doc change. On 2026-09-21 I ran it after doc edits, twice with two passes each, and
  burned hours for nothing.
- **Every test carries exactly one gate label** — `fast` or `integration`. A label filter is fail-OPEN: a test
  without a label is silently skipped by BOTH gates and the run still goes green. That happened here — three of
  22 tests (`appstreamtest`, `frontend-qml-loads`, `frontend-qmllint`) ran in no gate at all after the fast gate
  was introduced. `tests/pruefe-label.cmake` now turns that into fail-CLOSED: a missing label fails the run and
  names the test. Label names are English, like every other test name and label in this project.
- **Serialising is not a fix.** On 2026-09-21 I added `RESOURCE_LOCK "audio"` because audio checks failed "only
  under -j2". Cost: gate runtime doubled. Result: with the lock AND on a cleaned-up machine (load 4.19)
  `test_ports.py` **still** failed with `last reading -inf dB`. Reverted. The real cause that round was sandbox
  leftovers (next point) — not parallelism.
- **No test run may leave daemons behind.** Measured: three sandbox daemons kept running **8.5 h** after their
  suite finished and held the machine's load at 9–10 on 4 cores. PipeWire is soft-realtime: a missed deadline
  produces silence, i.e. `-inf dB` in a measurement that has nothing to do with the code.
  `tests/integration/conftest.py` sets `PR_SET_PDEATHSIG` on every Popen of the suite — an `atexit` handler
  cannot cover the SIGKILL case, the kernel can. Each run prints `host load X on N cores` in its header; if that
  line carries a warning, every audio measurement in the run is worthless.
- **That protection only exists under pytest.** `conftest.py` installs it by replacing `subprocess.Popen`
  globally, so it is active for the suite and for nothing else. Importing `Stack` from a throwaway script
  (`python3 /tmp/probe.py`) gets the *original* Popen, and the daemon it starts outlives the script: on
  2026-09-21 three `kmixdeckd` survived that way, up to 4.8 h, each on its own `/tmp/kmixdeck-pw-*` bus.
  They break nothing functionally, they just burn cores that the next audio measurement needs. So for a
  quick probe either write it as a test and run it through pytest, or `from conftest import *` first — and
  check `pgrep -a kmixdeckd` when you are done.
- **Run the whole suite before you push:** `cd build && ctest --output-on-failure` must be 10/10. Isolated green
  is not green — three of today's daemon bugs only showed under full-suite load.
- **A test that skips itself is not a green test.** `integration-shortcuts` (CT-1) needs `Xvfb`, `xdotool` and
  the real `kglobalacceld` (Debian/Ubuntu: `apt install xvfb xdotool kglobalacceld`; the binary lands in
  `/usr/lib/<triplet>/libexec/`, not on `$PATH`). Without them the file skips and the run still says "passed" —
  so after a fresh checkout check `ctest -R shortcuts -V` once and look for `3 passed`, not `3 skipped`.
- **Do not build or run daemons in the tree while ctest runs.** Half of today's red runs were self-inflicted.
  This includes "isolated" A/B comparisons: on 2026-09-21 I ran three suites "alone" while a full gate was
  running next to them (load 8.3–11.0). Both sides were oversubscribed, so the green proved nothing. Before any
  A/B: check `cat /proc/loadavg` against `nproc` and record both in the log.
- **Never rebuild while a gate is running — and you no longer have to remember it.** Use `tools/gate.sh`
  (holds an exclusive lock on `build/.gate.lock`, checks load, runs ruff + ctest, names its log) and
  `tools/bau.sh` instead of `cmake --build` (refuses with exit 3 while that lock is held; `--force` if you
  knowingly invalidate the run). A second `gate.sh` is refused too — two gates oversubscribe dbus/pipewire and
  both results become worthless. Why the locks exist: the binary under test is a shared resource. On
  2026-09-22 `vollgate4` came back "2 of 23 failed" — five named failures, all in the last two suites — while
  I had rebuilt five times during its run, twice with a deliberately sabotaged binary for a counter-check.
  That gate proved nothing about the code and cost 17 minutes. Same class of mistake as running two suites
  side by side: the measurement was contaminated by my own hands. While a gate runs: read, write docs, think.
- **A green "can it be set" test is not proof that a feature works.** FX-9 ducking shipped with tests in all
  three frontends — CLI, browser, KDE dialog, plus a badge test and triple parity. Every one of them checked
  that the four values can be SET and read back. None of them listened. The ducker read the channel monitor,
  applied its gain and played into `kmixdeck.null`, beside the audible path: the daemon reported −18 dB while
  the mix moved 0.06 dB (specs/fx9-ducking.md, 2026-09-22). **For anything that claims to change the sound,
  one test must measure the sound** — `pw.level_at()` before/after, plus the return to the old level. Two
  traps when you write one: (a) both test tones are the same 1 kHz sine, so the trigger must be routed OUT of
  the mix you measure (`cell set <trigger> <mix> 0`) or phase addition gives you anything from −inf to +6 dB;
  (b) a release needs ~1.5 s, because a silent channel first has to drop out of the meter map.
- **When a live factor multiplies a stored value, the echo must be divided back out.** `applyChannelGain()`
  writes `stored × pan × duck` to PipeWire, and `onNode()` stored that echo as the new truth — so the next
  tick ducked the already-ducked value and the fader crawled towards silence over a stream (measured: 0.1166
  after release, below the ducked 0.1259). Pan had the same hole forever; it only never showed because pan
  changes rarely and a ducker writes 25×/s.
- **A red run says nothing about your code until you know what ran next to it.** On 2026-09-21
  `test_service_cli.py` went **22 failed / 22 passed**, and every message was `kmixdeck: no session bus` or
  `pw-dump … exit status 255` — not one about the feature under test. My first explanation was memory
  pressure, and it was wrong: the timestamps show that run ending **8 seconds** after a full `ctest` in the
  same tree, i.e. I had started the single file next to a full gate and broken the rule three lines above
  this one. Both logs carry the same `no session bus` errors (20 and 25 of them) because they took each
  other's buses away. The same file, same working tree, alone: **44/44 green**.
  So before you read a single assertion: `cat /proc/loadavg`, `free -g`, and `ps -ef | grep ctest` — a load
  check at the *start* of a wait loop proves nothing about what starts during it. Then confirm suspicion
  against the committed state (`git stash` → build → run) instead of reading the diff: that is what proved
  `test_cl5_tree` was already red before this branch (seven leftover scenes from earlier tests in the same
  module, fixed here by looking for the test's own scene instead of comparing the whole list).

## Where things live

- Requirements (source of truth, one row per feature with status): `docs/spec/requirements.md`
- Decisions: `docs/adr/`
- Measured device facts: `docs/devices/`
- Daemon: `src/mixer.*`, `src/daemon/`, `src/pipewire/`; CLI: `src/cli/`; KDE UI: `src/frontend/`, `src/qml/`
- Tests: `tests/integration/` (sandboxed PipeWire + WirePlumber + private D-Bus per suite)

- **Ein Pegel beweist keinen Signalweg, wenn beide Wege am selben Knoten enden.** Fuer Kanaele
  sitzt die FX-Kette VOR dem Plain-Sink (`kmixdeck.sample -> kmixdeck.fx.<slug> -> .out ->
  kmixdeck.channel.<slug>`, gemessen mit `pw-link -l`). Ein Pegel am Kanal-Sink ist deshalb in
  jedem Fall post-chain — er kann nicht unterscheiden, ob eine Quelle die Kette durchlaufen oder
  umgangen hat. Drei Fassungen des CT-8-FX-Tests waren gruen, obwohl der Fix zurueckgedreht war.
  Was trennt: die echte Verlinkung pruefen (`pw_sandbox.sink_of(stream)`). Merksatz: bei "laeuft X
  durch Y" erst fragen, ob die Messstelle die beiden Faelle ueberhaupt auseinanderhalten KANN.
- **Gegenprobe pro Teilfix, nicht pro Feature.** Ein Test, der zwei Fixes gleichzeitig absichern
  soll, kann von einem der beiden gruen gehalten werden. Bei CT-8 einzeln zurueckgedreht: Zielwahl
  raus => rot, Neustart raus => rot. Erst damit ist bewiesen, dass beide Teile abgesichert sind.
- **Bauen VOR dem Messen, Zeitstempel belegen.** `stat -c '%y' build/bin/kmixdeckd` gegen die
  geaenderte Quelle. Eine Gegenprobe, die das alte Binary testet, ist wertlos — und faellt nicht
  auf, weil sie plausibel gruen ist.
- **Ein Audio-Ergebnis neben einem laufenden Gate ist keine Messung.** Am
  2026-09-22 habe ich `test_service_cli.py` gestartet, waehrend `gate.sh` lief:
  21 von 44 rot mit `kmixdeck: no session bus` und `pw-dump rc=255`. Kein
  Produktfehler, sondern zwei PipeWire-Graphen auf vier Kernen —
  `gate.sh` warnt genau davor, und ich habe die Warnung selbst ausgeloest und
  dann ignoriert. Dasselbe Muster erklaert `integration-ports`: im Gate rot nach
  19,32 s, allein bei Grundlast 1,51 **20/20 gruen in 348 s** (Faktor 18). Vor
  jeder Audio-Suite: `pgrep -af gate.sh` und `/proc/loadavg` lesen. Ein Rotlauf
  unter Fremdlast wird nicht diskutiert, er wird wiederholt.
- **Der rote Test ist selten der schuldige Test.** `test_ct7_export_import…` war
  seit mindestens 2026-09-21 im Verbund rot und allein gruen. Es waren ZWEI
  Verursacher hintereinander, und das ist der eigentliche Lehrsatz: nach dem
  ersten Fix war der Test noch rot, nur an einer spaeteren Stelle.
  (1) Die Core-Zeile „MX-10 mix mute" liess `stream` stumm — die modulweite
  `stack`-Fixture traegt das neun Tests weiter, CT-7 exportierte einen stummen
  Mix und misst `-inf dB` statt `-33 dB`. (2) Die CT-8-Zeile liess ein
  120-s-Sample auf einem Board-Kanal laufen, der mit 1,0 in jeden Mix speist —
  CT-7 las den Mix **+4,6 dB ueber** dem Kanal statt −9 dB darunter.
  Einzelproben fanden (2) nicht: mit nur einer Nachbarzeile war CT-7 jedes Mal
  gruen, erst alle neun zusammen zeigten es. Wenn Einzelproben gruen sind und
  der Verbund rot, ist die Ursache die SUMME — dann misst man im Fehlerfall den
  Zustand, statt weiter Paare zu bilden: der Assert nennt jetzt Kanaele, Mutes
  und Zellen, und `board` stand sofort in der Liste. Nie das Timeout hochdrehen,
  das verdeckt nur die Diagnose. Jede CORE-Zeile, die Zustand setzt, nennt ihr
  Undo als 10. Tabellenfeld (vier Zeilen brauchten es).
- **`msgmerge`-Rateschaetzungen sind Falschaussagen, keine Uebersetzungen.** Ein
  neuer String erbt per Fuzzy-Match die Uebersetzung eines aehnlich geschriebenen
  alten. Gemessen 2026-09-22: `Add sample…` → „Mix hinzufügen …", `Choose an audio
  file` → „Bild auswählen", `Remove this sample` → „Kanal entfernen". Alle drei
  haetten im UI etwas Falsches behauptet, und `msgfmt` zaehlt sie als
  **uebersetzt** — nur `--statistics` nennt sie separat als `fuzzy`. Nach jedem
  `tools/extract-messages.sh` jede Fuzzy-Marke einzeln lesen und die Zeile neu
  schreiben, nie die Marke allein entfernen. Die Marke heisst uebrigens
  `#, fuzzy, kde-format`, nicht `#, fuzzy` — ein Filter auf die kurze Form
  trifft nichts.
- **Eine berechnete Property beweist kein Signal.** `Mixer.Samples` wird bei jedem Lesen neu
  gebildet, also ist Pollen immer korrekt — auch wenn der Daemon schweigt. Die Clients pollen aber
  nicht, sie rendern aus `PropertiesChanged`. Ein Test, der die Property abfragt, war gruen, obwohl
  `Sampler::stop()` kein `finished()` feuerte und jedes Pad ewig "playing" zeigte (2026-09-22).
  Was trennt: `busctl --user monitor --match "...member='PropertiesChanged',path='/org/kmixdeck1'"`
  mitschneiden und auf die Ansage pruefen — so wie ein Client es sieht.
