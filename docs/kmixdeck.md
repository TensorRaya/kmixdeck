# NAME


kmixdeck - per-application mixing desk for PipeWire

# SYNOPSIS


```
kmixdeck [--json] <command> [<sub-command>] [args…]
```

# DESCRIPTION


The CLI is a thin client of the `org.kmixdeck1` D-Bus service. Everything it can
do, the window, the tray, the web UI and the Stream Deck plugin do through the
same calls — the CLI is just the one you can script. Every command accepts
`--json` (`-j`) for machine-readable output.

A **channel** is a row: where applications and hardware inputs land. It has one
fader per mix (the *cells*), a trim, a pan, an optional group and an ordered FX
chain.

A **mix** is a column, an output bus: a master fader, a mute, one or more
hardware outputs, a fallback output, an FX chain, and it is exposed to other
software as the capture source `kmixdeck.source.<slug>` (OBS, Discord…).

A **cell** is one channel inside one mix — the single fader where a row meets a
column.

# EXAMPLES


Set one cell without touching the other mixes:

```sh
kmixdeck cell set game stream -12dB
```

Make the monitor follow the stream mix for one channel:

```sh
kmixdeck cell link discord monitor stream
```

Route a mix to hardware, then add a second output:

```sh
kmixdeck mix output stream alsa_output.usb-Focusrite_Scarlett-00.analog-stereo
kmixdeck mix output-add stream alsa_output.pci-0000_00_1f.3.analog-stereo:AUX3,AUX4
```

Feed a channel from a microphone:

```sh
kmixdeck channel input voice alsa_input.usb-RODE_NT-USB-00.mono-fallback
```

Move an application, apply an FX preset, back up, query with jq:

```sh
kmixdeck app move firefox browser
kmixdeck fx set channel voice "$(kmixdeck fx presets | jq -c '.["Podcast voice"]')"
kmixdeck export > ~/kmixdeck-$(date +%F).json
kmixdeck --json status | jq '.mixes[] | {Slug, Volume, Outputs}'
```

Scenes:

```bash
kmixdeck scene save "stream night"     # current state, named
kmixdeck scene recall "stream night"   # back to it, everything else to unity
kmixdeck scene recall talkback --add   # layer talkback on top of what is set now
```

# OPTIONS


`--json`, `-j`
: Machine-readable output. Property names are those of the D-Bus interfaces in
`interfaces/*.xml` and are stable.

`--help`
: The compact version of this page.

`--version`
: Version and nothing else.

Options go **before** the command: everything after it is taken literally, so
that `-12dB` is a level and not an option. `kmixdeck --json status` works,
`kmixdeck status --json` prints the table.

# COMMANDS


## Overview and state

`status`
: The matrix: every channel × every mix with level and mute, plus outputs,
inputs, apps. `--json` gives the whole object tree — this is what the tests and
the Stream Deck plugin read.
> kmixdeck status                   # the matrix as a table
> kmixdeck --json status | jq '.mixes[] | {Slug, Volume}'

`tree`
: The signal path as a tree: device → channel → cell → mix → output, with the
applications on each channel and the FX in each cell. `status` answers "how loud
is channel X in mix Y"; `tree` answers "where does this sound come from and where
does it go", which is the question when something is silent. Fits 80 columns —
names are shortened, never wrapped. Box-drawing characters only in a UTF-8
locale, otherwise plain ASCII; colour is dropped when `NO_COLOR` is set or stdout
is not a terminal. `--json` gives the same nesting as an object, so scripts never
have to parse the ASCII back.
> kmixdeck tree                     # the signal path, top to bottom
> NO_COLOR=1 kmixdeck tree          # no escape sequences, for logs and pipes
> kmixdeck --json tree | jq '.channels[] | select(.cells[].muted)'

`levels [--once]`
: Live peak meters at 25 Hz (`#` peak, `=` RMS, `!` clip). `--json` prints one
object per tick. Ctrl-C stops and unsubscribes. `--once` prints a single reading
and exits (scripts, tests).
> kmixdeck levels --once            # one reading, then exit
> kmixdeck --json levels --once | jq '.[] | select(.Clip)'

