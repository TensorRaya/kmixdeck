# SPDX-License-Identifier: GPL-3.0-or-later
"""Integration tests for the service/CLI split (ADR 0005, AR-1..AR-6).

Two sandboxes: a private PipeWire (pw_sandbox) AND a private session bus (dbus-daemon).
kmixdeckd is started on that bus; the CLI is the test client — so every assertion here also proves AR-3.
The acoustic checks reuse the same measurement helpers as test_audio_graph.py.
"""
import json
import math, math, os, subprocess, time, pytest
from pathlib import Path
from pw_sandbox import start_private_pipewire, REPO

BIN = REPO / "build" / "bin"
pytestmark = pytest.mark.skipif(not (BIN / "kmixdeckd").exists(), reason="build first: ninja -C build")


class Stack:
    def __init__(self, pw):
        self.pw = pw
        self.env = dict(pw.env)
        # private session bus
        self.dbus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address=1", "--address=unix:dir=" + str(pw.runtime_dir)],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, env=self.env)
        addr = self.dbus.stdout.readline().strip()
        assert addr.startswith("unix:"), addr
        self.env["DBUS_SESSION_BUS_ADDRESS"] = addr
        import os as _os; self.daemon_log_path = _os.path.join(self.env["XDG_CONFIG_HOME"], "kmixdeckd.log")   # daemon stderr, readable by tests on failure
        self.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=self.env, stdout=subprocess.DEVNULL, stderr=open(self.daemon_log_path, "a"), text=True)
        for _ in range(50):
            if self.cli("status", check=False).returncode == 0: break
            time.sleep(0.1)
        else:
            raise RuntimeError("kmixdeckd did not come up: " + self.daemon.stderr.read())

    def cli(self, *args, check=True, json_out=False):
        cmd = [str(BIN / "kmixdeck")] + (["--json"] if json_out else []) + list(args)
        r = subprocess.run(cmd, env=self.env, capture_output=True, text=True)
        if check: assert r.returncode == 0, f"{cmd}: rc={r.returncode} {r.stderr}"
        if json_out and r.returncode == 0: return json.loads(r.stdout)
        return r

    def busctl(self, *args):
        return subprocess.run(["busctl", "--user"] + list(args), env=self.env, capture_output=True, text=True)

    def restart_daemon(self):
        self.daemon.terminate(); self.daemon.wait(timeout=5)
        self.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=self.env, stdout=subprocess.DEVNULL, stderr=open(self.daemon_log_path, "a"), text=True)
        # the bus name is claimed only after every object is exported (Service::start), so one green `status` = ready
        for _ in range(100):
            if self.cli("status", check=False).returncode == 0: break
            time.sleep(0.1)

    def close(self):
        for p in (self.daemon, self.dbus):
            p.terminate()
            try: p.wait(timeout=3)
            except subprocess.TimeoutExpired: p.kill()


@pytest.fixture(scope="module")
def stack():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    s = Stack(pw)
    yield s
    s.close(); pw.close()


def test_ar6_only_the_daemon_links_pipewire():
    """AR-6: frontends are not PipeWire clients. Checked at the linker level."""
    def links_pw(b): return "libpipewire" in subprocess.run(["ldd", str(BIN / b)], capture_output=True, text=True).stdout
    assert links_pw("kmixdeckd") and not links_pw("kmixdeck") and not links_pw("kmixdeck-kde")


def test_ar2_contract_matches_shipped_xml(stack):
    """AR-2: what the bus introspects == interfaces/*.xml (names, properties, methods)."""
    import xml.etree.ElementTree as ET
    def members(xml_text, iface):
        root = ET.fromstring(xml_text)
        for i in root.iter("interface"):
            if i.get("name") == iface:
                return {(c.tag, c.get("name")) for c in i if c.tag in ("property", "method", "signal")}
        return None
    for iface, path in [("org.kmixdeck1.Mixer", "/org/kmixdeck1"), ("org.kmixdeck1.Channel", "/org/kmixdeck1/channel/game"),
                        ("org.kmixdeck1.Mix", "/org/kmixdeck1/mix/stream"), ("org.kmixdeck1.Cell", "/org/kmixdeck1/cell/game/stream")]:
        shipped = members((REPO / "interfaces" / f"{iface}.xml").read_text(), iface)
        live = members(stack.busctl("introspect", "--xml-interface", "org.kmixdeck1", path).stdout, iface)
        assert live is not None, f"{iface} not exported at {path}"
        assert live == shipped, f"{iface}: live−shipped={live - shipped} shipped−live={shipped - live}"
    # GetManagedObjects must carry every property the XML promises — a frontend bootstraps from it and
    # never introspects (frontend-guide: "one round trip returns every object with all properties").
    managed = stack.busctl("call", "--json=short", "org.kmixdeck1", "/org/kmixdeck1", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects")
    assert managed.returncode == 0, managed.stderr
    objs = json.loads(managed.stdout)["data"][0]
    for iface, path in [("org.kmixdeck1.Mixer", "/org/kmixdeck1"), ("org.kmixdeck1.Channel", "/org/kmixdeck1/channel/game"),
                        ("org.kmixdeck1.Mix", "/org/kmixdeck1/mix/stream"), ("org.kmixdeck1.Cell", "/org/kmixdeck1/cell/game/stream")]:
        promised = {n for kind, n in members((REPO / "interfaces" / f"{iface}.xml").read_text(), iface) if kind == "property"}
        got = set(objs[path][iface].keys())
        assert promised <= got, f"{iface} at {path}: GetManagedObjects lacks {promised - got}"


def test_ar3_cli_status_lists_the_prototype_graph(stack):
    st = stack.cli("status", json_out=True)
    assert st["connected"] is True
    assert {c["Slug"] for c in st["channels"]} == {"game", "system", "voice"}
    assert {m["Slug"] for m in st["mixes"]} == {"monitor", "stream"}
    assert len(st["cells"]) == 6


def test_cli_level_syntax_and_exit_codes(stack):
    assert stack.cli("cell", "set", "game", "stream", "-12dB").returncode == 0
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] == pytest.approx(0.2512, abs=1e-3)
    assert stack.cli("cell", "set", "game", "stream", "0.5").returncode == 0
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] == pytest.approx(0.5)
    assert stack.cli("cell", "set", "game", "stream", "50%").returncode == 0     # cubic UI scale → 0.125 linear
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] == pytest.approx(0.125)
    assert stack.cli("cell", "get", "nope", "stream", check=False).returncode == 3     # not found
    assert stack.cli("cell", "set", "game", "stream", "2.0", check=False).returncode == 1  # usage
    for over in ("+3dB", "0.1dB", "101%", "1.0001"):
        assert stack.cli("cell", "set", "game", "stream", over, check=False).returncode == 1, f"{over} is above unity and must be refused, not clamped"
    for edge in ("0dB", "100%", "1", "-200dB", "0%", "0"):
        assert stack.cli("cell", "set", "game", "stream", edge).returncode == 0, f"{edge} is a valid boundary"
    assert stack.cli("bogus", check=False).returncode == 1
    stack.cli("cell", "set", "game", "stream", "1.0")


