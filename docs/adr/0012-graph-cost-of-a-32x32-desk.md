# ADR 0012 — What a 32×32 desk costs the graph (measured)

**Status:** accepted 2026-09-20 (finding) · consequence 3 decided in ADR 0013 (2026-09-28)

## Context

`test_dv30c` failed in 1 of 6 full runs, never on its own. While hunting down the cause it
became clear that nobody knew how large the largest supported layout really is in the graph.
The test comment had been calculating with *"32 channels +
4 mixes ≈ 45 loopback clients"* for a year.

## Measurement

Three isolated runs (throwaway measurement script), private PipeWire, fresh daemon,
time series during setup:

| State | Nodes (total / kmixdeck) |
|---|---|
| Empty daemon | 29 / 27 |
| + virtual 32×32 device | 32 / 30 |
| + 8 channels | 88 / 86 |
| + 16 channels | 144 / 142 |
| + 24 channels | 200 / 198 |
| + 32 channels | 256 / 254 |
| + Mix r0 | 331 / 329 |
| + Mix r1 | 406 / 404 |
| + Mix r2 | 481 / 479 |
| + Mix r3 | **556 / 554** |

Reproducibility: 554 in all three runs, exact. fds at the end: 3933 / 3846 /
3861. Setup time 19.9–23.1 s (CLI calls), after which all nodes are visible within
**2.1–2.4 s**. After teardown: back to 29 / 27 — **no leak**.

## What this means

The graph grows linearly and predictably:

- **+7 nodes per channel** (56 per 8 channels)
- **+75 nodes per mix** — a mix costs more than ten channels

The 75 per mix is the item that drives the size: 4 mixes = 300 nodes, more than
half of the whole. The cause is the architecture from ADR 0009: every
channel×mix cell is its own loopback, i.e. 32 × 4 = 128 cells plus their
ports and monitor nodes.

**Operational consequence:** ~3900 fds for a desk that a Ui24R user realistically
builds. The standard `RLIMIT_NOFILE` of 1024 covers **less than a quarter**
of that. That is why `daemon/main.cpp:23` raises the soft limit to the hard limit,
and that is why the EMFILE failure of 2026-09-18 was no mishap, but the
predictable consequence. The systemd unit sets `LimitNOFILE=65536`; anyone who
starts kmixdeckd by hand in a shell with `ulimit -n 1024` runs into the same wall.

## Consequences

1. `test_dv30c`: timeout 60 s → 180 s. Justified, not padded — the
   isolated value is 2.4 s, the factor covers the load of 19 preceding tests on the
   shared daemon. The comment now carries the measured values instead of the fantasy 45.
2. The fd requirement belongs in the user documentation, not only in a test:
   anyone running a large desk needs a raised limit. → open
3. **Open architecture question:** 75 nodes per mix is a lot. Whether cells
   can be merged (one loopback per channel instead of per cell, mixing in the
   filter graph) is uninvestigated. That would be a change to ADR 0009 and
   needs its own measurement — **not** to be decided within the scope of v0.3.0.

## What was explicitly NOT here

Before the measurement, three hypotheses were in the running, all refuted:

- an FD clamp as test lever (idle demand fluctuates 25–43, a constant is unusable)
- leftover nodes from the preceding test (27 → 27, no residue)
- the daemon's `no global` errors as the cause (367 occurrences, evenly distributed over
  14 %–92 % of the log → background noise)

The details are in `docs/review-v0.3.md`, B6–B8.

## Addendum 2026-09-21 — what was wrong with the measurement tool

The day after this measurement it became clear that the diagnostic block of the hunter script
(a throwaway hunter script) collected its numbers **after** the `finally` branch of the
failed test — and `test_dv31` restarts the daemon there.
The "159 fds / 38 nodes on failure" that came out of it is therefore the
state of a freshly started daemon, not the state at the time of the error.

**For this ADR that changes nothing:** the table above comes from three isolated
runs with their own daemon, no foreign `finally` in between, and 554 nodes
reproduced exactly. The numbers are valid.

What is to be learned from this belongs here anyway, because the next person will
use this tool again: **a diagnostic block must measure before
cleanup code runs.** Otherwise it describes the repaired state and reads like
a finding. Same class of error as the old `wait_level`, which returned an
unconfirmed value (see CONTRIBUTING): the tool delivers a number
that looks like a measurement but is not one.

Also refuted on 2026-09-21: an alleged "settling time of 3.86 s"
when setting a trim. Measured (throwaway script, CLI return to
first level): the value is in place after **26 ms** and stays within 0.3 dB over 5 s.
There is no fade — `pw_node_set_param` sets hard. The 3.86 s were
the run time of the measurement loop itself, because every level measurement needs 1.5 s of recording.
Details in `docs/review-v0.3.md`, B9.