`loudness [--once]`
: EBU R128 per mix that has the meter on (UX-18): momentary, short-term and
integrated loudness in LUFS plus true peak in dBTP. `--json` prints
`{slug: [M, S, I, TP]}`.
> kmixdeck loudness --once          # M/S/I in LUFS, true peak in dBTP

`watch`
: Prints every property change on the bus as it happens. Useful to see what a UI
action actually did.
> kmixdeck watch                    # every bus change, until Ctrl-C

`undo`
: Restores the last removed channel or mix — including its cells, outputs and FX
(CH-9). One step.
> kmixdeck mix remove talkback && kmixdeck undo   # brings it back with cells and FX

`patch <file>|- [--dry-run]`
: Changes the configuration non-interactively with an RFC 7386 merge patch —
the same view the web bridge speaks (AR-8), so a patch that works there works
here: `{"objects": {"<path>": {"<Property>": value}}, "root": {…}}`. Paths and
property names come from `kmixdeck tree --json`. `-` reads from stdin.
`--dry-run` prints what would change and touches nothing.
All or nothing: every assignment is checked against the daemon's own properties
first (does it exist, is it writable, does the type fit), and only if *all* of
them pass is anything written — a typo in the last field must not leave the first
nine applied. A rejected patch names the path and property that failed.
This is not `import`: that one replaces the whole layout from an `export`
document, whose arrays RFC 7386 would overwrite wholesale. `patch` changes
individual properties and leaves everything it does not mention alone.
> kmixdeck patch quiet-night.json                 # apply it
> kmixdeck patch quiet-night.json --dry-run       # show what it would do
> echo '{"objects":{"/org/kmixdeck1/channel/voice":{"Muted":true}}}' | kmixdeck patch -

## First run, backup, restore

`setup`
: Shows the first-run plan: *Monitor* → default output, *Voice* ← default mic,
running apps → channels by media role (UX-3).
> kmixdeck setup                    # shows the plan, changes nothing

`setup --apply`
: Does it.
> kmixdeck setup --apply            # creates the default desk

`export [file]`
: Writes the layout **and** every fader/trim/mute as one JSON document (to
stdout without a file). This is the backup (CT-7).
> kmixdeck export > ~/kmixdeck-$(date +%F).json

`import <file>`
: Replaces the running layout with that document and applies the levels. Refuses
documents it cannot parse and keeps the current layout.
> kmixdeck import ~/kmixdeck-2026-09-21.json

## Channels

`channel list`
: Slug and name of every channel (`--json`: all properties).
> kmixdeck channel list             # slug and name of every channel

`channel add <name>`
: Creates the channel; prints its object path.

`channel remove <slug>`
: Removes it (undoable).

`channel rename <slug> <name>` · `channel icon <slug> <icon|none>` · `channel color <slug> <#rrggbb|none>`
: Presentation. Icons are freedesktop icon names (`audio-input-microphone`).

`channel move <slug> <index|up|down|top|bottom>`
: Row order in every UI (UX-9).

`channel trim <slug> <level>`
: Pre-fader gain of the channel itself (applies to every mix).

`channel mute <slug> [on|off]`
: Mutes the channel in every mix. Without `on/off`: toggles.

`channel pan <slug> [<-1..1>|L|C|R]`
: Stereo position (DV-22). Without a value: prints it.

`channel default [<slug>|none]`
: Where never-seen applications land (CH-5).

`channel group <slug> [<name>|none]` · `channel groups`
: Grouped channels move together: trim as one dB delta, mute mirrored (CH-8).

`channel input <slug> <ref|none>`
: The channel's hardware input, or none.

`channel inputs <slug>` · `channel input-add <slug> <ref>` · `channel input-remove <slug> <ref>`
: Several wires into one channel (ADR 0009).