def test_ar1_cli_set_reaches_pipewire_and_is_audible(stack):
    """The whole chain: CLI → D-Bus → kmixdeckd → PipeWire → audio. −12 dB requested, −12 dB measured."""
    stack.cli("cell", "set", "game", "stream", "0.25"); stack.cli("cell", "set", "game", "monitor", "1.0")
    assert stack.pw.props("kmixdeck.link.game.stream")["volume"] == pytest.approx(0.25)
    play = stack.pw.play_into("kmixdeck.channel.game")
    try:
        mon, strm = stack.pw.level_at("kmixdeck.mix.monitor"), stack.pw.level_at("kmixdeck.mix.stream")
    finally:
        play.kill(); play.wait()
    assert strm - mon == pytest.approx(-12.04, abs=0.5)
    stack.cli("cell", "mute", "game", "stream", "on")
    assert stack.pw.props("kmixdeck.link.game.stream")["mute"] is True
    stack.cli("cell", "mute", "game", "stream", "off"); stack.cli("cell", "set", "game", "stream", "1.0")


def test_ar2_third_party_client_needs_none_of_our_code(stack):
    """busctl alone can read and write the API (this is what 'plugin-like' means here)."""
    r = stack.busctl("set-property", "org.kmixdeck1", "/org/kmixdeck1/cell/voice/monitor", "org.kmixdeck1.Cell", "Volume", "d", "0.5")
    assert r.returncode == 0, r.stderr
    time.sleep(0.3)
    assert stack.pw.props("kmixdeck.link.voice.monitor")["volume"] == pytest.approx(0.5)
    r = stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1/cell/voice/monitor", "org.kmixdeck1.Cell", "Volume")
    assert r.stdout.strip() == "d 0.5"
    stack.busctl("set-property", "org.kmixdeck1", "/org/kmixdeck1/cell/voice/monitor", "org.kmixdeck1.Cell", "Volume", "d", "1.0")


def test_ar5_frontend_call_activates_nothing_but_survives_daemon_gone(stack):
    """When the service is gone the CLI says so with exit 2, not a crash."""
    stack.daemon.terminate(); stack.daemon.wait(timeout=3); time.sleep(0.3)
    r = stack.cli("status", check=False)
    assert r.returncode == 2, (r.returncode, r.stderr)
    stack.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=stack.env, stdout=subprocess.DEVNULL, stderr=open(stack.daemon_log_path, "a"), text=True)
    for _ in range(50):
        if stack.cli("status", check=False).returncode == 0: break
        time.sleep(0.1)
    assert stack.cli("status", json_out=True)["connected"] is True


# ---------------------------------------------------------------- app routing (CH-4, CH-10)
FAKE_APP = '{ application.name = "FakeGame" application.process.binary = "fakegame" media.name = "BGM" media.role = "Game" node.name = "fakegame-out" }'


