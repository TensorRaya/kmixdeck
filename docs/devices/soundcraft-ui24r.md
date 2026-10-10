# Soundcraft Ui24R over USB — as seen from Linux

Measured on the studio capture host, 2026-09-16, from a privileged one-shot pod with `/dev` mounted (the host has no
ALSA userland, no PipeWire; OBS runs containerised). Nothing was installed on the host.

## Identity

| | |
|---|---|
| USB | `05fc:0010` "Harman Soundcraft Si MADI combo card" (lsusb name; ALSA calls it `Soundcraft Ui24`) |
| ALSA card | `Ui24`, one PCM `hw:N,0` with one capture and one playback stream |
| Speed | USB 2.0 high speed, async, implicit feedback |

## Streams (from `/proc/asound/cardN/stream0`)

| Direction | Channels | Format | Rate | Notes |
|---|---|---|---|---|
| Capture (Ui24R → host) | **32** | S32_LE (24 valid bits) | 48 kHz only | one interleaved stream |
| Playback (host → Ui24R) | **32** | S32_LE / S16_LE | 48 kHz only | one interleaved stream |

Consequences for kmixdeck (ADR 0009):

- PipeWire will expose **one source node with 32 ports** (`capture_AUX0…AUX31`) and **one sink node with 32
  ports** (`playback_AUX0…AUX31`) — exactly the shape DV-13 assumes. The virtual device (`devices virtual add
  --in 32 --out 32`) is a faithful stand-in.
- 48 kHz is the only rate: the graph must run at 48 kHz when the Ui24R is present (PipeWire does that per
  default; do not force 44.1 in `default.clock.rate`).
- No per-channel names from the device: connectors are numbered 1–32. Names ("Mic", "Music L") live in
  our layout, not in the device (DV-26 shows the number and our label).

## What the 32 capture channels carry

Set on the Ui24R itself (Settings → USB → routing). Default firmware layout is inputs 1–24 = the 24 analogue
inputs post-preamp, 25–32 configurable (Aux/Sub/Master). The recorded RMS below was taken with the desk idle,
the floor per channel is what the AD converters deliver with nothing connected / muted:

```
in 01   -91.1 dBFS
in 02   -91.1 dBFS
in 03   -90.0 dBFS
in 04   -90.0 dBFS
in 05  -120.5 dBFS
in 06  -120.7 dBFS
in 07  -999.0 dBFS
in 08  -999.0 dBFS
in 09  -999.0 dBFS
in 10  -999.0 dBFS
in 11   -82.8 dBFS
in 12  -111.2 dBFS
in 13  -111.3 dBFS
in 14  -111.0 dBFS
in 15  -101.5 dBFS
in 16  -102.0 dBFS
in 17  -110.9 dBFS
in 18  -111.2 dBFS
in 19  -109.9 dBFS
in 20  -109.7 dBFS
in 21  -101.6 dBFS
in 22  -101.6 dBFS
in 23  -102.4 dBFS
in 24  -102.4 dBFS
in 25  -102.2 dBFS
in 26  -102.4 dBFS
in 27  -102.3 dBFS
in 28  -102.4 dBFS
in 29  -111.2 dBFS
in 30  -111.1 dBFS
in 31  -106.5 dBFS
in 32  -106.5 dBFS
```

Channels 7–10 are digital silence (exactly 0) — those USB slots were unassigned in the desk's USB routing at
measurement time. Every other slot carries converter noise (−80…−120 dBFS), i.e. it IS routed.

## Mixer controls (ALSA)

Only the standard USB Audio Class controls: `Mic Capture Switch/Volume` (×2), `Playback Switch/Volume` (×2),
`Internal Clock Validity`. No per-channel gain over USB — gain is on the desk.

## Open

- Which USB slot maps to which physical input on OUR desk (the routing is configurable on the Ui24R) — read
  from the desk's web UI, then pin the names in the layout.

## Playback side: one deployment (aether, measured 2026-10-08 / 2026-10-10)

Host → Ui24R playback port `AUX<n>` arrives on the desk as source `ub.<n>`, shown as **DAW-<n+1>** in the
input source picker. The desk input chooses it; nothing on the desk's USB routing page is involved.

| kmixdeck | playback ports | desk source | desk input |
|---|---|---|---|
| mix `speaker_bot` (channel `tts_voice`) | `AUX2,AUX3` | `ub.2/3` (DAW-3/4) | 21/22 `SPK BOT` |
| mix `media_out` (channel `media`) | `AUX4,AUX5` | `ub.4/5` (DAW-5/6) | 23/24 `MEDIA` |

`AUX0,AUX1` (DAW-1/2) stay unused there: the raw Ui24 sink is the host's default sink, so any stream without a
target lands on those two ports.

Measured on the desk's own input meter (VU2), 1 kHz sine at −20 dBFS into the channel, cell at −18 dB:
**−38.3 dBFS** on both inputs, desk idle at −80 dBFS. The −0.3 dB is the desk meter's 1/3 dB step.

Ducking (FX-9) on the same deployment: `media` ducked by `tts_voice`, depth −18 dB, threshold −40 dBFS,
attack 10 ms, release 300 ms. Media at −44.3 dBFS dropped to **−62.3 dBFS** (−18.0 dB) within one desk meter
frame (~0.1 s) of a real TTS clip starting. It comes back about 1.6 s after the trigger stops, not after the
300 ms release: the ducker reads the published peaks, which carry the UX-16 ballistics (hold 320 ms, then
fall 20 dB/s). Prediction for a −20 dBFS trigger: 0.32 s + 20 dB / (20 dB/s) = 1.32 s until the peak crosses
−40 dBFS, plus the 300 ms release = 1.62 s. Measured with a −20 dBFS tone into `tts_voice`: media starts
rising 1.29 s and is back at full level 1.56 s after the tone ended (desk meter, ~0.1 s frames).
