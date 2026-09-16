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
