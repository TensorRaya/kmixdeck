# ADR 0015 — Ship a fixed filter-chain module on PipeWire 1.6.0–1.6.2

**Status:** accepted 2026-10-06 · **Owner:** project owner · **Drives:** DV-34 · **Research:**
`docs/research/pipewire-5202-filter-chain.md`

## Context

Every cell chain (ADR 0013) and every effects chain (ADR 0008) is a `libpipewire-module-filter-chain`. In
PipeWire 1.6.0, 1.6.1 and 1.6.2 that module reads freed memory while it connects its streams (upstream issue
#5202, introduced by `56a4ab5234`, fixed by `cf88df2185` in 1.6.3). Whether a chain is hit depends on the byte size
of its params, so it gets likelier with every mix and channel. The module runs in two processes, and both crash:

- **kmixdeckd**, which loads chains at runtime. Measured with a probe that makes every stale read fault: 5 of 5
  runs SIGSEGV on the steps of `test_mx1` (add a mix, add a channel, set a cell).
- **The PipeWire server**, which loads the same chains from the login fragment `90-kmixdeck.conf`. With a
  fragment for 4 channels × 3 mixes: 5 of 5 server starts SIGSEGV. That takes all audio of the session down.

Without the probe the freed bytes are usually still intact, so the bug shows up rarely and as garbage, not as a
crash: CI runs 37378915814 and 37391578286 (`pw-dump` not valid UTF-8 in `test_cl5`/`test_cl7`). Ubuntu 26.04
ships 1.6.2 and has no update with the fix (checked 2026-10-06).

The owner's rule (2026-10-06): this has to be solved on kmixdeck's side, not by waiting for the distribution.

## Options

| Option | Verdict |
|---|---|
| Wait for Ubuntu to backport the fix | Rejected by the owner. Users on 26.04 crash until then. |
| Mark the affected tests as expected failures | Hides a crash that users have. Rejected. |
| Keep chains small enough that the builder never grows | Depends on byte sizes; the next mix or a longer slug brings it back. A bandage. |
| Run the filter graph inside the node (`audioconvert.filter-graph`, PipeWire ≥ 1.4) | Avoids the module, but audioconvert keeps the channel count: a cell chain turns 2 channels into 2 per mix. Does not fit ADR 0013. |
| Go back to one loopback per cell | That is what ADR 0013 replaced (listen backlog, file limit). |
| **Ship the fixed module under its own name and use it only on 1.6.0–1.6.2** | Chosen. |
| Raise the minimum PipeWire to 1.4 | Not needed: 1.0–1.5 are not affected. Also impossible as planned at first: the 1.6 module source does not build against 1.4 (`pw_loop_lock`, `SPA_KEY_AUDIO_LAYOUT` missing, measured on Debian trixie, 1.4.2). |

## Decision

1. **Vendored source.** `third_party/pipewire/module-filter-chain.c` is upstream's 1.6.3 file, byte for byte
   (sha256 in `third_party/pipewire/README.md`), MIT, with PipeWire's `COPYING`. It is never edited here. The 1.6.3
   file is the patched 1.6.2 file plus two changed lines in a documentation comment, nothing else.
2. **Built only where it is needed.** CMake builds `libpipewire-module-kmixdeck-filter-chain.so` only when
   `libpipewire-0.3` is 1.6.0–1.6.2, with the compile flags of PipeWire's own `meson.build` (`gnu11`,
   `-fvisibility=hidden`, `SPA_AUDIO_MAX_CHANNELS=128u`, …). The source needs nothing from PipeWire's build tree
   except `PACKAGE_VERSION`.
3. **Installed where PipeWire looks.** PipeWire finds modules only in `PIPEWIRE_MODULE_DIR` or in its compiled-in
   module directory (`pw_context_load_module` → `find_module`; absolute paths are not accepted since
   `6bc07dfe0e`). So the module installs into `pkg-config --variable=moduledir libpipewire-0.3`, like
   `pipewire-module-xrdp` does. For a prefix inside `$HOME` that directory is not writable; the module is then
   left out of the default install and `cmake --install build --component pipewire-module` (as root) adds it.
4. **One choice, used everywhere.** `pw::filterChainModule()` decides once per daemon start: the kmixdeck module
   if the linked libpipewire is 1.6.0–1.6.2 **and** the file is in PipeWire's module path, otherwise PipeWire's
   own. Runtime loads (`Mixer::ensureCellGraph`, `Mixer::applyFx`) and the login fragment
   (`Layout::toPipewireConf`) take the same name, so the server and the daemon never disagree. An affected
   PipeWire without the kmixdeck module logs a warning naming the bug and the missing file.
5. **A missing module must not cost the session its sound.** Fragment entries for the kmixdeck module carry
   `flags = [ nofail ]`. Without it, a fragment left behind after kmixdeck is uninstalled would make PipeWire
   refuse its whole configuration. PipeWire's own module keeps the old entry; it cannot be missing.
6. **Self-healing on upgrade.** When the distribution ships 1.6.3, the next daemon start chooses PipeWire's own
   module, finds that the fragment on disk differs, and rewrites it (the existing drift check in `reconcile()`).

## Consequences

- One more C target, built on three PipeWire versions only. Nothing changes on 1.0–1.5 or ≥ 1.6.3.
- The vendored file is frozen at 1.6.3. It does not get later filter-chain changes, which is fine: it is only used
  on 1.6.0–1.6.2, whose filter-graph plugin matches it.
- The probe that made the bug deterministic (`tests/tools/realloc-guard.c`) is part of the test suite now and is
  validated by the suite before it is trusted.
- Distribution packages install the module into the system module directory; it is owned by the kmixdeck package.

## Verification

`tests/integration/test_pipewire_5202.py` (DV-34), run with the probe preloaded only into the process under test:

- the probe turns a stale read into SIGSEGV and leaves a normal program alone;
- kmixdeckd survives building cell chains, and the module it maps is the one `filterChainModule()` names;
- the PipeWire server survives loading the fragment for 4 channels × 3 mixes, and maps the same module;
- a fragment that names the kmixdeck module while the file is missing still lets PipeWire start.

Seen red first: with PipeWire's own 1.6.2 module, the daemon and the server tests crash (5 of 5 each, see
Context).
