# qpwgraph — analysis for kmixdeck

State of the code under investigation: upstream `rncbc/qpwgraph` v1.0.4 (2026-08-26), 17.7k lines of Qt/C++ in 39 files. Analyzed on 2026-09-19.

## Short profile

qpwgraph is a **direct PipeWire client with its own GUI**: a QMainWindow with a QGraphicsScene canvas, a registry subscription, all life in one process. No daemon, no IPC layer — the GUI *is* the state holder. That is interesting for us only in points that work independently of this architecture (below, column "Adopt"). For the fundamental question — daemon with D-Bus, frontends interchangeable (AR-1..AR-9) — the opposite of their design is the confirmation of our model: qpwgraph can patch nothing without a visible window, and its state hangs off a QSettings file of the GUI process.

Its centerpiece is the **patchbay profile**: an XML file with connection pairs, scanned at startup and matched against the live graph. That is a relative of kmixdeck's layout JSON, but with two mechanisms we do not have and might need.

## Table — what to adopt, how, and why

| # | Point | qpwgraph 1.0.4 | kmixdeck today | Adopt? | Why |
|---|---|---|---|---|---|
| 1 | **Gain-less edges as plain links** | Link creation via `link-factory` with `LINK_OUTPUT_NODE/PORT` + `LINK_INPUT_NODE/PORT`, plus `OBJECT_LINGER=true` and `LINK_PASSIVE` from the environment. No own client per edge. | Every edge is a module load: a PipeWire client per edge, **measured 20–21 fds** (idle daemon 14 fds; a mix output edge +21, a cell +20, a virtual device +20). | **No — remeasured, does not hold up.** See the measurement below. | The premise ("the gain-less edges are the majority") is false: in a realistic setup **76 % of the edges are cells**, and those *must* stay loopback, because the playback volume there is the fader. Gain-less is only 19 %. On top of that, per the D-Bus API **every** wire can carry a trim (`Channel.SetWireTrim`, `Mix.SetWireTrim`) — a plain link would have to be rebuilt as a loopback live on the first trim turn, i.e. repatching while the audio is running. Effort and risk against 19 % of a total that, after DV-30 (`LimitNOFILE=65536`), no longer has a limit. |
| 2 | **Exclusive vs. additive when loading a profile** | Two switches: *Activated* (apply the profile) and *Exclusive*. Exclusive: connections not in the profile are disconnected. Non-exclusive: the profile only adds. Both also as CLI: `-d/--deactivated`, `-n/--nonexclusive`. | CT-9 (scenes) is 📝: recall writes the stored subset into the layout, with no statement about foreign edges. | **Yes,** as the default for `scene recall`: exclusive. Additive as the flag `--add`. | Without an exclusivity rule, a scene switch is not deterministic — an app that has picked up WirePlumber default routing in the meantime survives the scene. Exclusivity is exactly what distinguishes a "scene" from a "catch basin". |
| 3 | **Auto-pin of new edges** | Switch "Auto pin": every freshly laid connection is written automatically into the active profile. Manually disconnected ones are *not* re-pinned. | Existing scene logic (not yet built) would, on every recall, write everything over the heads of the running session. | Adopt with CT-9: recall = target state; what the user changes *after* the recall becomes part of the scene when auto-pin is on. | That is the only sensible merge rule for "load a scene, then keep working". Manually-disconnected-stays means: the user's intent beats the profile list. |
| 4 | **Merger list (regex per node name)** | Node names that appear multiple times (browsers!) are merged into one logical node for patchbay matching. Own options page with a regex list; matching runs via `nodeNameEx` instead of `nodeName`. | We group apps under `appKey = application.name` (CH-6), but the patchbay cards (DV-24) are shown per PipeWire *node* — a Chromium with four streams yields four card rows. | **Yes,** toned down: same `application.name` → one card, positions merged. No regex list needed, the key is already there. | In real setups (browser + Discord + OBS), column 1 of the patchbay otherwise bloats to two- or three-fold. The merger concept solves exactly that; the regex list of that implementation is ballast, because we already have the grouping key. |
| 5 | **Node identity by name, not id** | `NodeNameKey(name, mode, type)` as the hash key; ChangeLog since 0.9.0: short-lived nodes with reused ids, same-name-different-id handled explicitly. | Layout and app assignment hang off `application.name`/`node.name`, the live registry in `graph.cpp` off the `uint32 id` — correct as a *live* cache. | Confirmed, nothing to change. | The id-only pitfall is already avoided on-disk for us (CH-6); their change log proves that others had it. |
| 6 | **Version in the profile root** | `version` attribute on the root element, migration on load (`< "0.5.0"` → clean up legacy names). | Layout.json has `version` + sidecar `.vN-from-newer-kmixdeck` for files from a newer version. | Comparable, nothing to adopt. | Both sides solve the same problem; ours is better suited to two frontends (JSON instead of XML). |
| 7 | **Recently used profiles as tray menu** | System tray menu "Presets" with recently used profile paths; click one = apply. | The UX-17 tray popover shows Overview, no scenes. | Small: last paragraph in the tray popover "Last loaded: …" with direct recall. | Two clicks for the most common case (machine woke up, fetch the scene). The rest of the tray layout stays ours. |
| 8 | **Node color types (audio/video/midi/midi2/other) with editable colors** | Five port-type color cards in the options dialog, persistence via `ColorsGroup` with hex keys; MIDI 2 (UMP) as its own type. | The web patchbay sorts by direction, colors only for levelled vs. silent edges. | No. | For us the edge carries its purpose in the ref (`>L`, `>R`, position); color classes by media class are second-hand information. |
| 9 | **Free canvas with topological sort ("Arrange Nodes")** | Rank = distance from the source, columns, then the columns vertically aligned to the means of the target Y; Repel Overlapping Nodes as an extra. | The patchbay is deliberately fixed-column (sources → channels → outputs → monitors, DV-24/DV-27). | No. | Their algorithm optimizes wires on a wild graph — for us the structure is the semantics (one column = one stage in the signal path). Sorting it away would cost the readability that the layout is made of. |
| 10 | Thumb-view corner, zoom range, fullscreen, pinch zoom | Gimmicky QGraphicsView features with corner positions and settings keys. | The web UI is responsive without its own zoom infrastructure. | No. | Window management, no domain behaviour. |
| 11 | The GUI owns the graph (no daemon) | Everything in one process: no patching without a window, state in the GUI's QSettings. | AR-1: the daemon carries everything, frontends are leaves. | No — counter-reference. | Their model is the reason their patches have to be rescanned when the window closes; ours doesn't. Belongs in ADR-0010 as a contrast. |

