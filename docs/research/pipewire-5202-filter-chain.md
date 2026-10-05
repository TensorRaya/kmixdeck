# PipeWire #5202: filter-chain use-after-free on PipeWire ≤ 1.6.2 (measured 2026-10-06)

## Symptom

CI run 37378915814 (`release/0.3`, Ubuntu 26.04 runner): `integration-service_cli` red in
`test_cl5_tree_shows_the_signal_path` and `test_cl7_shell_completion_uses_one_source` with
`UnicodeDecodeError: 'utf-8' codec can't decode byte 0x81` while reading `pw-dump`. Both tests passed in the run
before. Local container `kmx-ci` (same image): reproduced 1 out of 3 full-file runs; two raw dumps were captured.

## Where the damage is

Both captured dumps break at the same place: the `PropInfo` of the filter-chain node `kmixdeck.cells.mikrofon`.
After the `Gain 4` entry comes `"name": "\x91$"`, followed by keys that are not valid JSON. Decoding with
`errors="replace"` does not help, because `json.loads` still fails (`Expecting ',' delimiter`).

## Root cause

This is upstream PipeWire issue #5202, fixed in commit `cf88df2185b5aff889907654bfb05557de458acf`
("filter-chain: don't corrupt the enumerated properties"), first released in **1.6.3**. In `setup_streams()` the
module dereferences every param in its dynamic POD builder and only then builds `EnumFormat`. If that build grows
the builder past a 4096-byte step, `realloc` can move it, and `pw_stream_connect()` reads the stale pointers.
Whether a given chain is hit depends on the byte size of its params (control names, count, values). Whether it
corrupts or works depends on the allocator. The faulty pattern exists in every tag from 1.0.0 to 1.6.2.

The module runs inside **kmixdeckd** (`Graph::loadLoopback`, `pw_context_load_module`), not in the PipeWire
server. So the effect is a corrupted capture-stream param set or a crash of kmixdeckd.

## Proof (A/B, one variable)

The probe is an `LD_PRELOAD` that turns every page-multiple `realloc` into move-and-`PROT_NONE`, so a stale read
faults deterministically. It was validated first: without the probe, a deliberate stale read returns its old byte;
with the probe it returns rc=139.

| kmixdeckd run with probe, steps of `test_mx1` | filter-chain module | result |
|---|---|---|
| stock | `/usr/lib/.../libpipewire-module-filter-chain.so` (1.6.2-1ubuntu1.2) | SIGSEGV in `pw_stream_connect` ← `module_init` (filter-chain) ← `Graph::loadLoopback` (graph.cpp:483) ← `Mixer::ensureCellGraph` |
| fixed | 1.6.2 + cherry-pick of `cf88df2185`, via `PIPEWIRE_MODULE_DIR` (mapping checked in `/proc/<pid>/maps`) | all steps rc=0, daemon alive |

## Who is affected

- Ubuntu 26.04 (resolute): `1.6.2-1ubuntu1.2` in -updates, which does not include the fix (checked with
  `apt-get changelog` and the Launchpad publishing history on 2026-10-06). The next Ubuntu series ships 1.6.8.
- Debian sid/forky: 1.6.9 (fixed). trixie: 1.4.2 (not checked; the pattern exists in tags 1.4.0 and 1.4.9).
- `CMakeLists.txt` accepts `libpipewire-0.3>=1.0`, so every supported version up to 1.6.2 is affected.

## What did not reproduce it

- Without a probe: 0 bad out of 350 dumps (150 with the starter layout, 200 with the `test_cl5` layout).
- `glibc.malloc.perturb=165`: 0 bad, 5 layouts (3–7 mixes), one dump each.
- Move-and-poison `realloc` (old block filled with 0xA5, then freed): 0 bad, 4 layouts (3–6 mixes). This probe
  also ran `test_service_cli.py` 44/44 green.

Why poisoning shows nothing is a hypothesis, not measured: 0xA5A5A5A5 as a POD size is invalid, so the stream
most likely drops the poisoned params instead of passing garbage on, and pw-dump has nothing broken to show. In
the CI case the freed block was reused, which left params that parse but contain garbage. Only the `PROT_NONE`
probe made the bug deterministic.

## Open

- Third full `test_ports.py` run in the container: kmixdeckd died during dv29/dv29b/dv31 ("service not
  reachable"). A garbage POD size could make the stream read past the block, so this could be the same bug. That
  is not proven, because the daemon log was removed with the sandbox.
- The Blade's PipeWire version is not checked yet (host unreachable on 2026-10-06).
