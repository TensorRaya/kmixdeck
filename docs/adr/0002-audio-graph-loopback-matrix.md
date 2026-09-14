# ADR 0002 — Audio graph: where the (channel × mix) gain lives

- Status: **accepted** (2026-09-14, validated with `prototype/` — measurements below)
- Date: 2026-09-14
- Depends on: `docs/research/pipewire-kde-technical-notes.md` §1–2

## Context

The one requirement no existing Linux tool satisfies is **MX-2**: the same
channel at *different* levels in *different* mixes, without touching the
application's own volume. Sonusmix [#72](https://codeberg.org/sonusmix/sonusmix/issues/72)
describes exactly this and never solved it, because in its model a "group"
has one volume.

PipeWire offers four building blocks (see research notes for sources):

| Block | What it is | Gain where? |
|---|---|---|
| `module-loopback` | capture stream → playback stream pair | each stream has its own volume (`capture.volumes` / `playback.volumes`), restorable by WirePlumber |
| `module-filter-chain` | DSP graph (builtin `mixer`, `ladspa`, `lv2`) between a capture and a playback stream | builtin `mixer` node has `Gain N` controls per input; plugins add latency |
| `module-combine-stream` | one stream fanned out to N sinks (or N sources combined) | per-stream volume only; no per-input gain per output |
| custom SPA node | our own in-process node | anything, but we are then *in* the audio path (violates DV-1) |

## Decision (proposed)

**A channel is a null sink. A mix is a null sink. Each (channel, mix) cell is
one `module-loopback` from the channel's monitor port into the mix, and the
cell's fader is that loopback's playback-stream volume.**

```
app ──▶ [Channel: Game] (Audio/Sink, null)
            │ monitor
            ├─ loopback(vol = Game@Monitor) ──▶ [Mix: Monitor] ──▶ headphones (loopback or direct)
            └─ loopback(vol = Game@Stream)  ──▶ [Mix: Stream]  ──▶ virtual source for OBS
```

- Effects (FX-1) live *between* app and channel sink: the channel is then a
  `filter-chain` virtual sink whose output feeds the loopbacks. Channels
  without effects stay plain null sinks (no DSP cost).
- Mix output to hardware is one more loopback (mix monitor → device), so a
  mix can go to several devices (MX-9) by adding loopbacks.
- The mix's virtual capture device for OBS (MX-3b) is the mix sink's monitor
  port, or an explicit `Audio/Source` loopback if OBS needs a "real" source.

## Why this and not the alternatives

- **combine-stream** fans one input to many outputs but cannot give each
  output its own gain per input — it is the wrong shape for a matrix.
- **filter-chain `mixer`** puts all gains in one DSP graph per mix. It works,
  but every mix becomes a DSP node that must be rebuilt when a channel is
  added, and gains live in filter controls that WirePlumber does *not*
  persist. Loopback volumes it does persist (`state.restore-props`).
- **custom SPA node** breaks DV-1 (app not in the audio path).
- Loopbacks are the primitive the PipeWire docs themselves suggest for
  routing ("classic VoiceMeeter-style"), add ~one quantum of latency per hop
  and no DSP. n channels × m mixes = n·m loopbacks; for 6 × 4 that is 24
  lightweight streams — needs measurement (see below), but this is what
  `pipewire-pulse` does for every app anyway.

## Validation (2026-09-14, PipeWire 1.6.2 / WirePlumber 0.5.13, headless VM, and PipeWire 1.6.8 on the reference laptop)

Prototype: `prototype/kmixdeck-prototype.conf` (3 channels × 2 mixes, pure config, no app).
Method: 1 kHz tone into channel *Game* (`pw-play`), RMS measured at the monitor ports of both
mixes (`pw-record` wired by hand with `pw-link` — **note**: `pw-record --target <sink>` picks an
arbitrary port and gave false "no difference" readings first; always wire explicitly).

| Test | Game→Monitor | Game→Stream | Δ stream−monitor | Expected |
|---|---|---|---|---|
| A | 1.0 | 1.0 | **−0.0 dB** | 0 |
| B | 1.0 | 0.25 (linear) | **−12.0 dB** | −12.0 |
| C | 0.5 | 1.0 | **+6.0 dB** | +6.0 |
| D | 1.0 | muted | **−inf** | −inf |
| E | — | System→Stream while Game→Stream muted | unaffected | unaffected |

**MX-2 holds**: a cell's `channelVolumes` on the loopback *playback* stream is the fader, and
it only affects that (channel, mix) pair.

**Persistence**: WirePlumber's stream state (`~/.local/state/wireplumber/stream-properties`)
keys `Output/Audio` streams by **`media.name`** (fallback: description). With
`media.name = node.name` set on every loopback stream, cell volume **and mute** survive
`systemctl --user restart pipewire wireplumber` and reboots without the app running.
Without `media.name` the key was the human-readable description — renaming a channel would
have lost its levels. This is now in the prototype config and is a requirement (DV-7).

**Cost**: all kmixdeck nodes run in the driver's cycle (quantum 1024/48 kHz), per-node DSP
time 1–3 µs; `pipewire` process ≈ 1 % of one core with a tone through 6 cells (VM, no
hardware device). Loopback adds one graph cycle per hop: channel→mix→device = 2 cycles ≈ 43 ms
at 1024/48k, less with a smaller quantum.

**Volume location findings** (for the implementation, measured): `channelVolumes` on the
loopback playback stream → works. `softVolumes`, `volume` on the same stream → no effect.
`channelVolumes` on the loopback *capture* stream → no effect. `channelVolumes` or
`monitorVolumes` on the channel null-sink → affects **all** mixes (that is the channel's
trim, CH-7, not a cell fader).

## Prototype checklist (kept for reference; all items now answered above)

1. Latency channel→mix→device ≤ 2 quanta at 48 kHz/1024 (~43 ms) — measure
   with `pw-top` and a loopback test; if too high, lower quantum for our
   streams via `node.latency`.
2. CPU for 24 loopbacks idle and under load on the reference laptop — target
   < 2 % of one core.
3. Volumes survive `systemctl --user restart pipewire wireplumber` and a
   reboot without our app running (WirePlumber `state.restore-props`).
4. Plasma volume applet shows the channel sinks and can move apps between them;
   our app observes the same `target.object` metadata (CT-5).
5. The loopback graph can be expressed *entirely* as a PipeWire config
   fragment (`~/.config/pipewire/pipewire.conf.d/kmixdeck.conf`), so DV-1 and
   DV-5 hold by construction: the app writes config and talks to the running
   graph; it never carries audio.

If (1) or (2) fail, fall back to one `filter-chain` per mix with a builtin
`mixer` node and persist gains ourselves. That decision would supersede this
ADR.

## Consequences

- The app's job is graph management + UI, not audio processing.
- "Delete channel" = remove one null sink and its loopbacks; apps on it get
  moved to the default channel first (CH-5).
- Naming convention for nodes (DV-7): `kmixdeck.channel.<slug>`,
  `kmixdeck.mix.<slug>`, `kmixdeck.link.<channel>.<mix>` — stable across
  updates, never derived from display names.
