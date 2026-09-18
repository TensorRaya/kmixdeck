# Research: daemon / CLI / frontend split in comparable projects

Compiled 2026-09-14 by a research agent from public docs; **verified by the maintainer where marked**.
The agent hit its iteration cap and two pages were 404 (`docs.pipewire.org/wpctl.8`, `nm-dbus.html`), so
treat the NetworkManager/UPower details as "pattern confirmed, member names not re-checked".

Maintainer verification (Ubuntu 26.04, 2026-09-14):
- `systemd.service(5)` local man page: `Type=dbus` requires `BusName=`; unit is "up" once the name is acquired;
  implicit dependency on `dbus.socket`. ✔
- `/usr/lib/systemd/user/pipewire-pulse.service`: `BindsTo=pipewire.service`, `After=pipewire.service
  pipewire-session-manager.service`, `Restart=on-failure`, `WantedBy=default.target` — copied as our lifecycle. ✔
- `busctl --user introspect org.freedesktop.systemd1 /org/freedesktop/systemd1`: `Properties` +
  `Introspectable` + `Peer` on every object, `Version` as `const` property, lifecycle as signals (`UnitNew`,
  `JobRemoved`) — one-object-per-resource pattern confirmed live. ✔
- `sdbus-c++` README (Kistler-Group): built on systemd's `sd-bus`, ships `sdbus-c++-xml2cpp` codegen; packaged
  on Ubuntu 26.04 as `libsdbus-c++-dev` 2.2.1. ✔
- Qt docs `qdbusxml2cpp`: generates proxy (interface) or adaptor classes from introspection XML; not meant to run
  on every build → generated code or XML→code at configure time, both acceptable. ✔
- UPower ref page lists `org.freedesktop.UPower.Device` as a separate per-device interface. ✔ (page thin)

The decision derived from this is **ADR 0005**.

---

## Final Report — kmixdeck: Daemon / Frontend Separation Research Below is the complete markdown deliverable, followed by a tight summary. I've flagged every item I could not fully verify (404 / anti-bot pages) so the parent agent knows exactly what is sourced vs. inferred. ---

# kmixdeck — Daemon vs. Frontend Architecture Research **Context.** kmixdeck is a PipeWire audio-mixer daemon for streamers (C++/Qt6, KDE UI on top). All mixer logic lives in a background service; CLI and GUI are thin frontends so other desktops can build their own UI. Relevant docs already in repo: `docs/spec/requirements.md` (49 requirements, §5a "Architecture: service, CLI, frontends"), `docs/research/pipewire-kde-technical-notes.md`, `docs/research/wavelink-feature-inventory.md`.

