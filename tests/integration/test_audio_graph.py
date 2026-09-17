# SPDX-License-Identifier: GPL-3.0-or-later
"""Integration tests for the ADR 0002 audio graph, against a private PipeWire daemon.

Each assertion here is a requirement from docs/spec/requirements.md, referenced by ID.
Run: pytest -v tests/integration   (needs pipewire, wireplumber, pw-* tools, ffmpeg; no sound card)
"""
import subprocess, math, pytest
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
    # WirePlumber flushes stream-properties on a ~1 s timer; wait for the VALUE on disk, not for a clock
    # (ctest16: 1.0 s of sleep was not enough under full-suite load — the restart then read the old file).
    import time
    for _ in range(80):
        f = next((pw.runtime_dir / "state").rglob("stream-properties"), None)
        if f and f"media.name:{cell('voice', 'stream')}=" in f.read_text() and "0.25" in f.read_text(): break
        time.sleep(0.1)
    else: pytest.fail("WirePlumber never persisted the volume to stream-properties")
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


def test_no_feedback_loop_mix_outputs_never_target_a_channel(pw):
    """Regression (2026-09-14): with the default sink = a kmixdeck channel, the monitor-mix output looped back
    into the channel. Every kmixdeck.out.* must be linked to nothing or to a non-kmixdeck sink."""
    links = subprocess.run(["pw-link", "-l"], env=pw.env, capture_output=True, text=True).stdout.splitlines()
    bad = []
    for i, l in enumerate(links):
        if l.startswith("kmixdeck.out.") and ":output_" in l:
            j = i + 1
            while j < len(links) and links[j].startswith(" "):
                if "|->" in links[j] and "kmixdeck.channel." in links[j]: bad.append((l.strip(), links[j].strip()))
                j += 1
    assert not bad, bad


