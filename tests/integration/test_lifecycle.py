# SPDX-License-Identifier: GPL-3.0-or-later
"""Lifecycle edge cases: create / rename / remove channels and mixes while audio is flowing.

Every test checks THREE truths and that they agree: the bus (kmixdeck CLI), the PipeWire graph (pw-cli /
pw-link) and the persisted layout + generated conf. Tests here are ordered — they build on each other.
"""
import json, subprocess, time
from pathlib import Path
import pytest
from test_service_cli import Stack, BIN, REPO, make_fake_sink, out_link_target
from pw_sandbox import start_private_pipewire


@pytest.fixture(scope="module")
def stack():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    s = Stack(pw)
    yield s
    s.close(); pw.close()


def node_names(stack):
    return {o.get("info", {}).get("props", {}).get("node.name") for o in stack.pw.dump() if o.get("type") == "PipeWire:Interface:Node"}


def our_nodes(stack, prefix):
    return sorted(n for n in node_names(stack) if n and n.startswith(prefix))


def wait(pred, tries=60, dt=0.1):
    for _ in range(tries):
        v = pred()
        if v: return v
        time.sleep(dt)
    return pred()


def layout(stack):
    return json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())


def conf(stack):
    return (Path(stack.pw.runtime_dir) / "pipewire.conf.d" / "90-kmixdeck.conf").read_text()


def status(stack):
    return stack.cli("status", json_out=True)


# ---------------------------------------------------------------- channels
def test_add_channel_creates_sink_cells_and_persists(stack):
    r = stack.cli("channel", "add", "Music")
    assert r.stdout.strip() == "/org/kmixdeck1/channel/music"
    assert wait(lambda: len(status(stack)["cells"]) == 8)
    stack.pw.wait_node("kmixdeck.channel.music")
    stack.pw.wait_node("kmixdeck.link.music.monitor"); stack.pw.wait_node("kmixdeck.link.music.stream")
    assert [c["slug"] for c in layout(stack)["channels"]] == ["game", "system", "voice", "music"]
    assert 'node.name = "kmixdeck.channel.music"' in conf(stack)
    # fresh cells are unity, unmuted — never inherit anything
    assert stack.pw.props("kmixdeck.link.music.stream")["volume"] == pytest.approx(1.0)


def test_add_channel_with_umlauts_and_spaces_slugs_cleanly(stack):
    r = stack.cli("channel", "add", "Übertragung  Ünd Mehr!")
    assert r.stdout.strip() == "/org/kmixdeck1/channel/ubertragung_und_mehr"
    assert wait(lambda: "kmixdeck.channel.ubertragung_und_mehr" in node_names(stack))
    st = status(stack)
    assert next(c for c in st["channels"] if c["Slug"] == "ubertragung_und_mehr")["Name"] == "Übertragung  Ünd Mehr!", "display name keeps the original"
    stack.cli("channel", "remove", "ubertragung_und_mehr")
    assert wait(lambda: "kmixdeck.channel.ubertragung_und_mehr" not in node_names(stack))


def test_add_duplicate_channel_is_rejected_not_duplicated(stack):
    before = our_nodes(stack, "kmixdeck.channel.")
    r = stack.cli("channel", "add", "Music", check=False)
    assert r.returncode != 0, "adding an existing channel must fail, not silently no-op"
    r2 = stack.cli("channel", "add", "  music ", check=False)   # same slug after slugify
    assert r2.returncode != 0
    time.sleep(0.5)
    assert our_nodes(stack, "kmixdeck.channel.") == before
    nodes = subprocess.run(["pw-cli", "ls", "Node"], env=stack.pw.env, capture_output=True, text=True).stdout
    assert nodes.count('"kmixdeck.channel.music"') == 1


def test_add_channel_with_empty_or_symbol_only_name_is_rejected(stack):
    for bad in ["", "   ", "!!!", "---"]:
        r = stack.cli("channel", "add", bad, check=False)
        assert r.returncode != 0, f"name {bad!r} must be rejected"
    assert "kmixdeck.channel.x" not in node_names(stack), "the slugify fallback 'x' must never become a real channel"


