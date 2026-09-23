# SPDX-License-Identifier: GPL-3.0-or-later
"""Nothing a user reads or presses overlaps something else, and nothing sticks out of its card — KDE window and
web UI, with the setup that broke it: long device names, a group, active effects AND active ducking, 2/5/8 mixes,
laptop to full-HD and a phone.

Why this exists (2026-09-23, Michel: "sehe da teilweise was überlappen"): with 5 mixes the KDE channel column was
216 px, the row needed ~320, and the ⋮ button sat 103 px inside the first mix column; the web header ran the device
name under the whole button row (22 findings at 1600 px, 50 on a phone). No test measured geometry — the pictures
showed it, the suite was green. Findings: two items overlap, one sticks out of its card, or (KDE) one is cut off
inside its card — half a number is as unreadable as one under a button. Both checks name items and pixels:
KDE `--gesture layout:overlaps|2` (Main.qml layoutOverlaps), web tests/integration/layout_overlaps.js."""
import json, subprocess, threading, time
from pathlib import Path
import pytest
from test_fx import fixture_stack
from test_service_cli import BIN, make_fake_source
from test_web import Web
from chrome_driver import Chrome

JS = (Path(__file__).parent / "layout_overlaps.js").read_text()
EXTRA_MIXES = ("Recording", "Talkback Bus", "Backup", "Mix 6", "Mix 7", "Mix 8")


@pytest.fixture(scope="module")
def stack():
    pw, s = fixture_stack()
    make_fake_source(s, "fake.mic", "Shure SM7B (RØDECaster Pro II)")
    ui = s.cli("devices", "virtual", "add", "Soundcraft Ui24R", "--in", "32", "--out", "32").stdout.strip()
    s.pw.wait_node(ui)
    for n in ("Music", "Discord", "Talkback", "Guest"): s.cli("channel", "add", n)
    for _ in range(50):
        d = s.cli("devices", "in", check=False).stdout
        if "fake.mic" in d and ui in d: break
        time.sleep(0.1)
    s.cli("channel", "input", "voice", "fake.mic")
    s.cli("channel", "input", "guest", f"{ui}:AUX8>R"); s.cli("channel", "input-add", "guest", f"{ui}:AUX3>L")
    s.cli("channel", "input", "talkback", f"{ui}:AUX7")
    s.cli("channel", "group", "game", "Media"); s.cli("channel", "group", "music", "Media")
    pre = json.loads(subprocess.run([str(BIN / "kmixdeck"), "--json", "fx", "presets"], env=s.env, capture_output=True, text=True).stdout)
    s.cli("fx", "set", "channel", "voice", json.dumps(next(iter(pre.values()))))
    s.cli("duck", "set", "music", "--by", "voice", "--depth", "-18")
    s.cli("duck", "set", "game", "--by", "discord", "--depth", "-9")
    s.cli("mix", "loudness", "stream", "-16")        # UX-18 read-out: it sat under ⋮/mute/master before
    # Real signal. With an idle analyser the read-out shows dashes; the first screenshot after the fix showed
    # "-42.3" digits running out of the card — a layout test on silence measures the wrong widths.
    # The tone is 120 s, the suite runs longer: it is replayed in a loop, or the web half (which runs after the
    # KDE half) measures dashes again — exactly how the first counter-check stayed green on the web side.
    tone, stop = pw.tone(), threading.Event()

    def play():
        while not stop.is_set():
            p = subprocess.Popen(["pw-play", "-P", '{ application.name="Spotify" node.name="spotify" }', str(tone)],
                                 env=pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            while p.poll() is None and not stop.is_set(): time.sleep(0.2)
            if p.poll() is None: p.kill()
            else: time.sleep(0.3); s.cli("app", "move", "Spotify", "music", check=False)
    th = threading.Thread(target=play, daemon=True); th.start()
    for _ in range(80):
        if "Spotify" in s.cli("app", check=False).stdout: break
        time.sleep(0.1)
    s.cli("app", "move", "Spotify", "music", check=False)
    time.sleep(3.0)
    yield s
    stop.set(); th.join(5); s.close(); pw.close()


def set_mixes(stack, n):
    have = [m["Slug"] for m in stack.cli("status", json_out=True)["mixes"]]
    want = ["monitor", "stream"] + [x.lower().replace(" ", "_") for x in EXTRA_MIXES[:max(0, n - 2)]]
    for name in EXTRA_MIXES[:max(0, n - 2)]:
        if name.lower().replace(" ", "_") not in have: stack.cli("mix", "add", name)
    for slug in have:
        if slug not in want: stack.cli("mix", "remove", slug)
    time.sleep(0.8)


def findings(text):
    lines = text.strip().splitlines()
    assert lines and lines[0].isdigit(), f"the layout check did not answer: {text[-400:]}"
    return lines[1:]


@pytest.mark.parametrize("mixes", [2, 5, 8])
def test_kde_mixer_has_no_overlaps(stack, mixes):
    set_mixes(stack, mixes)
    env = dict(stack.env, QT_QPA_PLATFORM="offscreen")
    bad = {}
    for size in ("1152x676", "1280x760", "1920x1080"):
        r = subprocess.run([str(BIN / "kmixdeck-kde"), "--size", size, "--gesture", "layout:overlaps|2"],
                           env=env, capture_output=True, text=True, timeout=90)
        out = r.stdout.split("gesture layout:overlaps|2 -> ", 1)
        assert len(out) == 2, f"{size}: no answer from the window: {r.stderr[-400:]}"
        f = findings(out[1].split("\ngesture ", 1)[0])
        if f: bad[size] = f[:12]
    assert not bad, "KDE window, %d mixes — overlapping or escaping items:\n%s" % (
        mixes, "\n".join(f"  {k}: " + "\n      ".join(v) for k, v in bad.items()))


@pytest.mark.parametrize("mixes", [2, 5, 8])
def test_web_mixer_has_no_overlaps(stack, mixes):
    set_mixes(stack, mixes)
    web = Web(stack, token="")
    bad = {}
    try:
        for w, h in ((1600, 1000), (1280, 800), (412, 915)):
            with Chrome(web.url, size=(w, h)) as ch:
                ch.wait("window.kmixdeck && window.kmixdeck.state.connected && "
                        "document.querySelector('[data-probe=\"channelFx/voice\"]')", 25)
                ch.wait(f"document.querySelectorAll('.mix-header').length === {mixes}", 10)
                # measured in the same JS turn as the check that numbers are showing: between two loops of the tone
                # the analyser drops back to dashes, and a measurement in that gap was green on the old CSS
                has_digits = "/\\d/.test((document.querySelector('[data-probe=\"lufsTP/stream\"]') || {}).textContent || '')"
                ch.wait(has_digits, 15)
                time.sleep(0.8)
                got = None
                for _ in range(40):
                    got = ch.eval("(() => { if (!(" + has_digits + ")) return null; return (\n" + JS.strip().rstrip(';') + "\n) })()")
                    if got is not None: break
                    time.sleep(0.25)
                assert got is not None, f"{w}x{h}: the loudness read-out never showed numbers to measure"
                f = findings(got)
                if f: bad[f"{w}x{h}"] = f[:12]
    finally:
        web.close()
    assert not bad, "web UI, %d mixes — overlapping or escaping items:\n%s" % (
        mixes, "\n".join(f"  {k}: " + "\n      ".join(v) for k, v in bad.items()))
