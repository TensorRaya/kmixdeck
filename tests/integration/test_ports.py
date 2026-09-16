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


def test_dv23_virtual_multichannel_device_is_a_real_device_and_persists(stack):
    """DV-23: `devices virtual add` gives us a Ui24R stand-in: 8 capture ports in the picker, an 8-port sink apps can
    play into, port refs work on it, and it is still there after a daemon restart (it is layout)."""
    node = stack.cli("devices", "virtual", "add", "Test Ui24R", "--in", "8", "--out", "8").stdout.strip()
    assert node == "kmixdeck.virt.test_ui24r"
    stack.pw.wait_node(node); stack.pw.wait_node(node + ".out")
    for _ in range(50):
        if node in stack.cli("devices", "in", json_out=True) and node + ".out" in stack.cli("devices", "out", json_out=True): break
        time.sleep(0.1)
    else: raise AssertionError("virtual device not listed as input AND output device")
    ports = stack.cli("--json", "devices", "ports", node, json_out=False)
    assert ports.returncode == 0 and all(f"AUX{i}" in ports.stdout for i in range(1, 9)), ports.stdout
    # a channel from two of its ports hears exactly those (same measurement as DV-17, on the virtual device)
    stack.cli("channel", "add", "Deck"); stack.cli("channel", "input", "deck", f"{node}:AUX5,AUX6")
    stack.pw.wait_node("kmixdeck.in.deck.in"); time.sleep(0.8)
    p = stack.pw.play_into_port(node, "input_AUX5")
    try:
        left = wait_level(lambda: stack.pw.level_at_port("kmixdeck.channel.deck", "monitor_FL"), lambda v: v > HOT)
        right = stack.pw.level_at_port("kmixdeck.channel.deck", "monitor_FR")
        assert left > HOT and right < SILENT, f"AUX5 → left only: L={left} R={right}"
    finally:
        p.kill(); p.wait()
    # a mix into two of its output ports
    stack.cli("mix", "output", "stream", f"{node}.out:AUX7,AUX8"); time.sleep(1.0)
    p = stack.pw.play_into("kmixdeck.channel.game")
    try:
        stack.cli("cell", "set", "game", "stream", "1.0")
        a7 = wait_level(lambda: stack.pw.level_at_port(node + ".out", "monitor_AUX7"), lambda v: v > HOT)
        a1 = stack.pw.level_at_port(node + ".out", "monitor_AUX1")
        assert a7 > HOT and a1 < SILENT, f"stream → AUX7 only: AUX7={a7} AUX1={a1}"
    finally:
        p.kill(); p.wait()
    stack.restart_daemon(); time.sleep(2.0)
    assert node in stack.cli("devices", "virtual", "list").stdout
    assert stack.pw.node(node) is not None and stack.pw.node(node + ".out") is not None, "virtual device must survive a daemon restart"
    stack.cli("mix", "output", "stream", "none"); stack.cli("channel", "remove", "deck")
    stack.cli("devices", "virtual", "remove", "test_ui24r"); time.sleep(0.8)
    assert stack.pw.node(node) is None


def test_dv21_side_bound_input_feeds_only_that_side(stack):
    """ADR 0009 A2: `node:POS>R` puts a mono port on the RIGHT side only (left silent) — "pan hard right" as a routing
    choice, no mono device needed. Two ports into one side is refused (PipeWire cannot fold them; measured)."""
    node = stack.cli("devices", "virtual", "add", "Side Ui24R").stdout.strip()
    stack.pw.wait_node(node)
    for _ in range(50):
        if node in stack.cli("devices", "in", json_out=True): break
        time.sleep(0.1)
    stack.cli("channel", "add", "Right Only"); stack.cli("channel", "input", "right_only", f"{node}:AUX1>R")
    stack.pw.wait_node("kmixdeck.in.right_only.in"); time.sleep(0.8)
    p = stack.pw.play_into_port(node, "input_AUX1")
    try:
        right = wait_level(lambda: stack.pw.level_at_port("kmixdeck.channel.right_only", "monitor_FR"), lambda v: v > HOT)
        left = stack.pw.level_at_port("kmixdeck.channel.right_only", "monitor_FL")
        assert right > HOT and left < SILENT, f"AUX1>R: L={left} R={right}"
    finally:
        p.kill(); p.wait()
    # two ports into ONE side is refused (PipeWire cannot fold two positions into one — ADR 0009 A2), value kept
    r = stack.cli("channel", "input", "right_only", f"{node}:AUX3,AUX4>L", check=False)
    assert r.returncode != 0 and "one port" in (r.stderr + r.stdout), (r.stderr, r.stdout)
    # bad side is refused, old value kept
    r = stack.cli("channel", "input", "right_only", f"{node}:AUX2>X", check=False)
    assert r.returncode != 0
    st = stack.cli("status", json_out=True)
    ch = next(c for c in st["channels"] if c["Slug"] == "right_only")
    assert ch["InputDevice"] == f"{node}:AUX1>R", ch
    stack.cli("channel", "remove", "right_only"); stack.cli("devices", "virtual", "remove", "side_ui24r")


