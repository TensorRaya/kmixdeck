"""ADR 0009 — port-based virtual devices (DV-13, DV-17…20), measured at the ports.

A fake 4-port source ("Ui24R") and a fake 4-port sink ("RØDECaster out") stand in for the hardware. Ports are
AUX1..AUX4 — the Pro-Audio naming real multichannel devices expose.

The fake source is `Audio/Source/Virtual`: that null-sink variant has `input_<POS>` ports we can feed the tone into
and `capture_<POS>` ports the daemon captures from — a plain `Audio/Source` null-sink has no inputs at all (and
drops its first position, PipeWire 1.x quirk)."""
import json
import subprocess
import time

import pytest

from test_service_cli import BIN, Stack
from pw_sandbox import start_private_pipewire

HOT, SILENT = -30.0, -60.0
POS = "[ AUX1 AUX2 AUX3 AUX4 ]"


@pytest.fixture(scope="module")
def stack():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    s = Stack(pw)
    yield s
    s.close(); pw.close()


def make_device(stack, name, desc, media_class):
    subprocess.run(["pw-cli", "create-node", "adapter",
                    f'{{ factory.name=support.null-audio-sink node.name={name} node.description="{desc}" media.class={media_class} audio.position={POS} object.linger=true }}'],
                   env=stack.pw.env, capture_output=True)
    stack.pw.wait_node(name)
    for _ in range(50):   # the daemon must have seen it (Mixer.InputDevices / OutputDevices)
        r = subprocess.run([str(BIN / "kmixdeck"), "--json", "devices", "in" if media_class.startswith("Audio/Source") else "out"], env=stack.env, capture_output=True, text=True)
        if name in r.stdout: return
        time.sleep(0.1)
    raise AssertionError(f"daemon never listed {name}")


def wait_level(fn, pred, tries=6):
    v = float("-inf")
    for _ in range(tries):
        v = fn()
        if pred(v): return v
        time.sleep(0.4)
    return v


