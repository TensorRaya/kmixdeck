# ADR 0008 — Effects: one filter-chain per channel, in front of the channel sink

*Status: accepted · 2026-09-15*

## Context

FX-1…FX-7 ask for an ordered insert chain per channel (noise suppression, gate,
compressor, EQ, high-pass, limiter), bypassable, copyable, also on mixes. ADR 0002
already reserved the slot: *effects live between app and channel sink*.

Measured before designing (`/tmp/kmix-fx-proto.py`, private PipeWire, 1 kHz tone
at −24 dBFS into the chain, RMS at the channel sink):

| chain | result |
|---|---|
| gate −70 / ratio 1 / limit 0 (bypass) | −24.18 dB (ref −24.23) |
| gate threshold −5 dB | −∞ (closed) |
| sc4m ratio 20 @ −30 dB | −29.91 dB (≈6 dB reduction, as the maths says) |
| makeup +20 then limiter −20 | −23.16 dB (peaks held at −20) |

So `libpipewire-module-filter-chain` with the **swh LADSPA** set does what the labels
say, at zero added configuration. Two more findings that shape the design:

1. `pw-cli load-module` loads into *pw-cli's* process — the chain dies with it. The
   module has to live in a process that stays: **our daemon** (`pw_context_load_module`,
   like the loopbacks) and, for logins without the daemon, the **conf fragment**.
2. Control changes at runtime are not a module reload: filter-chain exposes every
   `control` as a **`Props` param on the capture node** (`params` array of
   `name value` pairs) — one `set_param`, glitch-free, no re-link.

## Decision

**D1 — Topology.** A channel *with* effects becomes
`kmixdeck.fx.<ch>.in` (Audio/Sink, the new entry point) → filter-chain →
`kmixdeck.fx.<ch>.out` → `kmixdeck.channel.<ch>` (unchanged null sink, still the
thing the cells and meters hang on). Apps and the hardware input keep targeting the
*channel*; when a chain exists the daemon retargets them to `fx.<ch>.in`.
Channels without effects stay plain null sinks — no DSP cost, no change (ADR 0002).

**D2 — Built-in set = swh + builtin, by label.** Only what is in `swh-plugins`
(Debian/Ubuntu/Arch/Fedora all ship it) plus PipeWire's own `builtin` biquads:

| kmixdeck type | node | why |
|---|---|---|
| `highpass` | builtin `bq_highpass` | free, zero latency |
| `gate` | ladspa `gate_1410:gate` | threshold/attack/hold/decay/range |
| `compressor` | ladspa `sc4m_1916:sc4m` | mono sidechain comp, the classic |
| `eq` | 3 × builtin `bq_lowshelf/bq_peaking/bq_highshelf` | UX: three knobs, not 31 |
| `limiter` | ladspa `fast_lookahead_limiter_1913:fastLookaheadLimiter` | brick wall |
| `noise` | ladspa `librnnoise_ladspa:noise_suppressor_mono` | **optional**: only offered when the .so is found (`LADSPA_PATH`, `/usr/lib/ladspa`); FX-1 asks for it, most distros do not package it |
| `ladspa` | any `plugin:label` + free `control` map | FX-2 "user MAY add any installed plugin" |

Each effect carries `type`, `enabled` (FX-5 per-effect bypass → `mixer`-free trick:
a bypassed node is simply **left out of the rendered graph**; re-render + reload is
~50 ms with no audio dropout because the *sink* `fx.<ch>.in` survives and buffers),
and a `params` map in the effect's own units (dB, ms, Hz).

**D3 — Chain bypass (FX-5 per chain)** = `Channel.FxEnabled=false`: apps are
retargeted straight to the channel sink again, the chain keeps existing (its state
is not lost). Same mechanism as unplug/park: retarget, never destroy.

**D4 — Mixes (FX-6).** Identical structure on `kmixdeck.mix.<mix>`: `fx.mix.<mix>.in`
→ chain → mix sink. The cells then target `fx.mix.<mix>.in`. Same code path, one
flag per owner (`channel` | `mix`).

**D5 — Persistence.** `layout.json` → `channels[].fx = { enabled, chain: [...] }`;
the conf fragment renders the same filter-chain so the graph exists at login before
the daemon (ADR 0007 D4). Presets (FX-4) are just a chain array saved under a name
in `~/.config/kmixdeck/presets/*.json`; two ship in the repo (`Voice — clean`,
`Voice — broadcast`). Copy between channels (FX-7) = copy the array; works while the
device is absent because nothing here touches a device.

**D6 — Bus.** `org.kmixdeck1.Channel.Fx` (`s`, JSON of `{enabled, chain}`) +
`SetFx(s)`. JSON, not a struct-of-variants: the shape is open-ended (`ladspa` type
takes arbitrary controls) and every frontend language has a JSON parser; a D-Bus
`a(sba{sd})` would freeze the schema for nothing.

## Consequences

- Latency: gate/comp/eq/hpf are IIR, ≈0; the lookahead limiter adds its lookahead
  (`latency` output port, a few ms) — reported, not hidden.
- One extra process-internal module per channel with effects; nothing for channels
  without.
- We depend on `swh-plugins` at runtime for gate/compressor/limiter. Missing plugin
  → the effect is refused at `SetFx` with a clear message and the CLI says which
  package to install. Never a silent no-op.
- Not in scope: sidechain from another channel, VST, plugin GUIs.

## Verification

`tests/integration/test_fx.py` — measured on the channel sink: gate closes on a
−24 dB tone at threshold −5, opens at −70; compressor ratio 20 @ −30 takes ≈6 dB;
limiter holds; per-effect bypass and chain bypass restore the reference within 1 dB;
apps and the hardware input follow the entry point; survives daemon restart; a mix
chain acts on the mix sink.

## Resolved (2026-09-16)

Three separate causes, found by differential measurement (probe scripts against the private PipeWire sandbox):

1. **Pod shape.** `Graph::setControl` passed the control key as a string where `spa_pod_builder_add_object` expects
   a uint32 prop id — rc 0, nothing applied. filter-chain reads controls from `Props.params = [ "<node>:<Label>" <float> ]`
   (the shape `pw-cli set-param <id> Props '{ params = [ … ] }'` sends). Built by hand with push_object/prop/push_struct.
2. **Stream loss on chain swap.** `applyFx` destroyed the old filter-chain before remembering which streams sat on it;
   with `node.dont-fallback` they went unlinked and nothing moved them onto the new node. Streams are captured first,
   then retargeted once the new entry node appears (`retargetWhenPresent`).
3. **CH-5 fought the chain.** `slugForSinkId` did not know `kmixdeck.fx.<slug>` is a channel, so a stream aimed at the
   fx entry counted as "nowhere" and auto-route dragged it onto the default channel. The fx entry now maps to its slug.

Also: `Chain::isActive()` (chain on AND ≥1 effect enabled) replaces `enabled && !effects.isEmpty()` everywhere — a chain
whose only effect is bypassed renders no module, so `channelEntry` must point at the plain sink.
`reconcile()` no longer rebuilds an existing chain (the 400 ms start-up reconcile used to re-create it after a SetFx).

Tests: `test_fx.py` 4/4, no xfail — gate at −40 passes the −24 dBFS tone, live threshold 0 → −inf, bypass restores.
