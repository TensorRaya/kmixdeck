# ADR 0013 — Cells are gains on a bus, not loopbacks

**Status:** accepted 2026-09-28 · **Supersedes:** the cell part of ADR 0002 and ADR 0009 ·
**Answers:** ADR 0012, Consequence 3

## Context

`test_dv30c` (32 mono channels + 4 stereo mixes on a 32×32 device) failed even after ADR 0012. Two
causes, both measured (2026-09-26/27; repeatable with `tools/measure-dv30c.py`):

1. **Listen backlog.** Every cell was its own `module-loopback`, and every `module-loopback` opens its
   **own client connection** to the PipeWire server. ~300 concurrent `connect()` against
   `listen(fd, 128)` → `EAGAIN` → socket dropped → 8 ms later `Broken pipe` → the module destroys itself.
   The edge is missing, without anyone reporting an error.
2. **File limit.** One loopback costs ~20 files (8 memfds, 8 eventfds, 2 sockets, rest), 264 of them ≈ 5300,
   spread across server and daemon. The soft limit of the PipeWire server is 4096.

The first repair was a drop-in raising the server's `RLIMIT_NOFILE`, plus retry logic in the daemon for
vanished edges. Both made the test green, and both were symptom treatment: it raises a limit that was
only reached because the graph starts a full program for every cell.

Terminology, because the discussion got it wrong: a PipeWire **link** between two ports costs almost
nothing (+64 files for 256 links, measured). What was expensive was the "edge" in the kmixdeck sense — a
complete `module-loopback` with its own socket connection, two stream nodes, and shared memory.

## Decision

A cell (channel × mix) is **a gain value**, not a node.

- **One filter chain per channel** `kmixdeck.cells.<channel>`. It taps the channel sink
  (`stream.capture.sink`, `node.target`) and distributes L/R via the builtin `copy` into one builtin `mixer`
  pair `L<mix>`/`R<mix>` per mix. The `"Gain 1"` of that pair **is** the cell fader.
- **One bus** `kmixdeck.bus`: a null sink with fixed 64 channels `AUX0..AUX63`, `stream.dont-remix`. Mix *i*
  occupies `AUX(2i)`/`AUX(2i+1)`. Every channel chain plays its 2·M outputs there; PipeWire sums them.
