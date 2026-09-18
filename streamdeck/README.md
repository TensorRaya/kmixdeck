# kmixdeck on a Stream Deck (CT-3)

`me.kmixdeck.sdPlugin/` is an [OpenAction](https://openaction.amankhanna.me) plugin — the format OpenDeck (Linux) and
Tacto speak, and a compatible subset of the Elgato Stream Deck SDK. It contains **no mixer logic**: every key press is
one call on the kmixdeck D-Bus API (through the `kmixdeck` CLI in `--json` mode), every title and state on the deck
comes from `kmixdeck watch`. Window, tray, CLI and the deck all read the same daemon — ADR 0010, rule 1.

## Actions

| Action | Setting | Key | Dial (Stream Deck +) |
|---|---|---|---|
| **Mute** | channel / mix / cell (channel in mix) | toggle mute; title shows name + level or MUTE | — |
| **Fader** | target, step (dB), key direction | ± step | rotate = ⅓ step per detent, press = mute |
| **Listen on** | output device | hear the monitor mixes there (UX-2) | — |

The property inspector lists channels, mixes, cells and output devices **live from the daemon** (it asks the plugin,
which asks kmixdeck) — no slugs to type.

## Install

```sh
kmixdeck streamdeck install      # symlinks the plugin into ~/.config/opendeck/plugins (and the Flatpak location if present)
# restart OpenDeck → its action list gains a "kmixdeck" category
kmixdeck streamdeck uninstall
```

Requirements on the host running OpenDeck: `kmixdeck` in `$PATH` (or `KMIXDECK_CLI=/path/to/kmixdeck`), Python 3,
`python3-websockets`.

## Tested how

`tests/integration/test_streamdeck.py` starts a fake OpenAction server (WebSocket, same handshake and events as
OpenDeck), launches the plugin exactly as OpenDeck would (`run.sh -port … -pluginUUID … -registerEvent … -info …`)
against a private PipeWire + kmixdeckd sandbox, and checks both directions: a `keyDown` mutes the cell in the daemon
(measured over the CLI), and a mute done elsewhere (CLI, standing in for window/tray) reaches the key as
`setState`/`setTitle`. Dial rotation, the listening device, the inspector's live lists, settings changes and an
unknown target (alert, no crash) are covered too. `tools/check-openaction-manifest.py` mirrors OpenDeck's serde rules
for `manifest.json` so a missing required field fails ctest instead of making the plugin silently not appear.

Not yet done on real hardware — OpenDeck isn't installed on the development machine. The protocol is the contract;
the first run on a physical deck is a documentation task, not a coding one.
