# SPDX-License-Identifier: GPL-3.0-or-later
"""Effects end-to-end (ADR 0008 / FX-1…FX-7): chain via CLI → filter-chain in the graph → sound changes live.

Same three truths as the lifecycle tests: bus, graph, layout.json — plus the acoustic one:
a gate with a high threshold silences a ~−24 dB signal, bypass brings it back.
"""
import pytest
import json, subprocess, time
from test_service_cli import Stack
from pw_sandbox import start_private_pipewire

# gate/compressor/limiter come from the swh LADSPA set (ADR 0008); without it every fx test is meaningless
import os, glob
_SWH = any(glob.glob(d + "/gate_1410.so") for d in (os.environ.get("LADSPA_PATH", "").split(":") + ["/usr/lib/ladspa", "/usr/lib64/ladspa", "/usr/lib/x86_64-linux-gnu/ladspa", "/usr/local/lib/ladspa"]) if d)
pytestmark = pytest.mark.skipif(not _SWH, reason="swh-plugins (LADSPA) not installed")

CHAIN = '{"enabled":true,"chain":[{"type":"gate","enabled":true,"params":{"threshold":-30}}]}'


def fixture_stack():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    return pw, Stack(pw)


def node_names(stack):
    return {o.get("info", {}).get("props", {}).get("node.name") for o in stack.pw.dump() if o.get("type") == "PipeWire:Interface:Node"}


def wait(pred, tries=80, dt=0.1):
    for _ in range(tries):
        if pred(): return True
        time.sleep(dt)
    return False


def test_fx_types_and_presets_are_on_the_bus():
    pw, s = fixture_stack()
    try:
        types = json.loads(s.cli("fx", "types").stdout)
        names = {t["type"] for t in types}
        assert {"highpass", "gate", "compressor", "eq", "limiter", "noise"} <= names     # FX-1 built-in set
        gate = next(t for t in types if t["type"] == "gate")
        assert {"threshold", "attack", "hold", "decay", "range"} == {p["key"] for p in gate["params"]}   # FX-2: exactly what gate_1410 exposes
        presets = json.loads(s.cli("fx", "presets").stdout)
        assert presets, "FX-4 wants one-click presets"
        clean = presets["Voice — clean"]
        assert [e["type"] for e in clean["chain"]] == ["noise", "highpass", "gate", "compressor", "limiter"]
    finally:
        s.close(); pw.close()


def test_fx_chain_set_get_and_live_control():
    pw, s = fixture_stack()
    try:
        assert s.cli("fx", "set", "channel", "voice", CHAIN).returncode == 0
        assert wait(lambda: any(n and n.startswith("kmixdeck.fx.voice") for n in node_names(s)))
        got = json.loads(s.cli("fx", "get", "channel", "voice").stdout)
        assert got["chain"][0]["type"] == "gate" and got["chain"][0]["params"]["threshold"] == -30    # FX-1 order survives

        # layout.json carries the chain → survives a daemon restart (DV-style persistence)
        data = json.loads((pw.runtime_dir / "config" / "kmixdeck" / "layout.json").read_text())
        assert next(c for c in data["channels"] if c["slug"] == "voice")["fx"]["chain"][0]["type"] == "gate"

        # FX-3: live control, no reload — same node ids before and after
        before = {n: s.pw.node_id(n) for n in node_names(s) if n and n.startswith("kmixdeck.fx.voice")}
        assert before, "fx nodes missing"
        assert s.cli("fx", "control", "channel", "voice", "threshold", "-1").returncode == 0
        after = {n: s.pw.node_id(n) for n in before}
        assert before == after, "live control must not rebuild the node"

        # FX-6: the same chain on a MIX (limiter on the Stream mix)
        assert s.cli("fx", "set", "mix", "stream", '{"enabled":true,"chain":[{"type":"limiter","enabled":true,"params":{"limit":-6}}]}').returncode == 0
        assert wait(lambda: any(n and n.startswith("kmixdeck.fx.mix.stream") for n in node_names(s)))

        # FX-7: copy Voice's chain onto Game — Game has no hardware input, chain works regardless
        assert s.cli("fx", "copy", "channel", "voice", "channel", "game").returncode == 0
        assert json.loads(s.cli("fx", "get", "channel", "game").stdout)["chain"][0]["type"] == "gate"

        # FX-5 chain bypass: enabled=false → fx node goes away, plain channel sink stays
        assert s.cli("fx", "set", "channel", "voice", '{"enabled":false,"chain":[{"type":"gate","enabled":true,"params":{}}]}').returncode == 0
        assert wait(lambda: "kmixdeck.fx.voice" not in node_names(s))
        assert "kmixdeck.channel.voice" in node_names(s)

        # clearing: empty chain → the plain shape again
        assert s.cli("fx", "clear", "channel", "voice").returncode == 0
        assert json.loads(s.cli("fx", "get", "channel", "voice").stdout or "null") in ({}, None)
    finally:
        s.close(); pw.close()