def test_rename_channel_changes_name_not_slug_and_persists(stack):
    stack.cli("channel", "rename", "music", "Musik")
    assert wait(lambda: next(c for c in status(stack)["channels"] if c["Slug"] == "music")["Name"] == "Musik")
    assert next(c for c in layout(stack)["channels"] if c["slug"] == "music")["name"] == "Musik"
    # slug + node names untouched → app routing and WirePlumber state keep working
    assert "kmixdeck.channel.music" in node_names(stack)
    assert stack.cli("channel", "rename", "does-not-exist", "X", check=False).returncode == 3


def test_remove_channel_with_running_app_moves_the_app_off_gracefully(stack):
    play = stack.pw.play_into("kmixdeck.channel.music")
    try:
        assert wait(lambda: any(a["Channel"].endswith("/music") for a in stack.cli("app", "list", json_out=True)))
        stack.cli("channel", "remove", "music")
        assert wait(lambda: "kmixdeck.channel.music" not in node_names(stack))
        # the app stream must still exist (PipeWire keeps it, it just lost its target) — never killed by us
        assert wait(lambda: stack.pw.node("pw-play") is not None or play.poll() is None)
        assert play.poll() is None, "removing a channel must not kill the application"
    finally:
        play.kill(); play.wait()


def test_remove_channel_removes_every_node_and_cell(stack):
    assert wait(lambda: len(status(stack)["cells"]) == 6)
    left = [n for n in node_names(stack) if n and ".music" in n or (n and n.startswith("kmixdeck.channel.music"))]
    assert left == [], f"stale nodes after channel removal: {left}"
    assert [c["slug"] for c in layout(stack)["channels"]] == ["game", "system", "voice"]
    assert "kmixdeck.channel.music" not in conf(stack)
    assert stack.cli("channel", "remove", "music", check=False).returncode == 3


def test_remove_channel_that_has_an_input_device_drops_the_input(stack):
    from test_service_cli import make_fake_source
    make_fake_source(stack, "fake.mic2", "Fake Mic 2")
    stack.cli("channel", "add", "Podcast")
    assert wait(lambda: "kmixdeck.channel.podcast" in node_names(stack))
    stack.cli("channel", "input", "podcast", "fake.mic2")
    stack.pw.wait_node("kmixdeck.in.podcast")
    stack.cli("channel", "remove", "podcast")
    assert wait(lambda: "kmixdeck.in.podcast" not in node_names(stack) and "kmixdeck.channel.podcast" not in node_names(stack))
    assert not any(i["slug"] == "podcast" for i in layout(stack)["inputs"]), "orphan input left in layout"
    assert "kmixdeck.in.podcast" not in conf(stack)


# ---------------------------------------------------------------- mixes
def test_add_mix_gets_output_edge_capture_source_and_parks(stack):
    stack.cli("mix", "add", "Recording")
    assert wait(lambda: len(status(stack)["cells"]) == 9)
    for n in ["kmixdeck.mix.recording", "kmixdeck.out.recording", "kmixdeck.source.recording",
              "kmixdeck.link.game.recording", "kmixdeck.link.system.recording", "kmixdeck.link.voice.recording"]:
        stack.pw.wait_node(n)
    assert wait(lambda: out_link_target(stack, "recording") == "kmixdeck.null"), "new mix must start parked, never on the default sink"
    m = next(m for m in stack.cli("mix", "list", json_out=True) if m["Slug"] == "recording")
    assert m["OutputDevice"] == "" and m["CaptureSource"] == "kmixdeck.source.recording" and m["OutputPresent"] is True
    src = stack.pw.node("kmixdeck.source.recording")["info"]["props"]
    assert src["media.class"] == "Audio/Source", "OBS must see the mix as an input device"


