# ADR 0011 — Web frontend: one Python bridge on D-Bus, vanilla ES modules, snapshot + patches over one WebSocket

Status: accepted · 2026-09-18 · covers AR-8, AR-9

## Question

AR-8 asks for a browser frontend with the same capabilities as the KDE window, served by a small bridge that speaks
only D-Bus. Before writing it: how do comparable projects do this in 2025/26, and is there something ready-made?

## What we looked at (2026-09-18)

**Ready-made?** GitHub, "pipewire web ui / browser mixer / websocket":
- *PipeDeck* (MajorMaxdom, MIT, 0★, 2026-04): the closest thing — Python `server.py` (52 kB, `websockets` lib) +
  one `public/app.js` (135 kB, vanilla), PWA manifest. Talks to audio via `pactl`/`parec` subprocesses, binds
  `0.0.0.0`, **no authentication** ("only run on a trusted network"). Its shape confirms our plan; its audio path
  (PulseAudio shell-outs) and its security default are what we must not copy.
- *universal-live-mixer* (MIT, 1★): Rust backend on JACK + npm/Vite frontend on :5173. Prototype; polls meters
  over HTTP, WebSocket "planned".
- *helmsman* (MIT, C11/GTK4), *OLMS* (GPL, Ardour + Open Stage Control): not browser / not PipeWire-mixer.
- Nothing reusable as a component. A browser UI for *our* object model has to be written; the transport pattern
  can be borrowed.

**How the grown-ups do it:**
- *OBS obs-websocket v5*: one WebSocket, JSON envelope `{op, d}`, `Hello` → `Identify` → `Identified`, events via
  subscription bitmask, request/response with `requestId`. Auth = password never on the wire: SHA256(password+salt)
  → base64 → SHA256(that+challenge). "Show Connect Info" renders a QR code.
  (protocol.md, github.com/obsproject/obs-websocket)
- *Home Assistant*: one WebSocket, `subscribe_events`, full state then diffs; long-lived bearer tokens; binds
  `0.0.0.0` but demands a password and recommends a firewall. (developers.home-assistant.io/docs/api/websocket)
- *Syncthing*: GUI on `127.0.0.1:8384` by default; to expose on the LAN you change the address **and** the docs
  tell you to set a password + HTTPS, otherwise "you are opening up your installation for the world". REST via
  `X-API-Key`. (docs.syncthing.net/users/guilisten.html)

**Frontend stack (lit.dev, svelte.dev, MDN, doc.qt.io):**
- Svelte 5 needs a compiler. Lit 3 and Preact+htm run from ES modules without a build. Vanilla ES modules with an
  import map cost nothing and are enough for ~2 000 lines maintained by one person.
- State sync: snapshot on connect, then JSON Merge Patch (RFC 7386) for changes — trivially mergeable, no index
  arithmetic like RFC 6902. Meters: 30 floats × 25 Hz ≈ 12 kB/s as JSON — binary frames buy nothing at our size.
- Faders: pointer events with implicit capture, ≥ 44 px touch targets; meters drawn by `requestAnimationFrame`
  from the last received frame. `role="slider"` + `aria-value*` + arrow keys for accessibility.
- Browser: a plain `http://` origin on the LAN is an insecure context — no Service Worker, no
  `navigator.clipboard`. A mixer UI needs neither. `ws://` from an `http://` page is fine (MDN).
- Serving: Qt 6.8 has QHttpServer + QWebSocketServer in-process; Python `websockets`/`aiohttp` is the sidecar
  option. Both work. What decides it is where the bridge should live (next point).

## Decision

1. **Separate process `kmixdeck-web`, Python, `websockets` + stdlib `http.server`, D-Bus via `dbus-next`/`GLib`.**
   Not in the daemon: the daemon is the audio path (ADR 0002/AR-6) and must not grow a network listener. Not Qt:
   the bridge does no UI work and Python is already the language of our integration tests, the Stream Deck plugin
   and the CLI probes — one contributor can read the whole chain. Same stack shape as PipeDeck, which is evidence
   the shape carries a full mixer UI.
2. **One WebSocket, one JSON view model — the same one Window, Tray and Stream Deck already consume (AR-9).**
   `hello` → snapshot `{channels, mixes, cells, apps, devices, patchbay, layout}` → `patch` messages (RFC 7386
   merge patches, keyed by the D-Bus object path) → `meters` frames at the daemon's 25 Hz `{path: peak}`.
   Client → server: `set {path, property, value}`, `call {path, method, args}` — **allowlisted**: only the
   `org.kmixdeck1.*` interfaces, only the methods and writable properties the CLI exposes. No generic proxy.
3. **Security default = Syncthing's:** bind `127.0.0.1`. `--lan` binds all interfaces **and** requires a token
   (generated on first `--lan`, stored `0600` under `$XDG_CONFIG_HOME/kmixdeck/web-token`); the token goes in the
   URL fragment once and into `sessionStorage`; every WebSocket `hello` carries it; `kmixdeck web qr` prints the
   URL as a QR code in the terminal (OBS "Show Connect Info"). Plain HTTP — TLS on a home LAN is a self-signed
   certificate warning on every phone; the token guards what matters, and a Wi-Fi where sniffing is a concern is
   not the network this was written for. Documented, not hidden.
4. **Frontend: vanilla ES modules, no build step, no dependencies.** `web/index.html`, `web/app.js`,
   `web/*.js` components (mixer grid, patchbay, apps, fx), `web/style.css`. Served by the bridge as static files.
   If it outgrows that, Lit is a drop-in without a compiler; Svelte would be the point where we accept a build.
5. **Feature parity is enforced the same way as for the other frontends:** `test_frontends_sync.py` gets a
   headless-Chrome driver (`google-chrome --headless=new --remote-debugging-port` is present on the dev host; CI
   uses the same) and every probe the window answers, the web UI answers too. SoT rows count the web UI as a fourth
   frontend from the day the bridge lands.

## Consequences

- `pip install websockets dbus-next` (or the distro packages) becomes a runtime dependency of the *optional*
  web frontend only; the daemon and KDE window are unaffected.
- A second WebSocket consumer (e.g. an OBS dock, a phone widget) costs no daemon work: it speaks the same JSON.
- We do **not** implement OBS-style challenge/response: our token is per-install and only sent over the LAN we
  chose to expose to; the extra round trip buys nothing without TLS. Revisit if TLS ever comes.
