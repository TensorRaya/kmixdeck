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
from test_service_cli import Stack, BIN, make_fake_sink, make_fake_source, out_link_target
from pw_sandbox import start_private_pipewire

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
    for _ in range(40):
        if out_link_target(stack, "monitor") == "fake.headphones": break
        time.sleep(0.1)
    settle()
    assert stack.pw.level_at("fake.headphones") > HOT
    stack.cli("cell", "set", "game", "monitor", "0.0"); settle()
    assert stack.pw.level_at("fake.headphones") < SILENT, "the device follows the mix fader"
    stack.cli("cell", "set", "game", "monitor", "1.0")
    stack.cli("mix", "output", "monitor", "none")
    for _ in range(40):
        if out_link_target(stack, "monitor") == "kmixdeck.null": break
        time.sleep(0.1)
    settle()
    assert stack.pw.level_at("fake.headphones") < SILENT, "'none' must actually stop the audio on the device"


def test_two_mixes_on_the_same_device_do_not_double(stack, tone):
    """Edge: Monitor AND Stream both on the headphones. Allowed, but the level must be the sum, not a crash/loop."""
    for mx in ("monitor", "stream"): stack.cli("mix", "output", mx, "fake.headphones")
    for _ in range(40):
        if out_link_target(stack, "monitor") == "fake.headphones" and out_link_target(stack, "stream") == "fake.headphones": break
        time.sleep(0.1)
    settle()
    both = stack.pw.level_at("fake.headphones")
    stack.cli("mix", "output", "stream", "none")
    for _ in range(40):
        if out_link_target(stack, "stream") == "kmixdeck.null": break
        time.sleep(0.1)
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
        for _ in range(30):
            if "fake.mic" in stack.cli("devices", "in", json_out=True): break
            time.sleep(0.1)
        stack.cli("channel", "input", "voice", "fake.mic")
        stack.pw.wait_node("kmixdeck.in.voice")
        time.sleep(1.0)
        assert stack.pw.level_at("kmixdeck.channel.voice") > HOT, "the mic must arrive in the voice channel"
        assert stack.pw.level_at("kmixdeck.mix.stream") > HOT and stack.pw.level_at("kmixdeck.mix.monitor") > HOT
        stack.cli("cell", "set", "voice", "stream", "0.0"); settle()
        assert stack.pw.level_at("kmixdeck.mix.stream") < SILENT and stack.pw.level_at("kmixdeck.mix.monitor") > HOT
        stack.cli("cell", "set", "voice", "stream", "1.0")
        stack.cli("channel", "input", "voice", "none")
        for _ in range(40):
            if stack.pw.node("kmixdeck.in.voice") is None: break
            time.sleep(0.1)
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
