# SPDX-FileCopyrightText: 2026 Raya Elena Solano
# SPDX-License-Identifier: GPL-3.0-or-later
"""ADR 0009 — port-based virtual devices (DV-13, DV-17…20), measured at the ports.

A fake 4-port source ("Ui24R") and a fake 4-port sink ("RØDECaster out") stand in for the hardware. Ports are
AUX1..AUX4 — the Pro-Audio naming real multichannel devices expose.

The fake source is `Audio/Source/Virtual`: that null-sink variant has `input_<POS>` ports we can feed the tone into
and `capture_<POS>` ports the daemon captures from — a plain `Audio/Source` null-sink has no inputs at all (and
drops its first position, PipeWire 1.x quirk)."""
import os
import subprocess
import time
from pathlib import Path

import pytest

from test_service_cli import BIN, Stack, destroy_node
from pw_sandbox import start_private_pipewire
from waiting import wait_for

HOT, SILENT = -30.0, -60.0
POS = "[ AUX1 AUX2 AUX3 AUX4 ]"


@pytest.fixture(autouse=True)
def _keine_leichen(request, stack):
    """Blame the test that leaves nodes behind, instead of the three that inherit them.

    The `stack` fixture is module-scoped on purpose (one daemon start costs seconds, 20 tests share it), so a
    test whose teardown did not run hands its leftovers to every following test. Measured 2026-09-22: dv30c
    failed for one missing wait and left 35 nodes behind, after which dv29/dv30/dv6 failed on their OWN fresh
    devices with "-inf dB" — the gate read "5 failed" and four of those were phantoms. This fixture makes the
    real culprit the one that goes red: it records the node count before the test and warns afterwards if the
    graph grew, so the noise cannot travel silently down the file.
    """
    # Nodes the DAEMON owns, not a test: they come and go with a restart (dv6 restarts it) and are not leaks.
    # kmixdeck.loudness is the loudness meter from src/pipewire/meters.cpp — it appeared in this report as a
    # phantom "leak" of dv6 on 2026-09-22 until it was filtered here.
    #
    # The fake.* devices below are SHARED ON PURPOSE, not leaked: make_device() creates them with
    # object.linger=true and 15 places in this file reuse fake.ui24r. Deleting them in a teardown would break
    # every later test — checked before touching them on 2026-09-22.
    NICHT_MEINE = {"kmixdeck.loudness", "kmixdeck.mix.stream", "kmixdeck.mix.monitor", "kmixdeck.null",
                   "fake.ui24r", "fake.rode.out"}

    def projekt_knoten():
        # Only OUR nodes: pw-play/pw-record stragglers of a test's own tone die on their own and are not a leak.
        return sorted(n for n in stack.pw.node_names()
                      if (n.startswith("kmixdeck.") or n.startswith("fake.")) and n not in NICHT_MEINE)
    vorher = projekt_knoten()
    yield
    nachher = projekt_knoten()
    neu = sorted(set(nachher) - set(vorher))
    if neu:
        request.node.add_report_section(
            "teardown", "leaked nodes",
            # The test's own name goes INTO the text: with -q pytest prints these sections under a bare "PASSES"
            # header without saying which test they belong to, which makes the report useless for exactly the
            # question it exists to answer (2026-09-22).
            f"{request.node.name}: cleanup did not finish — {len(neu)} node(s) left behind: {neu}\n"
            f"Everything after it in this file shares the same daemon and may fail for THIS reason.\n")

# ---------------------------------------------------------------- DV-30b: starving ONE edge
# The report path (LastError on the bus + "!!" in `status`) needs exactly one edge to fail while
# everything else stays up. Until 2026-09-20 this was done with a hard fd limit (FD_CLAMP) that had
# to sit inside the window "sink still comes up, loopback client does not". That window cannot be
# hit reliably: measured in the FULL file, the daemon's idle fd cost swings between 25 and 43 fds
# from run to run (clamp 40 → idle 40, clamp 42 → idle 25, clamp 44 → idle 43) — wider than the
# window itself, so any constant is a coin flip. Worse, the same number means opposite things in
# different contexts: 44 was "too loose" with -k and "too tight" in the full file.
#
# Deterministic instead, using an asymmetry in graph.cpp:
#   sink  = pw_core_create_object("adapter", …)      → created SERVER-side, unaffected
#   edge  = pw_context_load_module(d->context, …)    → loaded in the DAEMON's own pw_context (:301),
#                                                      resolved via PIPEWIRE_MODULE_DIR
# So: run pipewire/wireplumber against the real module dir, start only kmixdeckd against a mirror
# that symlinks every module EXCEPT libpipewire-module-loopback. The sink comes up, the pass-through
# edge cannot load, the report path fires. No calibration, no timing, nothing to re-measure.
# (Setting the variable globally does NOT work — pipewire itself builds starter.conf out of loopback
# modules, so the sandbox never comes up. Verified 2026-09-20.)
_MODULE_MIRROR = Path("/var/tmp/kmixdeck-test-modules-noloopback")


def module_dir_without_loopback() -> Path:
    """A PIPEWIRE_MODULE_DIR containing every module except the loopback one."""
    real = Path(subprocess.run(["pkg-config", "--variable=moduledir", "libpipewire-0.3"],
                               capture_output=True, text=True, check=True).stdout.strip())
    assert real.is_dir(), f"pipewire module dir not found: {real}"
    _MODULE_MIRROR.mkdir(parents=True, exist_ok=True)
    for stale in _MODULE_MIRROR.iterdir():
        stale.unlink()
    for so in real.iterdir():
        if so.name != "libpipewire-module-loopback.so":
            (_MODULE_MIRROR / so.name).symlink_to(so)
    assert not (_MODULE_MIRROR / "libpipewire-module-loopback.so").exists()
    return _MODULE_MIRROR


@pytest.fixture(scope="module")
def stack():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    s = Stack(pw)
    yield s
    s.close(); pw.close()


def make_device(stack, name, desc, media_class, positions=None):
    # audio.channels alongside the positions, as a driver-backed node carries it (the Ui24R shows AUX0..AUX31).
    pos = "[ " + " ".join(positions) + " ]" if positions else POS
    chans = f"audio.channels={len(positions)} " if positions else ""
    subprocess.run(["pw-cli", "create-node", "adapter",
                    f'{{ factory.name=support.null-audio-sink node.name={name} node.description="{desc}" media.class={media_class} {chans}audio.position={pos} object.linger=true }}'],
                   env=stack.pw.env, capture_output=True)
    stack.pw.wait_node(name)
    for _ in range(50):   # the daemon must have seen it (Mixer.InputDevices / OutputDevices)
        r = subprocess.run([str(BIN / "kmixdeck"), "--json", "devices", "in" if media_class.startswith("Audio/Source") else "out"], env=stack.env, capture_output=True, text=True)
        if name in r.stdout: return
        time.sleep(0.1)
    raise AssertionError(f"daemon never listed {name}")


def wait_level(fn, pred, tries=6, what="level"):
    """Repeat an audio MEASUREMENT until pred(level) holds; return that level. Raise if it never holds.

    🔴 Until 2026-09-20 this ended in `return v` — it handed back the last reading even when the predicate
    never held, so a missed level became a WRONG NUMBER instead of a timeout, and the caller's assertion
    then read like a product bug. That is the whole reason this raises. Same bug class as wait_prop and the
    two `return pred()` helpers; see CONTRIBUTING and docs/review-v0.3.md B9.

    On `tries`: each fn() call is a ~1.5 s RECORDING, so 6 tries are ~11.4 s of real waiting, not 2.4 s —
    do not reason about this window from the sleep alone (I got that wrong on 2026-09-20 and "fixed" a
    non-problem by raising tries to 20, which pushed integration-ports over its 600 s ctest limit).
    MEASURED (/var/tmp/trim_wahrheit.py): a wire trim is fully applied 26 ms after the CLI returns and stays
    within 0.3 dB over the next 5 s — there is no fade in the product (graph.cpp:369 sets channelVolumes
    hard via pw_node_set_param). So 6 tries are generous for a settle; if a level never arrives at all,
    more patience cannot help and only burns the suite's time budget."""
    v = float("-inf")
    for _ in range(tries):
        v = fn()
        if pred(v): return v
        time.sleep(0.4)
    raise AssertionError(f"{what} never satisfied the predicate in {tries} recordings (~{tries * 1.9:.0f}s), last reading {v:.2f} dB")