def test_dv21_side_bound_output_sends_only_that_side_of_the_mix(stack):
    """ADR 0009 A2 on the output side: `mix output stream virt.out:AUX5>R` puts only the mix's RIGHT side into AUX5;
    a left-only channel feeding that mix therefore never reaches AUX5, a right-only one does. One output port without
    a side takes the whole mix (PipeWire folds stereo→one port fine; it is the other direction it cannot do)."""
    node = stack.cli("devices", "virtual", "add", "Out Ui24R").stdout.strip()
    stack.pw.wait_node(node + ".out")
    for _ in range(50):
        if node + ".out" in stack.cli("devices", "out", json_out=True): break
        time.sleep(0.1)
    # one output port WITHOUT a side = the whole mix folded into that port (measured: works, unlike the input direction)
    stack.cli("mix", "output", "stream", f"{node}.out:AUX5"); time.sleep(1.2)
    stack.cli("cell", "set", "game", "stream", "1.0")
    p = stack.pw.play_into("kmixdeck.channel.game")
    try:
        a5 = wait_level(lambda: stack.pw.level_at_port(node + ".out", "monitor_AUX5"), lambda v: v > HOT)
        assert a5 > HOT, f"stereo mix into one port must be audible there: AUX5={a5}"
    finally:
        p.kill(); p.wait()
    stack.cli("cell", "set", "game", "stream", "0.0")
    stack.cli("mix", "output", "stream", f"{node}.out:AUX5>R"); time.sleep(1.2)
    # channel whose input lands on the LEFT side only → must not appear in AUX5
    stack.cli("channel", "add", "Left Mic"); stack.cli("channel", "input", "left_mic", f"{node}:AUX1>L")
    stack.pw.wait_node("kmixdeck.in.left_mic.in"); time.sleep(0.8)
    stack.cli("cell", "set", "left_mic", "stream", "1.0")
    p = stack.pw.play_into_port(node, "input_AUX1")
    try:
        l = wait_level(lambda: stack.pw.level_at_port("kmixdeck.mix.stream", "monitor_FL"), lambda v: v > HOT)
        a5 = stack.pw.level_at_port(node + ".out", "monitor_AUX5")
        assert l > HOT and a5 < SILENT, f"left-only channel must not reach a right-side output: mixL={l} AUX5={a5}"
    finally:
        p.kill(); p.wait()
    stack.cli("channel", "input", "left_mic", f"{node}:AUX1>R"); time.sleep(1.2)
    p = stack.pw.play_into_port(node, "input_AUX1")
    try:
        a5 = wait_level(lambda: stack.pw.level_at_port(node + ".out", "monitor_AUX5"), lambda v: v > HOT)
        a6 = stack.pw.level_at_port(node + ".out", "monitor_AUX6")
        assert a5 > HOT and a6 < SILENT, f"right-only channel → AUX5 only: AUX5={a5} AUX6={a6}"
    finally:
        p.kill(); p.wait()
    stack.cli("mix", "output", "stream", "none"); stack.cli("channel", "remove", "left_mic"); stack.cli("devices", "virtual", "remove", "out_ui24r")


