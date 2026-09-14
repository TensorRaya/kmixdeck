# SPDX-License-Identifier: GPL-3.0-or-later
"""Integration tests for the ADR 0002 audio graph, against a private PipeWire daemon.

Each assertion here is a requirement from docs/spec/requirements.md, referenced by ID.
Run: pytest -v tests/integration   (needs pipewire, wireplumber, pw-* tools, ffmpeg; no sound card)
"""
import math, pytest
from pw_sandbox import start_private_pipewire

CHANNELS = ["game", "system", "voice"]
MIXES = ["monitor", "stream"]


@pytest.fixture(scope="module")
def pw():
    d = start_private_pipewire()
    d.wait_node("kmixdeck.mix.stream")
    yield d
    d.close()


def cell(ch, mix): return f"kmixdeck.link.{ch}.{mix}"


def test_graph_comes_up_from_config_alone(pw):
    """DV-1: the graph exists without any app running — it's pure PipeWire config."""
    names = {o.get("info", {}).get("props", {}).get("node.name") for o in pw.dump()}
    for ch in CHANNELS: assert f"kmixdeck.channel.{ch}" in names
    for mx in MIXES: assert f"kmixdeck.mix.{mx}" in names
    for ch in CHANNELS:
        for mx in MIXES: assert cell(ch, mx) in names, f"cell {ch}×{mx} missing"
    assert "kmixdeck.source.stream" in names, "MX-3: stream mix must be exposed as a capture source"


def test_all_cells_default_to_unity(pw):
    for ch in CHANNELS:
        for mx in MIXES:
            p = pw.props(cell(ch, mx))
            assert p["volume"] == pytest.approx(1.0) and p["mute"] is False


def test_mx2_per_mix_level_is_independent(pw):
    """MX-2: the same channel at different levels in two mixes. Expect −12 dB for linear 0.25."""
    pw.set_volume(cell("game", "monitor"), 1.0); pw.set_volume(cell("game", "stream"), 0.25)
    play = pw.play_into("kmixdeck.channel.game")
    try:
        mon, strm = pw.level_at("kmixdeck.mix.monitor"), pw.level_at("kmixdeck.mix.stream")
    finally:
        play.kill(); play.wait()
    assert mon > -40, f"monitor mix silent ({mon} dB) — routing broken"
    assert strm - mon == pytest.approx(-12.04, abs=0.5), f"monitor={mon} stream={strm}"
    pw.set_volume(cell("game", "stream"), 1.0)


def test_mx2_other_direction(pw):
    pw.set_volume(cell("game", "monitor"), 0.5); pw.set_volume(cell("game", "stream"), 1.0)
    play = pw.play_into("kmixdeck.channel.game")
    try:
        mon, strm = pw.level_at("kmixdeck.mix.monitor"), pw.level_at("kmixdeck.mix.stream")
    finally:
        play.kill(); play.wait()
    assert strm - mon == pytest.approx(6.02, abs=0.5)
    pw.set_volume(cell("game", "monitor"), 1.0)


def test_ch4_mute_is_per_cell(pw):
    """CH-4: muting Game→Stream silences only that cell; Game→Monitor and System→Stream unaffected."""
    pw.set_volume(cell("game", "stream"), 1.0, mute=True)
    play = pw.play_into("kmixdeck.channel.game")
    try:
        mon, strm = pw.level_at("kmixdeck.mix.monitor"), pw.level_at("kmixdeck.mix.stream")
    finally:
        play.kill(); play.wait()
    assert mon > -40 and strm == -math.inf, f"monitor={mon} stream={strm}"
    play = pw.play_into("kmixdeck.channel.system")
    try:
        strm2 = pw.level_at("kmixdeck.mix.stream")
    finally:
        play.kill(); play.wait()
    assert strm2 > -40, "System→Stream must be unaffected by Game→Stream mute"
    pw.set_volume(cell("game", "stream"), 1.0, mute=False)


def test_dv7_levels_and_mute_survive_daemon_restart(pw):
    """DV-7: cell volume + mute persist across pipewire/wireplumber restart without the app."""
    pw.set_volume(cell("voice", "stream"), 0.25, mute=False)
    pw.set_volume(cell("voice", "monitor"), 1.0, mute=True)
    import time; time.sleep(1.0)  # WirePlumber writes state with a small delay
    pw.restart()
    assert pw.props(cell("voice", "stream")) == {"volume": pytest.approx(0.25), "mute": False}
    assert pw.props(cell("voice", "monitor")) == {"volume": pytest.approx(1.0), "mute": True}
    pw.set_volume(cell("voice", "stream"), 1.0); pw.set_volume(cell("voice", "monitor"), 1.0, mute=False)


def test_dv7_state_is_keyed_by_stable_name_not_display_name(pw):
    """The WirePlumber state key must be media.name == node.name so renames don't lose levels."""
    state = next((pw.runtime_dir / "state").rglob("stream-properties")).read_text()
    assert f"Output/Audio:media.name:{cell('game','stream')}=" in state, state[:600]
    assert "Game\\s→\\sStream" not in state.split("kmixdeck.link")[0]  # no description-keyed entries for our links


def test_cell_nodes_run_in_one_graph_cycle(pw):
    """NF (latency): while audio flows, every kmixdeck node is running and none asks for its own
    latency/quantum — they all follow the driver. (pw-top -b gives an empty table in a sandbox,
    so this reads node state + node.latency from pw-dump instead.)"""
    import time
    play = pw.play_into("kmixdeck.channel.game")
    try:
        time.sleep(1.5)
        nodes = [o for o in pw.dump() if str(o.get("info", {}).get("props", {}).get("node.name", "")).startswith("kmixdeck.")]
    finally:
        play.kill(); play.wait()
    running = [o for o in nodes if o["info"].get("state") == "running"]
    assert len(running) >= 6, [(o["info"]["props"]["node.name"], o["info"].get("state")) for o in nodes]
    own_latency = [o["info"]["props"]["node.name"] for o in nodes if "node.latency" in o["info"]["props"] or "node.force-quantum" in o["info"]["props"]]
    assert own_latency == [], f"nodes forcing their own quantum: {own_latency}"