def test_dv20_daemon_publishes_ports_of_a_device(stack):
    make_device(stack, "fake.ui24r", "Fake Ui24R", "Audio/Source/Virtual")
    r = subprocess.run(["busctl", "--user", "get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "DevicePorts"],
                       env=stack.env, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert "fake.ui24r" in r.stdout and "AUX2|capture_AUX2" in r.stdout and "AUX4|capture_AUX4" in r.stdout, r.stdout
    assert "monitor_" not in r.stdout
    # CLI view
    r = subprocess.run([str(BIN / "kmixdeck"), "devices", "ports", "fake.ui24r"], env=stack.env, capture_output=True, text=True)
    assert r.returncode == 0 and "AUX3" in r.stdout, r.stdout + r.stderr


def test_dv17_two_mono_channels_from_one_device_hear_only_their_port(stack):
    make_device(stack, "fake.ui24r", "Fake Ui24R", "Audio/Source/Virtual")
    stack.cli("channel", "add", "Talkback"); stack.cli("channel", "add", "Music")
    stack.cli("channel", "input", "talkback", "fake.ui24r:AUX4")
    stack.cli("channel", "input", "music", "fake.ui24r:AUX2,AUX3")
    assert stack.cli("channel", "input", "talkback", json_out=True)["InputDevice"] == "fake.ui24r:AUX4"
    stack.pw.wait_node("kmixdeck.in.talkback.in"); stack.pw.wait_node("kmixdeck.in.music.in"); time.sleep(0.8)
    # a null-sink-as-source: feeding input_AUX4 makes the tone appear on capture_AUX4
    p = stack.pw.play_into_port("fake.ui24r", "input_AUX4")
    try:
        tb = wait_level(lambda: stack.pw.level_at("kmixdeck.channel.talkback"), lambda v: v > HOT)
        mu = stack.pw.level_at("kmixdeck.channel.music")
        assert tb > HOT, f"talkback (AUX4) must hear its port: {tb}"
        assert mu < SILENT, f"music (AUX2+3) must NOT hear AUX4: {mu}"
    finally:
        p.kill(); p.wait()
    # stereo pair keeps sides: AUX2 → left of music, right stays silent
    p = stack.pw.play_into_port("fake.ui24r", "input_AUX2")
    try:
        left = wait_level(lambda: stack.pw.level_at_port("kmixdeck.channel.music", "monitor_FL"), lambda v: v > HOT)
        right = stack.pw.level_at_port("kmixdeck.channel.music", "monitor_FR")
        assert left > HOT and right < SILENT, f"AUX2 must land on the LEFT only: L={left} R={right}"
    finally:
        p.kill(); p.wait()
        stack.cli("channel", "remove", "talkback"); stack.cli("channel", "remove", "music")


def test_dv19_mono_input_reaches_both_sides_and_bad_port_is_rejected(stack):
    make_device(stack, "fake.ui24r", "Fake Ui24R", "Audio/Source/Virtual")
    stack.cli("channel", "add", "Mono Mic")
    stack.cli("channel", "input", "mono_mic", "fake.ui24r:AUX3")
    stack.pw.wait_node("kmixdeck.in.mono_mic.in"); time.sleep(0.8)
    p = stack.pw.play_into_port("fake.ui24r", "input_AUX3")
    try:
        left = wait_level(lambda: stack.pw.level_at_port("kmixdeck.channel.mono_mic", "monitor_FL"), lambda v: v > HOT)
        right = stack.pw.level_at_port("kmixdeck.channel.mono_mic", "monitor_FR")
        assert left > HOT and right > HOT, f"mono → both sides (DV-19): L={left} R={right}"
        assert abs(left - right) < 1.5, f"both sides equal: L={left} R={right}"
    finally:
        p.kill(); p.wait()
    r = stack.cli("channel", "input", "mono_mic", "fake.ui24r:AUX9", check=False)
    assert r.returncode != 0 and "AUX9" in r.stderr, r
    assert stack.cli("channel", "input", "mono_mic", json_out=True)["InputDevice"] == "fake.ui24r:AUX3", "a rejected ref must not change the input"
    r = stack.cli("channel", "input", "mono_mic", "fake.ui24r:AUX2,AUX3,AUX4", check=False)
    assert r.returncode != 0, "3 ports is neither mono nor stereo"
    stack.cli("channel", "remove", "mono_mic")


def test_dv18_mix_output_into_a_port_subset_leaves_other_ports_silent(stack):
    make_device(stack, "fake.rode.out", "Fake RØDECaster Out", "Audio/Sink")
    stack.cli("mix", "output", "stream", "fake.rode.out:AUX3,AUX4")
    assert stack.cli("mix", "get", "stream", json_out=True)["Outputs"] == ["fake.rode.out:AUX3,AUX4"]
    time.sleep(1.0)
    p = stack.pw.play_into("kmixdeck.channel.game")
    try:
        stack.cli("cell", "set", "game", "stream", "1.0")
        a2 = wait_level(lambda: stack.pw.level_at_port("fake.rode.out", "monitor_AUX3"), lambda v: v > HOT)
        a3 = stack.pw.level_at_port("fake.rode.out", "monitor_AUX4")
        a0 = stack.pw.level_at_port("fake.rode.out", "monitor_AUX1")
        a1 = stack.pw.level_at_port("fake.rode.out", "monitor_AUX2")
        assert a2 > HOT and a3 > HOT, f"stream must play into AUX3/AUX4: {a2} {a3}"
        assert a0 < SILENT and a1 < SILENT, f"AUX1/AUX2 must stay untouched (DV-18): {a0} {a1}"
    finally:
        p.kill(); p.wait()
    # persisted as the same string; survives a daemon restart
    stack.restart_daemon()
    assert stack.cli("mix", "get", "stream", json_out=True)["Outputs"] == ["fake.rode.out:AUX3,AUX4"]
    stack.cli("mix", "output", "stream", "none")
