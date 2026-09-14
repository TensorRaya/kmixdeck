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
    p = subprocess.Popen(["pw-play", "-P", FAKE_APP, str(stack.pw.tone())], env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        apps = stack.cli("app", "list", json_out=True)
        if any(a["Name"] == "FakeGame" for a in apps): return p, next(a for a in apps if a["Name"] == "FakeGame")
        time.sleep(0.1)
    p.kill(); raise AssertionError("fake app never appeared on the bus")


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
        for _ in range(30):
            if current_sink_of(stack) == "kmixdeck.channel.voice": break
            time.sleep(0.1)
        assert current_sink_of(stack) == "kmixdeck.channel.voice"
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
    stack.cli("app", "move", "FakeGame", "system"); time.sleep(3.0)   # WirePlumber save_after_timeout
    p.kill(); p.wait(); time.sleep(0.5)
    p, app = start_fake_app(stack)
    try:
        for _ in range(30):
            if current_sink_of(stack) == "kmixdeck.channel.system": break
            time.sleep(0.1)
        assert current_sink_of(stack) == "kmixdeck.channel.system"
        assert app["Channel"] == "/org/kmixdeck1/channel/system" or stack.cli("app", "list", json_out=True)[0]["Channel"] == "/org/kmixdeck1/channel/system"
    finally:
        p.kill(); p.wait()


def test_ch4_routing_survives_pipewire_restart(stack):
    """Same, across a pipewire+wireplumber restart (serials change; WirePlumber stores the target by node.name)."""
    p, _ = start_fake_app(stack)
    stack.cli("app", "move", "FakeGame", "system"); time.sleep(3.0)
    p.kill(); p.wait()
    stack.pw.restart()
    for _ in range(50):   # daemon reconnects? (AR-4) — at minimum the CLI must work again
        if stack.cli("status", check=False).returncode == 0: break
        time.sleep(0.2)
    p, _ = start_fake_app(stack)
    try:
        for _ in range(30):
            if current_sink_of(stack) == "kmixdeck.channel.system": break
            time.sleep(0.1)
        assert current_sink_of(stack) == "kmixdeck.channel.system"
    finally:
        p.kill(); p.wait()