def settled_level(fn, pred, tries=12, within=1.0, what="settled level"):
    """Like wait_level, but returns the level only once two consecutive readings agree within `within` dB — right
    after a device comes back the meter is still rising (loopback buffers filling) and a first-above-threshold
    reading is 5 dB short of the steady state under ctest load (DV-6, ctest29). Comparing levels needs settled ones.

    🔴 Raises instead of returning `prev` or -inf (2026-09-20, same fix as wait_level): a level that never
    settled is a timeout, not a measurement. Returning -inf made the CALLER's assertion fail with a nonsense
    number instead of saying "it never settled"."""
    prev = None
    for _ in range(tries):
        v = fn()
        if pred(v) and prev is not None and abs(v - prev) < within: return v
        prev = v if pred(v) else None
        time.sleep(0.4)
    raise AssertionError(f"{what} never settled within {within} dB over {tries} readings (last: {prev!r})")


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
    p = stack.pw.play_into_port(node + ".out", "playback_AUX5")
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
    wait_for(lambda: node in stack.cli("devices", "in", json_out=True), timeout=5.0, what="node in stack.cli('devices', 'in', json_out=True)")
    stack.cli("channel", "add", "Right Only"); stack.cli("channel", "input", "right_only", f"{node}:AUX1>R")
    stack.pw.wait_node("kmixdeck.in.right_only.in"); time.sleep(0.8)
    p = stack.pw.play_into_port(node + ".out", "playback_AUX1")
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
    wait_for(lambda: node + ".out" in stack.cli("devices", "out", json_out=True), timeout=5.0, what="node + '.out' in stack.cli('devices', 'out', json_out=True)")
    # one output port WITHOUT a side = the whole mix folded into that port (measured: works, unlike the input direction)
    stack.cli("mix", "output", "stream", f"{node}.out:AUX5"); time.sleep(1.2)
    stack.cli("cell", "set", "game", "stream", "1.0")
    p = stack.pw.play_into("kmixdeck.channel.game")
    try:
        a5 = wait_level(lambda: stack.pw.level_at_port(node + ".out", "monitor_AUX5"), lambda v: v > HOT, tries=12)
        if not a5 > HOT:
            links = subprocess.run(["pw-link", "-l"], env=stack.pw.env, capture_output=True, text=True).stdout
            nodes = subprocess.run(["pw-cli", "ls", "Node"], env=stack.pw.env, capture_output=True, text=True).stdout
            print("OUT LINKS:\n" + "\n".join(l for l in links.splitlines() if "kmixdeck.out.stream" in l or "|->" in l or "|<-" in l)[:3000])
            print("STREAM MIX OUTPUTS:", stack.cli("mix", "outputs", "stream", json_out=True))
            print("OUT NODES:", [l.strip() for l in nodes.splitlines() if "kmixdeck.out" in l or "out_ui24r" in l])
        assert a5 > HOT, f"stereo mix into one port must be audible there: AUX5={a5}"
    finally:
        p.kill(); p.wait()
    stack.cli("cell", "set", "game", "stream", "0.0")
    stack.cli("mix", "output", "stream", f"{node}.out:AUX5>R"); time.sleep(1.2)
    # channel whose input lands on the LEFT side only → must not appear in AUX5
    stack.cli("channel", "add", "Left Mic"); stack.cli("channel", "input", "left_mic", f"{node}:AUX1>L")
    stack.pw.wait_node("kmixdeck.in.left_mic.in"); time.sleep(0.8)
    stack.cli("cell", "set", "left_mic", "stream", "1.0")
    p = stack.pw.play_into_port(node + ".out", "playback_AUX1")
    try:
        l = wait_level(lambda: stack.pw.level_at_port("kmixdeck.mix.stream", "monitor_FL"), lambda v: v > HOT)
        a5 = stack.pw.level_at_port(node + ".out", "monitor_AUX5")
        assert l > HOT and a5 < SILENT, f"left-only channel must not reach a right-side output: mixL={l} AUX5={a5}"
    finally:
        p.kill(); p.wait()
    stack.cli("channel", "input", "left_mic", f"{node}:AUX1>R"); time.sleep(1.2)
    p = stack.pw.play_into_port(node + ".out", "playback_AUX1")
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
    ui = stack.cli("devices", "virtual", "add", "Wire Ui24R", "--in", "4", "--out", "4").stdout.strip()
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
    p = stack.pw.play_into_port(ui + ".out", "playback_AUX3")
    try:
        l = wait_level(lambda: sides()[0], lambda v: v > HOT); r = sides()[1]
        assert l > HOT and r < SILENT, f"Ui24R wire must land LEFT only: L={l} R={r}"
    finally:
        p.kill(); p.wait()
    p = stack.pw.play_into_port(rc + ".out", "playback_AUX1")
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
    p = stack.pw.play_into_port(rc + ".out", "playback_AUX1")
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
    wait_for(lambda: ui in stack.cli("devices", "in", json_out=True) and (ui + ".out") in stack.cli("devices", json_out=True), timeout=5.0, what="ui in stack.cli('devices', 'in', json_out=True) and (ui + '.out') in s")
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


def test_dv13_hotplug_new_multiport_device_is_listed_wired_replugged_and_persisted(stack):
    """DV-9/DV-12: is a newly plugged device recognised cleanly, and does unplug/replug come back as before?
    Sauber persistiert?' — the golden rule, measured on a device that appears from OUTSIDE the daemon (like a USB
    console), with a port-level wire on it, through unplug, replug and a daemon restart. Audio is measured, not
    only properties."""
    dev, desc = "hot.console", "Hotplug Console"
    # (1) plug in a new 4-port device the daemon has never seen → listed with all its ports, wire-able at once
    make_device(stack, dev, desc, "Audio/Source/Virtual")
    ports = stack.cli("devices", "ports", dev).stdout
    assert all(p in ports for p in ("AUX1", "AUX2", "AUX3", "AUX4")), ports
    stack.cli("channel", "add", "Hot")
    stack.cli("channel", "input-add", "hot", f"{dev}:AUX2>L")
    stack.cli("channel", "input-add", "hot", f"{dev}:AUX4>R")
    stack.pw.wait_node("kmixdeck.in.hot.in"); stack.pw.wait_node("kmixdeck.in.hot.w1.in"); time.sleep(0.8)

    def sides():
        return stack.pw.level_at_port("kmixdeck.channel.hot", "monitor_FL"), stack.pw.level_at_port("kmixdeck.channel.hot", "monitor_FR")
    def hear(port, want_side):
        p = stack.pw.play_into_port(dev, f"input_{port}")
        try:
            i = 0 if want_side == "L" else 1
            v = wait_level(lambda: sides()[i], lambda x: x > HOT); other = sides()[1 - i]
            assert v > HOT and other < SILENT, f"{port} must land on {want_side} only: L/R={sides()}"
        finally:
            p.kill(); p.wait()
    hear("AUX2", "L"); hear("AUX4", "R")
    assert stack.cli("channel", "input", "hot").stdout.strip() == f"{dev}:AUX2>L"

    # (2) unplug → greyed out, wires KEPT (not deleted), the channel itself stays
    destroy_node(stack, dev)
    for _ in range(50):
        ch = next(c for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == "hot")
        if not ch["InputPresent"]: break
        time.sleep(0.1)
    assert ch["InputPresent"] is False, ch
    assert stack.cli("channel", "inputs", "hot", json_out=True) == [f"{dev}:AUX2>L", f"{dev}:AUX4>R"], "wires must survive the absence"
    assert dev not in stack.cli("devices", "in", json_out=True)

    # (3) replug (same node.name, new node id) → present again, BOTH wires carry audio again, no user action
    make_device(stack, dev, desc, "Audio/Source/Virtual")
    for _ in range(80):
        ch = next(c for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == "hot")
        if ch["InputPresent"]: break
        time.sleep(0.1)
    assert ch["InputPresent"] is True, ch
    stack.pw.wait_node("kmixdeck.in.hot.in"); stack.pw.wait_node("kmixdeck.in.hot.w1.in"); time.sleep(1.0)
    hear("AUX2", "L"); hear("AUX4", "R")

    # (4) daemon restart with the device present → same wires, same audio
    stack.restart_daemon()
    assert stack.cli("channel", "inputs", "hot", json_out=True) == [f"{dev}:AUX2>L", f"{dev}:AUX4>R"]
    stack.pw.wait_node("kmixdeck.in.hot.in"); stack.pw.wait_node("kmixdeck.in.hot.w1.in"); time.sleep(1.0)
    hear("AUX2", "L"); hear("AUX4", "R")

    # (5) daemon restart while the device is UNPLUGGED → wires still in the layout, greyed; plug in → audio
    destroy_node(stack, dev)
    stack.restart_daemon()
    assert stack.cli("channel", "inputs", "hot", json_out=True) == [f"{dev}:AUX2>L", f"{dev}:AUX4>R"], "absent device must not erase the wiring"
    ch = next(c for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == "hot"); assert ch["InputPresent"] is False
    make_device(stack, dev, desc, "Audio/Source/Virtual")
    stack.pw.wait_node("kmixdeck.in.hot.in"); stack.pw.wait_node("kmixdeck.in.hot.w1.in"); time.sleep(1.0)
    hear("AUX2", "L"); hear("AUX4", "R")

    stack.cli("channel", "remove", "hot"); destroy_node(stack, dev)


