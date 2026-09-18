# SPDX-License-Identifier: GPL-3.0-or-later
"""Routing edge cases, measured acoustically: a 440 Hz tone goes in, RMS comes out at the point that matters.

Thresholds: a hot path reads > -30 dBFS, silence < -60 dBFS (the tone itself lands ≈ -12 dB at unity; the
noise floor of a null sink is far below -90). Every assertion here is about a promise from ADR 0002/0007:
 - a fader lives in exactly one (channel, mix) cell — never leaks into the other mix
 - channel trim/mute are before all mixes; mix mute silences the mix, not the channel
 - a parked mix output produces nothing on the default sink
 - the capture source carries the mix so OBS gets exactly what "Stream" is
 - a hardware input lands in its channel and follows the channel's cells
"""
import json, subprocess, time
from pathlib import Path
import pytest
from test_service_cli import Stack, make_fake_sink, out_link_target
from pw_sandbox import start_private_pipewire
from waiting import wait_for

HOT, SILENT = -30.0, -60.0


@pytest.fixture(scope="module")
def stack():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    s = Stack(pw)
    make_fake_sink(s, "fake.headphones", "Fake Headphones")
    make_fake_sink(s, "fake.default", "Fake Default Sink")
    subprocess.run(["wpctl", "set-default", str(pw.node_id("fake.default"))], env=pw.env, capture_output=True)
    yield s
    s.close(); pw.close()


@pytest.fixture
def tone(stack):
    """Tone into the game channel for the duration of one test; all faders reset afterwards."""
    p = stack.pw.play_into("kmixdeck.channel.game")
    yield p
    p.kill(); p.wait()
    for mx in ("monitor", "stream"):
        stack.cli("cell", "set", "game", mx, "1.0"); stack.cli("cell", "mute", "game", mx, "off")
    stack.cli("channel", "trim", "game", "1.0"); stack.cli("channel", "mute", "game", "off")


def settle(): time.sleep(0.4)


def node_names(stack):
    return [l.split('"')[1] for l in subprocess.run(["pw-cli", "ls", "Node"], env=stack.pw.env, capture_output=True, text=True).stdout.splitlines() if "node.name" in l]


def test_fader_in_one_mix_never_leaks_into_the_other(stack, tone):
    stack.cli("cell", "set", "game", "stream", "0.0"); settle()
    assert stack.pw.level_at("kmixdeck.mix.stream") < SILENT
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT, "monitor must be untouched by the stream fader"
    stack.cli("cell", "set", "game", "stream", "1.0"); stack.cli("cell", "set", "game", "monitor", "-40dB"); settle()
    s, m = stack.pw.level_at("kmixdeck.mix.stream"), stack.pw.level_at("kmixdeck.mix.monitor")
    assert s > HOT and m < s - 30, f"monitor -40 dB must show up as ~40 dB below stream: stream={s:.1f} monitor={m:.1f}"


def test_cell_mute_is_per_mix_and_reversible(stack, tone):
    stack.cli("cell", "mute", "game", "monitor", "on"); settle()
    assert stack.pw.level_at("kmixdeck.mix.monitor") < SILENT
    assert stack.pw.level_at("kmixdeck.mix.stream") > HOT
    stack.cli("cell", "mute", "game", "monitor", "off"); settle()
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT, "unmute must restore the previous level, not leave it at 0"


def test_channel_trim_and_mute_hit_every_mix(stack, tone):
    base = stack.pw.level_at("kmixdeck.mix.stream")
    stack.cli("channel", "trim", "game", "-20dB"); settle()
    s, m = stack.pw.level_at("kmixdeck.mix.stream"), stack.pw.level_at("kmixdeck.mix.monitor")
    assert base - 24 < s < base - 16 and base - 24 < m < base - 16, f"trim -20 dB must reach both mixes: {s:.1f}/{m:.1f} from {base:.1f}"
    stack.cli("channel", "mute", "game", "on"); settle()
    assert stack.pw.level_at("kmixdeck.mix.stream") < SILENT and stack.pw.level_at("kmixdeck.mix.monitor") < SILENT
    # cells report their own state, not the channel's — the UI shows the cell unmuted while the channel is muted
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Muted"] is False


