# Testing strategy

Status: binding for all contributions. Added 2026-09-14 after the first prototype measurements.

## Why this document exists

The first measurement of MX-2 was wrong: `pw-record --target <sink>` attached to an arbitrary
port and reported "no difference between mixes" for a graph that was in fact correct. An audio
mixer's requirements are *acoustic* claims (−12 dB here, silence there, same level after a
reboot) — they need to be proven with audio, automatically, on every change. Reading the graph
is not enough.

## How the projects we build on test

| Project | Pattern | What we take from it |
|---|---|---|
| **PipeWire** (`test/pwtest.c`) | Each test gets a fresh `XDG_RUNTIME_DIR`; a private `pipewire` daemon is spawned and `PIPEWIRE_REMOTE` points to it. The user's session is never touched. | Sandbox daemon per test session (`tests/integration/pw_sandbox.py`) |
| **plasma-pa** (`autotests/`, `appiumtests/`) | Pure logic via `ecm_add_test` + QTest; UI via Appium/Selenium against a spawned `pipewire` + `pipewire-pulse`. | `ecm_add_test` for C++ units; Appium later for the QML UI (issue #13) |
| **WirePlumber** | Lua-scripts tested through the running daemon's state files. | We assert on `stream-properties` contents for persistence (DV-7) |

## Three levels

### 1. Unit (C++, QTest, runs in < 1 s)
`tests/unit/*.cpp`, registered with `ecm_add_test`, linking the **QML-free** `kmixdeck_core`
static library. Covers: slugging (DV-7), node naming (ADR 0002), the cubic↔linear volume curve
(UX-7). No PipeWire process is involved. Runs on every `ctest`.

### 2. Integration (Python + pytest, ~40 s, no sound card)
`tests/integration/` starts a **private PipeWire + WirePlumber** with the prototype config and
proves requirements acoustically:

- A 1 kHz tone is played into a channel with `pw-play` **with autoconnect off**, then linked by
  port name with `pw-link`. Recording uses `pw-record` the same way, from the mix's
  `monitor_FL/FR` ports. Never `--target` (see "why this document exists").
- Level = RMS over ≥ 1 s via `ffmpeg astats`. Assertions are relative (Δ between mixes) with
  ±0.5 dB tolerance; absolute levels depend on the tone file, not on the graph.
- Every test names the requirement it covers (`MX-2`, `CH-4`, `DV-7`, …) in its docstring.
- Persistence is tested by **restarting the sandbox daemon** and re-reading the node Props.

Enabled automatically in `ctest` when `pytest`, `pipewire`, `wireplumber` and `ffmpeg` are
found; label `integration`.

### 3. UI (planned, issue #13)
Appium + `selenium-webdriver-at-spi`, as plasma-pa does: start the app under a sandbox daemon,
drive faders, assert the resulting node Props — the UI is tested through what it *does to
PipeWire*, not through screenshots.

## Rules

1. A requirement in `docs/spec/requirements.md` is marked ✅ only when a test in this repo
   asserts it. The test's docstring cites the requirement ID; the requirement's *Source*
   column cites the test.
2. Measurements go in the ADR that they validate (ADR 0002 has the table). Numbers in prose
   without a script that reproduces them are not accepted.
3. Nothing in `tests/` may touch the user's PipeWire. If a test needs a daemon it gets a
   sandbox from `pw_sandbox.start_private_pipewire()`.
4. `ctest` must pass before merge. `ninja -C build && ctest --test-dir build` is the whole gate.

## Running

```sh
cmake -S . -B build -G Ninja && ninja -C build
ctest --test-dir build --output-on-failure           # all three levels
ctest --test-dir build -L integration                # only the acoustic tests
pytest -v tests/integration                          # same, with per-test output
```

## Suites (one ctest each, run in parallel with `ctest -L integration -j4`)

| File | Covers | Style |
|---|---|---|
| `test_service_cli.py` | bus contract vs shipped XML + GetManagedObjects, CLI exit codes, app routing, devices, presence, levels | bus + graph |
| `test_audio_graph.py` | the generated PipeWire config alone (no daemon): DV-1, unity defaults, source node | graph only |
| `test_lifecycle.py` | create / rename / remove channels + mixes under load, duplicates, bad names, empty matrix, corrupt layout | bus + graph + files |
| `test_routing.py` | acoustic: fader isolation between mixes, trim/mute scope, parked = silent, device follows mix, +6 dB sum, capture source, hardware input, restart keeps levels | RMS measured |

Every suite starts its own private PipeWire (`pw_sandbox.py`) — the host's audio is never touched.
Found by these tests so far (kept as regression cases): D-Bus-invalid slugs with `-`, segfault on empty
name (`sendErrorReply` on an unregistered adaptor), orphan inputs after channel removal, mix output
loopback dying on unplug without `node.linger`, `GetManagedObjects` missing `InputDevices`.