def test_dv22_pan_moves_a_mono_source_between_the_sides_and_persists(stack):
    """DV-22: a one-port (mono) input lands centre by default; `channel pan` moves it — hard left = right side silent,
    hard right = left silent, centre = both equal; pan survives a daemon restart and is applied when the node returns."""
    ui = stack.cli("devices", "virtual", "add", "Pan Ui24R", "--in", "2", "--out", "2").stdout.strip()
    stack.pw.wait_node(ui)
    wait_for(lambda: ui in stack.cli("devices", "in", json_out=True), timeout=5.0, what="ui in stack.cli('devices', 'in', json_out=True)")
    stack.cli("channel", "add", "Panner"); stack.cli("channel", "input", "panner", f"{ui}:AUX1")
    stack.pw.wait_node("kmixdeck.in.panner.in"); time.sleep(0.8)
    stack.cli("cell", "set", "panner", "stream", "1.0"); stack.cli("cell", "mute", "panner", "stream", "off")
    def lr(): return stack.pw.level_at_port("kmixdeck.channel.panner", "monitor_FL"), stack.pw.level_at_port("kmixdeck.channel.panner", "monitor_FR")
    p = stack.pw.play_into_port(ui + ".out", "playback_AUX1")
    try:
        l, r = wait_level(lambda: lr()[0], lambda v: v > HOT), lr()[1]
        assert l > HOT and r > HOT and abs(l - r) < 1.5, f"centre must be equal both sides: L={l} R={r}"
        assert stack.cli("channel", "pan", "panner").stdout.strip() == "0"
        stack.cli("channel", "pan", "panner", "L"); time.sleep(0.6)
        l, r = wait_level(lambda: lr()[0], lambda v: v > HOT), lr()[1]
        assert l > HOT and r < SILENT, f"hard left: L={l} R={r}"
        stack.cli("channel", "pan", "panner", "1"); time.sleep(0.6)
        r, l = wait_level(lambda: lr()[1], lambda v: v > HOT), lr()[0]
        assert r > HOT and l < SILENT, f"hard right: L={l} R={r}"
        stack.cli("channel", "pan", "panner", "-0.5"); time.sleep(0.9)
        l, r = lr()
        assert l > HOT and r > HOT and l - r > 2.5, f"half left: L louder by ≈ 3 dB: L={l} R={r}"
        assert stack.cli("channel", "pan", "panner", "2", check=False).returncode != 0
    finally:
        p.kill(); p.wait()
    # persists and is re-applied to the fresh node after a restart
    stack.restart_daemon()
    assert stack.cli("channel", "pan", "panner").stdout.strip() == "-0.5"
    stack.pw.wait_node("kmixdeck.in.panner.in"); time.sleep(1.0)
    p = stack.pw.play_into_port(ui + ".out", "playback_AUX1")
    try:
        wait_level(lambda: lr()[0], lambda v: v > HOT); time.sleep(0.6)   # let both meters settle on the steady tone
        diffs = []
        for _ in range(4): l, r = lr(); diffs.append(l - r); time.sleep(0.3)
        assert l > HOT and r > HOT and max(diffs) > 2.5, f"pan must survive restart (cos(π/4) = −3 dB): L={l} R={r} diffs={diffs}"
    finally:
        p.kill(); p.wait()
    stack.cli("channel", "remove", "panner"); stack.cli("devices", "virtual", "remove", "pan_ui24r")