`channel wire <slug> <ref> [trim <level>] [mute on|off]`
: Per-wire trim and mute of one input wire (DV-14).

## Mixes

`mix list` · `mix add <name>` · `mix remove <slug>` · `mix rename` · `mix icon` · `mix color` · `mix move`
: As for channels.
> kmixdeck mix add "Talkback"       # new mix, unity everywhere

`mix duplicate <slug> <new name>`
: Copy with every cell level, output and FX (MX-8).

`mix volume <slug> <level>` · `mix mute <slug> [on|off]`
: Master fader and mute.

`mix output <slug> <ref|none>`
: Sets **the** output (replaces all). `none` removes every output — the mix keeps
running, only its capture source is audible.

`mix outputs <slug>` · `mix output-add <slug> <ref>` · `mix output-remove <slug> <ref>`
: Several hardware outputs at once (MX-9), e.g. headphones + a USB interface.

`mix fallback <slug> <ref|none>`
: Played while every output is unplugged (DV-15).

`mix wire <slug> <ref> [trim <level>] [mute on|off]`
: Per-wire trim/mute of one output wire.

`mix loudness <slug> [on|off|<LUFS>]`
: Switch the R128 meter for a mix or set its target line; no argument reads both
(UX-18).

`mix get <slug>`
: Every property of the mix as JSON.

## Cells

`cell get <ch> <mix>`
: Level and mute of that one fader.
> kmixdeck cell get game stream
> kmixdeck cell set game stream -12dB   # one cell, other mixes untouched

`cell set <ch> <mix> <level>`
: The level of channel *ch* in mix *mix* only — the other mixes do not move.

`cell mute <ch> <mix> [on|off]`
: Mute that one fader.

`cell link <ch> <mix> <other-mix|none>`
: This cell follows the same channel's cell in *other-mix* (volume + mute).
Touching the follower breaks the link (MX-7).

## Applications

`app list`
: Running application streams: id, name, running state, channel(s).
> kmixdeck app list                 # id, name, running state, channels
> kmixdeck app move firefox browser

`app move <id|name> <channel>`
: Move the stream to a channel. Remembered for next time the app starts (CH-4).

`app assign <id|name> <ch>[,<ch>…]`
: Several channels at once; the first is the primary (CH-12).

## Devices

`devices`
: Hardware outputs a mix can play to (`node.name → description`).
> kmixdeck devices                  # node.name → description

`devices in`
: Hardware inputs a channel can be fed by.

`devices ports <node.name>`
: Every port of a device and who is wired to it (ADR 0009).

`devices hide <node.name>` · `devices unhide` · `devices hidden`
: Keep a device out of every picker; still routable by name (CH-11).

`devices virtual list` · `devices virtual add <name> [--in N] [--out N]` · `devices virtual remove <slug>`
: Virtual devices: a named block of ports other software can use as a sound card
(DV-23).
> kmixdeck devices virtual add "Loopback A" --in 2 --out 2

## Listening and auditioning

`listen [<node.name>|none]`
: The device *you* listen on and which mixes play there (UX-2).
> kmixdeck listen                   # what plays where
> kmixdeck listen alsa_output.usb-Focusrite_Scarlett-00.analog-stereo

`audition channel|mix <slug>`
: Solo that one entity on the main output — everything else muted;
`audition none` restores the previous state exactly (UX-12). This is the
hold-to-listen button of the UIs.
> kmixdeck audition channel voice   # solo; everything else muted
> kmixdeck audition none            # exactly back to before (UX-12)

## Effects

Ordered insert chains on any channel or mix (ADR 0008). Builtin effects need
nothing; LADSPA effects (noise suppression, gate, compressor via swh-plugins /
rnnoise) are listed only when the library is installed.

`fx types`
: The catalog: every effect type with its controls, ranges and defaults (JSON).
> kmixdeck fx types                 # every effect with ranges and defaults

`fx presets`
: One-click chains ("Podcast voice", …) as editable starting points (FX-4).

