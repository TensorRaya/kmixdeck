"""AR-12 / ADR 0010 D5: a core-tier feature is done only when EVERY frontend shows it.

One table row per core feature: the change is made through the CLI (rule 1: the API is the feature), then read
back through (a) the CLI itself, (b) the KDE window (`--probe`), (c) the tray popover (`--gesture trayclick:1
--probe`). Adding a core feature = adding a row here; tools/sot-audit.py requires this file for every ✅ core row.
"""
import math, subprocess, time
import pytest
from test_service_cli import BIN, Stack, make_fake_sink  # noqa: F401
from test_presentation import kde, stack  # noqa: F401
from test_ports import wait_level


def tray(stack, *probes):
    return kde(stack, "--gesture", "trayclick:1", *sum((["--probe", p] for p in probes), []))


def window(stack, *probes, open_page=None):
    return kde(stack, *sum((["--probe", p] for p in probes), []), open_page=open_page)


def _cubic(db): return (10 ** (db / 20)) ** (1 / 3)


CORE = [
    # id, setup(stack), cli_check(stack), window_probes → check(dict), tray_probes → check(dict)
    ("MX-6 mix master fader",
     lambda s: s.cli("mix", "volume", "stream", "-6dB"),
     lambda s: abs(next(m for m in s.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Volume"] - 10 ** (-6 / 20)) < 0.01,
     ["mixFader/stream.value"], lambda g: abs(float(g["mixFader/stream.value"]) - _cubic(-6)) < 0.02,
     ["trayMixVolume/stream.value"], lambda g: abs(float(g["trayMixVolume/stream.value"]) - _cubic(-6)) < 0.02),
    ("MX-10 mix mute",
     lambda s: s.cli("mix", "mute", "stream", "on"),
     lambda s: next(m for m in s.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Muted"] is True,
     ["mixHeaderTitle/stream.text"], lambda g: "muted" in g["mixHeaderTitle/stream.text"].lower(),
     ["trayMixMute/stream.checked"], lambda g: g["trayMixMute/stream.checked"] == "true"),
    ("CH-7 channel mute",
     lambda s: s.cli("channel", "mute", "voice", "on"),
     lambda s: next(c for c in s.cli("status", json_out=True)["channels"] if c["Slug"] == "voice")["Muted"] is True,
     ["channelMute/voice.checked"], lambda g: g["channelMute/voice.checked"] == "true",
     ["trayChannelMute/voice.checked"], lambda g: g["trayChannelMute/voice.checked"] == "true"),
    ("CH-7 channel trim",
     lambda s: s.cli("channel", "trim", "game", "-12dB"),
     lambda s: abs(next(c for c in s.cli("status", json_out=True)["channels"] if c["Slug"] == "game")["Trim"] - 10 ** (-12 / 20)) < 0.01,
     ["channelTrim/game.value", "channelTrimText/game.text"], lambda g: abs(float(g["channelTrim/game.value"]) - _cubic(-12)) < 0.02 and g["channelTrimText/game.text"].startswith("-12"),
     ["trayChannelTrim/game.value"], lambda g: abs(float(g["trayChannelTrim/game.value"]) - _cubic(-12)) < 0.02),
    ("UX-2 listening device",
     lambda s: (make_fake_sink(s, "fake.ears", "My Ears"), time.sleep(0.5), s.cli("listen", "fake.ears")),
     lambda s: s.cli("listen").stdout.strip().split()[0] == "fake.ears",
     ["listeningDeviceBox.displayText"], lambda g: "My Ears" in g["listeningDeviceBox.displayText"],
     ["trayListening.text"], lambda g: "My Ears" in g["trayListening.text"]),
    ("DV-11 unplugged device is visible as such",
     lambda s: (make_fake_sink(s, "fake.gone", "Gone Sink"), time.sleep(0.5), s.cli("mix", "output-add", "monitor", "fake.gone"), time.sleep(0.5),
                __import__("test_service_cli").destroy_node(s, "fake.gone"), time.sleep(0.8)),
     lambda s: next(m for m in s.cli("status", json_out=True)["mixes"] if m["Slug"] == "monitor").get("OutputPresent") is False,
     ["mixIcon/monitor.devicePresent"], lambda g: g["mixIcon/monitor.devicePresent"] == "false",
     ["trayMixIcon/monitor.opacity"], lambda g: float(g["trayMixIcon/monitor.opacity"]) < 1.0),
]


@pytest.mark.parametrize("row", CORE, ids=[r[0] for r in CORE])
def test_core_feature_reaches_cli_window_and_tray(stack, row):
    name, setup, cli_ok, wprobes, wcheck, tprobes, tcheck = row
    setup(stack); time.sleep(0.4)
    assert cli_ok(stack), f"{name}: CLI/bus does not show the change (rule 1 broken)"
    g = window(stack, *wprobes)
    assert wcheck(g), f"{name}: KDE window does not show it: {g}"
    g = tray(stack, *tprobes)
    assert tcheck(g), f"{name}: tray does not show it: {g}"


def test_tray_click_opens_overview_double_click_opens_window(stack):
    """UX-17: one click → popover visible; two clicks inside the double-click interval → popover NOT shown (the
    window is raised instead). The popover has the Open-window button as the third way in."""
    g = kde(stack, "--gesture", "trayclick:1", "--probe", "trayOverview.visible", "--probe", "trayOpenWindow.visible")
    assert g["trayOverview.visible"] == "true" and g["trayOpenWindow.visible"] == "true", g
    g = kde(stack, "--gesture", "trayclick:2", "--probe", "trayOverview.visible")
    assert g["trayOverview.visible"] == "false", g


def test_tray_shows_only_what_overview_defines(stack):
    """ADR 0010 D4: the popover's rows are exactly overview()'s mixes/channels — no extra widgets. A mix added via the
    CLI appears in the tray without tray code."""
    stack.cli("mix", "add", "Tray Extra")
    g = tray(stack, "trayMix/tray_extra.visible", "trayMixVolume/tray_extra.value")
    assert g["trayMix/tray_extra.visible"] == "true", g
    stack.cli("mix", "remove", "tray_extra")


def test_ct4_tray_menu_offers_quick_mute_and_mix_switch(stack):
    """CT-4: the tray's context menu has a mute toggle per channel and per mix (checked = muted) and the
    'Listening to' switch with the current mix checked — all driven by the daemon's state, not by the tray."""
    from test_service_cli import make_fake_sink
    make_fake_sink(stack, "fake.tray", "Tray Phones"); time.sleep(0.4)
    stack.cli("mix", "output-add", "monitor", "fake.tray"); stack.cli("listen", "fake.tray")
    stack.cli("channel", "mute", "voice", "on"); stack.cli("mix", "mute", "stream", "on"); time.sleep(0.5)
    r = subprocess.run([str(BIN / "kmixdeck-kde"), "--gesture", "traymenu:1"], env=dict(stack.env, QT_QPA_PLATFORM="offscreen"), capture_output=True, text=True, timeout=40)
    items = [l.split(" ", 1)[1] for l in r.stdout.splitlines() if l.startswith("traymenu ")]
    assert "[x] Voice" in items and "[ ] Game" in items, items
    assert any(i.startswith("[x] ") and "Stream" in i for i in items) and any(i.startswith("[ ] ") and "Monitor" in i for i in items), items
    assert items.count("[x] Monitor") >= 1, f"listening-to entry should show Monitor checked: {items}"   # the Listening-to section
    stack.cli("channel", "mute", "voice", "off"); stack.cli("mix", "mute", "stream", "off"); stack.cli("mix", "output-remove", "monitor", "fake.tray")


def test_ch7_rms_and_clip_are_metered_in_the_daemon_and_shown_everywhere(stack):
    """CH-7: the daemon publishes rms/<key> and clip/<key> next to every peak (one meter loop → every frontend agrees).
    A 1 kHz sine at −20 dBFS: RMS is 3 dB under peak (√2). A sine past full scale lights clip/ on the channel and stays
    lit ≈ 1.5 s after the tone stops. Window and tray meters show the clip cap; the CLI prints '!'."""
    import json as _json
    def ticks(n=8):
        out = []
        p = subprocess.Popen([str(BIN / "kmixdeck"), "--json", "levels"], env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        for _ in range(n): out.append(_json.loads(p.stdout.readline()))
        p.terminate(); p.wait(timeout=5); return out
    stack.cli("channel", "mute", "game", "off"); stack.cli("channel", "trim", "game", "0dB")
    p = stack.pw.play_into("kmixdeck.channel.game")
    try:
        time.sleep(1.2)
        t = ticks()
        pk = max(x.get("channel/game", 0) for x in t); rm = max(x.get("rms/channel/game", 0) for x in t)
        assert pk > 0 and rm > 0, t[-1]
        ratio_db = 20 * math.log10(pk / rm)
        assert 2.0 < ratio_db < 4.0, f"sine: peak/RMS should be ≈ 3 dB, got {ratio_db:.1f} dB (peak {pk:.3f} rms {rm:.3f})"
        assert not any(x.get("clip/channel/game") for x in t), "a −20 dBFS tone must not clip"
    finally:
        p.kill(); p.wait()
    p = stack.pw.play_into("kmixdeck.channel.game", stack.pw.hot_tone())
    try:
        time.sleep(1.2)
        t = ticks()
        assert any(x.get("clip/channel/game") for x in t), "full-scale tone did not light the clip indicator: " + str([k for k in t[-1] if "game" in k])
        # every frontend: window channel header, tray channel row, CLI text
        w = kde(stack, "--probe", "channelMeter/game.clip")
        assert w["channelMeter/game.clip"] == "true", w
        tr = kde(stack, "--gesture", "trayclick:1", "--probe", "trayChannelMeter/game.clip")
        assert tr["trayChannelMeter/game.clip"] == "true", tr
        cli = subprocess.Popen([str(BIN / "kmixdeck"), "levels"], env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        time.sleep(0.6); cli.terminate(); txt = cli.communicate(timeout=5)[0]
        assert "]!" in txt and "=" in txt, txt[-300:]
    finally:
        p.kill(); p.wait()
    time.sleep(2.2)
    assert not any(x.get("clip/channel/game") for x in ticks(4)), "clip indicator must release ≈ 1.5 s after the last hot sample"


def test_ct7_export_import_round_trip_cli_and_window_and_garbage_is_refused(stack, tmp_path):
    """CT-7 backup/restore. Build a setup (channel, mix output, wire, faders, mutes, pan, app assignment), export via
    the CLI, wreck the setup, import via the WINDOW's menu handler — measured to be back. Then a second export must be
    byte-identical in content. Garbage and a foreign JSON are refused and change nothing."""
    import json as _json
    from test_service_cli import make_fake_sink, start_fake_app
    make_fake_sink(stack, "fake.spk", "Speakers")
    stack.cli("channel", "add", "Music"); stack.cli("mix", "output-add", "stream", "fake.spk")
    stack.pw.wait_nodes(["kmixdeck.channel.music", "kmixdeck.link.music.stream", "kmixdeck.link.music.monitor"]); time.sleep(0.5)
    stack.cli("cell", "set", "music", "stream", "-9dB"); stack.cli("cell", "mute", "music", "monitor", "on")
    stack.cli("channel", "trim", "music", "-4dB"); stack.cli("channel", "pan", "music", "-0.5"); stack.cli("mix", "mute", "monitor", "on")
    p, app = start_fake_app(stack); stack.cli("app", "assign", "FakeGame", "music,voice")
    stack.cli("listen", "fake.spk")
    time.sleep(1.2)
    def picture():
        st = stack.cli("status", json_out=True)
        return {
            "channels": sorted(c["Slug"] for c in st["channels"]),
            "trim": {c["Slug"]: round(c["Trim"], 3) for c in st["channels"]},
            "cmute": {c["Slug"]: c["Muted"] for c in st["channels"]},
            "pan": {c["Slug"]: c.get("Pan", 0) for c in st["channels"]},
            "cells": {c["Path"].rsplit("/", 2)[-2] + "/" + c["Path"].rsplit("/", 1)[-1]: (round(c["Volume"], 3), c["Muted"]) for c in st["cells"]},
            "mixes": {m["Slug"]: (sorted(m["Outputs"]), m["Muted"]) for m in st["mixes"]},
            "listen": stack.cli("listen").stdout.strip().split()[0],
            "apps": {a["Name"]: sorted(a["Channels"]) for a in stack.cli("app", "list", json_out=True)},
        }
    try:
        before = picture()
        assert before["cells"]["music/stream"][0] < 0.99 and before["cells"]["music/monitor"][1] is True and before["apps"]["FakeGame"] == ["music", "voice"], before
        # export via CLI
        f = tmp_path / "backup.kmixdeck.json"
        assert stack.cli("export", str(f)).stdout.startswith("exported")
        doc = _json.loads(f.read_text()); assert doc["kmixdeck.export"] == 1 and any(l["node"] == "kmixdeck.link.music.stream" for l in doc["levels"]), list(doc)
        # wreck it
        stack.cli("channel", "remove", "music"); stack.cli("mix", "mute", "monitor", "off"); stack.cli("mix", "output-remove", "stream", "fake.spk")
        stack.cli("channel", "trim", "voice", "-20dB"); stack.cli("listen", "none", check=False)
        time.sleep(0.8)
        wrecked = picture(); assert "music" not in wrecked["channels"] and wrecked["mixes"]["stream"][0] == []
        # garbage first: refused, nothing changes
        bad = tmp_path / "bad.json"; bad.write_text("{ this is not json")
        r = stack.cli("import", str(bad), check=False); assert r.returncode != 0 and "not JSON" in r.stderr, r
        foreign = tmp_path / "foreign.json"; foreign.write_text(_json.dumps({"hello": "world"}))
        r = stack.cli("import", str(foreign), check=False); assert r.returncode != 0 and "not a kmixdeck export" in r.stderr, r
        assert picture() == wrecked, "a refused import must not touch anything"
        # restore via the WINDOW (menu → file dialog → onAccepted handler)
        out = kde(stack, "--gesture", f"import:{f}")
        assert out[0].endswith("-> ok"), out
        for _ in range(60):
            if picture() == before: break
            time.sleep(0.2)
        after = picture()
        stack.pw.wait_nodes(["kmixdeck.channel.music", "kmixdeck.link.music.stream"]); time.sleep(1.0)   # nodes up AND levels applied
        assert after == before, "\n".join(f"{k}: {before[k]} -> {after[k]}" for k in before if before[k] != after[k])
        # measured, not just read: mute the app so only music plays into stream, then the mix sits −9 dB under the channel
        stack.cli("channel", "mute", "voice", "on"); stack.cli("channel", "mute", "game", "on")
        tone = stack.pw.play_into("kmixdeck.channel.music")
        try:
            lvl_ch = wait_level(lambda: stack.pw.level_at("kmixdeck.channel.music"), lambda v: v > -50, tries=12)
            lvl_mx = wait_level(lambda: stack.pw.level_at("kmixdeck.mix.stream"), lambda v: v > -60, tries=6)
            master = next(m for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Volume"]   # cubic
            expect = -9 + 20 * math.log10(master) if master > 0 else -90               # cell fader + whatever master the export carried (Mix.Volume is linear)
            assert abs((lvl_mx - lvl_ch) - expect) < 3, f"channel {lvl_ch:.1f} dB, stream mix {lvl_mx:.1f} dB, expected {expect:.1f} dB apart — the imported levels are not in effect"
            assert stack.pw.level_at("kmixdeck.mix.monitor") < -55, "music/monitor was exported muted — it must be muted after the import too"
        finally:
            tone.kill(); tone.wait(); stack.cli("channel", "mute", "voice", "off"); stack.cli("channel", "mute", "game", "off")
        # export again through the window: same content
        g = tmp_path / "again.kmixdeck.json"
        assert kde(stack, "--gesture", f"export:{g}")[0].endswith("-> ok")
        d1, d2 = _json.loads(f.read_text()), _json.loads(g.read_text())
        d1["levels"] = sorted(d1["levels"], key=lambda x: x["node"]); d2["levels"] = sorted(d2["levels"], key=lambda x: x["node"])
        for l in d1["levels"] + d2["levels"]: l["volume"] = round(l["volume"], 3)
        for d in (d1, d2): d.pop("knownApps", None)   # the measurement's pw-play was seen in between — that is a fact, not drift
        assert d1 == d2, [k for k in d1 if d1[k] != d2.get(k)]
    finally:
        p.kill(); p.wait()
        stack.cli("channel", "remove", "music", check=False); stack.cli("mix", "mute", "monitor", "off", check=False)
        stack.cli("mix", "output-remove", "stream", "fake.spk", check=False); stack.cli("channel", "trim", "voice", "0dB", check=False); stack.cli("listen", "none", check=False)