def test_fx_refuses_bad_input_and_keeps_the_old_chain():
    pw, s = fixture_stack()
    try:
        s.cli("fx", "set", "channel", "voice", CHAIN)
        r = s.cli("fx", "set", "channel", "voice", '{"enabled":true,"chain":[{"type":"does-not-exist"}]}', check=False)
        assert r.returncode == 4, r.stdout + r.stderr       # rejected with a reason, old chain kept
        assert json.loads(s.cli("fx", "get", "channel", "voice").stdout)["chain"][0]["type"] == "gate"
        r = s.cli("fx", "set", "channel", "voice", '{"enabled":true,"chain":[{"type":"gate","params":{"threshold":500}}]}', check=False)
        assert r.returncode == 4                            # out of range
        assert json.loads(s.cli("fx", "get", "channel", "voice").stdout)["chain"][0]["params"]["threshold"] == -30
    finally:
        s.close(); pw.close()


def test_fx_changes_the_sound_and_bypass_restores_it():
    """The tone sits at ≈ −24 dBFS (measured). Gate at −40 → open; raised to 0 → shut; effect bypassed → open again."""
    pw, s = fixture_stack()
    try:
        # baseline: voice → monitor with no effects at all (no chain yet → the entry IS the plain sink)
        play = s.pw.play_into("kmixdeck.channel.voice")
        time.sleep(0.6)
        base = s.pw.level_at("kmixdeck.mix.monitor")
        assert base > -30, f"baseline tone should be audible, got {base}"
        play.terminate(); play.wait(timeout=5)

        # FX on, threshold well below the signal (−40 vs. −24): gate passes.
        # Apps aim at the fx entry; cells capture the plain sink behind the chain (ADR 0008 D1/FX-3).
        s.cli("fx", "set", "channel", "voice", '{"enabled":true,"chain":[{"type":"gate","enabled":true,"params":{"threshold":-40,"range":-90,"hold":10,"decay":50}}]}')
        assert wait(lambda: "kmixdeck.fx.voice" in node_names(s))
        # a REAL app stream (autoconnect on, target.object set) — WirePlumber must follow the daemon's retargets
        # when the chain is swapped; play_into() links ports by hand and would never be re-linked (FX-5).
        play = subprocess.Popen(["pw-play", "-P", '{ application.name = "FxProbe" node.name = "fxprobe-out" target.object = "kmixdeck.fx.voice" }',
                                 str(s.pw.tone())], env=s.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.0)
        open_level = s.pw.level_at("kmixdeck.mix.monitor")
        assert open_level > -30, f"signal above the threshold must pass, got {open_level}"

        # threshold to 0 dB → the ~−24 dB signal falls below it, gate closes
        assert s.cli("fx", "control", "channel", "voice", "threshold", "0").returncode == 0
        time.sleep(0.8)
        shut_level = s.pw.level_at("kmixdeck.mix.monitor")
        assert shut_level < open_level - 10, f"raising the threshold must cut the signal: {open_level} → {shut_level}"

        # bypass the gate inside the chain: untouched level again
        s.cli("fx", "set", "channel", "voice", '{"enabled":true,"chain":[{"type":"gate","enabled":false,"params":{"threshold":0}}]}')
        time.sleep(0.8)
        byp_level = s.pw.level_at("kmixdeck.mix.monitor")
        assert byp_level > shut_level + 10, f"bypass must restore the level: {shut_level} → {byp_level}"
        play.terminate(); play.wait(timeout=5)
    finally:
        s.close(); pw.close()


def test_fx7_chain_copies_between_channels_even_with_absent_device():
    """FX-7: a chain is plain JSON on the bus — `fx get a` piped into `fx set b` copies it 1:1, also onto a channel
    whose input device is not plugged in (the chain is layout, the device edge is not)."""
    pw, s = fixture_stack()
    try:
        s.cli("fx", "set", "channel", "voice", CHAIN)
        src = json.loads(s.cli("fx", "get", "channel", "voice").stdout)
        from test_service_cli import make_fake_source, destroy_node, wait_prop
        s.cli("channel", "add", "Absent Mic")
        make_fake_source(s, "fake.mic7", "Fake Mic 7")
        assert wait(lambda: "fake.mic7" in s.cli("devices", "in", json_out=True))
        s.cli("channel", "input", "absent_mic", "fake.mic7")
        destroy_node(s, "fake.mic7")                                                   # unplug: DV-9 keeps the value
        assert wait_prop(s, "channel", "absent_mic", "InputPresent", False) is False
        assert s.cli("fx", "set", "channel", "absent_mic", json.dumps(src)).returncode == 0
        dst = json.loads(s.cli("fx", "get", "channel", "absent_mic").stdout)
        assert dst["chain"] == src["chain"] and dst["enabled"] == src["enabled"], (src, dst)
        assert wait(lambda: any(n and n.startswith("kmixdeck.fx.absent_mic") for n in node_names(s)))
        s.restart_daemon()
        assert json.loads(s.cli("fx", "get", "channel", "absent_mic").stdout)["chain"] == src["chain"]
    finally:
        s.close(); pw.close()