def test_add_duplicate_mix_is_rejected(stack):
    assert stack.cli("mix", "add", "Recording", check=False).returncode != 0
    assert stack.cli("mix", "add", "recording", check=False).returncode != 0
    time.sleep(0.3)
    nodes = subprocess.run(["pw-cli", "ls", "Node"], env=stack.pw.env, capture_output=True, text=True).stdout
    assert nodes.count('"kmixdeck.mix.recording"') == 1 and nodes.count('"kmixdeck.out.recording"') == 1


def test_mix_faders_are_independent_across_mixes(stack):
    """The whole point of the project (Sonusmix #72): a fader in one mix must not touch the others."""
    stack.cli("cell", "set", "game", "recording", "-20dB")
    stack.cli("cell", "mute", "game", "stream", "on")
    time.sleep(0.4)
    g = {mx: stack.cli("cell", "get", "game", mx, json_out=True) for mx in ("monitor", "stream", "recording")}
    assert g["recording"]["Volume"] == pytest.approx(0.1, abs=0.002) and g["recording"]["Muted"] is False
    assert g["stream"]["Muted"] is True and g["stream"]["Volume"] == pytest.approx(1.0)
    assert g["monitor"]["Muted"] is False and g["monitor"]["Volume"] == pytest.approx(1.0)
    stack.cli("cell", "mute", "game", "stream", "off"); stack.cli("cell", "set", "game", "recording", "1.0")


def test_rename_mix_keeps_routing_and_output(stack):
    make_fake_sink(stack, "fake.speakers", "Fake Speakers")
    stack.cli("mix", "output", "recording", "fake.speakers")
    assert wait(lambda: out_link_target(stack, "recording") == "fake.speakers")
    stack.cli("mix", "rename", "recording", "Aufnahme")
    assert wait(lambda: next(m for m in status(stack)["mixes"] if m["Slug"] == "recording")["Name"] == "Aufnahme")
    assert out_link_target(stack, "recording") == "fake.speakers", "renaming must not retarget"
    assert next(m for m in layout(stack)["mixes"] if m["slug"] == "recording")["name"] == "Aufnahme"


def test_remove_mix_with_output_and_audio_flowing_cleans_everything(stack):
    play = stack.pw.play_into("kmixdeck.channel.game")
    try:
        time.sleep(0.5)
        stack.cli("mix", "remove", "recording")
        assert wait(lambda: len(status(stack)["cells"]) == 6)
        assert wait(lambda: not [n for n in node_names(stack) if n and n.endswith(".recording") or (n and "recording" in n and n.startswith("kmixdeck."))], tries=80), \
            f"stale nodes: {[n for n in node_names(stack) if n and 'recording' in n]}"
        assert play.poll() is None, "removing a mix must not kill applications"
        # the other mixes still get audio
        assert stack.pw.level_at("kmixdeck.mix.monitor") > -30
    finally:
        play.kill(); play.wait()
    assert [m["slug"] for m in layout(stack)["mixes"]] == ["monitor", "stream"]
    assert "recording" not in conf(stack)
    assert "fake.speakers" in node_names(stack), "the hardware device must survive removal of the mix that used it"
    assert stack.cli("mix", "remove", "recording", check=False).returncode == 3


def test_remove_last_mix_and_last_channel_is_allowed_and_matrix_is_empty_but_alive(stack):
    """Edge: an empty matrix must not crash the daemon or leave the conf unparseable."""
    for m in ["monitor", "stream"]: stack.cli("mix", "remove", m)
    for c in ["game", "system", "voice"]: stack.cli("channel", "remove", c)
    assert wait(lambda: status(stack)["cells"] == [] and status(stack)["mixes"] == [] and status(stack)["channels"] == [])
    assert wait(lambda: not [n for n in node_names(stack) if n and n.startswith("kmixdeck.") and n != "kmixdeck.null"], tries=80), \
        f"leftovers: {[n for n in node_names(stack) if n and n.startswith('kmixdeck.')]}"
    assert "kmixdeck.null" in node_names(stack), "the parking sink stays"
    assert status(stack)["connected"] is True
    # conf still valid: PipeWire restart with it must not fail
    stack.pw.restart(wait_for=None)
    assert wait(lambda: "kmixdeck.null" in node_names(stack), tries=80)
    assert wait(lambda: stack.cli("status", check=False).returncode == 0, tries=80)