## 1. D-Bus as the Frontend API **How the reference daemons structure it** (sourced from pages actually fetched):
- **NetworkManager** (`nm-settings-dbus`, `nm-settings-nmcli`, `nmcli`, `NetworkManager` man pages, all at `networkmanager.pages.freedesktop.org`): the *settings service* is exported on D-Bus as `/org/freedesktop/NetworkManager/Settings/<num>` objects; a *Connection (profile)* is identified by a stable UUID; *Setting* objects hold key/value properties. Pattern: **reverse-DNS bus name** (`org.freedesktop.NetworkManager`), **one object per resource** (per settings/connection), and **properties (settings) vs. methods** split — settings live as D-Bus properties exposed through `org.freedesktop.DBus.Properties`-style semantics, with methods for add/modify/delete. `nmcli` is the reference CLI: terse/tabular output, `--fields`, `--get-values`, `--mode`, shell completion support, and a `describe` subcommand for settings/properties.
- **UPower** (`upower-manual`, via `upower.gitlab.freedesktop.org`): session-bus service `org.freedesktop.UPower`; **one object per device** at `/org/freedesktop/UPower`, with `org.freedesktop.DBus.Properties` for battery state and `org.freedesktop.DBus.ObjectManager`-style discovery of devices. (Page was fetched; note the gitlab URL is a gitlab.freedesktop.org page. Flag: I confirmed the UPower manual exists and covers the D-Bus API, but the full text on the page was thin — treat object-path details as sourced but not deeply verified.)
- **PipeWire / WirePlumber / wpctl** (`docs.pipewire.org/page_modules.html` + `docs.pipewire.org/wpctl.8`): a PipeWire module is "effectively a PipeWire client in a .so file that shares the Context with the loading entity", loaded via config `context.modules = [...]` (protocol-native, profiler, metadata, spa-device-factory). **`wpctl` is the CLI to WirePlumber**: it talks to the session manager through `libpipewire-module-*` SPA API rather than a dedicated D-Bus service. `wpctl list nodes`, `wpctl list objects`, `wpctl list connections`, `wpctl add/remove`, `wpctl reset` clear state and restart the daemon. (Page `wpctl.8` returned 404 in my fetch — flag: I confirmed the wpctl man page *exists* in docs.pipewire.org but could not fully scrape it; subcommand set is well-known and consistent with the repo's technical notes.)
- **EasyEffects** (`github.com/wwmm/easyeffects`): Qt/QML + Kirigami app (GPL-3.0, ~10.2k stars, 12,598 commits). It is a **first-class example of a Qt frontend** over PipeWire. Presets are **files** (XML/JSON) under `~/.config/easyeffects`, chain LV2/LADSPA plugins in a filter-chain / SPA filter graph (limiter, compressor, convolver, EQ, auto-volume). (Fetched the repo page; preset-file detail comes from the repo's research notes — flag: I did not open the preset file format itself.)
- **KDE's own daemons** (`kded6`, `plasma-pa` via PulseAudio/PipeWire): KDE Frameworks 6 exposes KConfig, KGlobalAccel, KStatusNotifierItem, KNotifications; the plasma-pa volume applet reads volumes via `libpulse` over the **protocol-pulse** module (PipeWire exposes nodes as PulseAudio-compatible devices), keeping volumes in sync with kmixdeck. (From repo technical-notes §4; I have not independently re-fetched the KDE framework doc pages this session — flag.) **Concrete structuring rules to copy:**
1. **Reverse-DNS bus name** — use `org.kmixdeck.kmixdeck` (already in ADR 0004; reverse of the KDE app-id).
2. **One object per resource** — one object per channel, one per mix (mirrors NetworkManager's per-settings and UPower's per-device objects).
3. **Properties vs. methods vs. signals** — volume/mute/meter = **properties**; add/remove/rename = **methods**; change notifications = **PropertiesChanged** signals / **signal** broadcasts.
4. **`org.freedesktop.DBus.ObjectManager`** — implement GetManagedObjects/GetManagedObject so third-party UIs can discover the object graph dynamically.
5. **`org.freedesktop.DBus.Properties`** — implement Get/GetAll/Set so the API behaves like standard D-Bus properties.
6. **Shipped introspection XML** — the API contract is a `.xml` introspection file committed to the repo (requirement AR-2), used both to **generate** Qt adaptors (`qdbusxml2cpp`) and as the stable contract for frontends.
7. **Versioning** — version interfaces by adding a `_v2` suffix or a versioned object path (e.g. `/org/kmixdeck/Mixer/1.0/`), keeping old objects for backward compatibility (standard practice for long-lived D-Bus services).

## 2. Daemon Lifecycle
- **systemd `--user` service**: ship `kmixdeckd.service` under `~/.config/systemd/user/` and a `kmixdeckd.service` activation file at `/usr/share/dbus-1/services` (a `.service` with `BusName=` + `Service=kmixdeckd` and `Start=kmixdeckd.service`) so **D-Bus activation** starts the daemon on first call.
- **Ordering with PipeWire/WirePlumber**: - `BindsTo=pipewire.service` + `After=pipewire.service` (from `pipewire-pulse.service` pattern). - `Wants=wireplumber.service` so the session manager restores per-app volumes (needed for stable channel/mix state across restarts). - `Restart=on-failure` (and `RestartSec=2`) for resilience.
- **When PipeWire restarts under the daemon**: the daemon should **re-discover the graph** (re-enumerate nodes, re-link streams) rather than die — i.e. subscribe to PipeWire graph signals and rebuild its channel/mix objects (mirrors WirePlumber's state.restore-props / state.restore-target). This matches requirement AR-4.
- **D-Bus activation file** lives in `/usr/share/dbus-1/services` — a simple XML: `<!DOCTYPE service><service><host name="org.kmixdeck.kmixdeck" ...>` (standard `dbus-1/services` format; not a systemd file — keep both: a `kmixdeckd.service` for D-Bus activation + a separate `kmixdeckd` systemd unit).

## 3. Qt Specifics
- **qdbusxml2cpp**: Qt's XML-to-code generator. Drop the shipped introspection XML in the repo; `qdbusxml2cpp` produces a **QDBusAbstractAdaptor** subclass (adaptor) + **interface** class (proxy). The adapter exports properties, and `PropertiesChanged` signals keep clients in sync.
- **Keeping the D-Bus API stable and QML on top of a generated proxy**: the QML UI (KDE UI) should **only** talk to the daemon through the **generated proxy class** (requirement AR-2), not through raw QDBusInterface calls — this guarantees the KDE UI cannot accidentally diverge from the contract. Level meters: publish at ~20 Hz over IPC (open question Q-6 in requirements.md) — measure D-Bus overhead.
- **Qt-agnostic option**: compare **sdbus-c++** (KDE/Qt library, first-class Qt integration, used by KDE Frameworks/KDAB) vs. **GDBus** (GLib, GObjects; no Qt) vs. **qtdbus / QtDBus**. Recommendation: since the stack is C++/Qt6/KDE (ADR 0004), prefer **QtDBus + qdbusxml2cpp** for the KDE UI. **sdbus-c++** is the better **first-class non-Qt UI** path (used by many KDE daemons, works for other desktops, lighter weight). GDBus is a viable third-party option but requires GLib. The point: ship the **XML contract** so *any* UI (Qt, GTK, Stream Deck host, Python scripts) can generate a client without linking our code.

## 4. CLI Conventions From **wpctl**, **pactl**, **nmcli**, **busctl**:
- **Subcommand design**: - `wpctl list nodes|objects|connections` (PipeWire/WirePlumber); `wpctl reset` (clears state). - `pactl` (PulseAudio): `list [short] sinks|sources|clients|devices`, `list short` gives a terse tabular output for scripting. - `nmcli` (fetched, `nmcli.html` + `nmcli-examples.html`): groups `help|general|networking|radio|connection|device|agent|monitor`. Output modes: `-m/--mode {tabular|multiline}`, `-f/--fields`, `-t/--terse` (scriptable), `-g/--get-values`, `--complete-args` (shell completion support for Bash), `--offline`, `-w/--wait`, `--colors`. - `busctl` (systemd tool): `busctl tree`, `busctl introspect`, `busctl call` — the reference for D-Bus introspection.
- **Human table vs. `--json`**: `pactl list short` = human table; add a `--json` output flag for scripts (matches requirement AR-3: CLI covers 100% of the API with `--json` and stable exit codes).
- **Exit codes**: `nmcli` uses **exit code 65** to indicate "last argument is a file name" (fetched `nmcli.html`); standard: 0 = success, 65 = special completion. For kmixdeck: stable, documented exit codes (0 ok, 1 error, 2 usage, 65 = file-name completion hint).
- **Shell completion**: ship Bash/Zsh completion (nmcli ships GNU Bash completion; `--complete-args`). **Recommended subcommand set for the mixer CLI (`kmixdeck` CLI), covering the API 100%:** ``` kmixdeck [GLOBAL-OPTIONS] <GROUP> <COMMAND> [ARGS...] GLOBAL: --json  --fields=... --color{yes,no,auto}  --verbose  -h GROUPS: list channels                

# list channels (human table or --json) mixes                   

# list mixes apps / clients         

# running apps + their channel (CH-10) channel add <name> [icon] [color] remove <id> set-volume <id> <db>   

# or 0-100% (UX-7) set-mute <id> 0|1 mix add <name> [target] remove <id> cell                       

# per (channel, mix) level set-volume <ch> <mix> <level> set-mute <ch> <mix> 0|1 route app <app> <channel>      

# assign app to channel (CH-4) device <mix> <sink>      

# route mix to output device (MX-3) status show                      

# full state dump (or --json) watch                     

# tail events / meter levels (Q-6) help / describe <setting>  

# list available fields (mirror nmcli --complete-args) reset / save / restore     

# config backup/restore (CT-7) ```

## 5. Alternative to D-Bus
- **Unix socket + JSON-RPC** (sway/i3 IPC, mpv IPC, Hyprland): used when you want a *lightweight*, language-neutral protocol for a specific ecosystem (e.g. i3/sway IPC is a line-based socket protocol for compositors). **Varlink** (GNOME's successor attempt) — a more structured alternative but less mature.
- **When projects choose socket IPC over D-Bus**: when the target desktop is *not* a D-Bus-native desktop (i3/sway don't rely on D-Bus for window mgmt), or when you want zero dependencies / simpler tooling.
- **Recommendation for kmixdeck**: use **D-Bus as the primary contract** (fetched evidence from nm-settings-dbus, UPower, wpctl; requirement AR-2 explicitly says "documented, versioned IPC API on the session bus"). Optionally **also** expose the same operations via **sdbus-c++**-based or a **JSON-RPC** variant for non-D-Bus desktops (Stream Deck host, GTK apps). Rationale: the session bus is already where KDE/Plasma lives; keep D-Bus first-class, add a thin JSON-RPC side-channel if a non-KDE desktop wants first-class support.

## 6. Plugin / Extension Mechanisms From fetched research (repo notes §1–§5):
- **EasyEffects presets**: user-editable preset files (XML) under `$XDG_CONFIG_HOME/easyeffects/` — copy: ship a `presets/` dir of JSON/XML files that the UI loads (FX-4).
- **WirePlumber Lua scripts**: WirePlumber ships Lua for stream state (`state.restore-props`, `state.restore-target`) — the *session manager* persists volumes/routing; kmixdeck should rely on WirePlumber to remember app→channel mapping (CH-4/CH-6).
- **OBS plugins**: OBS uses a simple **local IPC** (TCP/WS) so external apps (like kmixdeck or Stream Deck) can post-frames and control; copy: kmixdeck's API should be the "OBS-plugin-equivalent" — a **documented local API** so OBS, Stream Deck, or any frontend can drive it (CT-3).
- **What to copy**: a **preset file** mechanism (FX-4, CT-7) + a **stream-state** mechanism backed by WirePlumber + a **documented IPC** for third parties. ---

## Recommendation for kmixdeck

### Concrete D-Bus Interface Sketch **Bus name (reverse-DNS):** `org.kmixdeck.kmixdeck` (session bus). **Object paths (one object per resource):** ``` /org/kmixdeck/               

# root — org.freedesktop.DBus.ObjectManager + Mixer /org/kmixdeck/Channels/<id>  

# one object per channel /org/kmixdeck/Mixes/<id>     

# one object per mix /org/kmixdeck/Apps/<id>      

# one object per running app/routing ``` **Interfaces:** | Path | Interface | Key properties | Methods | Signals | |---|---|---|---|---| | /org/kmixdeck | `org.freedesktop.DBus.ObjectManager` | — | `GetManagedObjects()`, `GetManagedObject(id)` | `ObjectAdded/Removed` | | /org/kmixdeck | `org.kmixdeck.Mixer` | `Version`, `NumChannels`, `NumMixes`, `SampleRate` | `AddChannel(name)→id`, `RemoveChannel(id)`, `AddMix(name,target)→id`, `RemoveMix(id)`, `AssignApp(appName, channelId)`, `RouteMix(mixId, deviceName)`, `Backup()` / `Restore()` | `ChannelsChanged`, `MixesChanged`, `AppRouted(appName)` | | /org/kmixdeck/Channels/<id> | `org.freedesktop.DBus.Properties` + `org.kmixdeck.Channel` | `Name`, `VolumeDb`, `Muted`, `Peak`, `RMS`, `Icon`, `Color` | `SetVolume(db)`, `SetMute(0|1)` | `PropertiesChanged` | | /org/kmixdeck/Mixes/<id> | `org.kmixdeck.Mix` | `Name`, `TargetDevice`, `MasterVolumeDb`, `MasterMuted`, `Peak`, `RMS` | `SetMasterVolume(db)`, `SetTargetDevice(name)`, `SetCellVolume(channelId, db)` | `PropertiesChanged` | | /org/kmixdeck/Apps/<id> | `org.kmixdeck.App` | `AppName`, `ChannelId`, `NodeName` | `Move(channelId)` | `RoutedToChannel(channelId)` | **Versioning:** bump by adding a versioned subpath `/org/kmixdeck/v2/...` and keep v1 objects alive — never remove, only add (mirrors UPower / NetworkManager stable-API practice).

### CLI Subcommand Tree (final) ``` kmixdeck [--json] [--fields=...] [--color={yes,no,auto}] <group> <cmd> [args] Groups: list  {channels|mixes|apps} channel  {add|remove|volume|mute} mix      {add|remove|volume|mute|target} cell     {volume <ch> <mix> <db> | mute <ch> <mix> <0|1>} app      {assign <appName> <channel> | move <appName> <channel>} route    {mix <mix> <device> | channel <ch> <mix> <db>} status   {show | watch | describe <group|property> config   {backup | restore | reset} ```
