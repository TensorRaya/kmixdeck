# Contributing to kmixdeck

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