def test_parked_mix_plays_nothing_on_the_default_sink(stack, tone):
    assert out_link_target(stack, "stream") == "kmixdeck.null"
    assert stack.pw.level_at("kmixdeck.mix.stream") > HOT, "the mix itself carries audio"
    assert stack.pw.level_at("fake.default") < SILENT, "…but nothing may reach the default sink while parked (feedback trap, ADR 0002)"


def test_output_device_receives_exactly_the_mix(stack, tone):
    stack.cli("mix", "output", "monitor", "fake.headphones")
    wait_for(lambda: out_link_target(stack, "monitor") == "fake.headphones", timeout=4.0, what="out_link_target(stack, 'monitor') == 'fake.headphones'")
    settle()
    assert stack.pw.level_at("fake.headphones") > HOT
    stack.cli("cell", "set", "game", "monitor", "0.0"); settle()
    assert stack.pw.level_at("fake.headphones") < SILENT, "the device follows the mix fader"
    stack.cli("cell", "set", "game", "monitor", "1.0")
    stack.cli("mix", "output", "monitor", "none")
    wait_for(lambda: out_link_target(stack, "monitor") == "kmixdeck.null", timeout=4.0, what="out_link_target(stack, 'monitor') == 'kmixdeck.null'")
    settle()
    assert stack.pw.level_at("fake.headphones") < SILENT, "'none' must actually stop the audio on the device"


def test_two_mixes_on_the_same_device_do_not_double(stack, tone):
    """Edge: Monitor AND Stream both on the headphones. Allowed, but the level must be the sum, not a crash/loop."""
    for mx in ("monitor", "stream"): stack.cli("mix", "output", mx, "fake.headphones")
    wait_for(lambda: out_link_target(stack, "monitor") == "fake.headphones" and out_link_target(stack, "stream") == "fake.headphones", timeout=4.0, what="out_link_target(stack, 'monitor') == 'fake.headphones' and out_link_ta")
    settle()
    both = stack.pw.level_at("fake.headphones")
    stack.cli("mix", "output", "stream", "none")
    wait_for(lambda: out_link_target(stack, "stream") == "kmixdeck.null", timeout=4.0, what="out_link_target(stack, 'stream') == 'kmixdeck.null'")
    settle()
    one = stack.pw.level_at("fake.headphones")
    assert 4 < both - one < 8, f"two identical mixes summed should read ≈ +6 dB, got {both - one:.1f} (both={both:.1f} one={one:.1f})"
    stack.cli("mix", "output", "monitor", "none")