def test_dv4_daemon_follows_the_sessions_rate_and_quantum_and_forces_nothing():
    """DV-4: the session runs 44.1 kHz / quantum 256 (a DAW user's choice). kmixdeckd must live with it: every node it
    creates runs at the graph rate, nothing it writes contains clock.force-rate / clock.force-quantum, the meters
    still work, audio still flows — and the graph's rate/quantum are unchanged after the daemon joined."""
    import json, subprocess, time
    from pw_sandbox import start_private_pipewire
    from test_service_cli import Stack
    pw = start_private_pipewire(session_conf='context.properties = { default.clock.rate = 44100 default.clock.allowed-rates = [ 44100 ] default.clock.quantum = 256 default.clock.min-quantum = 256 default.clock.max-quantum = 256 }\n')
    try:
        def settings():
            for o in pw.dump():
                if o.get("type", "").endswith("Metadata") and o.get("props", {}).get("metadata.name") == "settings":
                    return {m["key"]: m["value"] for m in o.get("metadata", [])}
            return {}
        before = settings()
        assert before.get("clock.rate") == 44100 and before.get("clock.quantum") == 256, before
        pw.wait_node("kmixdeck.mix.stream")
        stack = Stack(pw)
        try:
            time.sleep(1.5)
            after = settings()
            assert after.get("clock.rate") == 44100 and after.get("clock.quantum") == 256, f"daemon changed the session clock: {before} → {after}"
            assert not after.get("clock.force-rate") and not after.get("clock.force-quantum"), f"daemon forced a clock: {after}"
            # nothing we ship or write asks for a rate
            written = (pw.runtime_dir / "pipewire.conf.d" / "90-kmixdeck.conf").read_text()
            for bad in ("clock.force-rate", "clock.force-quantum", "audio.rate", "node.rate", "clock.rate"):
                assert bad not in written, f"kmixdeck config asks for its own clock: {bad}"
            # every kmixdeck node runs at the graph rate
            for o in pw.dump():
                if not o.get("type", "").endswith("Node"): continue
                props = o.get("info", {}).get("props", {}); n = props.get("node.name", "")
                if not n.startswith("kmixdeck."): continue
                fmt = o.get("info", {}).get("params", {}).get("Format", [{}])
                rate = fmt[0].get("rate") if fmt else None
                assert rate in (None, 44100), f"{n} negotiated {rate} Hz in a 44.1 kHz session"
            # meters and audio still work at this rate
            p = pw.play_into("kmixdeck.channel.game")
            try:
                time.sleep(1.2)
                lv = subprocess.Popen([str(__import__('test_service_cli').BIN / "kmixdeck"), "--json", "levels"], env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
                ticks = [json.loads(lv.stdout.readline()) for _ in range(6)]; lv.terminate()
                assert max(t.get("channel/game", 0) for t in ticks) > 0.01, ticks[-1]
                assert pw.level_at("kmixdeck.mix.stream") > -40
            finally:
                p.kill(); p.wait()
        finally:
            stack.close()
    finally:
        pw.close()


def test_ct5_kmixdeck_and_the_pulse_world_show_one_truth():
    """CT-5: the Plasma volume applet (plasma-pa) speaks PulseAudio to pipewire-pulse. A channel's trim set in kmixdeck
    is the sink volume pactl shows; a mix's master mute set in kmixdeck is the mute pactl shows; and the other way
    round — the applet muting the 'Game' sink is what kmixdeck reports. Volumes are compared in dB, both ways."""
    import math, subprocess, time
    pulsectl = pytest.importorskip("pulsectl", reason="CT-5 needs the PulseAudio client (pip install pulsectl)")
    from pw_sandbox import start_private_pipewire
    from test_service_cli import Stack
    pw = start_private_pipewire()
    try:
        pw.start_pulse()
        pw.wait_node("kmixdeck.mix.stream")
        stack = Stack(pw); stack.env.update({k: pw.env[k] for k in ("PULSE_SERVER", "PULSE_RUNTIME_PATH")})
        try:
            time.sleep(1.0)
            with pulsectl.Pulse("ct5", server=pw.env["PULSE_SERVER"]) as pulse:
                def sink(name):
                    for _ in range(50):
                        for s in pulse.sink_list():
                            if s.name == name: return s
                        time.sleep(0.1)
                    raise AssertionError(f"pulse does not see {name}: {[s.name for s in pulse.sink_list()]}")
                def pulse_db(s):  # pulse's cubic volume → dB (what the applet shows as %)
                    v = pulse.volume_get_all_chans(s); return 20 * math.log10(v ** 3) if v > 0 else -90
                # kmixdeck → applet: channel trim −12 dB
                stack.cli("channel", "trim", "game", "-12dB"); time.sleep(0.6)
                g = sink("kmixdeck.channel.game")
                assert abs(pulse_db(g) - (-12)) < 0.5, f"applet shows {pulse_db(g):.1f} dB for a −12 dB trim"
                # kmixdeck → applet: mix master mute
                stack.cli("mix", "mute", "stream", "on"); time.sleep(0.6)
                assert sink("kmixdeck.mix.stream").mute == 1, "applet must show the stream mix muted"
                stack.cli("mix", "mute", "stream", "off"); time.sleep(0.6)
                assert sink("kmixdeck.mix.stream").mute == 0
                # applet → kmixdeck: the user drags Game to −6 dB and mutes System in the applet
                pulse.volume_set_all_chans(g, (10 ** (-6 / 20)) ** (1 / 3)); time.sleep(0.8)
                st = stack.cli("status", json_out=True)
                trim = next(c for c in st["channels"] if c["Slug"] == "game")["Trim"]
                assert abs(20 * math.log10(trim) - (-6)) < 0.5, f"kmixdeck reports trim {20 * math.log10(trim):.1f} dB after the applet set −6 dB"
                pulse.mute(sink("kmixdeck.channel.system"), True); time.sleep(0.8)
                assert next(c for c in stack.cli("status", json_out=True)["channels"] if c["Slug"] == "system")["Muted"] is True, "kmixdeck must show the applet's mute"
                pulse.mute(sink("kmixdeck.channel.system"), False); time.sleep(0.8)
                assert next(c for c in stack.cli("status", json_out=True)["channels"] if c["Slug"] == "system")["Muted"] is False
                # and it survives a daemon restart without a jump (the daemon re-applies ITS truth = the same numbers)
                stack.restart_daemon(); time.sleep(1.5)
                assert abs(pulse_db(sink("kmixdeck.channel.game")) - (-6)) < 0.5, "restart changed what the applet shows"
        finally:
            stack.cli("channel", "trim", "game", "0dB", check=False); stack.close()
    finally:
        pw.close()