## Measurement for point 1 (2026-09-19, sandbox)

I had suggested plain links as the first implementation point and then measured the premise. It does not hold.

Cost per edge, measured on the daemon's fd counter (`/proc/<pid>/fd`), sandbox PipeWire:

| Step | fds | Δ |
|---|---|---|
| idle, starter layout (2 mixes) | 14 | — |
| + 2 fake devices (no kmixdeck object) | 14 | 0 |
| + 1 mix→device edge | 35 | **+21** |
| + 1 channel (2 cells) + 1 device input | 95 | +60 |
| + 4 channels (8 cells) | 255 | +160 (**20 per cell**) |
| + virtual device 8×8 | 275 | **+20** |

Edge inventory for a realistic setup (6 channels × 4 mixes, 2 device inputs, 2 mix outputs), 84 edges:

| Edge type | Count | Share | Gain? |
|---|---|---|---|
| cell (`kmixdeck.link.*`) | 64 | **76 %** | yes — the playback volume IS the fader (ADR 0002) |
| mix output (`kmixdeck.out.*`) | 8 | 9 % | DV-14 trim possible |
| mix capture (`kmixdeck.source.*`) | 8 | 9 % | DV-14 trim possible |
| device input (`kmixdeck.in.*`) | 4 | 5 % | DV-14 trim possible |

**Result:** gain-less *at the moment of creation* is 19 % of the edges, and even those can get a trim at any time (`Channel.SetWireTrim` / `Mix.SetWireTrim`). A plain link would have to be rebuilt as a loopback live on the first trim turn — repatching while the audio is running, for a saving that, after `LimitNOFILE=65536` (DV-30), no longer squeezes anyone. **Rejected**, not implemented.

Side finding for #2/#3: cells dominate the edge count so clearly that a scene implementation (CT-9) should under no circumstances create/break edges, but only set volumes — which the spec anyway fixes that way ("not the wiring").


## What of this concretely lands in v0.3

**#1 is rejected** (measurement above). Remaining: **#2/#3** together with CT-9 (scenes), because the merge rule has to be defined there anyway — exclusive as the default, `--add` as the flag, auto-pin for "load a scene and keep working". **#4** (apps with the same `application.name` = one patchbay card) is a small change in `patchbay.js` plus grouping in `overview()`. **#7** (last-loaded scene in the tray popover) lands with CT-9. #5/#6 are confirmations without work, #8–#11 reasoned nos.

Honest yield of this analysis: **two** adoptable mechanisms (exclusivity rule, merger idea) and one refuted own hypothesis. For 17.7k lines of foreign code that is little — but the exclusivity rule is something I would otherwise have had to invent myself for CT-9, and their change-log history (0.9.0: "nodes with reused ids", 0.9.3: "players power-cycling their client on a whim") is the proof that our name-instead-of-id decision from CH-6 was the right one.