def test_dv14_wire_trim_and_mute_live_on_the_wire_not_on_the_device(stack):
    """DV-14: every wire has its own trim/mute inside kmixdeck. Measured: input wire trim halves what reaches the channel,
    mute silences it, the fake DEVICE's own volume is never touched (CT-5); the same on an output wire measured at the
    device's input port; value survives unplug/replug of the device and a daemon restart; refused for an unknown wire."""
    import os, json
    dev = "virt.trim.src"; make_device(stack, dev, "Trim Source", "Audio/Source/Virtual")
    sink = "virt.trim.sink"; make_device(stack, sink, "Trim Sink", "Audio/Sink")
    stack.cli("channel", "add", "Trimmed")
    stack.cli("channel", "input-add", "trimmed", f"{dev}:AUX1")
    stack.pw.wait_node("kmixdeck.in.trimmed")
    stack.cli("mix", "output-add", "stream", f"{sink}:AUX1,AUX2")
    wait_for(lambda: sink in stack.cli("mix", "outputs", "stream", json_out=True)[0] if stack.cli("mix", "outputs", "stream", json_out=True) else False, timeout=5.0, what="sink in stack.cli('mix', 'outputs', 'stream', json_out=True)[0] if sta")
    wait_for(lambda: stack.pw.node("kmixdeck.out.stream") or stack.pw.node("kmixdeck.out.stream.1"), timeout=8.0, what="stack.pw.node('kmixdeck.out.stream') or stack.pw.node('kmixdeck.out.st")
    hw_before = (stack.pw.props(dev), stack.pw.props(sink))

    def ch_level(): return stack.pw.level_at_port("kmixdeck.channel.trimmed", "monitor_FL")
    p = stack.pw.play_into_port(dev, "input_AUX1")
    try:
        unity = wait_level(ch_level, lambda v: v > HOT)
        # -- input wire: trim to 50 % (cubic) → linear 0.125 → -18 dB
        assert stack.cli("channel", "wire", "trimmed", f"{dev}:AUX1", "trim", "50%").returncode == 0
        half = wait_level(ch_level, lambda v: v < unity - 12)
        assert -21 < (half - unity) < -15, f"50 % trim should be ≈ -18 dB, got {half - unity:.1f} dB"
        shown = stack.cli("channel", "wire", "trimmed", f"{dev}:AUX1", json_out=True)
        assert abs(shown["trim"] - 0.5) < 0.01 and shown["muted"] is False, shown
        # -- mute
        stack.cli("channel", "wire", "trimmed", f"{dev}:AUX1", "mute", "on")
        wait_level(ch_level, lambda v: v < SILENT)
        stack.cli("channel", "wire", "trimmed", f"{dev}:AUX1", "mute", "off")
        wait_level(ch_level, lambda v: v > SILENT + 10)
        # -- output wire, measured at the sink's input side (monitor of the sink)
        def sink_level(): return stack.pw.level_at_port(sink, "monitor_AUX1")
        loud = wait_level(sink_level, lambda v: v > HOT - 30)
        assert stack.cli("mix", "wire", "stream", f"{sink}:AUX1,AUX2", "trim", "-12dB").returncode == 0
        quiet = wait_level(sink_level, lambda v: v < loud - 8)
        assert -15 < (quiet - loud) < -9, f"-12 dB output trim, got {quiet - loud:.1f} dB"
        # -- the hardware nodes themselves are untouched (CT-5)
        assert (stack.pw.props(dev), stack.pw.props(sink)) == hw_before, "device volume/mute was touched"
        # -- unknown wire is refused
        assert stack.cli("channel", "wire", "trimmed", f"{dev}:AUX3", "trim", "0.5", check=False).returncode != 0
    finally:
        p.kill(); p.wait()

    # -- unplug + replug: the trim is on the wire, so the new edge node gets it again
    destroy_node(stack, dev); time.sleep(0.6)
    make_device(stack, dev, "Trim Source", "Audio/Source/Virtual")
    stack.pw.wait_node("kmixdeck.in.trimmed"); time.sleep(0.8)
    p = stack.pw.play_into_port(dev, "input_AUX1")
    try:
        after = wait_level(ch_level, lambda v: v > SILENT + 10)
        assert -21 < (after - unity) < -15, f"trim lost across replug: {after - unity:.1f} dB vs unity"
    finally:
        p.kill(); p.wait()
    # -- restart: persisted on disk with the wire
    layout = json.loads(open(os.path.join(stack.env["XDG_CONFIG_HOME"], "kmixdeck", "layout.json")).read())
    wire = next(i for i in layout["inputs"] if i["channel"] == "trimmed")["device"]
    assert abs(wire["trim"] - 0.5) < 0.01, wire
    stack.restart_daemon()
    got = stack.cli("mix", "wire", "stream", f"{sink}:AUX1,AUX2", json_out=True)
    assert abs(got["trim"] - (10 ** (-12 / 20)) ** (1 / 3)) < 0.02, got
    stack.cli("mix", "output-remove", "stream", f"{sink}:AUX1,AUX2")
    # …and the channel + the two devices this test created. Nothing removed them until 2026-09-22, so the
    # `trimmed` channel with its 7 nodes stayed in the module-wide graph for every later test — small on its own,
    # but this is exactly how the graph grows into the state dv30c cannot survive.
    stack.cli("channel", "remove", "trimmed", check=False)
    destroy_node(stack, dev); destroy_node(stack, sink)


def test_dv28_ui24r_scale_32_in_32_out_every_port_routable_and_fast(stack):
    """DV-28 (every input and output of a Ui24R-class device configurable and routable): a 32-in/32-out
    device (the Ui24R's USB shape, measured on a headless box with the Ui24R on USB) — 32 mono channels on AUX1..32, mixes on AUX1+2 and AUX31+32,
    a second side-wire on a high port; every edge exists within a few seconds (not 0.7 s each: that was the test's own
    per-name polling, 2026-09-17), audio on port 32 reaches only channel 32, the mixes reach exactly their ports,
    and everything is back after a daemon restart."""
    import json, os
    # 🔴 try/finally around EVERYTHING, not just the tone (2026-09-22): the cleanup at the bottom used to sit in
    # straight-line code, so any failure above it left 32 channels + 64 loopbacks + the 32×32 device in the
    # module-wide graph. Measured that day: dv28 failed, leaked 225 nodes, and dv6/dv30/dv30c then failed in a
    # 711-node graph — one real failure, three phantoms, exactly the state the comment below warned about.
    try:
        stack.cli("devices", "virtual", "add", "Ui24R", "--in", "32", "--out", "32")
        din, dout = "kmixdeck.virt.ui24r", "kmixdeck.virt.ui24r.out"
        stack.pw.wait_nodes([din, dout])
        stack.pw.wait_ports(din, 32); stack.pw.wait_ports(dout, 32)
        assert len([p for p in stack.cli("devices", "ports", din).stdout.split() if p.startswith("AUX")]) == 32
        assert len([p for p in stack.cli("devices", "ports", dout).stdout.split() if p.startswith("AUX")]) == 32
        for i in range(1, 33): stack.cli("channel", "add", f"In {i}")
        for i in range(1, 33): stack.cli("channel", "input-add", f"in_{i}", f"{din}:AUX{i}")
        took = stack.pw.wait_nodes([f"kmixdeck.in.in_{i}" for i in range(1, 33)], timeout=30)
        assert took < 10, f"32 input edges took {took:.1f}s"
        # DV-29: whatever a mix returns to the desk's outputs is audible on the desk's inputs of the same number — the
        # pass-through is the point of a virtual device. A channel listening to the port a mix returns on is a feedback loop
        # exactly like on the real desk, so the returns go to ports no channel listens to.
        for i in (1, 2, 31, 32): stack.cli("channel", "input-remove", f"in_{i}", f"{din}:AUX{i}")
        stack.cli("mix", "output-add", "stream", f"{dout}:AUX1,AUX2")
        stack.cli("mix", "output-add", "monitor", f"{dout}:AUX31,AUX32")
        assert stack.cli("channel", "input-add", "in_21", f"{din}:AUX22>R").returncode == 0     # second wire, side-bound (DV-21/25)
        assert stack.cli("channel", "input-add", "in_1", f"{din}:AUX99", check=False).returncode != 0   # no such port → refused
        time.sleep(1.5)
        p = stack.pw.play_into_port(din + ".out", "playback_AUX30")
        try:
            try:
                wait_level(lambda: stack.pw.level_at_port("kmixdeck.channel.in_30", "monitor_FL"), lambda x: x > HOT)
            except AssertionError as err:
                # Diagnose 2026-09-22 (dv28 flaky 2/5 on HEAD as well): dump what the tone is ACTUALLY linked to.
                import subprocess as _sp
                links = _sp.run(["pw-link", "-l"], env=stack.pw.env, capture_output=True, text=True).stdout
                spuren = [l for l in links.splitlines() if "pw-play" in l or "AUX30" in l]
                spieler = [n for n in stack.pw.node_names() if "pw-play" in n]
                raise AssertionError(
                    f"tone on AUX30 never reached channel 30 ({err}).\n"
                    f"pw-play nodes present: {spieler}\n"
                    f"links mentioning pw-play or AUX30:\n  " + "\n  ".join(spuren[:12])) from err
            assert stack.pw.level_at_port("kmixdeck.channel.in_3", "monitor_FL") < SILENT, "port 30 leaked into channel 3"
            stack.cli("cell", "volume", "in_30", "stream", "1.0", check=False)
            wait_level(lambda: stack.pw.level_at_port(dout, "monitor_AUX1"), lambda x: x > HOT - 30)
            assert stack.pw.level_at_port(dout, "monitor_AUX31") > HOT - 30, "monitor mix did not reach AUX31"
            assert stack.pw.level_at_port(dout, "monitor_AUX5") < SILENT, "an unused output port carries signal"
        finally:
            p.kill(); p.wait()
        stack.restart_daemon()
        back = stack.pw.wait_nodes([f"kmixdeck.in.in_{i}" for i in range(3, 31)] + ["kmixdeck.out.stream", "kmixdeck.out.monitor"], timeout=40)
        assert back < 15, f"restart: 32 edges took {back:.1f}s to return"
        assert set(stack.cli("channel", "inputs", "in_21").stdout.split()) == {f"{din}:AUX21", f"{din}:AUX22>R"}
        layout = json.loads(open(os.path.join(stack.env["XDG_CONFIG_HOME"], "kmixdeck", "layout.json")).read())
        assert len([i for i in layout["inputs"] if i["channel"].startswith("in_")]) == 29   # 28 listening channels + the side wire on in_21
    finally:
        # leave the module-wide stack as we found it: 32 channels + 64 loopbacks + a 32×32 device made every later test
        # in this file run in a ~150-node graph — DV-6 went red only in that state (ports-full2, 2026-09-17)
        for i in range(1, 33): stack.cli("channel", "remove", f"in_{i}", check=False)
        stack.cli("mix", "output", "stream", "none", check=False); stack.cli("mix", "output", "monitor", "none", check=False)
        stack.cli("devices", "virtual", "remove", "ui24r", check=False)
        wait_for(lambda: not any(n.startswith("kmixdeck.in.in_") for n in stack.pw.node_names()), timeout=10.0, what="not any(n.startswith('kmixdeck.in.in_') for n in stack.pw.node_names()")