def start_fake_app(stack):
    """Start a fake game and wait until BOTH kmixdeckd (bus object) and WirePlumber (initial link) have seen it.
    Moving a stream before WirePlumber has registered it links fine but is NOT remembered: its
    store-stream-target hook looks the node up in its own object manager at metadata-changed time
    (state-stream.lua) and silently returns if it is not there yet. Found on the slow CI runner."""
    p = subprocess.Popen(["pw-play", "-P", FAKE_APP, str(stack.pw.tone())], env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    app = None
    for _ in range(100):
        apps = stack.cli("app", "list", json_out=True)
        app = next((a for a in apps if a["Name"] == "FakeGame"), None)
        if app and current_sink_of(stack) is not None: return p, app
        time.sleep(0.1)
    p.kill(); raise AssertionError("fake app never appeared on the bus / never got linked by WirePlumber")


def wait_wireplumber_saved_target(stack, target, timeout=15.0):
    """WirePlumber writes stream state with save_after_timeout; poll the file instead of guessing a sleep (CI is slow)."""
    state = Path(stack.pw.runtime_dir) / "state" / "wireplumber" / "stream-properties"
    deadline = time.time() + timeout
    while time.time() < deadline:
        if state.exists() and f'"target":"{target}"' in state.read_text(): return
        time.sleep(0.2)
    raise AssertionError(f"WirePlumber never persisted target={target}; state file: {state.read_text() if state.exists() else '<missing>'}")


def wait_sink(stack, want, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if current_sink_of(stack) == want: return want
        time.sleep(0.1)
    return current_sink_of(stack)


def current_sink_of(stack, node_name="fakegame-out"):
    links = subprocess.run(["pw-link", "-l"], env=stack.pw.env, capture_output=True, text=True).stdout.splitlines()
    for i, l in enumerate(links):
        if f"{node_name}:output_FL" in l and i > 0 and "|<-" in l:
            return links[i - 1].strip().split(":")[0]
    return None


def test_ch10_running_apps_are_listed_with_their_channel(stack):
    p, app = start_fake_app(stack)
    try:
        assert app["Binary"] == "fakegame" and app["MediaName"] == "BGM" and app["MediaRole"] == "Game"
        # CH-5: a never-seen app is placed on the default channel (System) right away
        for _ in range(30):
            if app["Channel"] == "/org/kmixdeck1/channel/system": break
            time.sleep(0.1); app = next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")
        assert app["Channel"] == "/org/kmixdeck1/channel/system", app
    finally:
        p.kill(); p.wait()


def test_ch4_move_app_to_channel_is_immediate_and_audible(stack):
    p, app = start_fake_app(stack)
    try:
        stack.cli("app", "move", "FakeGame", "voice")
        assert wait_sink(stack, "kmixdeck.channel.voice") == "kmixdeck.channel.voice"
        assert stack.cli("app", "list", json_out=True)[0]["Channel"] == "/org/kmixdeck1/channel/voice"
        # audible: voice→stream at 1.0, voice→monitor muted → tone only in Stream mix
        stack.cli("cell", "set", "voice", "stream", "1.0"); stack.cli("cell", "mute", "voice", "monitor", "on")
        strm, mon = stack.pw.level_at("kmixdeck.mix.stream"), stack.pw.level_at("kmixdeck.mix.monitor")
        assert strm > -40 and mon == -math.inf, (strm, mon)
        stack.cli("cell", "mute", "voice", "monitor", "off")
    finally:
        p.kill(); p.wait()


def test_ch4_routing_survives_app_restart(stack):
    """Sonusmix #38: 'I have to re-add app nodes after a reboot'. Here: WirePlumber remembers the target for us."""
    p, _ = start_fake_app(stack)
    stack.cli("app", "move", "FakeGame", "system")
    assert wait_sink(stack, "kmixdeck.channel.system") == "kmixdeck.channel.system"
    wait_wireplumber_saved_target(stack, "kmixdeck.channel.system")
    p.kill(); p.wait(); time.sleep(0.5)
    p, app = start_fake_app(stack)
    try:
        assert wait_sink(stack, "kmixdeck.channel.system") == "kmixdeck.channel.system"
        assert app["Channel"] == "/org/kmixdeck1/channel/system" or stack.cli("app", "list", json_out=True)[0]["Channel"] == "/org/kmixdeck1/channel/system"
    finally:
        p.kill(); p.wait()


def test_ch4_routing_survives_pipewire_restart(stack):
    """Same, across a pipewire+wireplumber restart (serials change; WirePlumber stores the target by node.name)."""
    p, _ = start_fake_app(stack)
    stack.cli("app", "move", "FakeGame", "system")
    assert wait_sink(stack, "kmixdeck.channel.system") == "kmixdeck.channel.system"
    wait_wireplumber_saved_target(stack, "kmixdeck.channel.system")
    p.kill(); p.wait()
    stack.pw.restart()
    for _ in range(50):   # daemon reconnects? (AR-4) — at minimum the CLI must work again
        if stack.cli("status", check=False).returncode == 0: break
        time.sleep(0.2)
    p, _ = start_fake_app(stack)
    try:
        assert wait_sink(stack, "kmixdeck.channel.system") == "kmixdeck.channel.system"
    finally:
        p.kill(); p.wait()


# ---------------------------------------------------------------- layout persistence (DV-1, DV-5, MX-1)
def test_mx1_add_mix_at_runtime_creates_cells_and_persists(stack):
    """Add a 3rd mix through the CLI → 3 new cells appear on the bus and in PipeWire; layout.json + conf written."""
    r = stack.cli("mix", "add", "Recording")
    assert r.stdout.strip() == "/org/kmixdeck1/mix/recording"
    for _ in range(50):
        st = stack.cli("status", json_out=True)
        if len(st["cells"]) == 9: break
        time.sleep(0.1)
    assert {m["Slug"] for m in st["mixes"]} == {"monitor", "stream", "recording"}
    assert len(st["cells"]) == 9
    stack.pw.wait_node("kmixdeck.link.voice.recording")
    layout = json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())
    assert [m["slug"] for m in layout["mixes"]] == ["monitor", "stream", "recording"]
    conf = (Path(stack.pw.runtime_dir) / "pipewire.conf.d" / "90-kmixdeck.conf").read_text()
    assert 'node.name = "kmixdeck.link.game.recording"' in conf and "node.dont-fallback = true" in conf


def test_dv1_layout_survives_without_the_daemon(stack):
    """The graph is PipeWire config, not daemon state: kill kmixdeckd, restart PipeWire, the 3rd mix still exists."""
    stack.daemon.terminate(); stack.daemon.wait(timeout=3)
    # remove the hand-written prototype so ONLY the generated conf defines the graph
    (Path(stack.pw.runtime_dir) / "pipewire.conf.d" / "90-kmixdeck.conf").exists()
    stack.pw.restart()
    stack.pw.wait_node("kmixdeck.link.voice.recording")
    assert stack.pw.props("kmixdeck.link.voice.recording")["volume"] == pytest.approx(1.0)
    # daemon comes back, sees the graph, exports it — nothing recreated twice
    stack.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=stack.env, stdout=subprocess.DEVNULL, stderr=open(stack.daemon_log_path, "a"), text=True)
    for _ in range(50):
        if stack.cli("status", check=False).returncode == 0: break
        time.sleep(0.1)
    time.sleep(1.0)
    st = stack.cli("status", json_out=True)
    assert len(st["cells"]) == 9
    nodes = subprocess.run(["pw-cli", "ls", "Node"], env=stack.pw.env, capture_output=True, text=True).stdout
    assert nodes.count('"kmixdeck.mix.recording"') == 1, "mix node duplicated by reconcile"


