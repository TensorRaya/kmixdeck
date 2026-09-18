# ADR 0001 — New project, not a fork of Sonusmix

- Status: accepted
- Date: 2026-09-14

## Context

Before writing anything we screened what exists on Linux for the
"Wave Link" job (per-app virtual channels, several output mixes, independent
per-mix levels, effects, external control):

| Candidate | Verdict |
|---|---|
| [Sonusmix](https://codeberg.org/sonusmix/sonusmix) (Rust, GTK4/relm4) | Closest in intent — explicitly "same features and workflows as Voicemeeter". **Unmaintained**: maintainer in [#73](https://codeberg.org/sonusmix/sonusmix/issues/73) (2025-07): *"I just personally don't have the time."* Does not build with Rust ≥ 1.90 ([#74](https://codeberg.org/sonusmix/sonusmix/issues/74)), dropped from nixpkgs 2025-09. The key requirement — one source at different levels in different mixes — is open and unanswered ([#72](https://codeberg.org/sonusmix/sonusmix/issues/72)). |
| Pulsemeeter | PulseAudio-era predecessor, dead. |
| pavucontrol / pwvucontrol / plasma-pa | Volume control only; no virtual channels, no mixes. |
| qpwgraph / Helvum | Patchbays: wire ports by hand, no mixer semantics, no per-route gain. |
| Carla | Plugin host / DAW-style; can do it, wrong shape for a streamer tool. |
| jack_mixer | JACK-era mixer, works on PipeWire, UI and model from 2005. |

Sonusmix source inspected (2026-09-14, `main`): ~8 000 lines Rust, of which
~2 100 are the PipeWire layer (`pipewire_api/`), the rest is GTK4/relm4 UI and
state. Its model is *Endpoint ↔ Group* links; "mix" is not a first-class
concept.

## Decision

Start a new project. Target KDE (Qt 6 / Kirigami). Treat Sonusmix as a
reference for pitfalls (e.g. [#38](https://codeberg.org/sonusmix/sonusmix/issues/38):
PipeWire renames app nodes; [#41](https://codeberg.org/sonusmix/sonusmix/issues/41):
source→group connection issues), not as a code base.

## Reasons

1. **UI toolkit mismatch.** Kirigami vs GTK4/relm4 — the whole UI layer
   (~70 % of the code) would be rewritten anyway.
2. **Domain model mismatch.** Our core entity is the *Mix* with per-channel
   gains; Sonusmix has no such entity, and its open issue #72 shows the model
   cannot express it without chaining virtual devices.
3. **Maintenance state.** No active maintainer to coordinate with; a fork would
   inherit a broken build and a stale dependency tree.
4. **License.** Sonusmix is MPL-2.0; we choose GPL-3.0-or-later to match the
   KDE ecosystem. Reusing MPL code file-by-file is possible but adds friction
   for little gain given point 1 and 2.

## Consequences

- We own the PipeWire layer. pipewire-rs (official Freedesktop bindings) or
  the C API directly — decided in ADR 0002 after the technical research.
- We write requirements first (`docs/spec/`) and keep them testable.
- We document what we learned from Sonusmix's issue tracker in
  `docs/research/`.
