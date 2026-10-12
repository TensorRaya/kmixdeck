# Release v0.4.0 — CI flakes ops-c44tb, journal

Basis: `db77483` (release/0.3, 102 commits since `v0.3.0`). Goal: tag v0.4.0 from a green CI, install on the
streaming host. Blocker was ops-c44tb: the last three release/0.3 runs were red, each on a different test.

## Pointer

| Test | CI red since 10-07 | Root cause | Kind | Fix |
|---|---|---|---|---|
| `ux18_silence_after_a_tone…` / `ux18_loudness_is_visible…` | 3 + 1 | Meter and analyser built against a node the graph did not know yet | product | `Meters::buildWantedLoudness/Peaks` |
| `dv30c_thirtytwo_by_thirtytwo…` | 2 | Edge watcher: LastError sticky after a late arrival; the retry left twins | product | `Mixer::checkPendingEdges`, `Graph::unloadLoopback` |
| `ux14_web_mix_end_to_end…` | 5 | Test clicked the link button before the page had re-rendered it | test | wait for `aria-pressed` |
| `ux18_loudness_is_visible…` (KDE M is a dash) | local only | 12 s tone ended before the fourth frontend read | test | 90 s tone, stopped in `finally` |

## Journal

Facts first, hypotheses marked. Measured in the CI image (`kmx-ci:26.04`, PipeWire 1.6.2, WirePlumber 0.5.13)
limited to 2 CPUs, "load" = 4 to 6 busy loops in the same container.

### CI history (GitHub Actions, release/** branches)

- FACT: 18 completed runs since 10-07. Failing tests: ux14 5×, ux18 floor 3×, dv30c 2×, ux18 published-without-
  subscriber 1×, ux18 visible 2× ("chrome did not start", setup), all others green.
- FACT: run 37946010907 (dv30c red): `LastError = "edge could not be created: Mix: r1 → output"`, while the
  same test's pw-dump shows `kmixdeck.out.r1` once, state running. Every node the test waits for was there.
- FACT: run 37629207867 (ux18 floor red): floor mix `[-70,-70,-70,-70]` after a 12 s -20 LUFS tone, the Stream
  mix next to it `I = -20.36`, `Levels.Subscribers = 0`.

### ux18: analyser for a mix that is not in the graph yet

- FACT: idle box, `mix add; mix loudness on` back to back: analyser linked to the new mix in 14 of 14 rounds,
  0.6-1.2 s.
- FACT: the parity test under 6 burners, with a wait for "analyser reads the mix" added: red once, the dump at
  that moment showed ONE `kmixdeck.loudness` stream, for `kmixdeck.mix.stream`; none for `kmixdeck.mix.r128`,
  although the bus said `Loudness = true`. Daemon log: `pipewire core error … defined target not found` twice.
- FACT (code): `createLoud()` and `create()` read the target's media class from the graph snapshot to decide
  `stream.capture.sink`. No node → no flag → the stream looks for a source of that name → WirePlumber: "target
  not found"; `node.dont-reconnect` keeps it unlinked for good.
- FACT (repro): PipeWire frozen with SIGSTOP between `mix add Frozen` and `mix loudness frozen on`, thawed 1 s
  later. Old daemon: analyser never linked, 3 of 3, two "target not found" lines each. New daemon: linked
  0.63-0.81 s after the thaw, 3 of 3, capture.sink true, no "target not found".
- FIX: meters and analysers are created only for nodes the graph knows; the wish is kept (`wantedLoud`,
  `wantedPeaks`) and built on `Graph::nodeAdded` (queued, same rule as CH-4).
- TEST: `test_fx.py::test_ux18_an_analyser_asked_for_before_its_mix_sink_exists_still_measures_that_mix`.
  Against the old meters.cpp: red ("did not happen within 15.0s"). Against the new: green.

### dv30c: an edge that arrives after the watcher's 3 s

- FACT (repro): PipeWire frozen 5 s while `mix add Late` runs. Old daemon, 2 of 2: LastError set, every node in
  the graph 0.4 s after the thaw, LastError still set 11 s later, and SIX twins: `kmixdeck.out.late`,
  `kmixdeck.source.late`, `kmixdeck.tap.late` and their `.in` halves, two of each. The twin sweep in
  `ensureCellGraph` covers cells and taps only, and the tap twin still survived.
- FACT (production): the streaming host, 5 days of journal: 0 "never appeared", 79 kmixdeck nodes, no twins. The
  watcher only fires under load; on that machine it has not.
- FIX 1: the watcher keeps the edges it reported (`m_edgeFailed`) while the layout still wants them; when the
  last one has arrived and nothing failed to LOAD (`moduleLoadFailed`), LastError is cleared. A module that did
  not load keeps its report (dv30b still proves that path).
- FIX 2: `Graph::loadLoopback` keeps a handle per module (with the capture and playback node names from the
  args), dropped on the module's own destroy event. The watcher's retry calls `Graph::unloadLoopback(name)`
  first, so a late first attempt cannot come up next to the retry.
- FACT (after): same repro, 2 of 2: LastError cleared 0.1-0.6 s after the last node, 0 twins after 10 s.
- TEST: `test_ports.py::test_dv30d_an_edge_that_arrives_late_clears_the_report_it_caused`. Against the old
  daemon: red ("LastError cleared once every edge arrived … did not happen within 10.0s"). Against the new: green.
- DOC: `LastError` description in `docs/dbus-descriptions.toml` (→ `docs/dbus-api.md`, regenerated).

### ux14: the click before the re-render

- FACT (code): the cell's link button decides at click time from what it was rendered with — pressed → unlink,
  not pressed → open the "Follow …" menu. The test waited for the DAEMON (`Follows` via the CLI) and clicked at
  once; the bridge's PropertiesChanged patch re-renders the cell a moment later.
- FACT: 6 of 6 green under 6 burners before the change, so the window is narrow; the CI failures say it exists.
- HYPOTHESIS (not reproduced locally): CI's red ux14 is that window. The change waits for what a person sees
  (`aria-pressed="true"`) before clicking; it cannot make a green run red.

### ux18 visible: the tone ends before the last frontend reads

- FACT: under 6 burners the KDE window read `M = –` while the bus still had `I`; the window start alone took long
  enough that the 12 s tone had ended (M is a 400 ms window). `tone_at_known_loudness(seconds=)` now makes a
  90 s tone for this test; the existing `finally` stops it.

### Under load, before and after (2 CPUs, 6 busy loops, one test at a time)

| Test | old (`db77483`) | new |
|---|---|---|
| `ux18_loudness_is_visible…` | red 2 of 3 (analyser never linked, then KDE `M = –`) | red 0 of 4 |
| `ux18_silence_after_a_tone…` | red 0 of 6 alone; red in the full file (4 loops, run 1) | red 0 of 4 |
| `ux14_web_mix_end_to_end…` | red 0 of 6 | red 0 of 4 |
| `dv30d…` (new) | red (LastError never cleared) | green |

The old side being green for ux14 and ux18 floor in isolation is why these are flakes: the gap needs the suite's
own load on top. The deterministic repros above (SIGSTOP) are the evidence for the cause, not these counts.

### Gates

- FACT: `ruff check .` 0, `tools/sot-audit.py` ok, `tools/dbus-doc.py` 130/130, `ninja` with
  `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON` 0 warnings, `appstreamcli validate` ok.
