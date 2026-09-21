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
- **Run the whole suite before you push:** `cd build && ctest --output-on-failure` must be 10/10. Isolated green
  is not green — three of today's daemon bugs only showed under full-suite load.
- **Do not build or run daemons in the tree while ctest runs.** Half of today's red runs were self-inflicted.
  This includes "isolated" A/B comparisons: on 2026-09-21 I ran three suites "alone" while a full gate was
  running next to them (load 8.3–11.0). Both sides were oversubscribed, so the green proved nothing. Before any
  A/B: check `cat /proc/loadavg` against `nproc` and record both in the log.
- **A red run under memory pressure says nothing about your code.** On 2026-09-21 `test_service_cli.py` went
  **22 failed / 22 passed** — and every message was `kmixdeck: no session bus` or `pw-dump … exit status 255`,
  not one of them about the feature under test. The suite starts its own dbus **and** pipewire per module; with
  ~2 GB free of 7 GB they stop coming up, and that looks exactly like a broken patch. The same file, same
  working tree, at load 2.6: **44/44 green**. So before you read a single assertion: `free -g` and
  `/proc/loadavg`. And confirm suspicion against the committed state (`git stash` → build → run) instead of
  reading the diff — that is what proved `test_cl5_tree` was already red before this branch (seven leftover
  scenes from earlier tests in the same module, fixed here by looking for the test's own scene instead of
  comparing the whole list).

## Where things live

- Requirements (source of truth, one row per feature with status): `docs/spec/requirements.md`
- Decisions: `docs/adr/`
- Measured device facts: `docs/devices/`
- Daemon: `src/mixer.*`, `src/daemon/`, `src/pipewire/`; CLI: `src/cli/`; KDE UI: `src/frontend/`, `src/qml/`
- Tests: `tests/integration/` (sandboxed PipeWire + WirePlumber + private D-Bus per suite)