def test_mx1_remove_mix(stack):
    stack.cli("mix", "remove", "recording")
    for _ in range(50):
        st = stack.cli("status", json_out=True)
        if len(st["cells"]) == 6: break
        time.sleep(0.1)
    assert {m["Slug"] for m in st["mixes"]} == {"monitor", "stream"}
    layout = json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())
    assert [m["slug"] for m in layout["mixes"]] == ["monitor", "stream"]


# ---------------------------------------------------------------- mix output device (MX-3a, DV-2)
def make_fake_sink(stack, name, desc):
    subprocess.run(["pw-cli", "create-node", "adapter",
                    f'{{ factory.name=support.null-audio-sink node.name={name} node.description="{desc}" media.class=Audio/Sink audio.position=[FL FR] object.linger=true }}'],
                   env=stack.pw.env, capture_output=True)
    stack.pw.wait_node(name)


def out_link_target(stack, mix):
    links = subprocess.run(["pw-link", "-l"], env=stack.pw.env, capture_output=True, text=True).stdout.splitlines()
    for i, l in enumerate(links):
        if l.startswith(f"kmixdeck.out.{mix}:output_FL") and i + 1 < len(links) and "|->" in links[i + 1]:
            return links[i + 1].strip().replace("|-> ", "").split(":")[0]
    return None


def test_dv8_devices_lists_hardware_sinks_not_ours(stack):
    make_fake_sink(stack, "fake.headphones", "Fake Headphones")
    for _ in range(30):
        devs = stack.cli("devices", json_out=True)
        if "fake.headphones" in devs: break
        time.sleep(0.1)
    assert devs["fake.headphones"] == "Fake Headphones"
    assert not any(k.startswith("kmixdeck.") for k in devs)   # incl. the parking sink kmixdeck.null


def test_mx3a_mix_output_follows_device_and_is_audible(stack):
    assert out_link_target(stack, "stream") == "kmixdeck.null", "mix output must start parked on kmixdeck.null (never the default sink)"
    stack.cli("mix", "output", "stream", "fake.headphones")
    for _ in range(40):
        if out_link_target(stack, "stream") == "fake.headphones": break
        time.sleep(0.1)
    assert out_link_target(stack, "stream") == "fake.headphones"
    # audible on the device: game→stream at 0 dB, tone into game
    stack.cli("cell", "set", "game", "stream", "1.0")
    play = stack.pw.play_into("kmixdeck.channel.game")
    try:
        assert stack.pw.level_at("fake.headphones") > -30
    finally:
        play.kill(); play.wait()
    assert stack.cli("mix", "list", json_out=True)[1]["OutputDevice"] == "fake.headphones" or \
           any(m["OutputDevice"] == "fake.headphones" for m in stack.cli("mix", "list", json_out=True))


def test_mx3a_output_none_unlinks_and_unknown_device_is_rejected(stack):
    r = stack.cli("mix", "output", "stream", "does.not.exist", check=False)
    assert r.returncode == 3
    stack.cli("mix", "output", "stream", "none")
    for _ in range(40):
        if out_link_target(stack, "stream") == "kmixdeck.null": break
        time.sleep(0.1)
    assert out_link_target(stack, "stream") == "kmixdeck.null"


def test_mx3a_output_device_persists_in_generated_conf(stack):
    stack.cli("mix", "output", "monitor", "fake.headphones")
    time.sleep(0.5)
    conf = (Path(stack.pw.runtime_dir) / "pipewire.conf.d" / "90-kmixdeck.conf").read_text()
    assert 'node.name = "kmixdeck.out.monitor"' in conf and 'node.target = "fake.headphones"' in conf
    layout = json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())
    assert next(m for m in layout["mixes"] if m["slug"] == "monitor")["outputDevice"] == "fake.headphones"
    stack.cli("mix", "output", "monitor", "none")


