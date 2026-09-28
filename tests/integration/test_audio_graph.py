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


def chain(ch): return f"kmixdeck.cells.{ch}"


def gains(pw, ch) -> dict:
    """ADR 0013: a cell is the "Gain 1" pair L<mix>/R<mix> of the channel's cell chain — read it from the Props params."""
    n = pw.node(chain(ch)); assert n is not None, f"cell chain of {ch} missing"
    for p in n["info"].get("params", {}).get("Props", []):
        ps = p.get("params") or []
        g = {ps[i]: ps[i + 1] for i in range(0, len(ps) - 1, 2) if str(ps[i]).endswith(":Gain 1")}
        if g: return g                              # the first Props with params is audioconvert's channelmix block
    raise AssertionError(f"{chain(ch)}: no control params")


def wait_gains(pw, ch, timeout=5.0, **want) -> dict:
    """The chain publishes its control values a moment after the node appears (the first Props read 0.0 for every
    gain, measured 2026-09-28) — wait for the VALUES, like wait_props() does for volumes."""
    import time
    t0 = time.time(); g = {}
    while time.time() - t0 < timeout:
        g = gains(pw, ch)
        if all(abs(g.get(k.replace("_", ":").replace(":Gain1", ":Gain 1"), -1) - v) < 1e-3 for k, v in want.items()): return g
        time.sleep(0.1)
    raise AssertionError(f"{chain(ch)}: gains never became {want} (last: {g})")


def chain_block(conf: str, ch: str) -> str:
    """The filter-chain module block of one channel in the rendered fragment."""
    return next(b for b in conf.split("libpipewire-module-filter-chain") if f'node.name = "{chain(ch)}"' in b)


def set_cell(pw, ch, mix, linear):
    """What the daemon's writeCell does (Graph::setControl) — here straight via pw-cli, no app running."""
    subprocess.run(["pw-cli", "set-param", str(pw.node_id(chain(ch))), "Props",
                    f'{{ params = [ "L{mix}:Gain 1" {linear} "R{mix}:Gain 1" {linear} ] }}'],
                   env=pw.env, check=True, capture_output=True)
    import time; time.sleep(0.3)


def test_graph_comes_up_from_config_alone(pw):
    """DV-1: the graph exists without any app running — it's pure PipeWire config. ADR 0013: one cell chain per
    channel, one bus, one tap per mix — and no per-cell loopback anywhere."""
    names = {o.get("info", {}).get("props", {}).get("node.name") for o in pw.dump()}
    for ch in CHANNELS: assert f"kmixdeck.channel.{ch}" in names
    for mx in MIXES: assert f"kmixdeck.mix.{mx}" in names
    assert "kmixdeck.bus" in names, "ADR 0013: the cell bus is missing"
    for ch in CHANNELS: assert chain(ch) in names, f"cell chain of {ch} missing"
    for mx in MIXES: assert f"kmixdeck.tap.{mx}" in names, f"bus tap of {mx} missing"
    assert not [n for n in names if n and n.startswith("kmixdeck.link.")], "ADR 0013: no per-cell loopback may exist"
    assert "kmixdeck.source.stream" in names, "MX-3: stream mix must be exposed as a capture source"


def test_all_cells_default_to_unity(pw):
    for ch in CHANNELS:
        g = wait_gains(pw, ch, **{f"{s}{mx}_Gain1": 1.0 for mx in MIXES for s in "LR"})
        for mx in MIXES:
            assert g[f"L{mx}:Gain 1"] == pytest.approx(1.0) and g[f"R{mx}:Gain 1"] == pytest.approx(1.0), (ch, mx, g)


def test_mx2_per_mix_level_is_independent(pw):
    """MX-2: the same channel at different levels in two mixes. Expect −12 dB for linear 0.25."""
    set_cell(pw, "game", "monitor", 1.0); set_cell(pw, "game", "stream", 0.25)
    play = pw.play_into("kmixdeck.channel.game")
    try:
        mon, strm = pw.level_at("kmixdeck.mix.monitor"), pw.level_at("kmixdeck.mix.stream")
    finally:
        play.kill(); play.wait()
    assert mon > -40, f"monitor mix silent ({mon} dB) — routing broken"
    assert strm - mon == pytest.approx(-12.04, abs=0.5), f"monitor={mon} stream={strm}"
    set_cell(pw, "game", "stream", 1.0)


def test_mx2_other_direction(pw):
    set_cell(pw, "game", "monitor", 0.5); set_cell(pw, "game", "stream", 1.0)
    play = pw.play_into("kmixdeck.channel.game")
    try:
        mon, strm = pw.level_at("kmixdeck.mix.monitor"), pw.level_at("kmixdeck.mix.stream")
    finally:
        play.kill(); play.wait()
    assert strm - mon == pytest.approx(6.02, abs=0.5)
    set_cell(pw, "game", "monitor", 1.0)


def test_ch4_mute_is_per_cell(pw):
    """CH-4: muting Game→Stream silences only that cell; Game→Monitor and System→Stream unaffected.
    ADR 0013: a muted cell is gain 0 on its pair (the daemon keeps the fader value in the layout)."""
    set_cell(pw, "game", "stream", 0.0)
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
    set_cell(pw, "game", "stream", 1.0)


def test_dv7_levels_and_mute_survive_daemon_restart():
    """DV-7: cell volume + mute survive a pipewire/wireplumber restart WITHOUT the app. ADR 0013: WirePlumber does not
    persist filter controls, so the daemon writes them into the config fragment; with kmixdeckd stopped, a restart
    must come back with exactly those gains (−12 dB in voice→stream, voice→monitor silent)."""
    import time
    from test_service_cli import Stack
    d = start_private_pipewire(); d.wait_node("kmixdeck.mix.stream")
    try:
        s = Stack(d)
        try:
            s.cli("cell", "set", "voice", "stream", "-12.0412dB")      # = linear 0.25
            s.cli("cell", "mute", "voice", "monitor", "on")
            conf = d.runtime_dir / "pipewire.conf.d" / "90-kmixdeck.conf"
            for _ in range(80):   # the fader write is debounced; wait for the VALUE in the fragment, not a clock
                if 'name = Lmonitor control = { "Gain 1" = 0 }' in chain_block(conf.read_text(), "voice"): break
                time.sleep(0.1)
            else: pytest.fail("the daemon never wrote the cell into the config fragment")
        finally:
            s.close()                                   # the app is GONE from here on
        d.restart()
        d.wait_node(chain("voice"))
        g = wait_gains(d, "voice", Lstream_Gain1=0.25, Lmonitor_Gain1=0.0)
        assert g["Lstream:Gain 1"] == pytest.approx(0.25, abs=0.002) and g["Lmonitor:Gain 1"] == 0.0, g
        play = d.play_into("kmixdeck.channel.voice")
        try:
            mon, strm = d.level_at("kmixdeck.mix.monitor"), d.level_at("kmixdeck.mix.stream")
        finally:
            play.kill(); play.wait()
        assert mon == -math.inf and strm > -40, f"monitor={mon} stream={strm}"
    finally:
        d.close()


def test_dv7_state_is_keyed_by_stable_name_not_display_name(pw):
    """Cell state is keyed by the stable slugs (chain node.name = channel slug, control = mix slug), never by the
    display name — renaming "Stream" in the UI must not lose a single cell."""
    conf = (pw.runtime_dir / "pipewire.conf.d" / "90-kmixdeck.conf").read_text()
    assert f'node.name = "{chain("game")}"' in conf and 'name = Lstream control = { "Gain 1" =' in conf
    assert "Game → Stream" not in conf and "L Stream" not in conf


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
    import math, time
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
