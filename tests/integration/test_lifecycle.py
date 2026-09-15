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