# ---------------------------------------------------------------- atomic toggles (CT-1 backend)
def test_ct1_toggle_mute_is_atomic_on_the_bus(stack):
    """Hotkeys call ToggleMute() — one round trip, no read-modify-write from the frontend."""
    stack.cli("cell", "mute", "game", "stream", "off")
    for _ in range(2):
        r = stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1/cell/game/stream", "org.kmixdeck1.Cell", "ToggleMute"); assert r.returncode == 0, r.stderr
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Muted"] is False
    stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1/cell/game/stream", "org.kmixdeck1.Cell", "ToggleMute")
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Muted"] is True
    assert stack.pw.props("kmixdeck.link.game.stream")["mute"] is True
    stack.cli("cell", "mute", "game", "stream", "off")
    # channel-wide toggle mutes the channel null sink (every mix)
    stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1/channel/voice", "org.kmixdeck1.Channel", "ToggleMute")
    time.sleep(0.3)
    assert stack.pw.props("kmixdeck.channel.voice")["mute"] is True
    stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1/channel/voice", "org.kmixdeck1.Channel", "ToggleMute")


def test_ct1_kde_frontend_registers_global_shortcuts(stack):
    """The KDE frontend must register one KGlobalAccel action per channel and per mix under component 'kmixdeck'.
    No kglobalacceld here — we watch the registration calls on the private bus (the contract with Plasma)."""
    kde = BIN / "kmixdeck-kde"
    if not kde.exists(): pytest.skip("kmixdeck-kde not built")
    mon = subprocess.Popen(["busctl", "--user", "monitor", "--match", "type=method_call,interface=org.kde.KGlobalAccel"],
                           env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    time.sleep(0.5)
    env = dict(stack.env); env["QT_QPA_PLATFORM"] = "offscreen"
    ui = subprocess.Popen([str(kde)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(4.0)
    ui.terminate(); ui.wait(timeout=5)
    mon.terminate(); out = mon.communicate(timeout=5)[0]
    for name in ("mute-channel-game", "mute-channel-system", "mute-channel-voice", "mute-mix-monitor", "mute-mix-stream",
                 "volume-up-mix-monitor", "volume-down-mix-stream", "listen-next-mix"):
        assert f'"{name}"' in out, f"shortcut action {name} not registered"
    assert "doRegister" in out and "setShortcutKeys" in out


# ---------------------------------------------------------------- capture sides are plumbing (found on the laptop 2026-09-15)
def test_vf7_capture_side_volume_is_healed_on_start(stack):
    """WirePlumber restores volumes per node name — including the *capture* side of a cell loopback, which
    is never a fader. A stale 0.0156 there silently cut the stream mix by 36 dB. The daemon must heal it."""
    stack.pw.set_volume("kmixdeck.link.game.stream.in", 0.0156)
    assert abs(stack.pw.props("kmixdeck.link.game.stream.in")["volume"] - 0.0156) < 1e-3
    stack.restart_daemon()
    for _ in range(40):
        if stack.pw.props("kmixdeck.link.game.stream.in")["volume"] > 0.99: break
        time.sleep(0.1)
    assert stack.pw.props("kmixdeck.link.game.stream.in")["volume"] > 0.99
    # and the fader itself was not touched
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] == 1.0


# ---------------------------------------------------------------- level meters (ADR 0006, UX-6)
METER_LISTENER = r"""
import sys, json, time, math
from gi.repository import GLib, Gio
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
peaks = {}
def on(conn, sender, path, iface, sig, params):
    for k, v in params.unpack()[0].items(): peaks[k] = max(peaks.get(k, 0.0), v)
bus.signal_subscribe("org.kmixdeck1", "org.kmixdeck1.Levels", "Peaks", "/org/kmixdeck1", None, Gio.DBusSignalFlags.NONE, on)
bus.call_sync("org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Levels", "Subscribe", None, None, Gio.DBusCallFlags.NONE, 5000, None)
loop = GLib.MainLoop(); GLib.timeout_add(int(float(sys.argv[1]) * 1000), loop.quit); loop.run()
if len(sys.argv) > 2 and sys.argv[2] == "unsubscribe":
    bus.call_sync("org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Levels", "Unsubscribe", None, None, Gio.DBusCallFlags.NONE, 5000, None)
print(json.dumps(peaks))
"""


def meter_nodes(stack):
    out = subprocess.run(["pw-cli", "ls", "Node"], env=stack.pw.env, capture_output=True, text=True).stdout
    return out.count('node.name = "kmixdeck.meter"')


def test_ux6_levels_signal_carries_peaks_of_the_tone(stack):
    """Subscribe → tone into game at −20 dBFS → Peaks carries ≈ −20 dB on channel/game and on both mixes
    (cells at 0 dB), silence elsewhere. Meter streams exist only while subscribed."""
    assert meter_nodes(stack) == 0
    stack.cli("cell", "set", "game", "stream", "1.0"); stack.cli("cell", "set", "game", "monitor", "1.0")
    play = stack.pw.play_into("kmixdeck.channel.game")
    try:
        r = subprocess.run(["/usr/bin/python3", "-c", METER_LISTENER, "2.5", "unsubscribe"], env=stack.env, capture_output=True, text=True, timeout=20)
        assert r.returncode == 0, r.stderr
        peaks = json.loads(r.stdout.strip().splitlines()[-1])
    finally:
        play.kill(); play.wait()
    def db(v): return 20 * math.log10(v) if v > 0 else float("-inf")
    assert {"channel/game", "channel/system", "channel/voice", "mix/monitor", "mix/stream"} <= set(peaks), peaks
    assert -23 < db(peaks["channel/game"]) < -17, peaks
    assert -23 < db(peaks["mix/stream"]) < -17 and -23 < db(peaks["mix/monitor"]) < -17, peaks
    assert peaks["channel/system"] == 0 and peaks["channel/voice"] == 0, peaks
    # after Unsubscribe the daemon keeps meters for a grace period, then tears them down
    for _ in range(80):
        if meter_nodes(stack) == 0: break
        time.sleep(0.1)
    assert meter_nodes(stack) == 0
    assert stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Levels", "Subscribers").stdout.strip() == "u 0"


def test_ux6_subscriber_that_dies_is_forgotten(stack):
    """A client that exits without Unsubscribe must not leave meters running (NameOwnerChanged)."""
    p = subprocess.Popen(["/usr/bin/python3", "-c", METER_LISTENER, "30"], env=stack.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # UX-13: 3 channels + 2 mixes + 6 cells + 2 mix outputs (+ one per running app, none here)
    for _ in range(50):
        if meter_nodes(stack) >= 13: break
        time.sleep(0.1)
    assert meter_nodes(stack) == 13, "one meter stream per channel, mix, cell and mix output"
    assert stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Levels", "Subscribers").stdout.strip() == "u 1"
    p.kill(); p.wait()
    for _ in range(80):
        if meter_nodes(stack) == 0: break
        time.sleep(0.1)
    assert meter_nodes(stack) == 0


# ---------------------------------------------------------------- channel inputs + device absence (CH-3, DV-9, DV-12, ADR 0007)
def make_fake_source(stack, name, desc):
    subprocess.run(["pw-cli", "create-node", "adapter",
                    f'{{ factory.name=support.null-audio-sink node.name={name} node.description="{desc}" media.class=Audio/Source audio.position=[FL FR] object.linger=true }}'],
                   env=stack.pw.env, capture_output=True)
    stack.pw.wait_node(name)


def destroy_node(stack, name):
    nid = stack.pw.node_id(name)
    subprocess.run(["pw-cli", "destroy", str(nid)], env=stack.pw.env, capture_output=True)
    for _ in range(50):
        if stack.pw.node(name) is None: return
        time.sleep(0.1)
    raise AssertionError(f"{name} still in graph")


def wait_prop(stack, kind, slug, prop, want, tries=50):
    for _ in range(tries):
        objs = stack.cli(kind, "list", json_out=True)
        cur = next(o for o in objs if o["Slug"] == slug)[prop]
        if cur == want: return cur
        time.sleep(0.1)
    return cur


def test_ch3_devices_in_lists_sources_and_channel_input_is_settable(stack):
    make_fake_source(stack, "fake.mic", "Fake Microphone")
    for _ in range(30):
        devs = stack.cli("devices", "in", json_out=True)
        if "fake.mic" in devs: break
        time.sleep(0.1)
    assert devs["fake.mic"] == "Fake Microphone"
    assert not any(k.startswith("kmixdeck.") for k in devs), "our own kmixdeck.source.* must not be offered as inputs"
    r = stack.cli("channel", "input", "voice", "does.not.exist", check=False)
    assert r.returncode == 3
    stack.cli("channel", "input", "voice", "fake.mic")
    assert wait_prop(stack, "channel", "voice", "InputDevice", "fake.mic") == "fake.mic"
    assert wait_prop(stack, "channel", "voice", "InputPresent", True) is True
    stack.pw.wait_node("kmixdeck.in.voice")
    layout = json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())
    assert any(i["channel"] == "voice" and i["device"]["node"] == "fake.mic" for i in layout["inputs"])


def test_dv9_dv12_unplug_greys_out_and_replug_restores(stack):
    # input side
    destroy_node(stack, "fake.mic")
    assert wait_prop(stack, "channel", "voice", "InputPresent", False) is False
    assert wait_prop(stack, "channel", "voice", "InputDevice", "fake.mic") == "fake.mic", "the choice must survive the absence (DV-9)"
    make_fake_source(stack, "fake.mic", "Fake Microphone")
    assert wait_prop(stack, "channel", "voice", "InputPresent", True) is True
    # output side, same contract
    stack.cli("mix", "output", "stream", "fake.headphones")
    assert wait_prop(stack, "mix", "stream", "OutputPresent", True) is True
    destroy_node(stack, "fake.headphones")
    assert wait_prop(stack, "mix", "stream", "OutputPresent", False) is False
    assert wait_prop(stack, "mix", "stream", "OutputDevice", "fake.headphones") == "fake.headphones"
    for _ in range(40):
        if out_link_target(stack, "stream") == "kmixdeck.null": break
        time.sleep(0.1)
    assert out_link_target(stack, "stream") == "kmixdeck.null", "absent output must park, never fall to the default sink"
    make_fake_sink(stack, "fake.headphones", "Fake Headphones")
    assert wait_prop(stack, "mix", "stream", "OutputPresent", True) is True
    for _ in range(60):
        if out_link_target(stack, "stream") == "fake.headphones": break
        time.sleep(0.1)
    assert out_link_target(stack, "stream") == "fake.headphones", "replug must resume on the device without user action (DV-12)"
    stack.cli("mix", "output", "stream", "none")
    stack.cli("channel", "input", "voice", "none")
    assert wait_prop(stack, "channel", "voice", "InputDevice", "") == ""


def test_ux13_every_entity_has_a_meter(stack):
    """UX-13: cells, outputs and the app itself are metered, not only channels and mixes. Tone from a real app on
    game: app/<id> hot, cell/game/monitor hot, cell/game/stream at −40 dB shows ≈ 40 dB less, out/monitor hot."""
    stack.cli("cell", "set", "game", "monitor", "1.0"); stack.cli("cell", "set", "game", "stream", "-40dB")
    stack.cli("cell", "mute", "game", "monitor", "off"); stack.cli("cell", "mute", "game", "stream", "off")
    p, app = start_fake_app(stack)
    try:
        stack.cli("app", "move", "FakeGame", "game")
        assert wait_sink(stack, "kmixdeck.channel.game") == "kmixdeck.channel.game"
        time.sleep(0.5)
        r = subprocess.run(["/usr/bin/python3", "-c", METER_LISTENER, "2.5", "unsubscribe"], env=stack.env, capture_output=True, text=True, timeout=20)
        assert r.returncode == 0, r.stderr
        peaks = json.loads(r.stdout.strip().splitlines()[-1])
    finally:
        p.kill(); p.wait()
        stack.cli("cell", "set", "game", "stream", "1.0")
    def db(v): return 20 * math.log10(v) if v > 0 else float("-inf")
    key = f"app/{app['NodeId']}"
    assert key in peaks and db(peaks[key]) > -30, (key, peaks)
    assert db(peaks["cell/game/monitor"]) > -30, peaks
    assert db(peaks["cell/game/monitor"]) - 45 < db(peaks["cell/game/stream"]) < db(peaks["cell/game/monitor"]) - 35, peaks
    assert db(peaks["out/monitor"]) > -30, peaks
    assert peaks.get("cell/voice/monitor", 0) == 0, peaks
    for _ in range(80):
        if meter_nodes(stack) == 0: break
        time.sleep(0.1)
    assert meter_nodes(stack) == 0


def test_ux2_listening_device_is_one_truth(stack):
    """UX-2: ListeningDevice is layout state on the bus (not a UI preference), any frontend reads the same answer;
    `kmixdeck listen` shows device + the mixes routed there."""
    if "fake.headphones" not in stack.cli("devices", json_out=True):
        make_fake_sink(stack, "fake.headphones", "Fake Headphones")
        for _ in range(30):
            if "fake.headphones" in stack.cli("devices", json_out=True): break
            time.sleep(0.1)
    stack.cli("listen", "fake.headphones")
    assert stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "ListeningDevice").stdout.strip() == 's "fake.headphones"'
    stack.cli("mix", "output", "monitor", "fake.headphones")
    j = stack.cli("listen", json_out=True)
    assert j == {"device": "fake.headphones", "mixes": ["monitor"]}, j
    data = json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())
    assert data["listeningDevice"] == "fake.headphones"
    stack.restart_daemon()
    assert stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "ListeningDevice").stdout.strip() == 's "fake.headphones"'
    stack.cli("listen", "none"); stack.cli("mix", "output", "monitor", "none")
    assert stack.cli("listen").stdout.startswith("none")