`fx get channel|mix <slug>`
: The current chain (JSON).

`fx set channel|mix <slug> '<json>'`
: Replace the chain. Validated: unknown type, out-of-range control or missing
plugin → refused with the reason.
> kmixdeck fx set channel voice "$(kmixdeck fx presets | jq -c '.["Podcast voice"]')"

`fx clear channel|mix <slug>`
: Remove it.

`fx copy channel|mix <from> channel|mix <to>`
: Copy a chain to another object.

`fx control channel|mix <slug> <node:Control> <value>`
: Live tweak of one control — no reload, no click.

## Scenes

A scene stores the *mix state* — every cell level and mute, plus per-mix level,
mute and FX bypass. It deliberately does **not** store the channel/mix set or the
wiring, so a scene stays applicable after you rename a channel or repatch a
device (CT-9). Recalling one is a single graph pass and is undoable like any
other change (CH-9).

`scene list`
: Every stored scene name, one per line (`--json` for an array).
> kmixdeck scene save "stream night"
> kmixdeck scene recall "stream night"

`scene save <name>`
: Store the current mix state under that name. Overwrites an existing scene of
the same name.

`scene recall <name>`
: Apply it. **Exclusive by default:** any mix the scene does not mention returns
to unity, so the same scene always sounds the same.

`scene recall <name> --add`
: Apply it but leave unmentioned mixes where they are — for layering a scene on
top of the current state.

`scene delete <name>`
: Remove it.

`save` and `delete` re-read the scene list afterwards and fail loudly if the
daemon did not do what it confirmed — cheap, and it is your proof the scene
really is stored.

## Stream Deck

`streamdeck install` · `streamdeck uninstall` · `streamdeck path`
: Hooks the OpenAction plugin into OpenDeck's plugin folder (CT-3). Works
without the daemon. See `streamdeck/README.md`.
> kmixdeck streamdeck install       # hooks the plugin into OpenDeck

# REFERENCES

## Levels
Levels are written one of three ways, everywhere a level is expected:

`0.25`
: linear 0…1

`-12dB`
: decibels

`63%`
: the UI's cubic fader position: `100%` = 0 dB, `63%` ≈ −12 dB, `50%` ≈ −18 dB,
`25%` ≈ −36 dB — the same curve as Plasma's volume slider.

## Slugs
Channels and mixes are addressed by their *slug*: the name lower-cased,
ASCII-folded, runs of non-letters → `_` (`"Game Audio"` → `game_audio`,
`"Ünïcode Name!"` → `unicode_name`). `kmixdeck channel list` shows slug and name
side by side.

## Port references
`<ref>` names hardware ports. `node.name` alone means "the whole device".
`node.name:AUX3` means one port, `node.name:AUX3,AUX4` a stereo pair,
`node.name:AUX3>L` binds a port to the left side only (DV-21).
`kmixdeck devices ports <node.name>` lists what a device offers and who uses it.

# EXIT STATUS


`0`
: ok

`1`
: usage

`2`
: service not reachable

`3`
: not found

`4`
: the daemon refused (the reason is on stderr)

# FILES


`~/.config/kmixdeckrc`
: Layout and levels, written by the daemon.

`interfaces/*.xml`
: The D-Bus interface definitions the `--json` property names come from.

# SEE ALSO


`pw-cli`(1), `pw-link`(1), `wpctl`(1), `pipewire`(1)

Project documentation: `docs/` in the source tree — `docs/spec/requirements.md`
for the requirement IDs referenced above, `docs/adr/` for the design decisions.

# NOTES

- Every write returns immediately after the daemon has **accepted** it; the audio
  graph follows within a few ms. To wait for the effect, poll `--json status` or
  use `watch`.
- `--json` output is stable per property name; property names are those of the
  D-Bus interfaces in `interfaces/*.xml`.
- The CLI works without a running daemon only for `streamdeck …`; everything else
  starts the daemon via D-Bus activation if it is installed, or exits `2` if not.

