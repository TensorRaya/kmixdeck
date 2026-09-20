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
- **Run the whole suite before you push:** `cd build && ctest --output-on-failure` must be 10/10. Isolated green
  is not green — three of today's daemon bugs only showed under full-suite load.
- **Do not build or run daemons in the tree while ctest runs.** Half of today's red runs were self-inflicted.

## Where things live

- Requirements (source of truth, one row per feature with status): `docs/spec/requirements.md`
- Decisions: `docs/adr/`
- Measured device facts: `docs/devices/`
- Daemon: `src/mixer.*`, `src/daemon/`, `src/pipewire/`; CLI: `src/cli/`; KDE UI: `src/frontend/`, `src/qml/`
- Tests: `tests/integration/` (sandboxed PipeWire + WirePlumber + private D-Bus per suite)
