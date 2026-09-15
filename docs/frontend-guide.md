# Writing a frontend for kmixdeck

kmixdeck is a service. The Kirigami app in this repo is *one* frontend; yours can be a GTK app,
a Stream Deck plugin, a Waybar module, a Python script. You need no code from this repository
— only a D-Bus client library for your language.

## The 5-minute tour with `busctl`

```sh
# Is it there? (This call starts the service if it is not running — D-Bus activation.)
busctl --user call org.kmixdeck1 /org/kmixdeck1 org.freedesktop.DBus.ObjectManager GetManagedObjects

# All objects, one by one
busctl --user tree org.kmixdeck1

# Read a fader
busctl --user get-property org.kmixdeck1 /org/kmixdeck1/cell/game/stream org.kmixdeck1.Cell Volume
# d 1

# Set it (linear 0..1; 0.25 = −12 dB)
busctl --user set-property org.kmixdeck1 /org/kmixdeck1/cell/game/stream org.kmixdeck1.Cell Volume d 0.25

# Mute
busctl --user set-property org.kmixdeck1 /org/kmixdeck1/cell/game/stream org.kmixdeck1.Cell Muted b true

# Watch everything change live (your UI subscribes to exactly these signals)
busctl --user monitor org.kmixdeck1

# Create things
busctl --user call org.kmixdeck1 /org/kmixdeck1 org.kmixdeck1.Mixer AddMix s "Recording"
```

## Object model

```
org.kmixdeck1                                   (bus name; "1" = API major version)
└── /org/kmixdeck1                              org.kmixdeck1.Mixer + org.freedesktop.DBus.ObjectManager
    ├── channel/<slug>                          org.kmixdeck1.Channel   what apps play into
    ├── mix/<slug>                              org.kmixdeck1.Mix       what you / the stream hear
    ├── cell/<channel>/<mix>                    org.kmixdeck1.Cell      THE fader: this channel in this mix
    └── app/<id>                                org.kmixdeck1.App       a running application stream (planned)
```

A UI is a grid: channels down, mixes across, one `Cell` per intersection. Everything else is
decoration. The introspection XML in [`interfaces/`](../interfaces/) is the authoritative contract.

## Devices (ADR 0007)

- `Mixer.OutputDevices` / `Mixer.InputDevices` (`a{ss}`): hardware sinks a mix can play to / hardware
  sources a channel can be fed by — `node.name` → human description. Your own virtual nodes
  (`kmixdeck.*`) are deliberately not in these lists.
- `Mix.OutputDevice` is one `node.name` (or empty = capture-only). The daemon renders one loopback per
  mix output; unplugging a device parks its output on the hidden `kmixdeck.null` sink and WirePlumber
  re-links it when the device returns — no frontend action needed. Grey out a device when it is not in
  the list; keep the configured value, it comes back.
- `Mix.CaptureSource` names the virtual Audio/Source node (`kmixdeck.source.<mix>`) that OBS or
  Discord can pick as their input device.

## Level meters (ADR 0006)

`org.kmixdeck1.Levels` on the root object: `Subscribe()` / `Unsubscribe()`, then one `Peaks(a{sv})`
signal per tick (~25 Hz), keyed `channel/<slug>` and `mix/<slug>`, linear 0..1. The service only runs
the meter graph while somebody is subscribed — call `Unsubscribe()` when your window hides.

## Undo (CH-9)

`RemoveChannel`/`RemoveMix` are never dead ends: afterwards `Mixer.UndoDescription` (`s`) holds a human string
(`channel “Music”`) and `Mixer.Undo()` brings everything back — layout entry, links, hardware input,
default-channel flag, outputs, fallback, master level *and every cell's fader/mute*. One level; any later
add clears it (`UndoDescription` becomes `""`). Show it as a toast with an action, that is what the KDE
frontend does; you do not have to keep any state yourself.

## Linked cells (MX-7)

`Cell.Follows` (`o`, read/write) — object path of another **mix**; the cell then mirrors volume and mute of
the same channel's cell in that mix ("Stream follows Monitor for Game"). `/` = independent. Semantics are
Wave Link's: **writing `Volume` or `Muted` on a following cell breaks the link** — the daemon does that, you
only have to re-read `Follows` (it arrives in the same `PropertiesChanged`). Self and A↔B loops are refused.

## Several outputs per mix (MX-9) and the fallback (DV-15)