def test_capture_source_carries_the_stream_mix_for_obs(stack, tone):
    """OBS records kmixdeck.source.stream. It must be the stream mix — and only that."""
    def src_level():
        out = stack.pw.runtime_dir / f"src-{time.time_ns()}.wav"
        rec = subprocess.Popen(["timeout", "2.5", "pw-record", "-P", "{ node.autoconnect = false }", "--rate", "48000", "--channels", "2", "--format", "s16", str(out)],
                               env=dict(stack.pw.env, PW_LATENCY="1024/48000"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.6)
        for ch in ("FL", "FR"): subprocess.run(["pw-link", f"kmixdeck.source.stream:capture_{ch}", f"pw-record:input_{ch}"], env=stack.pw.env, capture_output=True)
        rec.wait()
        return stack.pw.rms_db(out)
    assert src_level() > HOT
    stack.cli("cell", "mute", "game", "stream", "on"); settle()
    assert src_level() < SILENT, "muting game in STREAM must silence the capture source"
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT, "…while monitor keeps playing"


def test_hardware_input_lands_in_its_channel_and_follows_the_cells(stack):
    """A mic attached to 'voice' must be audible in both mixes and obey voice's faders.
    The "mic" is a real Audio/Source with a tone behind it: null sink → pw-loopback → Audio/Source node
    (a bare null-audio-sink with media.class=Audio/Source has no input ports and stays silent)."""
    subprocess.run(["pw-cli", "create-node", "adapter", "{ factory.name=support.null-audio-sink node.name=fake.mic.feed media.class=Audio/Sink audio.position=[FL FR] object.linger=true monitor.channel-volumes=true }"],
                   env=stack.pw.env, capture_output=True)
    stack.pw.wait_node("fake.mic.feed")
    lb = subprocess.Popen(["pw-loopback", "-n", "fake-mic",
                           "--capture-props", "{ node.name=fake.mic.cap node.target=fake.mic.feed stream.capture.sink=true node.passive=true audio.position=[FL FR] }",
                           "--playback-props", '{ node.name=fake.mic node.description="Fake Microphone" media.class=Audio/Source audio.position=[FL FR] }'],
                          env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    play = None
    try:
        stack.pw.wait_node("fake.mic")
        play = stack.pw.play_into("fake.mic.feed")
        wait_for(lambda: "fake.mic" in stack.cli("devices", "in", json_out=True), timeout=3.0, what="'fake.mic' in stack.cli('devices', 'in', json_out=True)")
        stack.cli("channel", "input", "voice", "fake.mic")
        stack.pw.wait_node("kmixdeck.in.voice")
        time.sleep(1.0)
        assert stack.pw.level_at("kmixdeck.channel.voice") > HOT, "the mic must arrive in the voice channel"
        assert stack.pw.level_at("kmixdeck.mix.stream") > HOT and stack.pw.level_at("kmixdeck.mix.monitor") > HOT
        stack.cli("cell", "set", "voice", "stream", "0.0"); settle()
        assert stack.pw.level_at("kmixdeck.mix.stream") < SILENT and stack.pw.level_at("kmixdeck.mix.monitor") > HOT
        stack.cli("cell", "set", "voice", "stream", "1.0")
        stack.cli("channel", "input", "voice", "none")
        wait_for(lambda: stack.pw.node("kmixdeck.in.voice") is None, timeout=4.0, what="stack.pw.node('kmixdeck.in.voice') is None")
        settle()
        assert stack.pw.level_at("kmixdeck.channel.voice") < SILENT, "detaching the input must stop the audio"
    finally:
        if play: play.kill(); play.wait()
        lb.kill(); lb.wait()


def test_levels_survive_daemon_restart_exactly(stack, tone):
    """Faders are PipeWire state: kill and restart the daemon mid-tone — no click to unity, no reset."""
    stack.cli("cell", "set", "game", "stream", "-18dB"); stack.cli("cell", "mute", "game", "monitor", "on"); settle()
    before = stack.pw.level_at("kmixdeck.mix.stream")
    stack.restart_daemon(); time.sleep(1.0)
    after = stack.pw.level_at("kmixdeck.mix.stream")
    assert abs(before - after) < 1.0, f"stream level changed across daemon restart: {before:.1f} → {after:.1f}"
    assert stack.pw.level_at("kmixdeck.mix.monitor") < SILENT, "mute must survive too"
    c = stack.cli("cell", "get", "game", "stream", json_out=True)
    assert c["Volume"] == pytest.approx(10 ** (-18 / 20), abs=0.003)


def test_mix_master_fader_and_mute_hit_outputs_and_capture_source(stack, tone):
    """MX-6: the mix master sits on the mix sink → one control for every output and for OBS's capture source."""
    stack.cli("mix", "output", "stream", "fake.headphones")
    wait_for(lambda: out_link_target(stack, "stream") == "fake.headphones", timeout=4.0, what="out_link_target(stack, 'stream') == 'fake.headphones'")
    settle()
    base_dev = stack.pw.level_at("fake.headphones")
    assert base_dev > HOT
    stack.cli("mix", "volume", "stream", "-20dB"); settle()
    dev = stack.pw.level_at("fake.headphones")
    assert base_dev - 24 < dev < base_dev - 16, f"master -20 dB must reach the device: {base_dev:.1f} → {dev:.1f}"
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT, "the other mix is untouched"
    m = next(m for m in stack.cli("mix", "list", json_out=True) if m["Slug"] == "stream")
    assert m["Volume"] == pytest.approx(0.1, abs=0.003) and m["Muted"] is False
    stack.cli("mix", "mute", "stream", "on"); settle()
    assert stack.pw.level_at("fake.headphones") < SILENT
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Muted"] is False, "cells keep their own state under a mix mute"
    # ToggleMute is atomic (hotkey / Stream Deck path)
    stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1/mix/stream", "org.kmixdeck1.Mix", "ToggleMute")
    settle()
    assert stack.pw.level_at("fake.headphones") > HOT - 20
    stack.cli("mix", "volume", "stream", "1.0")
    stack.cli("mix", "output", "stream", "none")
    assert stack.cli("mix", "volume", "stream", "+3dB", check=False).returncode in (1, 4), "above unity must be refused"


def _out_targets(stack, mix):
    """All link targets of kmixdeck.out.<mix>*: {node.name -> sink}"""
    links = subprocess.run(["pw-link", "-l"], env=stack.pw.env, capture_output=True, text=True).stdout.splitlines()
    res = {}
    for i, l in enumerate(links):
        if l.startswith(f"kmixdeck.out.{mix}") and ":output_FL" in l:
            src = l.split(":")[0]
            for j in range(i + 1, min(i + 4, len(links))):
                if "|->" in links[j]: res[src] = links[j].split("|->")[1].strip().split(":")[0]; break
    return res


def _wait_targets(stack, mix, want, tries=60):
    for _ in range(tries):
        t = _out_targets(stack, mix)
        if set(t.values()) == set(want): return t
        time.sleep(0.1)
    return _out_targets(stack, mix)


def test_mx9_mix_plays_to_several_outputs_and_dv15_fallback_takes_over(stack, tone):
    """MX-9: headphones AND speakers at once; DV-15: while both are unplugged the fallback plays, never the default sink."""
    from test_service_cli import make_fake_sink, destroy_node
    make_fake_sink(stack, "fake.speakers", "Fake Speakers")
    make_fake_sink(stack, "fake.fallback", "Fake Fallback")
    stack.cli("mix", "output", "monitor", "fake.headphones")
    stack.cli("mix", "output-add", "monitor", "fake.speakers")
    assert stack.cli("mix", "outputs", "monitor", json_out=True) == ["fake.headphones", "fake.speakers"]
    assert stack.cli("mix", "output-add", "monitor", "fake.speakers").returncode == 0, "adding twice is idempotent"
    assert stack.cli("mix", "outputs", "monitor", json_out=True) == ["fake.headphones", "fake.speakers"]
    t = _wait_targets(stack, "monitor", {"fake.headphones", "fake.speakers"})
    assert set(t.values()) == {"fake.headphones", "fake.speakers"}, t
    settle()
    hp, sp = stack.pw.level_at("fake.headphones"), stack.pw.level_at("fake.speakers")
    assert hp > HOT and sp > HOT and abs(hp - sp) < 1.0, f"both outputs carry the same mix: hp={hp:.1f} sp={sp:.1f}"
    # the single-output view replaces only the FIRST entry
    stack.cli("mix", "output", "monitor", "fake.headphones")
    assert stack.cli("mix", "outputs", "monitor", json_out=True) == ["fake.headphones", "fake.speakers"]
    # DV-15: fallback while everything is gone. (tone.wav is 20 s; this test is longer — restart the tone)
    tone.kill(); tone.wait(); tone2 = stack.pw.play_into("kmixdeck.channel.game")
    stack.cli("mix", "fallback", "monitor", "fake.fallback")
    assert stack.pw.level_at("fake.fallback") < SILENT, "fallback is silent while a primary output is present"
    destroy_node(stack, "fake.headphones"); destroy_node(stack, "fake.speakers")
    t = _wait_targets(stack, "monitor", {"fake.fallback", "kmixdeck.null"}, tries=80)
    assert "fake.fallback" in t.values(), f"fallback must take over: {t}"
    time.sleep(1.0)   # two destroys + a retarget: give the loopback a moment to run again before measuring
    lvl = max(stack.pw.level_at("fake.fallback") for _ in range(2))
    assert lvl > HOT, f"fallback linked but silent: {lvl}"
    assert stack.pw.level_at("fake.default") < SILENT, "never the default sink"
    m = next(m for m in stack.cli("mix", "list", json_out=True) if m["Slug"] == "monitor")
    assert m["OutputPresent"] is False and m["Outputs"] == ["fake.headphones", "fake.speakers"], "configuration is kept while unplugged"
    # replug → primary wins again, fallback goes quiet
    make_fake_sink(stack, "fake.headphones", "Fake Headphones")
    t = _wait_targets(stack, "monitor", {"fake.headphones", "kmixdeck.null"}, tries=80)
    assert "fake.headphones" in t.values(), t
    settle()
    assert stack.pw.level_at("fake.headphones") > HOT and stack.pw.level_at("fake.fallback") < SILENT
    # cleanup: back to the module's baseline
    stack.cli("mix", "output-remove", "monitor", "fake.speakers")
    assert stack.cli("mix", "output-remove", "monitor", "fake.speakers", check=False).returncode == 4, "removing a non-output is an error"
    stack.cli("mix", "fallback", "monitor", "none"); stack.cli("mix", "output", "monitor", "none")
    assert stack.cli("mix", "outputs", "monitor", json_out=True) == []
    _wait_targets(stack, "monitor", {"kmixdeck.null"})
    tone2.kill(); tone2.wait()


def test_mx7_cell_link_stream_follows_monitor_and_breaks_when_touched(stack, tone):
    """MX-7: game/stream follows game/monitor — volume AND mute mirror, measured; touching stream unlinks."""
    def follows(): return stack.cli("cell", "get", "game", "stream", json_out=True)["Follows"]
    stack.cli("cell", "link", "game", "stream", "monitor")
    assert follows() == "/org/kmixdeck1/mix/monitor"
    stack.cli("cell", "set", "game", "monitor", "-20dB"); settle()
    s, m = stack.pw.level_at("kmixdeck.mix.stream"), stack.pw.level_at("kmixdeck.mix.monitor")
    assert abs(s - m) < 1.0 and m < -25, f"stream must mirror monitor's −20 dB: stream={s:.1f} monitor={m:.1f}"
    assert stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] == pytest.approx(0.1, abs=0.003)
    stack.cli("cell", "mute", "game", "monitor", "on"); settle()
    assert stack.pw.level_at("kmixdeck.mix.stream") < SILENT, "mute mirrors too"
    stack.cli("cell", "mute", "game", "monitor", "off"); settle()
    assert abs(stack.pw.level_at("kmixdeck.mix.stream") - s) < 1.0, "unmute restores the mirrored −20 dB, not unity"
    # the link survives a daemon restart (it is layout, not graph state)
    stack.restart_daemon()
    assert follows() == "/org/kmixdeck1/mix/monitor"
    stack.cli("cell", "set", "game", "monitor", "0dB"); settle()
    assert stack.pw.level_at("kmixdeck.mix.stream") > HOT
    # touching the follower breaks the link — and monitor is untouched
    stack.cli("cell", "set", "game", "stream", "-40dB"); settle()
    assert follows() == "/"
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT and stack.pw.level_at("kmixdeck.mix.stream") < -45
    stack.cli("cell", "set", "game", "monitor", "-6dB"); settle()
    assert stack.pw.level_at("kmixdeck.mix.stream") < -45, "unlinked: stream no longer follows"
    # guards
    assert stack.cli("cell", "link", "game", "stream", "stream", check=False).returncode == 1
    stack.cli("cell", "link", "game", "stream", "monitor")
    stack.busctl("set-property", "org.kmixdeck1", "/org/kmixdeck1/cell/game/monitor", "org.kmixdeck1.Cell", "Follows", "o", "/org/kmixdeck1/mix/stream")
    assert stack.cli("cell", "get", "game", "monitor", json_out=True)["Follows"] == "/", "A↔B loop must be refused"
    stack.cli("cell", "link", "game", "stream", "none")
    assert follows() == "/"
    stack.cli("cell", "set", "game", "monitor", "1.0")


def test_ch12_multi_assign_relays_only_that_app(stack):
    """CH-12: an app on game+voice is heard in both channels — but the relay must carry ONLY that app.
    Measured on the dev machine 2026-09-16: a relay capturing the primary channel's monitor dragged every other app of
    that channel along. So: A → game+voice, B → game only; mute A; voice must go silent while game stays hot."""
    from test_service_cli import FAKE_APP, current_sink_of
    b_props = FAKE_APP.replace("FakeGame", "OtherGame").replace("fakegame", "othergame")
    a = subprocess.Popen(["pw-play", "-P", FAKE_APP, str(stack.pw.tone())], env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    b = subprocess.Popen(["pw-play", "-P", b_props, str(stack.pw.tone())], env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        # both on the bus AND linked by WirePlumber (see start_fake_app: moving an unlinked stream is not remembered)
        for _ in range(100):
            names = {x["Name"] for x in stack.cli("app", "list", json_out=True)}
            if {"FakeGame", "OtherGame"} <= names and current_sink_of(stack) and current_sink_of(stack, "othergame-out"): break
            time.sleep(0.1)
        assert {"FakeGame", "OtherGame"} <= names, names
        stack.cli("app", "move", "OtherGame", "game")
        stack.cli("app", "assign", "FakeGame", "game,voice")
        app = next(x for x in stack.cli("app", "list", json_out=True) if x["Name"] == "FakeGame")
        assert app["Channels"] == ["game", "voice"], app
        stack.pw.wait_node("kmixdeck.relay.FakeGame.voice"); time.sleep(0.8)
        assert stack.pw.level_at("kmixdeck.channel.voice") > HOT, "voice must hear the multi-assigned app"
        # kill A: voice must fall silent although B keeps playing into game
        a.kill(); a.wait(); time.sleep(0.6)
        v, g = stack.pw.level_at("kmixdeck.channel.voice"), stack.pw.level_at("kmixdeck.channel.game")
        assert v < SILENT, f"relay leaked another app of the primary channel into voice: {v:.1f} dBFS"
        assert g > HOT, f"game must still carry OtherGame: {g:.1f} dBFS"
        # persisted like CH-4 (layout, not PipeWire state) — and the relay comes back after a daemon restart,
        # capturing the app node (a fragment from an older renderer captured the channel sink — dev machine 2026-09-16)
        layout = json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())
        assert any(x["key"] == "FakeGame" and x["channels"] == ["game", "voice"] for x in layout["apps"]), layout.get("apps")
        conf = (Path(stack.pw.runtime_dir) / "config" / "pipewire" / "pipewire.conf.d" / "90-kmixdeck.conf").read_text()
        assert 'node.name = "kmixdeck.relay.FakeGame.voice.in"' in conf and 'node.target = "fakegame-out"' in conf, conf
        # After a restart WITHOUT the app running there must be NO relay: a relay whose capture side waits for an
        # absent node left its playback half linked to the channel and stalled the channel→mix path (monitor
        # recordings empty, found 2026-09-16 — this test's leftovers broke every later test in the session).
        stack.restart_daemon(); time.sleep(1.5)
        assert not [n for n in node_names(stack) if n.startswith("kmixdeck.relay.")], "relay must not exist while its app is gone"
        assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT, "channel→mix path must be alive after the restart"
        # …and it comes back the moment the app node appears again, capturing the app node (not the channel sink)
        a = subprocess.Popen(["pw-play", "-P", FAKE_APP, str(stack.pw.tone())], env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        rin = stack.pw.wait_node("kmixdeck.relay.FakeGame.voice.in", timeout=10)
        assert rin["info"]["props"].get("node.target") == "fakegame-out", rin["info"]["props"]
    finally:
        for p in (a, b):
            if p.poll() is None: p.kill(); p.wait()
        stack.cli("app", "assign", "FakeGame", "none", check=False)


def test_ux12_audition_mix_is_audible_and_release_restores(stack, tone):
    """UX-12 measured at the output, not at a property: holding 'listen' on the Monitor mix must leave the Monitor
    mix AUDIBLE (bug 2026-09-16: every channel was muted too → silence), the Stream mix silent, and release must
    bring back exactly the previous state."""
    settle()
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT and stack.pw.level_at("kmixdeck.mix.stream") > HOT
    stack.cli("audition", "mix", "monitor"); settle()
    m, s_ = stack.pw.level_at("kmixdeck.mix.monitor"), stack.pw.level_at("kmixdeck.mix.stream")
    assert m > HOT, f"auditioned mix went silent: {m:.1f} dBFS"
    assert s_ < SILENT, f"other mix still audible during audition: {s_:.1f} dBFS"
    assert stack.pw.level_at("kmixdeck.channel.game") > HOT, "channels must stay untouched when a MIX is auditioned"
    stack.cli("audition", "none"); settle()
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT and stack.pw.level_at("kmixdeck.mix.stream") > HOT


def test_ux12_audition_channel_keeps_mixes_alive(stack, tone):
    """Auditioning a CHANNEL silences the other channels only; the mixes (and so the headphones) keep playing."""
    settle()
    stack.cli("audition", "channel", "game"); settle()
    assert stack.pw.level_at("kmixdeck.channel.game") > HOT
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT, "mix went silent while auditioning a channel"
    stack.cli("audition", "none"); settle()
    assert stack.pw.level_at("kmixdeck.mix.monitor") > HOT



def test_mx2_cell_state_survives_a_foreign_writer(stack):
    """A cell's volume/mute belongs to the user. WirePlumber's restore-stream (or any pw-cli) writing the node behind the
    daemon's back is detected from the echo and undone — at any time, not only in a fixed window after creation
    (ctest35: CT-7 lost a mute and DV-6 a volume seconds after a timed retry had fired). A later user change still wins."""
    stack.cli("channel", "add", "Probe"); time.sleep(0.3)
    stack.cli("cell", "set", "probe", "stream", "-6dB")
    nid = stack.pw.node_id("kmixdeck.link.probe.stream")
    def cell(): return next(c for c in stack.cli("status", json_out=True)["cells"] if c["Path"].endswith("/probe/stream"))
    def foreign(props):
        r = subprocess.run(["pw-cli", "set-param", str(nid), "Props", props], env=stack.env, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
    try:
        time.sleep(1.0)
        foreign("{ volume: 1.0, channelVolumes: [1.0, 1.0], mute: false }"); time.sleep(1.0)
        assert abs(cell()["Volume"] - 10 ** (-6 / 20)) < 0.01, cell()
        time.sleep(2.0)   # a second event later — not the same burst
        foreign("{ volume: 1.0, channelVolumes: [1.0, 1.0], mute: true }"); time.sleep(1.0)
        c = cell(); assert abs(c["Volume"] - 10 ** (-6 / 20)) < 0.01 and c["Muted"] is False, c
        stack.cli("cell", "set", "probe", "stream", "-12dB"); time.sleep(0.8)
        assert abs(cell()["Volume"] - 10 ** (-12 / 20)) < 0.01, "the user's newer intent must win over the guard"
        log = open(stack.daemon_log_path).read()
        assert log.count("rewriting") >= 2, "the daemon must log what it undid"
    finally:
        stack.cli("channel", "remove", "probe", check=False)
