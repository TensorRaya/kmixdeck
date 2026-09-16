#!/usr/bin/env python3
"""Render kmixdeck-kde offscreen against a private sandbox daemon (same harness as the integration tests), with a
layout like the laptop: 3 channels, 2 mixes, a playing app, a mic device. Usage: render-ui.py OUT.png [--open channel] [--size WxH]"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "integration"))
from test_fx import fixture_stack
from test_service_cli import make_fake_source, make_fake_sink, start_fake_app

out = sys.argv[1]; extra = sys.argv[2:]
pw, s = fixture_stack()
try:
    make_fake_source(s, "fake.mic", "RØDECaster Pro II Secondary")
    make_fake_sink(s, "fake.headphones", "RØDECaster Pro II Speaker")
    # ADR 0009: a multichannel source like the Ui24R (ports AUX1..AUX8) so the picker shows a port grid
    import subprocess
    subprocess.run(["pw-cli", "create-node", "adapter", '{ factory.name=support.null-audio-sink node.name=fake.ui24r node.description="Soundcraft Ui24R" media.class=Audio/Source/Virtual audio.position=[ AUX1 AUX2 AUX3 AUX4 AUX5 AUX6 AUX7 AUX8 ] object.linger=true }'], env=s.pw.env, capture_output=True)
    s.pw.wait_node("fake.ui24r")
    for _ in range(30):
        if "fake.headphones" in s.cli("devices", json_out=True): break
        time.sleep(0.1)
    s.cli("mix", "output", "monitor", "fake.headphones"); s.cli("listen", "fake.headphones")
    for _ in range(30):
        if "fake.ui24r" in s.cli("devices", "in", json_out=True): break
        time.sleep(0.1)
    s.cli("channel", "add", "Talkback"); s.cli("channel", "input", "talkback", "fake.ui24r:AUX7")
    s.cli("channel", "input", "voice", "fake.mic")
    p, app = start_fake_app(s)
    s.cli("app", "move", "FakeGame", "game")
    s.cli("cell", "set", "game", "monitor", "-6dB")
    time.sleep(1.0)
    env = dict(s.env); env["QT_QPA_PLATFORM"] = "offscreen"
    bin_ = os.path.join(os.path.dirname(s.cli_bin) if hasattr(s, "cli_bin") else os.path.expanduser("~/repos/kmixdeck/build/bin"), "kmixdeck-kde")
    r = subprocess.run([bin_, "--screenshot", out] + extra, env=env, capture_output=True, text=True, timeout=40)
    print([l for l in (r.stdout + r.stderr).splitlines() if "screenshot" in l or "rror" in l])
    p.kill(); p.wait()
finally:
    s.close(); pw.close()