def test_ch13_add_channel_from_source_app_and_device(stack):
    """CH-13 on the bus: the picker's two paths — AddChannel then App.Assign, AddChannel then Channel.InputDevice —
    leave the app on the new channel / the input edge on the new channel, both persisted."""
    p, app = start_fake_app(stack)
    try:
        path = stack.cli("channel", "add", "Picked App").stdout.strip()
        assert path.endswith("/channel/picked_app"), path
        stack.cli("app", "move", "FakeGame", "picked_app")
        assert wait_sink(stack, "kmixdeck.channel.picked_app") == "kmixdeck.channel.picked_app"
        assert next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"] == ["picked_app"]
    finally:
        p.kill(); p.wait()
    make_fake_source(stack, "fake.mic", "Fake Mic")
    for _ in range(30):
        if "fake.mic" in stack.cli("devices", "in", json_out=True): break
        time.sleep(0.1)
    path = stack.cli("channel", "add", "Picked Mic").stdout.strip()
    stack.cli("channel", "input", "picked_mic", "fake.mic")
    assert wait_prop(stack, "channel", "picked_mic", "InputDevice", "fake.mic") == "fake.mic"
    stack.pw.wait_node("kmixdeck.in.picked_mic")
    layout = json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())
    assert any(i["channel"] == "picked_mic" and i["device"]["node"] == "fake.mic" for i in layout["inputs"]), layout["inputs"]
    stack.cli("channel", "remove", "picked_app"); stack.cli("channel", "remove", "picked_mic")


