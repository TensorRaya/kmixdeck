# PipeWire filter-chain module, vendored (ADR 0015)

`module-filter-chain.c` is PipeWire's `src/modules/module-filter-chain.c` at tag **1.6.3**
(commit `cc3d0d1191266b263f6d0fa03fce1d1ef57151cc`), byte for byte:

```
sha256 84761fad518942d3ab9dc41c109a81a435b9439cc6236d226087bf42f8670d48  module-filter-chain.c
sha256 8909c319a7e27dbb33a15b9035f89ab3b7b2f6a12f8bcddc755206a8db1ada44  COPYING
```

License: MIT, see `COPYING` (PipeWire's own file, same tag). Do not edit the source here. If it ever has to
change, replace it with another upstream file and update the hashes.

## Why it is here

PipeWire 1.6.0, 1.6.1 and 1.6.2 ship a filter-chain module that reads freed memory while it connects its streams
(upstream #5202, fixed by `cf88df2185` in 1.6.3). kmixdeck's cell and effects chains are filter-chains, and both
kmixdeckd and the PipeWire server crash on it. Measurements: `docs/research/pipewire-5202-filter-chain.md`.

## How it is used

- CMake builds it as `libpipewire-module-kmixdeck-filter-chain.so` **only** when `libpipewire-0.3` is
  1.6.0–1.6.2. On every other version nothing here is compiled.
- It is installed into PipeWire's module directory (`pkg-config --variable=moduledir libpipewire-0.3`), because
  PipeWire loads modules from nowhere else.
- kmixdeckd uses it, and names it in the login fragment, only when the running libpipewire is 1.6.0–1.6.2 and
  the file is in PipeWire's module path (`src/pipewire/filterchain.cpp`). Otherwise PipeWire's own module is used.

The 1.6.3 file is the 1.6.2 file plus the fix; the only other difference is two lines in a documentation comment.
It builds against the 1.6.0–1.6.2 headers and needs nothing from PipeWire's build tree but `PACKAGE_VERSION`. It
does not build against 1.4 (`pw_loop_lock`, `SPA_KEY_AUDIO_LAYOUT` are missing there), which is fine, because 1.4
is not affected.