def test_rebuild_from_empty_and_default_layout_on_missing_file(stack):
    stack.cli("channel", "add", "Game"); stack.cli("mix", "add", "Monitor"); stack.cli("mix", "add", "Stream")
    assert wait(lambda: len(status(stack)["cells"]) == 2)
    stack.pw.wait_node("kmixdeck.link.game.stream")
    assert wait(lambda: out_link_target(stack, "stream") == "kmixdeck.null")
    # delete the layout file under the running daemon → next Save must recreate it, not crash
    (Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").unlink()
    stack.cli("mix", "add", "Extra")
    assert wait(lambda: (Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").exists())
    assert [m["slug"] for m in layout(stack)["mixes"]] == ["monitor", "stream", "extra"]


def test_corrupt_layout_file_does_not_take_the_daemon_down(stack):
    p = Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json"
    good = p.read_text()
    p.write_text("{ this is not json")
    stack.restart_daemon()
    assert stack.cli("status", check=False).returncode == 0, "daemon must come up with a corrupt layout (CT-7)"
    # the graph in PipeWire is still the truth → objects are rebuilt from it
    st = status(stack)
    assert {m["Slug"] for m in st["mixes"]} >= {"monitor", "stream", "extra"}
    p.write_text(good)
    stack.restart_daemon()   # the daemon must reload the restored file — leaving it running on the rebuilt-from-graph state would desync layout.json from the daemon


# ---------------------------------------------------------------- CH-5: default channel for never-seen apps
def _start_app(stack, name, node):
    # WirePlumber keys its restore-target state by media.role FIRST (state-stream.lua formKey) and pw-play
    # defaults the role to "Music" — so two test apps would inherit each other's target. Give each its own
    # role so the key is unique per app, which is the situation CH-4/CH-5 describe.
    return subprocess.Popen(["pw-play", "-P", f'{{ application.name="{name}" node.name="{node}" media.role="{node}" }}', str(stack.pw.tone())],
                            env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _app(stack, name):
    return next((a for a in stack.cli("app", "list", json_out=True) if a["Name"] == name), None)


def _wait_channel(stack, name, path, tries=60):
    for _ in range(tries):
        a = _app(stack, name)
        if a and a["Channel"] == path: return a
        time.sleep(0.1)
    return _app(stack, name)


@pytest.fixture(scope="module")
def fresh(stack):
    """The lifecycle tests above legitimately leave the matrix in an arbitrary shape. CH-5 needs the starter
    layout (Game/System/Voice) — own sandbox, no dependency on test order."""
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    s = Stack(pw)
    yield s
    s.close(); pw.close()


def test_ch5_new_app_lands_on_the_default_channel_known_apps_are_left_alone(fresh):
    stack = fresh
    assert stack.cli("channel", "default").stdout.strip() == "system", "starter layout: System is the default (CH-5)"
    # 1) never-seen app → system, immediately, and WirePlumber remembers it
    p = _start_app(stack, "Fresh App", "freshapp")
    try:
        a = _wait_channel(stack, "Fresh App", "/org/kmixdeck1/channel/system")
        assert a["Channel"] == "/org/kmixdeck1/channel/system", f"new app not auto-routed: {a}"
        assert "Fresh App" in layout(stack)["knownApps"]
        # 2) the user moves it → that is remembered by WirePlumber (CH-4) …
        stack.cli("app", "move", str(a["NodeId"]), "game")
        assert _wait_channel(stack, "Fresh App", "/org/kmixdeck1/channel/game")["Channel"].endswith("/game")
        from test_service_cli import wait_wireplumber_saved_target
        wait_wireplumber_saved_target(stack, "kmixdeck.channel.game")
    finally:
        p.kill(); p.wait()
    time.sleep(0.5)
    # … and on restart the app is KNOWN → the default channel must not override the user's choice
    p = _start_app(stack, "Fresh App", "freshapp")
    try:
        a = _wait_channel(stack, "Fresh App", "/org/kmixdeck1/channel/game")
        assert a["Channel"].endswith("/game"), f"known app was re-routed to the default channel: {a}"
    finally:
        p.kill(); p.wait()


def test_ch5_default_none_leaves_new_apps_on_the_system_sink(fresh):
    stack = fresh
    stack.cli("channel", "default", "none")
    assert stack.cli("channel", "default").stdout.strip() == "none"
    p = _start_app(stack, "Untouched App", "untouched")
    try:
        time.sleep(1.5)
        a = _app(stack, "Untouched App")
        assert a is not None and a["Channel"] == "/", f"with default=none kmixdeck must not touch new apps: {a}"
    finally:
        p.kill(); p.wait()
    stack.cli("channel", "default", "system")


def test_ch5_default_survives_restart_and_dies_with_its_channel(fresh):
    stack = fresh
    stack.cli("channel", "default", "voice")
    stack.restart_daemon()
    assert stack.cli("channel", "default").stdout.strip() == "voice"
    assert stack.cli("channel", "default", "nope", check=False).returncode == 3
    stack.cli("channel", "remove", "voice")
    assert wait(lambda: stack.cli("channel", "default").stdout.strip() == "none"), "removing the default channel must reset the default, not leave a dangling slug"
    stack.cli("channel", "add", "Voice"); stack.cli("channel", "default", "system")


def test_refused_property_writes_never_kill_the_daemon(stack):
    """Regression: a QDBusContext-less property setter calling sendErrorReply() segfaulted kmixdeckd.
    Every writable property gets an out-of-range/unknown value; the daemon must survive and keep the old value."""
    cases = [
        ("/org/kmixdeck1/cell/game/stream", "org.kmixdeck1.Cell", "Volume", "d", "5.0"),
        ("/org/kmixdeck1/channel/game", "org.kmixdeck1.Channel", "Trim", "d", "5.0"),
        ("/org/kmixdeck1/channel/game", "org.kmixdeck1.Channel", "InputDevice", "s", "no.such.device"),
        ("/org/kmixdeck1/mix/stream", "org.kmixdeck1.Mix", "Volume", "d", "-1"),
        # (Mix.OutputDevice deliberately ACCEPTS unknown names: a not-yet-plugged device is a valid choice, ADR 0007)
        ("/org/kmixdeck1", "org.kmixdeck1.Mixer", "DefaultChannel", "o", "/org/kmixdeck1/channel/nope"),
        ("/org/kmixdeck1", "org.kmixdeck1.Mixer", "DefaultChannel", "o", "/not/a/channel"),
    ]
    stack.cli("cell", "set", "game", "stream", "0.5")
    for path, iface, prop, sig, val in cases:
        stack.busctl("set-property", "org.kmixdeck1", path, iface, prop, sig, val)
        assert stack.daemon.poll() is None, f"daemon died on {iface}.{prop} = {val!r}"
        assert stack.cli("status", check=False).returncode == 0, f"daemon unresponsive after {iface}.{prop} = {val!r}"
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] == pytest.approx(0.5)
    assert stack.cli("channel", "default").stdout.strip() != "nope"
    stack.cli("cell", "set", "game", "stream", "1.0")


# ---------------------------------------------------------------- CH-9: undo removal
def test_ch9_undo_restores_channel_with_faders_links_input_and_default(fresh):
    stack = fresh
    from test_service_cli import make_fake_source
    make_fake_source(stack, "fake.undo.mic", "Undo Mic")
    stack.cli("cell", "set", "voice", "stream", "-18dB"); stack.cli("cell", "mute", "voice", "monitor", "on")
    stack.cli("channel", "trim", "voice", "-6dB")
    stack.cli("cell", "link", "game", "stream", "monitor")
    stack.cli("channel", "input", "voice", "fake.undo.mic"); stack.pw.wait_node("kmixdeck.in.voice")
    stack.cli("channel", "default", "voice")
    assert stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "UndoDescription").stdout.strip() == 's ""'
    stack.cli("channel", "remove", "voice")
    assert wait(lambda: "kmixdeck.channel.voice" not in node_names(stack))
    assert "Voice" in stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "UndoDescription").stdout
    assert stack.cli("channel", "default").stdout.strip() == "none"
    r = stack.cli("undo"); assert "Voice" in r.stdout
    assert wait(lambda: "kmixdeck.channel.voice" in node_names(stack) and "kmixdeck.link.voice.stream" in node_names(stack) and "kmixdeck.in.voice" in node_names(stack), tries=80)
    def cell(ch, mx): return stack.cli("cell", "get", ch, mx, json_out=True)
    assert wait(lambda: abs(cell("voice", "stream")["Volume"] - 10 ** (-18 / 20)) < 0.003, tries=60), cell("voice", "stream")
    assert wait(lambda: cell("voice", "monitor")["Muted"] is True, tries=30)
    ch = next(c for c in stack.cli("channel", "list", json_out=True) if c["Name"] == "Voice")
    assert ch["Trim"] == pytest.approx(10 ** (-6 / 20), abs=0.003) and ch["InputDevice"] == "fake.undo.mic"
    assert stack.cli("channel", "default").stdout.strip() == "voice"
    assert cell("game", "stream")["Follows"].endswith("/monitor"), "links of OTHER channels untouched"
    assert stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "UndoDescription").stdout.strip() == 's ""', "one level only"
    assert stack.cli("undo", check=False).returncode == 3
    # persisted: a daemon restart keeps the restored channel
    stack.restart_daemon()
    assert "Voice" in [c["Name"] for c in stack.cli("channel", "list", json_out=True)]
    assert wait(lambda: stack.cli("cell", "get", "voice", "stream", check=False).returncode == 0, tries=60)
    stack.cli("channel", "default", "system"); stack.cli("channel", "input", "voice", "none"); stack.cli("cell", "link", "game", "stream", "none")
    stack.cli("cell", "set", "voice", "stream", "1.0"); stack.cli("cell", "mute", "voice", "monitor", "off"); stack.cli("channel", "trim", "voice", "1.0")