`Mix.Outputs` (`as`) lists every hardware sink the mix plays to; `Mix.AddOutput(s)` / `Mix.RemoveOutput(s)`
edit it. `Mix.OutputDevice` (`s`) is the single-output view — it reads `Outputs[0]` and *replaces only that
entry*, so a simple frontend and a multi-output one can coexist. `Mix.FallbackOutput` (`s`) is played to
while **every** entry of `Outputs` is unplugged; empty means silence — kmixdeck never falls back to the
system default sink on its own (feedback trap, ADR 0002).

## Default channel for new applications (CH-5)

`Mixer.DefaultChannel` (`o`, read/write) — object path of the channel that applications kmixdeck has
**never routed before** are moved to when they first appear; `/` turns this off. Applications the user has
placed once are left alone forever (WirePlumber remembers their target; kmixdeck only remembers *that* it
has seen them, in `layout.json` → `knownApps`). Removing the default channel resets the property to `/`.

## A note on refused property writes

`org.freedesktop.DBus.Properties.Set` cannot return a kmixdeck error: Qt answers it before our setter runs.
An out-of-range or unknown value is **ignored and logged** by the daemon; read the property back if you
need to know. Methods (`SetVolumeDb`, `ToggleMute`, `MoveTo`, `AddChannel` …) do return proper errors —
prefer them when you want feedback.

## The three things your frontend must do

1. **Bootstrap** with `GetManagedObjects()` — one round trip returns every object with all
   properties.
2. **Stay in sync** by subscribing to `org.freedesktop.DBus.Properties.PropertiesChanged`
   (sender `org.kmixdeck1`, any path) and `InterfacesAdded` / `InterfacesRemoved` on
   `/org/kmixdeck1`. Never poll.
3. **Write** with `org.freedesktop.DBus.Properties.Set`. Do not wait for the reply to update
   your widget — the `PropertiesChanged` that follows is the truth (the service may clamp).

## Units and conventions

- `Cell.Volume`, `Channel.Trim`: **linear** amplitude 0..1 — what PipeWire uses.
  - dB: `20·log10(v)`; 0.5 → −6.02 dB, 0.25 → −12.04 dB, 0 → −∞.
  - If you want a Plasma-like fader feel, display `cbrt(v)` and write `x³` (cubic curve).
- Slugs (`game`, `stream`) are stable IDs; `Name` is the display string and may change.
- Paths are lowercase `[a-z0-9_]` (D-Bus object-path alphabet), so they are safe in URLs, config files and shell.
- Errors come back as D-Bus errors (`org.freedesktop.DBus.Error.InvalidArgs` for out-of-range).

## Lifecycle you can rely on

- The service is D-Bus-activatable: your first call starts it. It runs as a `systemd --user`
  unit bound to PipeWire, restarts on failure.
- If it disappears, `org.freedesktop.DBus.NameOwnerChanged` for `org.kmixdeck1` fires with an
  empty new owner. Show "service not running", then re-bootstrap when it returns.
- `Mixer.Connected` tells you whether the *service* has PipeWire. Audio keeps flowing either
  way — the graph lives in PipeWire, not in the service (ADR 0002).

## Generated bindings

| Language / toolkit | Generator | Command |
|---|---|---|
| C++/Qt | `qdbusxml2cpp` | `qdbusxml2cpp -p cellproxy interfaces/org.kmixdeck1.Cell.xml` |
| C++ (no Qt) | `sdbus-c++-xml2cpp` | `sdbus-c++-xml2cpp interfaces/org.kmixdeck1.Cell.xml --proxy=cell-proxy.h` |
| C / GTK | `gdbus-codegen` | `gdbus-codegen --interface-prefix org.kmixdeck1. --generate-c-code kmixdeck interfaces/*.xml` |
| Python | `dbus-next`, `pydbus`, `jeepney` | introspect at runtime; no generation step |
| Rust | `zbus` | `zbus-xmlgen` |

## Reference implementation

`src/cli/main.cpp` (~250 lines) is a complete client: bootstrap, set, error handling, `--json`,
`watch`. Read that before anything else. `src/frontend/mixerclient.cpp` is the same idea kept
live for a GUI.

## Compatibility promise

Within `org.kmixdeck1`, members are only ever **added**. A breaking change ships as
`org.kmixdeck2` alongside, with `org.kmixdeck1` kept alive for at least one release cycle.
