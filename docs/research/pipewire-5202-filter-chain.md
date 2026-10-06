# PipeWire #5202: filter-chain use-after-free on PipeWire 1.6.0–1.6.2 (measured 2026-10-06)

## Symptom

CI run 37378915814 (`release/0.3`, Ubuntu 26.04 runner): `integration-service_cli` red in
`test_cl5_tree_shows_the_signal_path` and `test_cl7_shell_completion_uses_one_source` with
`UnicodeDecodeError: 'utf-8' codec can't decode byte 0x81` while reading `pw-dump`. Both tests passed in the run
before. Local container `kmx-ci` (same image): reproduced 1 out of 3 full-file runs; two raw dumps were captured.
CI run 37391578286 (`e655c10`, a Markdown-only commit on top of the green run 37390870334) failed the same two
tests again, at byte 928153 instead of 928288: same code, one run green, one red.

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
corrupts or works depends on the allocator.

The faulty order came in with `56a4ab5234` ("filter-chain: support no input or output streams", 2026-01-21). All
46 release tags from 1.0.0 to 1.6.9 were checked for it (EnumFormat built into the same builder after the deref):
only **1.6.0, 1.6.1 and 1.6.2** have it. 1.0.x, 1.2.x, 1.4.x and 1.5.81–1.5.85 build `EnumFormat` first and are
not affected. The module file is byte-identical in 1.6.0 and 1.6.2.

The module runs in **two processes**, and both are hit:

- **kmixdeckd** loads a filter-chain for every cell chain and fx chain it builds at runtime
  (`Graph::loadLoopback` → `pw_context_load_module`). Effect: corrupted params on the capture stream, or a
  SIGSEGV of kmixdeckd.
- **The PipeWire server** loads the same chains from the generated config fragment
  (`~/.config/pipewire/pipewire.conf.d/90-kmixdeck.conf`) at every login. Effect: a SIGSEGV of the server, which
  takes all audio of the session down, not only kmixdeck.

## Proof (A/B, one variable)

The probe is an `LD_PRELOAD` that turns every page-multiple `realloc` into move-and-`PROT_NONE`, so a stale read
faults deterministically. It was validated first: without the probe, a deliberate stale read returns its old byte;
with the probe it returns rc=139.

| Process under the probe | Steps | filter-chain module | Result |
|---|---|---|---|
| kmixdeckd | starter layout, then `mix add`, `channel add`, `cell set` | stock (1.6.2-1ubuntu1.2) | **5/5 SIGSEGV**; backtrace `pw_stream_connect` ← `module_init` (filter-chain) ← `Graph::loadLoopback` ← `Mixer::ensureCellGraph` |
| kmixdeckd | same | 1.6.2 + `cf88df2185` | 5/5 alive |
| kmixdeckd, under gdb | full `test_service_cli.py` | stock | 16 of 44 red, 2 of 5 daemon starts SIGSEGV |
| kmixdeckd, under gdb | full `test_service_cli.py` | 1.6.2 + `cf88df2185` | 43 of 44 green, 0 crashes (the red one, `test_ar6`, ran `ldd` on the gdb wrapper script) |
| PipeWire server | restart with the starter fragment (3 channels × 2 mixes) | stock | 5/5 alive |
| PipeWire server | restart with a fragment for 4 channels × 3 mixes | stock | **5/5 SIGSEGV** at start |
| every process | full `test_service_cli.py`, three runs | 1.6.2 + `cf88df2185` | 38/44 (see Open), 43/44 (the gdb wrapper again), 44/44 |
| every process | full `test_service_cli.py` | stock | 12/44 |

In the kernel log of the stock runs, `kmixdeckd` and `pipewire` both segfault at
`libpipewire-0.3.so.0.1602.0+0x9c3f3`. The Ubuntu debug symbols (`libpipewire-0.3-0t64-dbgsym`, build id
`c66f81fd…`) resolve it to `add_param` (inlined from `spa/include/spa/pod/body.h:421`), the function that copies
the params handed to `pw_stream_connect`.

The fixed module was built out of tree against the system headers; the source needs nothing from PipeWire's build
but `PACKAGE_VERSION`. Its mapping in the process was checked in `/proc/<pid>/maps`. The patched 1.6.2 file and
upstream's 1.6.3 file differ in two lines of a documentation comment only.

## Who is affected

- Ubuntu 26.04 (resolute): `1.6.2-1ubuntu1.2` in -updates, which does not include the fix (checked with
  `apt-get changelog` and the Launchpad publishing history on 2026-10-06). The next Ubuntu series ships 1.6.8.
- Debian sid/forky: 1.6.9 (fixed). Debian trixie: 1.4.2 (not affected, see the tag check above).
- Any other distribution that ships 1.6.0, 1.6.1 or 1.6.2.

What kmixdeck does about it: ADR 0015.

## What did not reproduce it

- Without a probe: 0 bad out of 350 dumps (150 with the starter layout, 200 with the `test_cl5` layout).
- `glibc.malloc.perturb=165`: 0 bad, 5 layouts (3–7 mixes), one dump each.
- Move-and-poison `realloc` (old block filled with 0xA5, then freed): 0 bad, 4 layouts (3–6 mixes). This probe
  also ran `test_service_cli.py` 44/44 green.
- System load: a repro run that ended at load 9.98 had 0 bad dumps out of 150, so load alone does not trigger it.

Why poisoning shows nothing is a hypothesis, not measured: 0xA5A5A5A5 as a POD size is invalid, so the stream
most likely drops the poisoned params instead of passing garbage on, and pw-dump has nothing broken to show. In
the CI case the freed block was reused, which left params that parse but contain garbage. Only the `PROT_NONE`
probe made the bug deterministic.

## Open

- First of the three runs with the probe in every process (pytest, PipeWire, WirePlumber, kmixdeckd) and the
  fixed module: 6 of 44 red, kmixdeckd gone ("service not reachable"). Not seen again in two reruns; the daemon log
  was removed with the sandbox, so the cause is unknown.
- Third full `test_ports.py` run in the container: kmixdeckd died during dv29/dv29b/dv31 ("service not
  reachable"), with the stock module. Could be the same bug, not proven, the daemon log was removed with the
  sandbox.
- The Blade's PipeWire version is not checked yet (host unreachable on 2026-10-06).
