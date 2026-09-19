# `kmixdeck` — command-line reference

The CLI is a thin client of the `org.kmixdeck1` D-Bus service. Everything it can do, the window, the tray, the web UI and the
Stream Deck plugin do through the same calls — the CLI is just the one you can script. Every command accepts `--json`
(`-j`) for machine-readable output; `kmixdeck --help` prints the compact version of this page.

```
kmixdeck [--json] <command> [<sub-command>] [args…]
```

Options go **before** the command: everything after it is taken literally, so that `-12dB` is a level and not an option.
`kmixdeck --json status` works, `kmixdeck status --json` prints the table.

**Exit codes:** `0` ok · `1` usage · `2` service not reachable · `3` not found · `4` the daemon refused (the reason is on stderr).

**Levels** are written one of three ways, everywhere a level is expected:
`0.25` (linear 0…1) · `-12dB` · `63%` (the UI's cubic fader position: `100%` = 0 dB, `63%` ≈ −12 dB, `50%` ≈ −18 dB,
`25%` ≈ −36 dB — the same curve as Plasma's volume slider).

**Slugs.** Channels and mixes are addressed by their *slug*: the name lower-cased, ASCII-folded, runs of non-letters → `_`
(`"Game Audio"` → `game_audio`, `"Ünïcode Name!"` → `unicode_name`). `kmixdeck channel list` shows slug and name side by side.

---

## Overview and state

| Command | What it does |
|---|---|
| `status` | The matrix: every channel × every mix with level and mute, plus outputs, inputs, apps. `--json` gives the whole object tree — this is what the tests and the Stream Deck plugin read. |
| `levels [--once]` | Live peak meters at 25 Hz (`#` peak, `=` RMS, `!` clip). `--json` prints one object per tick. Ctrl-C stops and unsubscribes. `--once` prints a single reading and exits (scripts, tests). |
| `loudness [--once]` | EBU R128 per mix that has the meter on (UX-18): momentary, short-term and integrated loudness in LUFS plus true peak in dBTP. `--json` prints `{slug: [M, S, I, TP]}`. |
| `watch` | Prints every property change on the bus as it happens. Useful to see what a UI action actually did. |
| `undo` | Restores the last removed channel or mix — including its cells, outputs and FX (CH-9). One step. |

## First run, backup, restore

| Command | What it does |
|---|---|
| `setup` | Shows the first-run plan: *Monitor* → default output, *Voice* ← default mic, running apps → channels by media role (UX-3). |
| `setup --apply` | Does it. |
| `export [file]` | Writes the layout **and** every fader/trim/mute as one JSON document (to stdout without a file). This is the backup (CT-7). |
| `import <file>` | Replaces the running layout with that document and applies the levels. Refuses documents it cannot parse and keeps the current layout. |

## Channels (rows)

A channel is where applications and hardware inputs land. It has one fader per mix (the *cells*), a trim, a pan, an optional
group and an ordered FX chain.

| Command | What it does |
|---|---|
| `channel list` | Slug and name of every channel (`--json`: all properties). |
| `channel add <name>` | Creates the channel; prints its object path. |
| `channel remove <slug>` | Removes it (undoable). |
| `channel rename <slug> <name>` · `icon <slug> <icon\|none>` · `color <slug> <#rrggbb\|none>` | Presentation. Icons are freedesktop icon names (`audio-input-microphone`). |
| `channel move <slug> <index\|up\|down\|top\|bottom>` | Row order in every UI (UX-9). |
| `channel trim <slug> <level>` | Pre-fader gain of the channel itself (applies to every mix). |
| `channel mute <slug> [on\|off]` | Mutes the channel in every mix. Without `on/off`: toggles. |
| `channel pan <slug> [<-1..1>\|L\|C\|R]` | Stereo position (DV-22). Without a value: prints it. |
| `channel default [<slug>\|none]` | Where never-seen applications land (CH-5). |
| `channel group <slug> [<name>\|none]` · `channel groups` | Grouped channels move together: trim as one dB delta, mute mirrored (CH-8). |
| `channel input <slug> <ref\|none>` | The channel's hardware input, or none. |
| `channel inputs <slug>` · `input-add <slug> <ref>` · `input-remove <slug> <ref>` | Several wires into one channel (ADR 0009). |
| `channel wire <slug> <ref> [trim <level>] [mute on\|off]` | Per-wire trim and mute of one input wire (DV-14). |

**`<ref>` — naming hardware ports.** `node.name` alone means "the whole device". `node.name:AUX3` means one port,
`node.name:AUX3,AUX4` a stereo pair, `node.name:AUX3>L` binds a port to the left side only (DV-21). `kmixdeck devices ports <node.name>`
lists what a device offers and who uses it.

## Mixes (columns)

A mix is an output bus: it has a master fader, a mute, one or more hardware outputs, a fallback output, an FX chain, and is
exposed to other software as the capture source `kmixdeck.source.<slug>` (OBS, Discord…).

| Command | What it does |
|---|---|
| `mix list` · `add <name>` · `remove <slug>` · `rename` · `icon` · `color` · `move` | As for channels. |
| `mix duplicate <slug> <new name>` | Copy with every cell level, output and FX (MX-8). |
| `mix volume <slug> <level>` · `mix mute <slug> [on\|off]` | Master fader and mute. |
| `mix output <slug> <ref\|none>` | Sets **the** output (replaces all). `none` removes every output — the mix keeps running, only its capture source is audible. |
| `mix outputs <slug>` · `output-add <slug> <ref>` · `output-remove <slug> <ref>` | Several hardware outputs at once (MX-9), e.g. headphones + a USB interface. |
| `mix fallback <slug> <ref\|none>` | Played while every output is unplugged (DV-15). |
| `mix wire <slug> <ref> [trim <level>] [mute on\|off]` | Per-wire trim/mute of one output wire. |
| `mix get <slug>` | Every property of the mix as JSON. |

## Cells (a channel in a mix)

| Command | What it does |
|---|---|
| `cell get <ch> <mix>` | Level and mute of that one fader. |
| `cell set <ch> <mix> <level>` | The level of channel *ch* in mix *mix* only — the other mixes do not move. |
| `cell mute <ch> <mix> [on\|off]` | Mute that one fader. |
| `cell link <ch> <mix> <other-mix\|none>` | This cell follows the same channel's cell in *other-mix* (volume + mute). Touching the follower breaks the link (MX-7). |

## Applications

| Command | What it does |
|---|---|
| `app list` | Running application streams: id, name, running state, channel(s). |
| `app move <id\|name> <channel>` | Move the stream to a channel. Remembered for next time the app starts (CH-4). |
| `app assign <id\|name> <ch>[,<ch>…]` | Several channels at once; the first is the primary (CH-12). |

## Devices

| Command | What it does |
|---|---|
| `devices` | Hardware outputs a mix can play to (`node.name → description`). |
| `devices in` | Hardware inputs a channel can be fed by. |
| `devices ports <node.name>` | Every port of a device and who is wired to it (ADR 0009). |
| `devices hide <node.name>` · `unhide` · `hidden` | Keep a device out of every picker; still routable by name (CH-11). |
| `devices virtual list` · `add <name> [--in N] [--out N]` · `remove <slug>` | Virtual devices: a named block of ports other software can use as a sound card (DV-23). |

## Listening and auditioning

| Command | What it does |
|---|---|
| `listen [<node.name>\|none]` | The device *you* listen on and which mixes play there (UX-2). |
| `audition channel\|mix <slug>` | Solo that one entity on the main output — everything else muted; `audition none` restores the previous state exactly (UX-12). This is the hold-to-listen button of the UIs. |

## Effects

Ordered insert chains on any channel or mix (ADR 0008). Builtin effects need nothing; LADSPA effects (noise suppression,
gate, compressor via swh-plugins / rnnoise) are listed only when the library is installed.

| Command | What it does |
|---|---|
| `mix loudness <slug> [on\|off\|<LUFS>]` | Switch the R128 meter for a mix or set its target line; no argument reads both (UX-18). |
| `fx types` | The catalog: every effect type with its controls, ranges and defaults (JSON). |
| `fx presets` | One-click chains ("Podcast voice", …) as editable starting points (FX-4). |
| `fx get channel\|mix <slug>` | The current chain (JSON). |
| `fx set channel\|mix <slug> '<json>'` | Replace the chain. Validated: unknown type, out-of-range control or missing plugin → refused with the reason. |
| `fx clear channel\|mix <slug>` | Remove it. |
| `fx copy channel\|mix <from> channel\|mix <to>` | Copy a chain to another object. |
| `fx control channel\|mix <slug> <node:Control> <value>` | Live tweak of one control — no reload, no click. |

## Stream Deck

| Command | What it does |
|---|---|
| `streamdeck install` · `uninstall` · `path` | Hooks the OpenAction plugin into OpenDeck's plugin folder (CT-3). Works without the daemon. See `streamdeck/README.md`. |

---

## Examples

```sh
kmixdeck cell set game stream -12dB            # game at −12 dB in the stream mix; the monitor mix is untouched
kmixdeck cell link discord monitor stream      # discord in the monitor follows discord in the stream
kmixdeck mix output stream alsa_output.usb-Focusrite_Scarlett-00.analog-stereo
kmixdeck mix output-add stream alsa_output.pci-0000_00_1f.3.analog-stereo:AUX3,AUX4
kmixdeck channel input voice alsa_input.usb-RODE_NT-USB-00.mono-fallback
kmixdeck app move firefox browser
kmixdeck fx set channel voice "$(kmixdeck fx presets | jq -c '.["Podcast voice"]')"
kmixdeck export > ~/kmixdeck-$(date +%F).json
kmixdeck --json status | jq '.mixes[] | {Slug, Volume, Outputs}'
```

## Scripting notes

- Every write returns immediately after the daemon has **accepted** it; the audio graph follows within a few ms. To wait for
  the effect, poll `--json status` or use `watch`.
- `--json` output is stable per property name; property names are those of the D-Bus interfaces in `interfaces/*.xml`.
- The CLI works without a running daemon only for `streamdeck …`; everything else starts the daemon via D-Bus activation
  if it is installed, or exits `2` if not.