def test_dv6_sleep_wake_every_device_gone_and_back_routing_intact_no_restart(stack):
    """DV-6: suspend/resume as PipeWire sees it — EVERY device node disappears at once (USB re-enumerates) and comes
    back a moment later with new ids. Nothing the user set may be lost and nothing may need a restart: the listening
    device, both mix outputs, the port-level input wire, faders/mutes and the app's channel all stand, and audio flows
    input→channel→mix→speakers again within seconds. Measured, not read from properties."""
    from test_service_cli import make_fake_sink, destroy_node, start_fake_app
    import subprocess
    # the "laptop": speakers + headphones + a 4-in interface, one input wire, one app
    make_fake_sink(stack, "fake.speakers", "Laptop Speakers"); make_fake_sink(stack, "fake.cans", "Headphones")
    stack.cli("devices", "virtual", "add", "Interface", "--in", "4", "--out", "4"); stack.pw.wait_nodes(["kmixdeck.virt.interface"])
    iface = "kmixdeck.virt.interface"
    for _ in range(50):   # node first, its ports a moment later (ctest25 under load: "has no port 'AUX3'")
        if "AUX3" in stack.cli("devices", "ports", iface, check=False).stdout: break
        time.sleep(0.1)
    # this test owns the mixes' outputs: the module-wide stack may still carry an output from an earlier test
    # (fake.rode.out from DV-17 — present, so OutputPresent stayed True and this test went red under ctest30 only)
    stack.cli("mix", "output", "stream", "none"); stack.cli("mix", "output", "monitor", "none")
    stack.cli("mix", "output-add", "monitor", "fake.cans"); stack.cli("listen", "fake.cans"); stack.cli("mix", "output-add", "stream", "fake.speakers")
    stack.cli("channel", "add", "Mic"); stack.cli("channel", "input-add", "mic", f"{iface}:AUX3")
    stack.cli("cell", "set", "mic", "stream", "-6dB"); stack.cli("channel", "mute", "game", "on")
    # the cell loopback of a brand-new channel is created asynchronously; WirePlumber writes its default volume to a
    # node it has never seen right after creation (MX-8, 2026-09-17). Make sure the -6 dB really landed before the
    # sleep/wake part measures whether it SURVIVED — otherwise "lost over sleep" and "never applied" look the same.
    for _ in range(50):
        v = next((c["Volume"] for c in stack.cli("status", json_out=True)["cells"] if c["Path"].endswith("/mic/stream")), None)
        if v is not None and abs(v - 10 ** (-6 / 20)) < 0.02: break
        time.sleep(0.1)
    else:
        stack.cli("cell", "set", "mic", "stream", "-6dB"); time.sleep(0.5)
        v = next(c["Volume"] for c in stack.cli("status", json_out=True)["cells"] if c["Path"].endswith("/mic/stream"))
        assert abs(v - 10 ** (-6 / 20)) < 0.02, f"cell set -6dB never applied (status {v:.3f}) — a daemon bug, not a sleep/wake one"
    p, app = start_fake_app(stack); stack.cli("app", "assign", "FakeGame", "voice")
    # the app plays the SAME test tone as the mic; two coherent tones summed in the stream mix land anywhere between
    # +6 dB and cancellation depending on their phase (measured −20.7 vs −25.5 dB between runs, ctest29). The level
    # comparison below is about the mic path, so the app's send into the stream mix is muted; it keeps playing into
    # the monitor mix, which is what the app part of this test checks.
    stack.cli("cell", "mute", "voice", "stream", "on")
    # …and make sure the mute LANDED (ctest34: the chain diagnosis showed 'voice/stream cell': [(1, False)] = unmuted,
    # the app's tone summed into the mic measurement; same WirePlumber restore-stream window as the -6 dB above)
    for _ in range(50):
        c = next((c for c in stack.cli("status", json_out=True)["cells"] if c["Path"].endswith("/voice/stream")), None)
        if c and c["Muted"] is True: break
        time.sleep(0.1)
    else:
        raise AssertionError("cell mute voice/stream never landed in the daemon (restore-stream race?)")
    stack.pw.wait_nodes(["kmixdeck.in.mic", "kmixdeck.out.stream", "kmixdeck.out.monitor"])
    time.sleep(1.0)
    tone = stack.pw.play_into_port(iface + ".out", "playback_AUX3")
    try:
        before = settled_level(lambda: stack.pw.level_at("fake.speakers"), lambda v: v > SILENT + 10, tries=15)
        assert before > SILENT + 10, "baseline: mic tone should reach the speakers via the stream mix"
        # --- sleep: every device vanishes in one go (the virtual interface too — it is a "device" to the daemon)
        destroy_node(stack, "fake.speakers"); destroy_node(stack, "fake.cans")
        stack.cli("devices", "virtual", "remove", "interface", check=False)   # the USB interface is gone as well
        wait_for(lambda: not any(n in stack.pw.node_names() for n in ("fake.speakers", "fake.cans", iface)), timeout=5.0, what="not any(n in stack.pw.node_names() for n in ('fake.speakers', 'fake.ca")
        # the daemon learns of the removal from PipeWire's registry — under a full ctest run that took > 1 s (ctest26/28)
        for _ in range(100):
            st = stack.cli("status", json_out=True)
            if next(m for m in st["mixes"] if m["Slug"] == "stream")["OutputPresent"] is False and next(c for c in st["channels"] if c["Slug"] == "mic")["InputPresent"] is False: break
            time.sleep(0.1)
        assert stack.cli("listen").stdout.strip().startswith("fake.cans"), "listening device must be remembered while it is gone"
        assert "fake.speakers" in next(m for m in st["mixes"] if m["Slug"] == "stream")["Outputs"], "mix output must be remembered while it is gone"
        assert stack.cli("channel", "inputs", "mic", json_out=True) == [f"{iface}:AUX3"], "input wire must be remembered"
        if next(c for c in st["channels"] if c["Slug"] == "mic")["InputPresent"] is not False or next(m for m in st["mixes"] if m["Slug"] == "stream")["OutputPresent"] is not False:
            live = [n for n in stack.pw.node_names() if n in ("fake.speakers", "fake.cans", iface)]
            devs = stack.cli("devices", check=False).stdout.replace("\n", " | ")[:400]
            log = subprocess.run(["tail", "-c", "1500", stack.daemon_log_path], capture_output=True, text=True).stdout
            raise AssertionError(f"daemon still sees a removed device after 10 s: mic InputPresent={next(c for c in st['channels'] if c['Slug'] == 'mic')['InputPresent']} stream OutputPresent={next(m for m in st['mixes'] if m['Slug'] == 'stream')['OutputPresent']}\n graph still has: {live}\n devices: {devs}\n daemon log: {log}")
        # --- wake: everything re-enumerates (new ids), in a different order than it left
        stack.cli("devices", "virtual", "add", "Interface", "--in", "4", "--out", "4")
        make_fake_sink(stack, "fake.cans", "Headphones"); make_fake_sink(stack, "fake.speakers", "Laptop Speakers")
        stack.pw.wait_nodes([iface, "fake.cans", "fake.speakers"])
        tone.kill(); tone.wait()
        tone = stack.pw.play_into_port(iface + ".out", "playback_AUX3")
        t0 = time.time()
        after = wait_level(lambda: stack.pw.level_at("fake.speakers"), lambda v: v > SILENT + 10, tries=25)
        if not after > SILENT + 10:
            st2 = stack.cli("status", json_out=True)
            chain = {n: round(stack.pw.level_at(n), 1) for n in ("kmixdeck.channel.mic", "kmixdeck.mix.stream")}
            chain[iface + " AUX3"] = round(stack.pw.rms_db(stack.pw.record_port(iface, "capture_AUX3")), 1)
            links = {k: subprocess.run(["pw-link", "-l", k], env=stack.env, capture_output=True, text=True).stdout.replace("\n", " | ")[:300]
                     for k in ("kmixdeck.in.mic.in:input_AUX3", "kmixdeck.in.mic:output_FL", "kmixdeck.out.stream:output_FL", "fake.speakers:playback_FL")}
            edges = sorted(n for n in stack.pw.node_names() if n.startswith(("kmixdeck.in.", "kmixdeck.out.")))
            log = subprocess.run(["tail", "-c", "2000", stack.daemon_log_path], capture_output=True, text=True).stdout
            raise AssertionError(f"after wake the mic tone does not reach the speakers again ({after:.1f} dB)\n chain: {chain}\n mixes: {[(m['Slug'], m['Outputs'], m['OutputPresent']) for m in st2['mixes']]}\n mic inputs: {stack.cli('channel', 'inputs', 'mic', json_out=True)} present={next(c for c in st2['channels'] if c['Slug'] == 'mic')['InputPresent']}\n edges: {edges}\n links: {links}\n daemon log: {log}")
        took = time.time() - t0
        after = settled_level(lambda: stack.pw.level_at("fake.speakers"), lambda v: v > SILENT + 10, tries=15)
        if abs(after - before) >= 3:
            vols = {}
            for o in stack.pw.dump():
                n = o.get("info", {}).get("props", {}).get("node.name", "")
                if n.startswith(("kmixdeck.in.mic", "kmixdeck.channel.mic", "kmixdeck.link.mic.stream", "kmixdeck.mix.stream", "kmixdeck.out.stream")):
                    vols[n] = [p.get("channelVolumes") for p in o["info"].get("params", {}).get("Props", []) if "channelVolumes" in p]
            chain = {n: round(stack.pw.level_at(n), 1) for n in ("kmixdeck.channel.mic", "kmixdeck.channel.voice", "kmixdeck.channel.game", "kmixdeck.channel.system", "kmixdeck.mix.stream", "fake.speakers")}
            chain["voice/stream cell"] = [(round(c["Volume"], 3), c["Muted"]) for c in stack.cli("status", json_out=True)["cells"] if c["Path"].endswith("/voice/stream")]
            chain["fakegame links"] = subprocess.run(["pw-link", "-o", "-l", "fakegame-out"], env=stack.env, capture_output=True, text=True).stdout.replace("\n", " | ")[:300]
            links = subprocess.run(["pw-link", "-l", "fake.speakers:playback_FL"], env=stack.env, capture_output=True, text=True).stdout.replace("\n", " | ")[:400]
            raise AssertionError(f"level after wake differs: {before:.1f} → {after:.1f} dB\n volumes: {vols}\n chain: {chain}\n speakers FL links: {links}")
        cans = wait_level(lambda: stack.pw.level_at("fake.cans"), lambda v: v > SILENT + 10, tries=10)
        if not cans > SILENT + 10:
            st2 = stack.cli("status", json_out=True)
            raise AssertionError("monitor mix must play to the headphones again (%.1f dB)\n mixes: %s\n monitor mix level: %.1f  mic cell/monitor: %s\n out.monitor links: %s\n cans inputs: %s" % (
                cans, [(m["Slug"], m["Outputs"], m["OutputPresent"]) for m in st2["mixes"]], stack.pw.level_at("kmixdeck.mix.monitor"),
                [(round(c["Volume"], 2), c["Muted"]) for c in st2["cells"] if c["Path"].endswith("/mic/monitor")],
                subprocess.run(["pw-link", "-l", "kmixdeck.out.monitor:output_FL"], env=stack.env, capture_output=True, text=True).stdout.replace("\n", " | ")[:300],
                subprocess.run(["pw-link", "-l", "fake.cans:playback_FL"], env=stack.env, capture_output=True, text=True).stdout.replace("\n", " | ")[:300]))
        st = stack.cli("status", json_out=True)
        assert next(c for c in st["channels"] if c["Slug"] == "mic")["InputPresent"] is True
        assert next(m for m in st["mixes"] if m["Slug"] == "stream")["OutputPresent"] is True
        assert next(c for c in st["channels"] if c["Slug"] == "game")["Muted"] is True, "mute lost over sleep/wake"
        cellv = next(c for c in st["cells"] if c["Path"].endswith("/mic/stream"))["Volume"]
        if abs(cellv - 10 ** (-6 / 20)) >= 0.02:
            pwv = [(o["id"], [p.get("channelVolumes") for p in o["info"].get("params", {}).get("Props", []) if "channelVolumes" in p]) for o in stack.pw.dump() if o.get("info", {}).get("props", {}).get("node.name", "") == "kmixdeck.link.mic.stream"]
            log = subprocess.run(["grep", "-a", "-n", "-i", "mic", stack.daemon_log_path], capture_output=True, text=True).stdout[-1500:]
            raise AssertionError(f"cell fader lost: status {cellv:.3f}, pipewire {pwv}\n daemon log (mic): {log}")
        assert next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"] == ["voice"], "app assignment lost"
        assert stack.cli("listen").stdout.strip().startswith("fake.cans")
        # no restart happened: same daemon pid on the bus
        owner = subprocess.run(["busctl", "--user", "status", "org.kmixdeck1"], env=stack.env, capture_output=True, text=True).stdout
        assert f"PID={stack.daemon.pid}" in owner
        print(f"DV-6: audio back {took:.1f}s after the devices returned")
    finally:
        tone.kill(); tone.wait(); p.kill(); p.wait()
        stack.cli("channel", "mute", "game", "off"); stack.cli("channel", "remove", "mic", check=False)
        stack.cli("mix", "output-remove", "stream", "fake.speakers", check=False); stack.cli("mix", "output-remove", "monitor", "fake.cans", check=False); stack.cli("listen", "none", check=False)
        # …and the DEVICES this test created. They were missing here until 2026-09-22: the test destroys them
        # mid-run and re-creates them, so an abort in between left fake.speakers/fake.cans/the interface in the
        # module-wide graph (measured: 5 nodes) and every later test ran with an output that should not exist.
        destroy_node(stack, "fake.speakers"); destroy_node(stack, "fake.cans")
        stack.cli("devices", "virtual", "remove", "interface", check=False)


