# ADR 0005 — Service / CLI / frontend split: `kmixdeckd` on the session bus

- Status: accepted
- Date: 2026-09-14
- Closes: requirements AR-1…AR-7; research `docs/research/daemon-frontend-split.md`

## Context

Owner's brief (2026-09-14): *all logic in a background service; a CLI; the KDE UI is just laid
on top so others can build UIs for their desktops.* ADR 0002 already put the audio graph into
PipeWire itself, so the service carries no audio — it manages graph objects, the layout model,
app routing, hotkey actions, and persistence.

Reference daemons examined (live introspection on Ubuntu 26.04 where available, otherwise
docs): NetworkManager, UPower, systemd (`org.freedesktop.systemd1`), WirePlumber/`wpctl`,
`pipewire-pulse`, EasyEffects.

## Decision

### 1. Three binaries, one contract
| Binary | Role |
|---|---|
| `kmixdeckd` | the service: only PipeWire client, owns the model, exports the D-Bus API |
| `kmixdeck` | CLI: thin D-Bus client, 100 % API coverage, `--json` |
| `kmixdeck-kde` | Kirigami UI: thin D-Bus client through a *generated* proxy |

The **introspection XML in `interfaces/`** is the contract. Both frontends are generated from
it (`qdbusxml2cpp`); third parties generate with whatever they like (`gdbus-codegen`,
`sdbus-c++-xml2cpp`, `dbus-python`, `zbus`…) — that is the "plugin" mechanism: a bus, not a
plugin ABI.

### 2. D-Bus on the session bus, not a private socket
i3/sway/mpv use private sockets because they live outside D-Bus desktops or need zero
dependencies. A mixer for desktop streamers runs inside a desktop session where D-Bus is
already there; we get for free: activation on first call, single instance, per-object
properties with change notification, `busctl`/`gdbus`/`qdbus` as debugging clients,
introspection so a frontend author needs no docs beyond the XML. A JSON-RPC side channel can
be added later without touching the model (AR-2 does not forbid a second transport).

### 3. Object model (pattern: UPower/NetworkManager — one object per resource)
```
bus name    org.kmixdeck1                      (suffix 1 = API major version; new major = new name, old stays)
/org/kmixdeck1                                 org.freedesktop.DBus.ObjectManager
                                               org.kmixdeck1.Mixer
/org/kmixdeck1/channel/<slug>                  org.kmixdeck1.Channel
/org/kmixdeck1/mix/<slug>                      org.kmixdeck1.Mix
/org/kmixdeck1/cell/<channel>/<mix>            org.kmixdeck1.Cell       ← the fader (ADR 0002)
/org/kmixdeck1/app/<id>                        org.kmixdeck1.App        (running audio streams, CH-4)
```
All objects implement `org.freedesktop.DBus.Properties` (with `PropertiesChanged`) and
`org.freedesktop.DBus.Introspectable`. Frontends bootstrap with one call:
`GetManagedObjects()`; they track lifecycle with `InterfacesAdded/Removed`.

Key members (full XML in `interfaces/`):
- `Mixer`: props `Version (s)`, `Connected (b)` [PipeWire]; methods `AddChannel(s name) → o`,
  `AddMix(s name) → o`, `RemoveChannel(o)`, `RemoveMix(o)`, `Save()`, `Restore(s path)`.
- `Channel`: props `Name (s)`, `Slug (s, const)`, `Icon (s)`, `Trim (d)` [linear], `Muted (b)`.
- `Mix`: props `Name`, `Slug (const)`, `OutputDevice (s)`, `CaptureSource (s, const)`.
- `Cell`: props `Channel (o, const)`, `Mix (o, const)`, `Volume (d)` [linear 0–1; UI curve is
  the frontend's business], `Muted (b)`; method `SetVolumeDb(d)` convenience.
- `App`: props `Name`, `Binary`, `NodeId (u)`, `Channel (o)`; method `MoveTo(o channel)`.

Volumes are **linear** on the bus (what PipeWire uses); cubic/dB mapping is presentation.
Level meters (Q-6) are *not* properties — if adopted, a dedicated `Meters` interface with a → **resolved by ADR 0006** (daemon meters, one `Peaks` signal per tick, measured 2–4 % of a core for 24 nodes)
`Levels(a{od})` signal at a fixed rate, opt-in via `Subscribe()`, so idle frontends cost nothing.

### 4. Lifecycle (pattern: `pipewire-pulse.service`)
`kmixdeckd.service` (user): `Type=dbus`, `BusName=org.kmixdeck1`, `BindsTo=pipewire.service`,
`After=pipewire.service wireplumber.service`, `Restart=on-failure`, `WantedBy=default.target`;
plus `/usr/share/dbus-1/services/org.kmixdeck1.service` with `SystemdService=kmixdeckd.service`
so a frontend's first call starts it. On PipeWire `EPIPE` the daemon reconnects with backoff and
re-mirrors the graph; objects that disappeared are removed via `InterfacesRemoved`.

### 5. Binding: QtDBus for our two frontends and the daemon
Same toolchain as the rest (ADR 0004); `qdbusxml2cpp -a` for the adaptors in the daemon,
`-p` for the proxies in CLI and UI. `sdbus-c++` (packaged, 2.2.1) is the recommendation *for
non-Qt C++ frontends*; GDBus for GTK. The contract makes the choice irrelevant to us.

### 6. CLI (conventions from `wpctl`, `nmcli`, `busctl`)
```
kmixdeck [--json] [--no-color]
  status                                   overview: channels × mixes table with levels
  channel  list | add <name> | remove <slug> | rename <slug> <name> | trim <slug> <vol> | mute <slug> [on|off]
  mix      list | add <name> | remove <slug> | rename <slug> <name> | output <slug> <device>
  cell     get <ch> <mix> | set <ch> <mix> <vol|NdB|N%> | mute <ch> <mix> [on|off]
  app      list | move <id|name> <channel>
  watch                                    stream PropertiesChanged / InterfacesAdded as lines or JSON
  save | restore <file>
```
Exit codes: 0 ok · 1 bad usage · 2 service unreachable · 3 object not found · 4 rejected by
service. Bash/zsh/fish completion generated from the same command table.

## Consequences
- The current in-process `Mixer` + `pw::Graph` move into `kmixdeckd`; the QML UI loses its
  direct PipeWire link and talks to a generated proxy (AR-2 becomes enforceable: the UI target
  does not link `libpipewire`).
- `tests/integration` grows a second sandbox: a private session bus (`dbus-run-session`) next
  to the private PipeWire, and tests drive the daemon **through the CLI** — the CLI becomes the
  test client, which also proves AR-3.
- `docs/frontend-guide.md` (AR-7) shows the whole API with `busctl` only, no code of ours.