- **One tap per mix** `kmixdeck.tap.<mix>`: a loopback that reads its two AUXes from the bus monitor and
  plays them into the mix input (into that mix's chain when mix FX is active, otherwise into the mix sink).
- Wiring is done exclusively via `target.object`/`node.target` in the config. WirePlumber establishes the
  links; **the graph arises from the config alone** (DV-1), no running daemon.

Limit: `SPA_AUDIO_MAX_CHANNELS = 64` → 32 mixes per bus. MX-1 forbids a fixed maximum, so every further
group of 32 mixes gets its own bus `kmixdeck.bus.<k>`, and every channel a chain
`kmixdeck.cells.<channel>@<k>` per group. Bus geometry and the arguments of the chain and the tap come from
**one** place (`namespace ADR13` in `layout.h`), shared by the config renderer and the runtime — they
cannot drift apart.

### The daemon stays the central API

Nothing about its role changes (owner, 2026-09-27): D-Bus API, `cell set/mute/get`, undo, scenes,
export/import, MX-7 linkages — all unchanged. What changed is only **what happens behind** `writeCell`:
instead of a `setVolume` on a loopback node, a `setControl` on two controls of the channel chain.

### Where cell state lives

WirePlumber persists stream volumes (`state-stream.lua`), but **no filter controls**. Therefore:

- The volume and mute of every cell are in `layout.json` (`"cells"`); the source of truth is the layout.
- The config renders them as `control = { "Gain 1" = <value> }` → after a PipeWire/WirePlumber restart
  without the daemon, every cell comes back with its value (measured,
  `test_dv7_levels_and_mute_survive_daemon_restart`).
- Mute = gain 0; the fader value is preserved in the layout.
- When an outsider (`pw-cli`) writes a cell gain, the daemon sees it in the echo (`Props.params`) and
  restores the layout value (MX-2, `test_mx2_cell_state_survives_a_foreign_writer`).
- When a mix is added or removed, the signature of the chain changes (`node.description` carries the
  mix list); the daemon rebuilds stale chains. The bus stays put, because its 64 channels are fixed.

### Cell levels (UX-13)

There is no longer a node per cell on which a meter could hang. UX-13 demands a post-fader level,
so the daemon computes `cell/<k>/<m> = channel peak × cell gain`. Mathematically identical, zero extra streams.

## Measured

Console built via the CLI, private PipeWire, **standard file limit 4096, no drop-in**. Reproducible with
`tools/measure-dv30c.py` (one run) or `tools/measure-dv30c.py F --runs 10 --load 3` (the series below).

32 × 4 (comparison with ADR 0012, measured there): loopback matrix **556 nodes, 3846–3933 fds** →
ADR 0013 **75 nodes, 1314 server files, 39 client connections, 264 links**.

32 × 32 (dv30c) — loopback matrix: red (listen backlog 128 with ~300 concurrent connections,
file limit 4096). ADR 0013, the F1–F10 run of ten **under load** (3 × `nice 19` endless loops, load 6–9 on
4 cores), state after the three fixes below:

| | F1–F10 |
|---|---|
| Nodes | **219** (identical across all 10 runs) |
| Server files | **1711** |
| Client connections | **90** |
| Links | **680** |
| Build via the CLI | **9,0–15,2 s** under load |
| Missing cells / LastError | **0 / empty**, 10 of 10 |
| Cell d1→r0 set to 0,25 | 0,25 in r0, 1,0 in r1 |

Before, under the same load: 1 of 5 runs red (`cells.d13` missing), 1 of 12 with 221 nodes instead of 219.
The remaining core errors in F1–F10 (0–30 per run, "no global N" after rewiring the mix outputs) came from
`destroyOurNodes`, which destroyed both halves of a module — addressed there, then 0 in the short run.

## Findings under load (2026-09-28)

Calm: 15/15 and 40/40 green. Under CPU contention (3 × `nice 19` loops, load 5–7,5 on 4 cores),
`kmixdeck.cells.d13` was missing in run L5 after 180 s (earlier in series 1: `out.r1`/`tap.r1`), and
one run each had two nodes too many (221 instead of 219). Measured with a trace of every request/destruction
(`KMX_TRACE_GRAPH`), three errors in the daemon, none in PipeWire:

1. **MX-2 evaluated the echo of a fresh chain.** `filter-graph.c` reports `control_data[0]`, which is 0,0
   until the graph is first set up (`pw-mon`: up to 9 echoes of 0,0 before the first correct value).
   The watcher took that for a foreign writer: **2578 write-backs** in a single 32×32 run, 72 already
   when creating a mix on a 3-channel console. Now: a chain is only evaluated after it has reported
   the layout back once. After the fix: 0 on mix creation; the foreign-writer test stays sharp.
2. **Double destruction.** Both streams of a module were destroyed, although one of them unloads the whole
   module, and reconciliation runs before the report destroyed the same ID again: 157× "no global N",
   244× "unknown resource N" (control values to dying nodes) in the 3-channel run. Now one destruction per
   module and no write accesses to nodes being torn down: **0 core errors**.
3. **Request marker.** `onNode` deleted the marker on *every* parameter echo; the old chain of the same
   name echoed until its removal, thereby freeing the still-loading new one → a second module (221 nodes).
   And a node that never came was never re-requested, even though the comment promised it → d13 was missing
   forever. Now: only the first appearance fulfills the request; "never appeared" releases it and
   re-requests; twins are reduced to the older one during reconciliation.

## What was removed

- `data/pipewire-50-kmixdeck-nofile.conf` and its CMake line — the graph fits within the standard
  limit again; a raised limit is no longer a prerequisite.
- The timed retries from the bandage (`m_edgeRetries`) — the mass failure they masked no longer occurs,
  because the ~300 concurrent connections no longer exist.
- `kmixdeck.link.<channel>.<mix>` nodes, `m_cells`, `Names::cellNode`.

## Consequences

- The per-cell level measurement is computed, not tapped. An error **within** the chain (e.g., a
  gain that never arrives) doesn't show up in the cell meter, only at the mix. The echo from `Props.params`
  covers exactly this case.
- Adding or removing a mix rebuilds all channel chains. That is a short interruption (module reload) on
  every channel — accepted, because mixes are rarely created and channels/cells are the frequent operation.
- Import of older exports with `kmixdeck.link.<k>.<m>` levels is still understood and mapped to cells.