def _links_into(stack, prefix: str) -> dict[str, str]:
    """{edge input port: source port} for every pw-link whose destination node starts with `prefix`."""
    out = subprocess.run(["pw-link", "-l", "-i"], env=stack.pw.env, capture_output=True, text=True).stdout
    table, cur = {}, None
    for line in out.splitlines():
        if not line.startswith(" "): cur = line.strip(); continue
        if cur and cur.startswith(prefix) and "|<-" in line: table[cur] = line.split("|<-", 1)[1].strip()
    return table


def test_dv30_rewiring_a_channel_retargets_its_links(stack):
    """DV-30 (found on the Ui24R 2026-09-18): `input-remove` + `input-add` on one channel MUST leave the PipeWire link
    table equal to the layout — old ports unlinked, new ports linked to the right side — and audio must follow."""
    node = stack.cli("devices", "virtual", "add", "Rewire", "--in", "8", "--out", "8").stdout.strip()
    # 🔴 try/finally around the body (2026-09-22): the two inner try blocks only guard the tones — the cleanup on
    # the last line sat in straight-line code, so a failed level assertion leaked the channel + device into the
    # module-wide graph for every later test. Measured: 12 nodes left behind after this test went red.
    try:
        stack.pw.wait_node(node)
        stack.pw.wait_ports(node, 8)
        stack.cli("channel", "add", "Patch")
        assert stack.cli("channel", "input-add", "patch", f"{node}:AUX2>L").returncode == 0
        assert stack.cli("channel", "input-add", "patch", f"{node}:AUX3>R").returncode == 0
        stack.pw.wait_node("kmixdeck.in.patch.in"); stack.pw.wait_node("kmixdeck.in.patch.w1.in"); time.sleep(0.8)
        # rewire: AUX2>L,AUX3>R  →  AUX3>L,AUX4>R   (remove by the EXACT ref the layout reports)
        assert stack.cli("channel", "input-remove", "patch", f"{node}:AUX2>L").returncode == 0
        assert stack.cli("channel", "input-remove", "patch", f"{node}:AUX3>R").returncode == 0
        assert stack.cli("channel", "input-add", "patch", f"{node}:AUX3>L").returncode == 0
        assert stack.cli("channel", "input-add", "patch", f"{node}:AUX4>R").returncode == 0
        assert stack.cli("channel", "inputs", "patch").stdout.split() == [f"{node}:AUX3>L", f"{node}:AUX4>R"]

        def sources():
            return sorted(_links_into(stack, "kmixdeck.in.patch").values())
        want = sorted([f"{node}:capture_AUX3", f"{node}:capture_AUX4"])
        wait_for(lambda: sources() == want, timeout=10, what=f"links after rewire, got {sources()}")
        # audio follows: AUX4 → right only, AUX2 (old) → nothing
        p = stack.pw.play_into_port(node + ".out", "playback_AUX4")
        try:
            right = wait_level(lambda: stack.pw.level_at_port("kmixdeck.channel.patch", "monitor_FR"), lambda v: v > HOT)
            left = stack.pw.level_at_port("kmixdeck.channel.patch", "monitor_FL")
            assert right > HOT and left < SILENT, f"AUX4 → R only: L={left} R={right}"
        finally:
            p.kill(); p.wait()
        p = stack.pw.play_into_port(node + ".out", "playback_AUX2")
        try:
            time.sleep(0.8)
            assert stack.pw.level_at_port("kmixdeck.channel.patch", "monitor_FL") < SILENT, "old port AUX2 still reaches the channel"
        finally:
            p.kill(); p.wait()
    finally:
        stack.cli("channel", "remove", "patch", check=False); stack.cli("devices", "virtual", "remove", "rewire", check=False)


