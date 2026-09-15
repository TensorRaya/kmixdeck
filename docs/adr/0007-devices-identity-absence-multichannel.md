# ADR 0007 — Devices: identity, direction, absence, multi-channel

- Status: accepted
- Date: 2026-09-15
- Adds requirements DV-9…DV-15, refines CH-3, MX-9, DV-3

## Context

Until now kmixdeck knew two kinds of things at the edges of its graph: *app streams* (routed into a channel)
and *hardware sinks* (a mix plays to one). That is not the whole picture of a streamer's desk:

- A **microphone or capture card** is a *source* that must feed a channel — it is not an app, it does not
  have an app name, and it disappears when unplugged.
- A **headset** is a *sink* that a mix plays to; it, too, disappears and comes back. Wave Link's users'
  loudest complaints (research L7, #44) are about exactly this: settings lost, devices renumbered.
- Some devices carry **more than a stereo pair**: a RØDECaster or a Ui24R exposes many channels on one
  USB PCM; the user wants "mic on channel 1–2, music return on 3–4", not "the RØDECaster".

Facts measured on the reference laptop (PipeWire 1.6.8, WirePlumber 0.5) and in this VM:

1. **Stable identity.** For an ALSA card PipeWire gives `device.name = alsa_card.<bus-id>` and per node
   `node.name = alsa_output.<bus-id>.<profile>__<port>__sink`. The `<bus-id>` (`usb-RØDE_RØDECaster_Pro_II-00`)
   is built from vendor/product/serial and **does not change across replug or reboot**; it does not
   contain the USB port path (`device.bus-path` does — we ignore that one). Bluetooth: `bluez_output.<MAC>.1`.
   Node **ids and serials are volatile** (Sonusmix #38 again). WirePlumber itself keys its state files by
   `node.name`. So do we.
2. **Node.name is the same key WirePlumber uses for `target.object`**, so a persisted device reference is
   directly usable as a loopback target the moment the node reappears. No lookup table.
3. **Multi-channel devices.** In the default *HiFi* profile PipeWire already splits a card into one stereo
   node per UCM port (`HiFi: Speaker`, `HiFi: Mic`, …). In the *Pro Audio* profile a card becomes **one node
   per PCM with all channels** (`AUX0…AUXn`). PipeWire ≥ 1.4 also has `api.alsa.split-enable` (UCM
   SplitPCM), which emits one node per channel subset with `api.alsa.split.position` — but only for cards
   whose UCM config declares splits. The RØDECaster in *HiFi* is a plain 2-in/2-out here (its multitrack
   mode is a firmware setting on the device); a Ui24R in Pro Audio is one 32-channel node.
4. **A loopback with a missing target dies** (ADR 0002 trap 4) — *unless* the stream has `node.linger = true`,
   in which case WirePlumber keeps it and links it as soon as the target shows up. That is the intended
   PipeWire mechanism for "wait for my device" and what device edges use.

## Decisions

### D1 — Device reference = `node.name`, plus a remembered face

A device reference stored in `layout.json` is `{ "node": "<node.name>", "description": "<last seen
node.description>", "channels": <n>, "seenAt": "<iso>" }`. `node` is the key. `description`/`channels` are
cached so the UI can show the device **while it is absent**. Nothing else (ids, serials, bus paths) is stored.

### D2 — Three edge kinds, each a first-class object on the bus

| Kind | PipeWire class | kmixdeck object | Attaches to | How |
|---|---|---|---|---|
| **Input device** (mic, capture card, line-in, BT headset mic) | `Audio/Source` | `org.kmixdeck1.Input` at `/org/kmixdeck1/input/<slug>` | a **channel** | loopback `kmixdeck.in.<slug>`: capture from the device, play into `kmixdeck.channel.<c>` |
| **Output device** (headphones, speakers, HDMI, BT) | `Audio/Sink` | `Mix.Outputs` (list) | a **mix** | one loopback `kmixdeck.out.<mix>.<n>` per output (MX-9: several at once) |
| **App stream** | `Stream/Output/Audio` | `org.kmixdeck1.App` (exists) | a **channel** | `target.object` metadata (ADR 0002) |

A *virtual* device is what kmixdeck itself creates: channels are virtual **sinks** (apps play into them),
`kmixdeck.source.<mix>` is a virtual **source** (OBS/Discord capture it). The rule of thumb for the UI: *things
that produce sound* (mics, apps) go into channels; *things that hear* (headphones, OBS) hang off mixes.

### D3 — Absent devices stay configured, greyed, and reattach by themselves

- An Input or Output whose `node` is not present is **kept** in the layout and shown with its cached
  description, greyed, tagged `Present = false`. Nothing is deleted on unplug — ever.
- While absent, the loopback simply **waits**: its device-side stream carries `node.linger = true` together
  with `node.dont-fallback = true`. Measured (WirePlumber `linking/find-defined-target.lua`, lines 116–126):
  with *linger* WirePlumber logs "waiting for defined target" instead of destroying the node, and when a node
  with that `node.name` appears it links it **by itself** — no metadata call, no daemon involvement (DV-3,
  DV-6). Verified for a capture side (input) and a playback side (output): absent → alive/unlinked, appears →
  linked within ~1 s, vanishes → alive/unlinked, returns → linked again; metadata retarget still works.
  This supersedes the `kmixdeck.null` parking of ADR 0002 trap 4 for device edges: the parking sink stays
  only for "this mix has *no* output configured" (an empty target, not an absent one).
- Fallback for outputs (DV-3): a mix MAY name a `FallbackOutput`; while the primary is absent the mix plays
  there; when the primary returns, it moves back. Default: no fallback = silence, never "the default sink"
  (that is how feedback loops start, Trap 3).

### D4 — Multi-channel devices: pick channels, not just devices

An Input or Output reference MAY carry `"channels": [i, j]` = which channels of the device node to use (0-based,
mono allowed). Implemented with the loopback's channel map on the device side (`capture.props.audio.position`
/ `playback.props.audio.position` set to the chosen port names, e.g. `[AUX2 AUX3]`) and the standard
`[FL FR]` on the kmixdeck side — PipeWire's channel mixer does the mapping, no code of ours in the audio path.
The device list (`Mixer.InputDevices` / `OutputDevices`) reports `channels` and `positions` per node so a
frontend can offer "channels 3–4 of Ui24R". Devices in *Pro Audio* profile appear as one big node — that is
where this matters. Switching a card's profile is **not** kmixdeck's job (Plasma's audio settings do it); we
document it in the user guide.

### D5 — Volume and mute on device edges

Input trim/mute live on the `kmixdeck.in.<slug>` loopback (playback side) like a cell; output level per
mix-output on `kmixdeck.out.<mix>.<n>`. The *device's own* hardware volume is left alone — that is Plasma's
applet (CT-5: one truth per knob; ours are the kmixdeck knobs, theirs are the hardware knobs).

## Consequences

- New interfaces `org.kmixdeck1.Input`; `Mix.OutputDevice` (string) becomes `Mix.Outputs` (`as`) —
  API minor bump within `org.kmixdeck1` (additive: `OutputDevice` stays as "first of Outputs" for one release
  with a deprecation note in the XML).
- Layout gains `inputs[]`; mixes gain `outputs[]` and optional `fallbackOutput`.
- Tests: input device → channel → mix → source audible; unplug (destroy fake node) → object stays, `Present`
  false, loopback parked; replug → relinked within a second with no call from a client; channel subset of a
  fake 8-channel device → only those channels arrive.
- Rejected: matching by `device.serial` (missing on many cards, empty on BT), by `bus-path` (changes with
  the port), by node id (volatile), and deleting absent devices (the Wave Link complaint).