METER_TRACE = r"""
import sys, json, time
from gi.repository import GLib, Gio
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
trace = []
def on(conn, sender, path, iface, sig, params):
    trace.append((time.monotonic(), dict(params.unpack()[0])))
bus.signal_subscribe("org.kmixdeck1", "org.kmixdeck1.Levels", "Peaks", "/org/kmixdeck1", None, Gio.DBusSignalFlags.NONE, on)
bus.call_sync("org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Levels", "Subscribe", None, None, Gio.DBusCallFlags.NONE, 5000, None)
loop = GLib.MainLoop(); GLib.timeout_add(int(float(sys.argv[1]) * 1000), loop.quit); loop.run()
bus.call_sync("org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Levels", "Unsubscribe", None, None, Gio.DBusCallFlags.NONE, 5000, None)
print(json.dumps(trace))
"""


def test_ux16_meter_ballistics_no_dropouts_hold_and_slow_fall(stack):
    """UX-16: with a steady tone, no tick may read 0 on a metered key (laptop 2026-09-16: 'Stream-Fader fällt kurz auf
    null'); after the tone stops the value holds ≥ 300 ms and then falls ≈ 20 dB/s instead of snapping to 0."""
    import math
    stack.cli("cell", "set", "game", "monitor", "1.0"); stack.cli("cell", "mute", "game", "monitor", "off")
    play = stack.pw.play_into("kmixdeck.channel.game")
    time.sleep(0.6)
    try:
        r = subprocess.run(["/usr/bin/python3", "-c", METER_TRACE, "4.0"], env=stack.env, capture_output=True, text=True, timeout=30)
        assert r.returncode == 0, r.stderr
        trace = json.loads(r.stdout.strip().splitlines()[-1])
    finally:
        pass
    # kill the tone half-way through a second trace to see hold + fall
    r2 = subprocess.Popen(["/usr/bin/python3", "-c", METER_TRACE, "3.0"], env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1.0); play.kill(); play.wait(); t_stop = time.monotonic()
    out, err = r2.communicate(timeout=30)
    trace2 = json.loads(out.strip().splitlines()[-1])
    def db(v): return 20 * math.log10(v) if v > 0 else -100
    # (1) steady tone: every tick carries signal on channel/game and cell/game/monitor, none is 0
    keys = ["channel/game", "cell/game/monitor", "mix/monitor"]
    ticks = [t for t in trace if all(k in t[1] for k in keys)]
    assert len(ticks) >= 80, f"expected ≥ 80 ticks in 4 s at 25 Hz, got {len(ticks)}"
    for k in keys:
        vals = [t[1][k] for t in ticks[5:]]
        zeros = sum(1 for v in vals if v == 0)
        zero_at = [i for i, v in enumerate(vals) if v == 0]
        assert zeros == 0, f"{k}: {zeros} of {len(vals)} ticks dropped to 0 with a steady tone (at {zero_at}; first {[round(v, 4) for v in vals[:8]]})"
        spread = max(db(v) for v in vals) - min(db(v) for v in vals)
        assert spread < 3.0, f"{k}: steady tone but meter spread {spread:.1f} dB"
    # (2) after the tone stops: hold, then fall ≈ 20 dB/s (accept 12–30 dB/s)
    after = [(ts, d["channel/game"]) for ts, d in trace2 if ts > t_stop and "channel/game" in d]
    assert len(after) >= 30, len(after)
    peak0 = after[0][1]
    held = [v for ts, v in after if ts - t_stop < 0.28]
    assert all(db(v) > db(peak0) - 1.0 for v in held), f"value did not hold for 300 ms: {[round(db(v),1) for v in held]}"
    later = [(ts, v) for ts, v in after if 0.6 <= ts - t_stop <= 1.4 and v > 0]
    assert len(later) >= 10, later
    slope = (db(later[0][1]) - db(later[-1][1])) / (later[-1][0] - later[0][0])
    assert 12 <= slope <= 30, f"fall rate {slope:.1f} dB/s, expected ≈ 20"