def test_dv30b_fd_limit_lifted_and_a_failed_edge_is_reported_not_swallowed(stack):
    """DV-30, second finding (Ui24R 2026-09-18): 47 edges = 47 PipeWire clients ≈ 913 fds; at the 1024 soft limit
    PipeWire logged only 'Protocol error' and the new wire was silently missing. Now: (a) the daemon lifts its soft
    fd limit to the hard one on start, (b) an edge module that still fails lands in Mixer.LastError and in `status`."""
    limits = open(f"/proc/{stack.daemon.pid}/limits").read()
    soft, hard = [int(x) for x in [l for l in limits.splitlines() if l.startswith("Max open files")][0].split()[3:5]]
    assert soft == hard, f"daemon soft fd limit {soft} != hard {hard}"
    # 🔴 PRECONDITION, not an assertion about other tests (2026-09-20): LastError is write-only in
    # the daemon — src/mixer.cpp sets it in exactly two places and clears it nowhere, so it lives as
    # long as the Mixer object. A predecessor that lost an edge (dv28 dropped "Input: in_16" under
    # the 32×32 load) therefore made THIS test fail on its clean-slate assert before the clamp was
    # even applied: dv30b accused itself of someone else's finding. A fresh daemon process means a
    # fresh Mixer, so restart first and only then demand the clean slate. (Do not look for an
    # `errors clear` verb — there is none, and inventing one would be a product change for a test.)
    stack.restart_daemon()
    assert stack.cli("status", json_out=True)["lastError"] == "", \
        "LastError not empty on a freshly restarted daemon — it is set at startup, see the journal"
    # (b) restart the daemon with a module dir that has no loopback module: the edge CANNOT load.
    stack.daemon.terminate(); stack.daemon.wait(timeout=5)
    env = dict(stack.env); env["PIPEWIRE_MODULE_DIR"] = str(module_dir_without_loopback())
    stack.daemon = subprocess.Popen([str(BIN / "kmixdeckd")], env=env, stdout=subprocess.DEVNULL,
                                    stderr=open(stack.daemon_log_path, "a"), text=True)
    # 🔴 try/finally, not straight-line code (2026-09-19): the `stack` fixture is scope="module", so this test
    # hands the SHARED daemon to every test after it. When an assert below fired, the restore lines never ran
    # and dv30c/dv31/dv29/dv29b all failed with "node ... did not appear" — one real failure, four fake ones,
    # and the diagnosis pointed at the wrong tests. A test that breaks a shared resource must give it back.
    try:
        wait_for(lambda: stack.cli("status", check=False).returncode == 0, timeout=20, what="daemon up with no loopback module")
        node = stack.cli("devices", "virtual", "add", "Clamp", "--in", "2", "--out", "2").stdout.strip()
        # The sink is created server-side via pw_core_create_object("adapter") and must still come up;
        # only the pass-through edge is a loopback MODULE loaded in the daemon's own pw_context. That
        # asymmetry is the whole point: precondition satisfied, exactly one edge starved.
        stack.pw.wait_node(node + ".out")
        err = wait_for(lambda: stack.cli("status", json_out=True)["lastError"], timeout=10, what="LastError after a starved edge")
        assert "edge could not be created" in err, err
        assert "!! edge could not be created" in stack.cli("status").stdout
        log = open(stack.daemon_log_path).read()   # either path names the edge: synchronous load failure or the async watcher
        assert "edge could not be created" in log and ("never appeared" in log or "failed to load" in log), log[-600:]
    finally:
        # back to a healthy daemon for the rest of the module, whatever happened above
        stack.cli("devices", "virtual", "remove", "clamp", check=False)
        stack.restart_daemon()
    assert stack.cli("status", json_out=True)["lastError"] == ""


def test_dv30c_thirtytwo_by_thirtytwo_desk_never_loses_an_edge(stack):
    """The layout from the real desk: 32 mono channels + 4 stereo mixes on a 32x32 device ≈ 45 loopback clients.
    Every edge node must come up and LastError must stay empty — this is exactly what failed on hardware at fd 1024."""
    node = stack.cli("devices", "virtual", "add", "Desk", "--in", "32", "--out", "32").stdout.strip()
    din, dout = node, node + ".out"
    # 🔴 try/finally (2026-09-19, same lesson as dv30b): this builds the biggest layout in the suite —
    # MEASURED 2026-09-20, three isolated runs: 554 kmixdeck nodes and ~3900 fds, not the "≈45 loopback
    # clients" this comment claimed for a year (wrong by a factor of 12). The graph grows linearly:
    # +56 nodes per 8 channels, +75 nodes per mix. Building it takes ~20-23 s of CLI calls, after which
    # every node is present within 2.1-2.4 s when the machine is idle.
    # The 60 s timeout below was sized for the imaginary 45 and is why this test was flaky (1 in 6 full
    # runs, never alone): after 19 preceding tests on the shared daemon, 554 nodes need longer than that.
    # 180 s now — not padding, the isolated number times a load factor. If this ever times out again,
    # the cause is NOT patience: measure the node count first (the assertion prints it).
    # When one node failed to appear, the teardown below never ran and dv31/dv29/dv29b inherited a daemon
    # with 35 leftover nodes, failing with "node ... did not appear" on their own fresh devices.
    # One real failure, three fake ones.
    try:
        stack.pw.wait_node(dout); stack.pw.wait_node(din, timeout=15)
        # A node is announced before its ports exist. On 2-in/2-out that window is a few ms; on 32x32 under
        # load it is not, and the CLI then rejects `…:AUX1` with an EMPTY port list (measured 2026-09-22).
        stack.pw.wait_ports(din, 32); stack.pw.wait_ports(dout, 32)
        for i in range(1, 33):
            stack.cli("channel", "add", f"d{i}"); stack.cli("channel", "input", f"d{i}", f"{din}:AUX{i}")
        for k in range(4):
            stack.cli("mix", "add", f"r{k}"); stack.cli("mix", "output", f"r{k}", f"{dout}:AUX{2*k+1},AUX{2*k+2}")
        stack.pw.wait_nodes([f"kmixdeck.in.d{i}.in" for i in range(1, 33)] + [f"kmixdeck.out.r{k}" for k in range(4)], timeout=180)
        st = stack.cli("status", json_out=True)
        assert st["lastError"] == "", st["lastError"]
        fds = len(os.listdir(f"/proc/{stack.daemon.pid}/fd"))
        assert fds > 500, f"the desk should cost >500 fds (the reason this test exists), got {fds}"
    finally:
        for k in range(4): stack.cli("mix", "remove", f"r{k}", check=False)
        for i in range(1, 33): stack.cli("channel", "remove", f"d{i}", check=False)
        stack.cli("devices", "virtual", "remove", "desk", check=False)