def test_ch9_undo_restores_mix_with_outputs_and_master_and_is_cleared_by_a_new_add(fresh):
    stack = fresh
    from test_service_cli import make_fake_sink
    make_fake_sink(stack, "fake.undo.hp", "Undo HP")
    stack.cli("mix", "output", "monitor", "fake.undo.hp"); stack.cli("mix", "fallback", "monitor", "fake.undo.hp")
    stack.cli("mix", "volume", "monitor", "-10dB"); stack.cli("cell", "set", "game", "monitor", "-30dB")
    stack.cli("mix", "remove", "monitor")
    assert wait(lambda: "kmixdeck.mix.monitor" not in node_names(stack) and "kmixdeck.out.monitor" not in node_names(stack))
    stack.cli("undo")
    assert wait(lambda: "kmixdeck.mix.monitor" in node_names(stack) and "kmixdeck.out.monitor" in node_names(stack), tries=80)
    m = None
    for _ in range(60):
        m = next(x for x in stack.cli("mix", "list", json_out=True) if x["Slug"] == "monitor")
        if abs(m["Volume"] - 10 ** (-10 / 20)) < 0.003: break
        time.sleep(0.1)
    assert m["OutputDevice"] == "fake.undo.hp" and m["FallbackOutput"] == "fake.undo.hp" and m["Volume"] == pytest.approx(10 ** (-10 / 20), abs=0.003), m
    assert wait(lambda: abs(stack.cli("cell", "get", "game", "monitor", json_out=True)["Volume"] - 10 ** (-30 / 20)) < 0.003, tries=60)
    # a new add clears the undo slot (undoing "old" after "new" is what confuses people)
    stack.cli("mix", "remove", "monitor")
    stack.cli("mix", "add", "Recording")
    assert stack.cli("undo", check=False).returncode == 3
    stack.cli("mix", "remove", "recording"); stack.cli("mix", "add", "Monitor")
    assert wait(lambda: "kmixdeck.mix.monitor" in node_names(stack), tries=60)
    stack.cli("cell", "set", "game", "monitor", "1.0")
