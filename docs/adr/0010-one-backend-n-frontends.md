# ADR 0010 — One backend, one view model, N frontends: a feature is done when every frontend has it

**Status:** accepted 2026-09-17 · **Owner:** Raya · **Requested by:** Michel

## Context

kmixdeck now has three frontends in our own hands, with a fourth planned:

| Frontend | Scope | State |
|---|---|---|
| `kmixdeck` (CLI) | everything, scriptable | shipped |
| `kmixdeck-kde` (Kirigami window) | everything, the working surface | shipped |
| **tray** (in `kmixdeck-kde`, UX-17) | the essentials at a glance; double-click opens the window | this ADR |
| web UI (AR-8) | everything, browser | planned |

Michel, 2026-09-17: *"Damit müssen ja drei UIs feature-synchron entwickelt werden. Regel 1: Backend und API first.
Regel 2: Ein Feature ist erst fertig, wenn es in allen UIs eingebaut ist, die wir entwickeln."*

Without a structure, rule 2 is a wish: every feature would be N hand-written UI patches that drift the moment one
is forgotten. AR-10 (golden rule: the life-cycle test) covers the daemon; nothing today proves that a feature reached
each frontend.

## Decision

### D1 — Backend first, always (AR-1/AR-2, restated as a gate)
A feature exists when it is on the D-Bus tree with its interface XML. The CLI is the first client and the reference
for the vocabulary (verbs, refs, units). No UI may hold state the daemon does not have.

### D2 — One view model, owned by the client library, consumed by every frontend
`MixerClient` (`src/frontend/`) is the **only** place that reads the bus and turns it into UI-facing data. It already
does this for the patchbay (`patchbay()`, AR-9) and the mixer page. This ADR makes it the rule:

- Every frontend (KDE window, tray, web bridge) links the same `MixerClient` and renders from its JSON-shaped view
  models — `patchbay()`, `overview()` (new, D3), `channels/mixes/apps` maps. **No frontend talks to D-Bus itself.**
- A new feature therefore lands in exactly ONE client-side place (`MixerClient`), plus rendering in each frontend.
  The data is shared; only the pixels differ.

### D3 — Feature tiers decide which frontend must show what
Not every frontend shows everything (the tray must not become the window). Each requirement row gets a **tier**:

| Tier | Meaning | CLI | KDE window | Tray | Web |
|---|---|---|---|---|---|
| **core** | you would miss it on a live stream: levels, mute, listening device, mix master, presence | must | must | must | must |
| **full** | everything else that has a UI: FX, pan, wire trim, patchbay, icons, ordering | must | must | — | must |
| **api** | daemon/CLI only: virtual devices, self-test hooks | must | optional | — | optional |

Placements decided on 2026-09-17 while closing rows (the rule is the tier's meaning, not the row's letter code):
- **MX-5 colour code → core.** A colour is there to be recognised at a glance, and the tray is the glance. Stripe on
  the window header, stripe on the tray row, `Color` on the bus, one palette from `MixerClient::colorPalette()`.
- **CT-7 import/export → full**, not api: the window offers it through the menu (file dialog); the tray does not —
  a file dialog has no place in a popover. The daemon owns the document (`Mixer.Export/Import`).
- **CH-11 hidden devices → full.** Hiding is a picker concern; the tray has no pickers except the listening device,
  which reads the same filtered `outputDevices` and therefore agrees for free. A hidden device that is *in use*
  stays visible where it is used — hiding is about the list, never about the routing.
- **MX-8 duplicate → full.** Copies icon, colour, FX, master and every cell level; never outputs (one device would
  carry the same audio twice).

The tier is a column in `docs/spec/requirements.md`. `tools/sot-audit.py` refuses a ✅ whose test list does not
name a proof for every frontend the tier demands (D5).

### D4 — The tray shows the `overview()` view model and nothing else
`MixerClient::overview()` returns: listening device + present?, per mix {name, icon, master, muted, present,
meter key}, per channel {name, icon, muted, meter key, input present}, count of running apps, daemon connected.
Click → popover rendered from `overview()`; double-click → the window (`kmixdeck-kde`, DBus-activated, single
instance). The tray never grows a feature that is not in `overview()`; if the tray needs it, `overview()` grows,
and the window's header bar renders the same struct (so "essentials" stays one definition, not two).

### D5 — Enforcement, not intention
1. **`tools/sot-audit.py`** knows the tier column and requires, per ✅ row: one AR-10 life-cycle test (daemon), and
   for each frontend the tier demands, a named test — `test_*` via `--probe` for KDE, `test_*_tray_*` via
   `kmixdeck-tray --probe` for the tray, CLI test for the CLI. Missing = build red.
2. **`tests/integration/test_frontends_sync.py`** (new): for every core-tier row, drives the change through the CLI
   and reads it back through `overview()` (tray path) AND a KDE `--probe` — one test per row, table-driven, so adding
   a row is adding a line, not a file.
3. **CONTRIBUTING.md** gets Michel's two rules verbatim, above AR-10.

## Consequences
- Features cost one client-lib change + N renderings; drift between frontends is caught by the audit, not by Michel.
- The tray is cheap: it is a second renderer of data that exists anyway.
- The web bridge (AR-8) becomes a serialiser of the same view models over WebSocket — no separate model.
- Cost of the rule: a core feature is not ✅ until the tray has it. That is the point.

## Rejected
- Tray talks to D-Bus directly — a second model to keep in sync; exactly the drift this ADR forbids.
- Tray as a Plasma applet (QML in plasmashell) — ties us to plasmashell's lifecycle and sandbox; a StatusNotifierItem
  (KStatusNotifierItem) works on every tray incl. non-Plasma and is what the Kirigami window can own or a tiny
  `kmixdeck-tray` binary can own. We start with the tiny binary (independent of window lifetime).