def test_dv31_zero_based_hardware_gets_one_based_labels_and_refs_accept_both(stack):
    """DV-31 (Ui24R 2026-09-18): the desk's USB ports are AUX0..AUX31 in PipeWire while its surface counts 1-based, so
    'Aux 1/2 = Main' was patched as AUX1/AUX2 — one channel off. `devices ports` must show the 1-based label next to the
    PipeWire name, and a ref may use either. Virtual devices (AUX1..) and FL/FR keep position == label."""
    # a 0-based hardware stand-in (null source with AUX0..AUX3, like the USB driver names them)
    # Audio/Source/Virtual, not plain Audio/Source: a null *source* without an input side drops AUX0 from its port set
    # (measured 2026-09-19 — sinks and virtual sources keep it; the driver-backed Ui24R node has AUX0 as well)
    make_device(stack, "fake.desk", "Desk 0-based", "Audio/Source/Virtual", positions=["AUX0", "AUX1", "AUX2", "AUX3"])
    stack.pw.wait_node("fake.desk"); wait_for(lambda: "fake.desk" in stack.cli("devices", "in").stdout, timeout=10, what="fake.desk listed")
    wait_for(lambda: len(stack.cli("--json", "devices", "ports", "fake.desk", json_out=True)) == 4, timeout=10, what="4 ports registered")
    ports = stack.cli("--json", "devices", "ports", "fake.desk", json_out=True)
    by_pos = {p["position"]: p for p in ports}
    assert by_pos["AUX0"]["label"] == "USB 1" and by_pos["AUX3"]["label"] == "USB 4", ports
    text = stack.cli("devices", "ports", "fake.desk").stdout
    assert "AUX0" in text and "USB 1" in text, text
    # a ref by label resolves to the position, and the layout stores the position (stable across restarts, DV-9)
    stack.cli("channel", "add", "Main"); assert stack.cli("channel", "input", "main", "fake.desk:USB 1,USB 2").returncode == 0
    assert stack.cli("channel", "inputs", "main").stdout.split() == ["fake.desk:AUX0,AUX1"]
    # by position still works, and a wrong one is refused with a message that names both spellings
    assert stack.cli("channel", "input", "main", "fake.desk:AUX2,AUX3").returncode == 0
    r = stack.cli("channel", "input", "main", "fake.desk:AUX4", check=False)
    assert r.returncode != 0 and "USB " in r.stderr and "AUX" in r.stderr, r.stderr   # both spellings offered
    # virtual devices are 1-based already: label == position, no relabeling
    node = stack.cli("devices", "virtual", "add", "Plain", "--in", "2", "--out", "2").stdout.strip(); stack.pw.wait_node(node)
    vports = stack.cli("--json", "devices", "ports", node, json_out=True)
    assert all(p["label"] == p["position"] for p in vports), vports
    stack.cli("channel", "remove", "main"); stack.cli("devices", "virtual", "remove", "plain"); destroy_node(stack, "fake.desk")


def test_dv29_virtual_device_passes_apps_through_to_its_input_side(stack):
    """DV-29 (Ui24R run 2026-09-18): what an app plays into `.out` port N of a virtual device must appear on port N of
    the input side, so a channel wired to the device hears the app — Loopback's virtual device. Measured before the
    fix: −24 dB into .out:AUX3, −inf at :capture_AUX3 and at the channel. Other ports must stay silent."""
    node = stack.cli("devices", "virtual", "add", "Games", "--in", "8", "--out", "8").stdout.strip()
    stack.pw.wait_node(node + ".out"); stack.pw.wait_node(node + ".pass.in"); stack.pw.wait_node(node)   # sink, capture half, source (= playback half)
    stack.cli("channel", "add", "Hears"); stack.cli("channel", "input", "hears", f"{node}:AUX3,AUX4")
    stack.pw.wait_node("kmixdeck.in.hears.in"); time.sleep(0.8)
    # the app: a tone into the device's OUTPUT side, port AUX3 (this is what `pw-play --target Games` would do)
    p = stack.pw.play_into_port(node + ".out", "playback_AUX3")
    try:
        cap = wait_level(lambda: stack.pw.level_at_port(node, "capture_AUX3"), lambda v: v > HOT)
        assert cap > HOT, f"pass-through .out:AUX3 → :AUX3 silent ({cap} dB)"
        assert stack.pw.level_at_port(node, "capture_AUX5") < SILENT, "port 5 carries port 3's audio"
        left = wait_level(lambda: stack.pw.level_at_port("kmixdeck.channel.hears", "monitor_FL"), lambda v: v > HOT)
        right = stack.pw.level_at_port("kmixdeck.channel.hears", "monitor_FR")
        assert left > HOT and right < SILENT, f"channel from AUX3,AUX4 hears AUX3 on L only: L={left} R={right}"
    finally:
        p.kill(); p.wait()
    # the pass-through goes with the device, leaving nothing behind
    stack.cli("channel", "remove", "hears"); stack.cli("devices", "virtual", "remove", "games")
    wait_for(lambda: stack.pw.node(node + ".pass.in") is None and stack.pw.node(node) is None and stack.pw.node(node + ".out") is None, timeout=10, what="virtual device + pass-through gone")


def test_dv29b_pass_through_survives_a_daemon_restart_and_comes_from_config_alone(stack):
    """AR-10: the generated PipeWire fragment must carry the pass-through too (the graph must come up from config
    without the daemon), and a daemon restart must not duplicate or lose it."""
    node = stack.cli("devices", "virtual", "add", "Persist", "--in", "4", "--out", "8").stdout.strip()
    stack.pw.wait_node(node + ".out"); stack.pw.wait_node(node + ".pass.in", timeout=15); stack.pw.wait_node(node)
    conf = open(os.path.join(stack.env["XDG_CONFIG_HOME"], "pipewire", "pipewire.conf.d", "90-kmixdeck.conf")).read()
    assert "kmixdeck.virt.persist.pass" in conf, "pass-through missing from the generated fragment"
    line = next(l for l in conf.splitlines() if "kmixdeck.virt.persist.pass.in" in l)
    cap = line.split("capture.props")[1].split("playback.props")[0]
    assert "AUX4" in cap and "AUX5" not in cap, f"pass-through must carry min(in,out)=4 ports: {cap}"
    stack.restart_daemon(); time.sleep(2.0)
    virt = sorted(n for n in stack.pw.node_names() if n.startswith("kmixdeck.virt.persist"))
    assert virt == ["kmixdeck.virt.persist", "kmixdeck.virt.persist.out", "kmixdeck.virt.persist.pass.in"], virt
    stack.cli("devices", "virtual", "remove", "persist")
