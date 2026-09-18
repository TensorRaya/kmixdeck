# The web UI — kmixdeck in a browser

`kmixdeck-web` serves the mixer to any browser on your machine or your LAN: a phone next to the microphone, a tablet on the
desk, a laptop in another room — or the desk PC itself when it runs headless (ADR 0011, AR-8). It has the same features as
the window: mixer grid with live meters, apps, patchbay, effects, export/import, undo.

![web UI](screenshots/web-mixer.png)

## Start it

```sh
kmixdeck-web                 # http://127.0.0.1:7420 — this machine only, no token
kmixdeck-web --lan           # every interface, token required; prints the URL with the token to open on the phone
kmixdeck-web --port 8080     # another port
```

For the phone use case, enable it once and forget it:

```sh
systemctl --user enable --now kmixdeck-web      # LAN mode; disabled by default because it opens a port
```

**Requirements:** the kmixdeck service running on the same machine (it is started on demand), Python 3 with `python3-gi`
and `python3-websockets ≥ 11`. If one is missing the bridge says which package to install and exits.

## Security model

- **Localhost by default.** Without `--lan` the bridge binds `127.0.0.1` and asks for no token.
- **Token on the LAN.** `--lan` binds all interfaces and requires the token from `~/.config/kmixdeck/web-token` (created
  on first start, mode 0600). It is compared in constant time. The token travels in the URL **fragment** — it is never sent
  to the server as HTTP, only inside the WebSocket hello — and is kept in `sessionStorage`, so it dies with the tab.
- **Same-origin only.** A WebSocket upgrade from another origin (a page you visited, a `file://` page) is refused with
  403 — a tab from elsewhere cannot drive your mixer even on localhost.
- **Allowlist.** The bridge forwards only the D-Bus methods and properties named in the daemon's own introspection; anything
  else is refused. It never touches PipeWire itself (AR-6).
- **Static files are jailed** to the UI directory; frames are capped at 1 MiB.
- Anyone with the token controls your audio. Treat it like a password: `--lan` prints a warning for that reason.

## What is on the page

**Mixer** (default tab). Channels are rows, mixes are columns, like the window. Each row: icon, name, the channel's source
line (click → hardware-input picker), mute, then one **cell** per mix — a fader with the live meter in its track, a mute and a
link button. Each column header: mix name, the output picker (tick several outputs at once), master fader with its meter,
mute, **listen** (hold to audition), FX. The ⋮ menus on rows and columns hold rename, icon, colour, move, duplicate,
remove. **I hear:** at the top shows which mix goes to your listening device — one tap switches. Ctrl+Z undoes the last
removal.

**Apps.** Every application stream with its live level and running state; tap a channel chip to assign, tap again to
remove. Several chips = the app plays into several channels (first = primary).

**Patchbay.** Three columns — sources → channels → mixes → outputs — with every wire drawn. Drag from a jack to a jack to
create a wire; click a wire for per-wire trim, mute and remove. Devices that are unplugged stay in place, greyed.

**FX drawer** (the FX button on a row or column). The chain of the selected channel or mix: catalog, presets, bypass per
effect, live sliders that change the running node without a reload.

**Export / Import** (top right, or Ctrl+S). Export downloads the daemon's backup document (layout + every level). Import takes
such a file, asks once, and replaces the running layout; a broken file is refused with a message and nothing changes.

**Reconnect.** If the bridge or the daemon goes away, the page says so and reconnects by itself (backoff 0.5 → 8 s); the
first frame after reconnect is a full snapshot, so what you see is never stale.

## How it talks to the daemon

One WebSocket. The first frame from the client is `{"op":"hello","token":"…"}`; the server answers with a full
`{"op":"snapshot","state":{…}}` of the object tree and then streams `{"op":"patch",…}` (RFC 7386 merge patches) as
properties change, plus `{"op":"meters","peaks":{…}}` at the daemon's 25 Hz while any meter is visible. The client sends
`{"op":"set", path, iface, prop, value}` and `{"op":"call", path, iface, method, args}`; a refused write comes back as
`{"op":"error", …}` and the UI shows it as a toast. Latency over WLAN in the same house: median 5 ms, p95 11 ms (measured),
so faders feel direct. No audio crosses the wire — only control and meters.

The UI is plain ES modules in `web/static/` — no build step, no framework. `app.js` (tabs, undo, export/import),
`client.js` (socket, state, patches), `mixer.js`, `apps.js`, `patchbay.js`, `fx.js`, `widgets.js`. Every interactive element
carries a `data-probe` attribute; that is what the integration tests drive.

## Proven by

`tests/integration/test_web.py` — 13 tests in headless Chrome, every step checked at the CLI: bridge + allowlist + token +
origin, meters at 25 Hz, static-file jail, mix end-to-end (add → rename → output → fader → mute → link → remove → undo), app
chips, patchbay menu and drag, FX drawer, export/import round trip, reconnect. The six `tier:core` rows of the requirements
are additionally proven in the browser by `test_frontends_sync.py` next to CLI, window and tray.