def test_dv25_several_wires_into_one_channel_from_two_devices(stack):
    """ADR 0009 B1 / DV-25: a channel takes several wires. Ui24R 3 → left, RØDECaster-ish device 1 → right; each
    side hears only its wire; both survive a daemon restart; removing the primary promotes the other wire."""
    ui = stack.cli("devices", "virtual", "add", "Wire Ui24R", "--in", "4", "--out", "2").stdout.strip()
    rc = stack.cli("devices", "virtual", "add", "Wire Rode", "--in", "2", "--out", "2").stdout.strip()
    for n in (ui, rc): stack.pw.wait_node(n)
    for _ in range(50):
        devs = stack.cli("devices", "in", json_out=True)
        if ui in devs and rc in devs: break
        time.sleep(0.1)
    stack.cli("channel", "add", "Duo")
    stack.cli("channel", "input-add", "duo", f"{ui}:AUX3>L")
    stack.cli("channel", "input-add", "duo", f"{rc}:AUX1>R")
    assert stack.cli("channel", "inputs", "duo", json_out=True) == [f"{ui}:AUX3>L", f"{rc}:AUX1>R"]
    assert stack.cli("channel", "input", "duo").stdout.strip() == f"{ui}:AUX3>L", "InputDevice must be Inputs[0]"
    stack.pw.wait_node("kmixdeck.in.duo.in"); stack.pw.wait_node("kmixdeck.in.duo.w1.in"); time.sleep(0.8)

    def sides():
        return stack.pw.level_at_port("kmixdeck.channel.duo", "monitor_FL"), stack.pw.level_at_port("kmixdeck.channel.duo", "monitor_FR")
    p = stack.pw.play_into_port(ui, "input_AUX3")
    try:
        l = wait_level(lambda: sides()[0], lambda v: v > HOT); r = sides()[1]
        assert l > HOT and r < SILENT, f"Ui24R wire must land LEFT only: L={l} R={r}"
    finally:
        p.kill(); p.wait()
    p = stack.pw.play_into_port(rc, "input_AUX1")
    try:
        r = wait_level(lambda: sides()[1], lambda v: v > HOT); l = sides()[0]
        assert r > HOT and l < SILENT, f"Rode wire must land RIGHT only: L={l} R={r}"
    finally:
        p.kill(); p.wait()
    # a second wire into the SAME side sums (two edges, one sink) — allowed
    stack.cli("channel", "input-add", "duo", f"{ui}:AUX4>R")
    assert len(stack.cli("channel", "inputs", "duo", json_out=True)) == 3
    # duplicate is a no-op, unknown port refused
    stack.cli("channel", "input-add", "duo", f"{ui}:AUX4>R")
    assert len(stack.cli("channel", "inputs", "duo", json_out=True)) == 3
    assert stack.cli("channel", "input-add", "duo", f"{ui}:AUX9", check=False).returncode != 0

    stack.restart_daemon()
    assert stack.cli("channel", "inputs", "duo", json_out=True) == [f"{ui}:AUX3>L", f"{rc}:AUX1>R", f"{ui}:AUX4>R"], "wires must survive a restart"
    stack.pw.wait_node("kmixdeck.in.duo.w1.in"); time.sleep(0.8)
    p = stack.pw.play_into_port(rc, "input_AUX1")
    try:
        assert wait_level(lambda: sides()[1], lambda v: v > HOT) > HOT, "right wire silent after restart"
    finally:
        p.kill(); p.wait()
    # removing the primary promotes the next wire
    assert stack.cli("channel", "input-remove", "duo", f"{ui}:AUX3>L").returncode == 0
    ins = stack.cli("channel", "inputs", "duo", json_out=True)
    assert ins[0] == f"{rc}:AUX1>R" and len(ins) == 2, ins
    assert stack.cli("channel", "input", "duo").stdout.strip() == f"{rc}:AUX1>R"
    stack.cli("channel", "remove", "duo")
    stack.cli("devices", "virtual", "remove", "wire_ui24r"); stack.cli("devices", "virtual", "remove", "wire_rode")


def test_dv24_patchbay_gestures_reach_the_daemon(stack):
    """DV-24: the patchbay's drag (jack → jack) and click (on a wire) are the same calls the UI makes; driven through
    `kmixdeck-kde --gesture` so we can prove they change the daemon, not just the picture."""
    from test_service_cli import BIN
    kde = BIN / "kmixdeck-kde"
    if not kde.exists(): pytest.skip("kmixdeck-kde not built")
    ui = stack.cli("devices", "virtual", "add", "Gesture Ui24R", "--in", "4", "--out", "4").stdout.strip()
    stack.pw.wait_node(ui); stack.pw.wait_node(ui + ".out")
    for _ in range(50):
        if ui in stack.cli("devices", "in", json_out=True) and (ui + ".out") in stack.cli("devices", json_out=True): break
        time.sleep(0.1)
    stack.cli("channel", "add", "Deck")
    env = dict(stack.env); env["QT_QPA_PLATFORM"] = "offscreen"

    def gesture(*specs):
        args = [str(kde)]
        for s in specs: args += ["--gesture", s]
        r = subprocess.run(args, env=env, capture_output=True, text=True, timeout=30)
        return [l for l in r.stdout.splitlines() if l.startswith("gesture ")]

    # drag Ui24R AUX2 → Deck's R jack; drag Deck R → mix stream (unmute the send); drag stream L → Ui24R out AUX3
    out = gesture(f"connect:dev/{ui}|AUX2|ch/deck|R", "connect:ch/deck|R|mix/stream|R", f"connect:mix/stream|L|outdev/{ui}.out|AUX3")
    assert all(l.endswith("-> ok") for l in out), out
    time.sleep(0.6)
    assert stack.cli("channel", "inputs", "deck", json_out=True) == [f"{ui}:AUX2>R"]
    assert stack.cli("cell", "get", "deck", "stream", json_out=True)["Muted"] is False
    assert f"{ui}.out:AUX3>L" in stack.cli("mix", "outputs", "stream", json_out=True)
    # nonsense pairs are refused with a reason, and nothing changes
    out = gesture(f"connect:dev/{ui}|AUX1|dev/{ui}|AUX2", f"connect:dev/{ui}|AUX1|mix/stream|L")
    assert not any(l.endswith("-> ok") for l in out), out
    assert stack.cli("channel", "inputs", "deck", json_out=True) == [f"{ui}:AUX2>R"]
    # click on the wires → gone again
    out = gesture(f"remove:input|deck|{ui}:AUX2>R", f"remove:output|stream|{ui}.out:AUX3>L", "remove:cell|deck|stream")
    time.sleep(0.6)
    assert stack.cli("channel", "inputs", "deck", json_out=True) == []
    assert f"{ui}.out:AUX3>L" not in stack.cli("mix", "outputs", "stream", json_out=True)
    assert stack.cli("cell", "get", "deck", "stream", json_out=True)["Muted"] is True
    stack.cli("channel", "remove", "deck"); stack.cli("devices", "virtual", "remove", "gesture_ui24r")
