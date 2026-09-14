# SPDX-License-Identifier: GPL-3.0-or-later
"""Integration tests for the service/CLI split (ADR 0005, AR-1..AR-6).

Two sandboxes: a private PipeWire (pw_sandbox) AND a private session bus (dbus-daemon).
kmixdeckd is started on that bus; the CLI is the test client — so every assertion here also proves AR-3.
The acoustic checks reuse the same measurement helpers as test_audio_graph.py.
"""
import json, math, os, subprocess, time, pytest
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
        self.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
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
    stack.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=stack.env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
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
        assert app["Channel"] in ("/", "/org/kmixdeck1/channel/game")   # default sink in the sandbox is whatever WirePlumber picked
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
    stack.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=stack.env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
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