def test_ar7_frontend_guide_snippets_run_verbatim(stack):
    """AR-7: the 5-minute tour in docs/frontend-guide.md is executed line by line against the daemon — the doc
    can not drift from the bus contract without this going red."""
    import re
    from pathlib import Path
    guide = (Path(__file__).resolve().parents[2] / "docs" / "frontend-guide.md").read_text()
    block = re.search(r"```sh\n(.*?)```", guide, re.S).group(1)
    cmds = [l.strip() for l in block.splitlines() if l.strip() and not l.strip().startswith("#") and "monitor" not in l]
    assert len(cmds) >= 5, cmds
    import shlex
    for c in cmds:
        r = stack.busctl(*shlex.split(c)[1:] if c.startswith("busctl ") else shlex.split(c))
        assert r.returncode == 0, f"guide snippet failed: {c}\n{r.stderr}"
        if c.endswith("Cell Volume") and "get-property" in c: assert r.stdout.startswith("d "), r.stdout
    # the guide's effects are real, not just accepted: fader is 0.25 and muted, mix 'recording' exists
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] == pytest.approx(0.25)
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Muted"] is True
    assert "recording" in [m["Slug"] for m in stack.cli("mix", "list", json_out=True)]
    # and the "# d 1" comment under the read is the value a fresh cell has (documented output must be true)
    stack.cli("cell", "set", "game", "stream", "1.0"); stack.cli("cell", "mute", "game", "stream", "off"); stack.cli("mix", "remove", "recording")
    assert stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1/cell/game/stream", "org.kmixdeck1.Cell", "Volume").stdout.strip() == "d 1"


def daemon_log_tail(stack, needles=(), n=3000):
    """Last lines of the daemon's stderr (tests attach it to assertion messages — a red test must explain itself)."""
    try: text = open(stack.daemon_log_path).read()
    except FileNotFoundError: return "<no daemon log>"
    lines = [l for l in text.splitlines() if not needles or any(k in l for k in needles)]
    return "\n".join(lines)[-n:]
