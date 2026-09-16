# ADR 0009 — Port-based virtual devices (DV-13, DV-17…20)

**Status:** accepted 2026-09-16 · **Owner:** Michel · **Drives:** DV-13, DV-17, DV-18, DV-19, DV-20

## Context

A RØDECaster Pro II is one USB node with 8+ capture and 8+ playback ports (Mic 1–4, Bluetooth, USB 1/2 …); a
Soundcraft Ui24R is one node with 32 in / 32 out. kmixdeck treated a device as *one* thing: `channel input voice
<node>` took the whole node, `mix output monitor <node>` the same. That is fine for a headset and wrong for a mixer:
Michel wants "CH 18+19 as a stereo channel *Music*", "CH 21 mono as *Talkback*", "Headphones 2 as the *Stream*
output" — several virtual devices carved from one piece of hardware, each behaving like any other channel/output.

## Decision

**D1 — one reference syntax everywhere.** A device reference is `node[:PORT[,PORT]]`:

| Reference | Meaning |
|---|---|
| `alsa_input.ui24r` | the whole node, its natural layout (unchanged behaviour) |
| `alsa_input.ui24r:AUX18,AUX19` | stereo from two named ports (L = first, R = second, order is the user's) |
| `alsa_input.ui24r:AUX21` | mono from one port |
| `alsa_output.rodecaster:AUX2,AUX3` | a mix plays into exactly these two playback ports |

`PORT` is the port's `audio.channel` (what `audio.position` lists: `FL`, `AUX2`, …). It is what PipeWire itself
uses to link a stream's `audio.position` to a device's ports, so no name table of our own is needed. The same
string is the D-Bus value (`Channel.InputDevice`, `Mix.Outputs[]`), the CLI argument, and the key in `layout.json`
(`DeviceRef{node, positions}` serialises to exactly this). One truth (CT-5 spirit).

**D2 — implementation is `audio.position` on the loopback's device-side stream.** PipeWire's loopback module links
a stream port to the device port with the *same* `audio.channel`. Setting `capture.props.audio.position = [AUX18
AUX19]` on the input edge, or `playback.props.audio.position = [AUX2 AUX3]` on an output edge, therefore selects
the port subset — nothing else in the graph changes, no extra node, no manual links to maintain. Unselected ports
of the device are untouched (DV-18). This is what `DeviceRef::positions` was reserved for (ADR 0007 D4); it was
never plumbed through the API.

**D3 — mono ↔ stereo is explicit, never silent.** A mono reference (one port) feeds a stereo channel as *dual mono*
(`capture.props.audio.position = [MONO]`, the channel-side stream stays `[FL FR]`; the loopback's channelmix
upmixes). A stereo mix into a mono port downmixes (`playback.props.audio.position = [MONO]`). Both cases are shown
in the picker as "mono → both sides" / "stereo → mono sum" before the user confirms (DV-19). A reference that
names ports the device does not have is rejected on the bus (`InvalidArgs`), not parked.

**D4 — the daemon publishes ports, frontends never read PipeWire.** New read-only property
`Mixer.DevicePorts` → `a{sas}`: node → ordered list of `POSITION|name|alias` triples for every non-monitor port.
The picker (CH-13) and the routing view (UX-15) render "device → ports" from this list and mark a port
"in use by <channel/mix>" by scanning the layout (DV-20). `kmixdeck devices ports <node>` prints the same.

**D5 — no port-level trims yet.** Per-edge trim/mute (DV-14) stays a separate step; a port-based virtual device
is a plain channel/output and gets DV-14 for free when that lands.

## Consequences

- Layout format unchanged (`positions` already existed) — old layouts load as before.
- The cell matrix, meters, FX, listen, routing view all work on port-based channels without knowing about ports.
- Presence (DV-9) is per *node*: unplugging the Ui24R greys out every channel carved from it.
- Not covered: one *channel* spanning two *devices* (impossible in one loopback; also not asked for).

## Verification

`test_dv17_*` (two mono channels from one 4-port fake device hear only their own port; a stereo pair L/R keeps
sides), `test_dv18_*` (a mix into ports 3–4 leaves ports 1–2 silent), `test_dv19_*` (mono → stereo channel reaches
both sides; bad port ref rejected), `test_dv20_*` (`DevicePorts` lists ports; CLI prints "in use by").

## Amendment A (2026-09-16) — sides and pan (DV-21, DV-22, DV-23)

Michel, after seeing the first cut: *"I'd expect to address the single channels and link them — not just one
beam, but connect left and right individually; in doubt put L and R on two different mixes. And a mono mic must
be able to become stereo again."* And the concrete case: Ui24R over USB = 32 mono channels, one XLR = one channel,
so a microphone is exactly ONE port to drag.

**A1. A mono port needs no "mono device".** One port → channel = centre (both sides, equal). That is the mixing-desk
default (mono strip, pan centre) and what a mic wants 99 % of the time. Measured in `test_dv19_…`.

**A2. Sides are part of the reference, not of the device.** The ref grows an optional side selector per port:

```
node:POS            mono port → both sides          (unchanged)
node:POS,POS        two ports → L, R                (unchanged)
node:POS>L          port only into the LEFT side of the channel   (right stays silent / free for another ref)
node:POS>R          port only into the RIGHT side
```
**Not possible — measured, not assumed:** `node:POS,POS>L` (two ports summed into one side). PipeWire 1.6's
loopback does not fold two named input positions into one output position — every variant tried on 2026-09-16
(`[AUX3 AUX4]→[FL]`, with `channelmix.upmix`, with `stream.dont-remix`, `[MONO]` capture, `[FL FR]` capture) was
silent while `[AUX3 AUX4]→[FL FR]`, `[AUX4]→[MONO]` and `[AUX4]→[FL]` all work. The daemon refuses that ref with a
message instead of building a silent edge. If someone needs L+R→one side, that is a filter-chain mixer node (ADR 0008
territory), not a loopback.
Mix outputs use the same grammar the other way round: `node:POS>L` = only the mix's LEFT side goes into that port.
`node:POS` (one output port, no side) = the whole mix folded into that port — measured working (`[FL FR]→[AUX5]`
−24 dB, same as source), so the asymmetry is real: PipeWire folds *stereo stream → one device port*, but not
*two device ports → one stream side*. DV-22 holds on the output side as written.

**A3. One channel, two sources (cross-linking).** `Channel.InputDevice` stays one string for the common case; a
channel MAY have a second input edge with the other side: `Channel.InputDevices` (as) = e.g.
`["ui24r:AUX18>L", "rode:Mic 2>R"]`. Each ref renders as its own loopback whose playback side declares only that
channel position (`audio.position = [ FL ]`), so the two edges do not fight. Mono-only edges are the natural fit for
"L and R of one source on two different mixes": make two channels, `src:AUX1>L`-style refs, route each to its mix.
Yes, it makes little sense musically — Michel said so himself — but it costs nothing because it falls out of A2.

**A4. Pan is a channel property, not routing.** `Channel.Pan` (d, -1..1, default 0) sets the channel-sink's
channelVolumes (constant-power law). For a mono source it is a position, for stereo a balance. The picker text
says what happens: "Mono → centre", "Mono → 30 % left". Pan does not touch the edges; it is applied on the channel
node's monitor volume — cheap, live, undoable.

**A5. Routing view (UX-15) gets L/R handles.** Every source, channel and output box shows two small pads (L, R)
at its edge; dragging from a source pad to a channel pad creates the side-ref. Dragging box-to-box keeps the
current "whole" behaviour. Edges from a side-ref are drawn to the pad, not to the box centre.

**A6. Virtual device (DV-23) is daemon-owned.** `Mixer.AddVirtualDevice(name, in, out)` creates a persistent
null-sink pair (`kmixdeck.virt.<slug>` Audio/Source/Virtual with `in` ports, `…out` Audio/Sink with `out` ports),
listed as a normal input and output device and rebuilt on every start from the layout. Tests and render-ui use it
instead of hand-made pw-cli nodes.

**Not in scope:** arbitrary N×M matrices per channel (that is a patchbay, not a channel strip); per-side FX.

## Amendment B (2026-09-16) — Patchbay, Loopback-style (DV-24…27), Web UI (AR-8/9)

Michel: *"I meant more the variant: a hardware device like the Ui24R that has x connectors you can wire — like
Loopback on the Mac."* Reference studied: rogueamoeba.com/loopback (tour screenshot). What we take from it:

- **Cards, not boxes.** A source is a card with a header (title, on/off) and one row per connector: label, live
  level bar, jack on the right edge. A destination card has jacks on the left edge. Channels have both.
- **Wires jack→jack**, S-curves, accent colour, no arrowheads. Drag to create, click to delete.
- **Three columns** Sources → Output Channels → Monitors; the Monitors column is hidden behind "Show Monitors".
  Ours: Sources → Channels → Outputs (+ Monitors optional), because our channel IS the virtual device.
- **A device with many connectors is one card** (Ui24R = 32 rows, collapsible to the wired ones).

What we do NOT copy: Loopback's per-virtual-device sidebar (our devices are channels/mixes already listed on the
Mixer page); Pass-Thru (a channel with no FX is that).

**B1. Data model stays: wires are refs.** A wire is `node:POS>L|R` (A2). New: a channel may have SEVERAL input
refs (`Channel.Inputs`, as) — one loopback edge each, playback side `[FL]` or `[FR]`; the existing
`Channel.InputDevice` remains the "first/primary" view for simple frontends. Mix outputs already are a list.
Two wires into the same side of the same channel are allowed (they sum in the channel's null-sink — that is what a
sink does), so "USB Mic → L" and "Ui24R 3 → L" both work; the two-ports-into-one-side limit from A2 is an edge-level
limit and does not apply to two separate edges.

**B2. One view model for two frontends (AR-9).** The KDE patchbay and the web patchbay render the same JSON:
`{cards:[{id, kind, title, on, rows:[{pos, label, level, jackIn, jackOut, usedBy}]}], wires:[{from:{card,pos},
to:{card,pos}, ref}]}` — produced client-side from the D-Bus tree today, by `kmixdeck-web` for the browser.

**B3. Web UI (AR-8)** is a bridge, not a second daemon: `kmixdeck-web` (Python, aiohttp + dbus-next) subscribes to
the object manager, pushes state + 25 Hz meters over one WebSocket, forwards actions to D-Bus. Static
HTML/JS, no build step, no framework lock-in. Reason: aether runs headless with the Ui24R on USB; the stream is
mixed from wherever we sit.
