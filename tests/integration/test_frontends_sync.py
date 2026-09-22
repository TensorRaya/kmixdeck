# SPDX-FileCopyrightText: 2026 Raya Elena Solano
# SPDX-License-Identifier: GPL-3.0-or-later
"""AR-12 / ADR 0010 D5: a core-tier feature is done only when EVERY frontend shows it.

One table row per core feature: the change is made through the CLI (rule 1: the API is the feature), then read
back through (a) the CLI itself, (b) the KDE window (`--probe`), (c) the tray popover (`--gesture trayclick:1
--probe`). Adding a core feature = adding a row here; tools/sot-audit.py requires this file for every ✅ core row.
"""
import json, math, subprocess, time
from pathlib import Path
import pytest
from test_service_cli import BIN, Stack, make_fake_sink  # noqa: F401
from test_presentation import kde, stack  # noqa: F401
from test_ports import wait_level
from waiting import wait_for


def tray(stack, *probes):
    return kde(stack, "--gesture", "trayclick:1", *sum((["--probe", p] for p in probes), []))


def window(stack, *probes, open_page=None):
    return kde(stack, *sum((["--probe", p] for p in probes), []), open_page=open_page)


def browser(stack, *probes, view=None):
    """The web UI's answer to the same question: 'probe.attr.path' → string, via headless Chrome against the bridge.

    `view` switches the tab first — the counterpart to window()'s open_page. Without it only the default view is
    rendered, and a probe for an element on another tab returns "<not found>" (measured 2026-09-22 on the CT-8 pads,
    which live on the soundboard tab)."""
    from test_web import Web
    from chrome_driver import Chrome, CHROME
    if not CHROME: pytest.skip("no chrome/chromium for the web UI")
    web = Web(stack, token="")
    try:
        ch = Chrome(web.url + (f"#{view}" if view else ""))
        try:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected && document.querySelector('[data-probe]')", 15); time.sleep(0.6)
            if view:
                # The URL hash is read once at startup; switching later happens through a tab CLICK (the app has no
                # hashchange listener, measured 2026-09-22). Click it, and wait for the tab to actually be selected —
                # the tab for an opt-in view is hidden until its state arrives.
                sel = json.dumps(f'[data-view="{view}"]')
                ch.wait(f"!!document.querySelector({sel})", 10)
                ch.eval(f"document.querySelector({sel}).click()", False)
                time.sleep(0.4)
            out = {}
            for p in probes:
                name, attr = p.split(".", 1)
                out[p] = ch.probe(name, attr)
            return out
        finally: ch.close()
    finally: web.close()


def _cubic(db): return (10 ** (db / 20)) ** (1 / 3)


CORE = [
    # id, setup(stack), cli_check(stack), window_probes → check(dict), tray_probes → check(dict)
    ("MX-6 mix master fader",
     lambda s: s.cli("mix", "volume", "stream", "-6dB"),
     lambda s: abs(next(m for m in s.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Volume"] - 10 ** (-6 / 20)) < 0.01,
     ["mixFader/stream.value"], lambda g: abs(float(g["mixFader/stream.value"]) - _cubic(-6)) < 0.02,
     ["trayMixVolume/stream.value"], lambda g: abs(float(g["trayMixVolume/stream.value"]) - _cubic(-6)) < 0.02,
     ["mixFader/stream.dataset.value"], lambda g: abs(float(g["mixFader/stream.dataset.value"]) - 10 ** (-6 / 20)) < 0.02,
     lambda s: s.cli("mix", "volume", "stream", "0dB")),   # undo: a -6 dB master shifts every later level measurement
    ("MX-10 mix mute",
     lambda s: s.cli("mix", "mute", "stream", "on"),
     lambda s: next(m for m in s.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Muted"] is True,
     ["mixHeaderTitle/stream.text"], lambda g: "muted" in g["mixHeaderTitle/stream.text"].lower(),
     ["trayMixMute/stream.checked"], lambda g: g["trayMixMute/stream.checked"] == "true",
     ["mixHeader/stream.className", "mixMute/stream.ariaPressed"], lambda g: "muted" in g["mixHeader/stream.className"] and g["mixMute/stream.ariaPressed"] == "true",
     lambda s: s.cli("mix", "mute", "stream", "off")),   # undo: a muted stream makes every later level measurement -inf
    ("CH-7 channel mute",
     lambda s: s.cli("channel", "mute", "voice", "on"),
     lambda s: next(c for c in s.cli("status", json_out=True)["channels"] if c["Slug"] == "voice")["Muted"] is True,
     ["channelMute/voice.checked"], lambda g: g["channelMute/voice.checked"] == "true",
     ["trayChannelMute/voice.checked"], lambda g: g["trayChannelMute/voice.checked"] == "true",
     ["channelMute/voice.ariaPressed"], lambda g: g["channelMute/voice.ariaPressed"] == "true",
     lambda s: s.cli("channel", "mute", "voice", "off")),   # undo: a muted voice silences later mix measurements
    ("CH-7 channel trim",
     lambda s: s.cli("channel", "trim", "game", "-12dB"),
     lambda s: abs(next(c for c in s.cli("status", json_out=True)["channels"] if c["Slug"] == "game")["Trim"] - 10 ** (-12 / 20)) < 0.01,
     ["channelTrim/game.value", "channelTrimText/game.text"], lambda g: abs(float(g["channelTrim/game.value"]) - _cubic(-12)) < 0.02 and g["channelTrimText/game.text"].startswith("-12"),
     ["trayChannelTrim/game.value"], lambda g: abs(float(g["trayChannelTrim/game.value"]) - _cubic(-12)) < 0.02,
     ["channelHeader/game.dataset.trim"], lambda g: abs(float(g["channelHeader/game.dataset.trim"]) - 10 ** (-12 / 20)) < 0.02,
     lambda s: s.cli("channel", "trim", "game", "0dB")),   # undo: a -12 dB trim shifts later level measurements
    ("UX-2 listening device",
     lambda s: (make_fake_sink(s, "fake.ears", "My Ears"), time.sleep(0.5), s.cli("listen", "fake.ears")),
     lambda s: s.cli("listen").stdout.strip().split()[0] == "fake.ears",
     ["listeningDeviceBox.displayText"], lambda g: "My Ears" in g["listeningDeviceBox.displayText"],
     ["trayListening.text"], lambda g: "My Ears" in g["trayListening.text"],
     ["hearLabel.textContent"], lambda g: "My Ears" in g["hearLabel.textContent"]),
    ("MX-5 colour code",
     lambda s: (s.cli("mix", "color", "stream", "#3daee9"), s.cli("channel", "color", "voice", "#E93D58")),
     lambda s: next(m for m in s.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Color"] == "#3daee9"
               and next(c for c in s.cli("status", json_out=True)["channels"] if c["Slug"] == "voice")["Color"] == "#e93d58",
     ["mixColorStripe/stream.color", "mixColorStripe/stream.visible", "channelColorStripe/voice.color"],
     lambda g: g["mixColorStripe/stream.color"] == "#3daee9" and g["mixColorStripe/stream.visible"] == "true" and g["channelColorStripe/voice.color"] == "#e93d58",
     ["trayMixColor/stream.color", "trayChannelColor/voice.color", "trayMixColor/monitor.visible"],
     lambda g: g["trayMixColor/stream.color"] == "#3daee9" and g["trayChannelColor/voice.color"] == "#e93d58" and g["trayMixColor/monitor.visible"] == "false",
     ["mixColorStripe/stream.computed.background-color", "channelColorStripe/voice.computed.background-color"],
     lambda g: g["mixColorStripe/stream.computed.background-color"] == "rgb(61, 174, 233)" and g["channelColorStripe/voice.computed.background-color"] == "rgb(233, 61, 88)"),
    ("CT-9 a saved scene is recallable in every frontend",
     # Rule 1: the change goes in through the CLI. Then each frontend must OFFER the scene — that is what
     # "faders move visibly in every frontend" means for a snapshot feature. The opt-in rule is part of the
     # assertion: before a scene exists the recall control is absent, so saving one is what makes it appear.
     lambda s: (s.cli("cell", "set", "game", "stream", "0.5"), s.cli("scene", "save", "Stream")),
     lambda s: s.cli("scene", "list", json_out=True) == ["Stream"],
     ["recallSceneAction.visible", "sceneAction.Stream.text", "saveSceneAction.visible"],
     lambda g: g["recallSceneAction.visible"] == "true" and g["sceneAction.Stream.text"] == "Stream" and g["saveSceneAction.visible"] == "true",
     # The tray's scene MENU is a closed QQC2.Menu — its items do not exist in the item tree until it pops
     # up, and the probe walker only sees the popover's contentItem. So the tray proof is the control that
     # IS visible: the button appears (it is hidden while Scenes is empty) and names the scene it offers.
     ["traySceneRecall.visible", "traySceneRecall.text"],
     lambda g: g["traySceneRecall.visible"] == "true" and "Scene" in g["traySceneRecall.text"],
     ["scenePick.dataset.count", "scenePick.hidden", "sceneSave.hidden"],
     lambda g: g["scenePick.dataset.count"] == "1" and g["scenePick.hidden"] == "false" and g["sceneSave.hidden"] == "false"),
    ("CT-8 a sample is playable in every frontend",
     # Rule 1: the change goes in through the CLI. The opt-in rule is part of the assertion — every surface hides
     # its soundboard control until a board with samples exists, so adding one is what makes them appear.
     # The stack fixture is module-scoped, so another test in this file may already have created the board and the
     # sample — `check=False` makes the setup idempotent instead of failing on rc=4 "already exists".
     lambda s: (s.cli("channel", "add", "--soundboard", "Board", check=False),
                s.cli("sample", "add", "board", str(_sample_wav(s)), "--name", "jingle", check=False),
                s.cli("sample", "play", "jingle"), time.sleep(1.0)),
     lambda s: s.cli("sample", "list", "board", json_out=True)[0]["sounding"] is True,
     # The window proof is the WAY IN: the action a user without a shell can click. It was missing entirely —
     # nothing called soundboardPanelOpen() except main.cpp, so the panel existed but was unreachable from the
     # running window (measured 2026-09-22). The panel's own contents are proven in
     # test_soundboard.py::test_ct8_all_three_frontends_expose_the_soundboard, which pushes it via --open.
     ["soundboardAction.visible", "soundboardAction.text"],
     lambda g: g["soundboardAction.visible"] == "true" and "Soundboard" in g["soundboardAction.text"],
     ["traySampleFire.visible", "traySampleFire.text"],
     lambda g: g["traySampleFire.visible"] == "true" and "Sample" in g["traySampleFire.text"],
     ["view:soundboard", "samplePad:board:jingle.className", "board:board.hidden"],
     lambda g: "sounding" in g["samplePad:board:jingle.className"] and g["board:board.hidden"] == "false",
     # undo: the sample is a 120 s tone and the board is a real channel feeding every mix at 1.0 — left running it
     # adds roughly +5 dB to any later level measurement (measured 2026-09-22: it broke test_ct7_export_import…,
     # which expected the music channel −9 dB under the stream mix and read it 4.6 dB ABOVE).
     lambda s: (s.cli("sample", "stop", "jingle", check=False), s.cli("channel", "remove", "board", check=False))),
    ("DV-11 unplugged device is visible as such",
     lambda s: (make_fake_sink(s, "fake.gone", "Gone Sink"), time.sleep(0.5), s.cli("mix", "output-add", "monitor", "fake.gone"), time.sleep(0.5),
                __import__("test_service_cli").destroy_node(s, "fake.gone"), time.sleep(0.8)),
     lambda s: next(m for m in s.cli("status", json_out=True)["mixes"] if m["Slug"] == "monitor").get("OutputPresent") is False,
     ["mixIcon/monitor.devicePresent"], lambda g: g["mixIcon/monitor.devicePresent"] == "false",
     ["trayMixIcon/monitor.opacity"], lambda g: float(g["trayMixIcon/monitor.opacity"]) < 1.0,
     ["mixOutput/monitor.textContent"], lambda g: "⚠" in g["mixOutput/monitor.textContent"]),
]


@pytest.mark.parametrize("row", CORE, ids=[r[0] for r in CORE])
def test_core_feature_reaches_cli_window_and_tray(stack, row):
    name, setup, cli_ok, wprobes, wcheck, tprobes, tcheck, bprobes, bcheck = row[:9]
    setup(stack); time.sleep(0.4)
    try:
        assert cli_ok(stack), f"{name}: CLI/bus does not show the change (rule 1 broken)"
        g = window(stack, *wprobes)
        assert wcheck(g), f"{name}: KDE window does not show it: {g}"
        g = tray(stack, *tprobes)
        assert tcheck(g), f"{name}: tray does not show it: {g}"
        # A probe may live on a tab that is not the default one (CT-8's pads sit on the Soundboard tab). Rather than
        # widening every row by a field only one of them needs, the row names the view by prefixing a probe with
        # "view:<name>" — the browser helper is told to switch there first.
        ansicht = next((p.split(":", 1)[1] for p in bprobes if p.startswith("view:")), None)
        g = browser(stack, *[p for p in bprobes if not p.startswith("view:")], view=ansicht)
        assert bcheck(g), f"{name}: web UI does not show it (AR-8/AR-9): {g}"
    finally:
        # The `stack` fixture is MODULE-scoped: whatever a row switches on stays on for every test after it.
        # Measured 2026-09-22: "MX-10 mix mute" left `stream` muted, so test_ct7_export_import… exported a muted
        # mix and then measured -inf dB where it expected -33 dB — red since at least 2026-09-21 and blamed on
        # CT-7, which is green on its own. A row that changes state names its own undo as a 10th field.
        if len(row) > 9 and row[9] is not None:
            row[9](stack); time.sleep(0.3)


def test_ct9_recall_from_the_window_and_the_tray_actually_moves_the_faders(stack):
    """The CORE row above proves every frontend OFFERS the scene. This proves the offer does something:
    a recall triggered through the window's own gesture path must land on the bus and move the cell back.
    Listing a scene you cannot recall would pass a probe check and still be useless."""
    stack.cli("cell", "set", "game", "stream", "0.25")
    stack.cli("scene", "save", "Quiet")
    stack.cli("cell", "set", "game", "stream", "1.0")

    kde(stack, "--gesture", "recallScene:Quiet")
    wait_for(lambda: abs(stack.cli("cell", "get", "game", "stream", json_out=True)["Volume"] - 0.25) < 0.01,
             timeout=10.0, what="cell back at the scene value after a window-driven recall")

    # and the save gesture is the other direction: the window writes a scene the CLI can see
    stack.cli("cell", "set", "game", "stream", "0.75")
    kde(stack, "--gesture", "saveScene:FromWindow")
    wait_for(lambda: "FromWindow" in stack.cli("scene", "list", json_out=True),
             timeout=10.0, what="scene saved through the window")


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
        wait_for(lambda: picture() == before, timeout=12.0, what="picture() == before")
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
            if abs((lvl_mx - lvl_ch) - expect) >= 3:   # name WHAT is feeding the mix, not just the number: a stray unmuted channel or a leftover tone is the usual cause
                _st = stack.cli("status", json_out=True)
                _ch = [(c["Slug"], c["Muted"], round(c["Trim"], 3)) for c in _st["channels"]]
                _ce = [(c["Path"].rsplit("/", 2)[-2], round(c["Volume"], 3), c["Muted"]) for c in _st["cells"] if c["Path"].endswith("/stream")]
                raise AssertionError(f"channel {lvl_ch:.1f} dB, stream mix {lvl_mx:.1f} dB, expected {expect:.1f} dB apart — the imported levels are not in effect\n"
                                     f"  master={master:.4f} channels={_ch}\n  cells in stream={_ce}")
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


def test_mx5_eight_mixes_all_faders_visible_and_bad_colour_refused(stack):
    """MX-5: eight mixes at once — every master fader and every cell fader of a channel is on screen (no hidden
    faders); the window's own scroll area may scroll, but each fader item has a size and is on the page. A colour
    that is not #rrggbb is refused by the daemon, the previous colour stays."""
    slugs = []
    try:
        for i in range(6):   # + monitor + stream = 8
            stack.cli("mix", "add", f"Mix {i + 3}")
        mixes = [m["Slug"] for m in stack.cli("status", json_out=True)["mixes"]]
        slugs = [m for m in mixes if m not in ("monitor", "stream")]
        stack.pw.wait_nodes([f"kmixdeck.mix.{sl}" for sl in slugs]); time.sleep(0.8)
        assert len(mixes) >= 8, mixes
        probes = []
        for m in mixes: probes += [f"mixFader/{m}.visible", f"mixFader/{m}.width", f"cell/voice/{m}.visible", f"cell/voice/{m}.height"]
        g = kde(stack, *sum((["--probe", p] for p in probes), []))
        hidden = [p for p in probes if p.endswith(".visible") and g.get(p) != "true"]
        flat = [p for p in probes if (p.endswith(".width") or p.endswith(".height")) and float(g.get(p, "0") or 0) < 8]
        assert not hidden and not flat, f"hidden: {hidden} flat: {flat}"
        # MX-5: columns share the width instead of scrolling. At 1280 px
        # six mixes fit without a scrollbar (headers fold to two rows), at 1600 px all eight do; the folded header keeps
        # every control (mute, master, listen, ⋮) and the master fader stays wide enough to grab.
        for sl in slugs[-2:]: stack.cli("mix", "remove", sl)
        six = [m for m in mixes if m not in slugs[-2:]]; time.sleep(0.8)
        g6 = kde(stack, "--size", "1280x760", "--probe", "mixStrip.needed", "--probe", "mixStrip.availableWidth", "--probe", "mixHeader/monitor.narrow",
                 "--probe", "mixFader/monitor.width", "--probe", "mixMute/monitor.visible", "--probe", "mixHeader/monitor.height", "--probe", "channelHeader/game.width")
        assert int(g6["mixStrip.needed"]) <= int(g6["mixStrip.availableWidth"]), f"six mixes must fit at 1280 px without scrolling: {g6}"
        assert g6["mixHeader/monitor.narrow"] == "true" and float(g6["mixFader/monitor.width"]) >= 60, g6
        assert g6["mixMute/monitor.visible"] == "true"
        g8 = kde(stack, "--size", "1600x760", "--probe", "mixStrip.needed", "--probe", "mixStrip.availableWidth") if len(six) == 6 else None
        for i in range(2): stack.cli("mix", "add", f"Mix {i + 7}")
        slugs = [m["Slug"] for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] not in ("monitor", "stream")]
        stack.pw.wait_nodes([f"kmixdeck.mix.{sl}" for sl in slugs]); time.sleep(0.8)
        g8 = kde(stack, "--size", "1600x760", "--probe", "mixStrip.needed", "--probe", "mixStrip.availableWidth")
        assert int(g8["mixStrip.needed"]) <= int(g8["mixStrip.availableWidth"]), f"eight mixes must fit at 1600 px: {g8}"
        for sl in slugs: stack.cli("mix", "remove", sl)
        slugs = []; time.sleep(0.8)
        gw = kde(stack, "--size", "1280x760", "--probe", "mixHeader/monitor.narrow", "--probe", "mixHeader/monitor.width")
        assert gw["mixHeader/monitor.narrow"] == "false", f"two mixes: the header is back to one row: {gw}"
        # bad colour
        stack.cli("mix", "color", "stream", "#3daee9")
        r = stack.cli("mix", "color", "stream", "blue", check=False); assert r.returncode != 0, r
        r = stack.cli("mix", "color", "stream", "#12345", check=False); assert r.returncode != 0, r
        assert next(m for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Color"] == "#3daee9"
        stack.cli("mix", "color", "stream", "none")
        assert next(m for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Color"] == ""
    finally:
        for sl in slugs: stack.cli("mix", "remove", sl, check=False)
        stack.cli("mix", "color", "stream", "none", check=False); stack.cli("channel", "color", "voice", "none", check=False)


def test_mx8_duplicate_mix_copies_levels_fx_colour_not_outputs_from_cli_and_window(stack):
    """MX-8: a duplicated mix starts with the source's icon, colour, FX chain, master level/mute and every cell
    fader/mute — measured on the copy — but with NO outputs (two mixes on one device would double the audio).
    Once via the CLI, once via the window's menu handler; the copy's slug comes from the new name."""
    from test_service_cli import make_fake_sink
    make_fake_sink(stack, "fake.dup.out", "Dup Out")
    stack.cli("mix", "output-add", "stream", "fake.dup.out"); stack.cli("mix", "color", "stream", "#ef973c"); stack.cli("mix", "icon", "stream", "camera-web")
    stack.cli("mix", "volume", "stream", "-6dB"); stack.cli("cell", "set", "game", "stream", "-12dB"); stack.cli("cell", "mute", "system", "stream", "on")
    presets = stack.cli("fx", "presets", json_out=True)
    preset = presets[0] if isinstance(presets, list) else next(iter(presets.values()))
    stack.cli("fx", "set", "mix", "stream", json.dumps(preset.get("chain", preset)), check=False)
    time.sleep(0.8)
    src_fx = stack.cli("fx", "get", "mix", "stream", json_out=True)
    created = []
    try:
        for how in ("cli", "window"):
            name = "Stream copy" if how == "cli" else "Stream two"
            if how == "cli":
                slug = stack.cli("mix", "duplicate", "stream", name).stdout.strip()
            else:
                assert kde(stack, "--gesture", f"duplicate:stream|{name}")[0].endswith("-> ok")
                slug = next(m["Slug"] for m in stack.cli("status", json_out=True)["mixes"] if m["Name"] == name)
            created.append(slug)
            stack.pw.wait_nodes([f"kmixdeck.mix.{slug}", f"kmixdeck.link.game.{slug}", f"kmixdeck.link.system.{slug}"])
            for _ in range(50):   # levels are applied as the nodes come up
                st = stack.cli("status", json_out=True)
                if abs(next(x for x in st["mixes"] if x["Slug"] == slug)["Volume"] - 10 ** (-6 / 20)) < 0.01: break
                time.sleep(0.1)
            m = next(x for x in st["mixes"] if x["Slug"] == slug); src = next(x for x in st["mixes"] if x["Slug"] == "stream")
            assert m["Name"] == name and m["Icon"] == "camera-web" and m["Color"] == "#ef973c", m
            assert m["Outputs"] == [] and src["Outputs"] == ["fake.dup.out"], "outputs must not be copied"
            assert stack.cli("fx", "get", "mix", slug, json_out=True) == src_fx, "FX chain must be copied"
            assert abs(m["Volume"] - 10 ** (-6 / 20)) < 0.01, m["Volume"]
            cells = {c["Path"].rsplit("/", 2)[-2]: (round(c["Volume"], 3), c["Muted"]) for c in st["cells"] if c["Path"].endswith("/" + slug)}
            assert cells["game"][0] < 0.99 and cells["system"][1] is True, cells
            # measured on the copy: game plays −12 dB (cell) −6 dB (master) under the channel; system is silent
            stack.cli("channel", "mute", "voice", "on")
            tone = stack.pw.play_into("kmixdeck.channel.game")
            try:
                ch = wait_level(lambda: stack.pw.level_at("kmixdeck.channel.game"), lambda v: v > -50, tries=12)
                mx = wait_level(lambda s=slug: stack.pw.level_at(f"kmixdeck.mix.{s}"), lambda v: v > -60, tries=6)
                # the mix sink's monitor is post master: −12 dB (cell) −6 dB (master) under the channel
                assert abs((mx - ch) - (-18)) < 3, f"copy {slug}: channel {ch:.1f}, mix {mx:.1f} (expected −18 dB apart)"
            finally:
                tone.kill(); tone.wait(); stack.cli("channel", "mute", "voice", "off")
            # the window shows the copy with the same colour stripe as the source
            g = kde(stack, "--probe", f"mixColorStripe/{slug}.color", "--probe", f"mixHeaderTitle/{slug}.text")
            assert g[f"mixColorStripe/{slug}.color"] == "#ef973c" and name in g[f"mixHeaderTitle/{slug}.text"], g
        r = stack.cli("mix", "duplicate", "nope", "x", check=False); assert r.returncode != 0
        r = stack.cli("mix", "duplicate", "stream", "Stream copy", check=False); assert r.returncode != 0 and "exists" in r.stderr, r
    finally:
        for sl in created: stack.cli("mix", "remove", sl, check=False)
        stack.cli("mix", "output-remove", "stream", "fake.dup.out", check=False); stack.cli("mix", "color", "stream", "none", check=False)
        stack.cli("mix", "icon", "stream", "none", check=False); stack.cli("mix", "volume", "stream", "0dB", check=False)
        stack.cli("cell", "set", "game", "stream", "0dB", check=False); stack.cli("cell", "mute", "system", "stream", "off", check=False)
        stack.cli("fx", "clear", "mix", "stream", check=False)


def test_ch11_hidden_device_leaves_every_picker_stays_routable_and_comes_back(stack):
    """CH-11: hide a device → gone from the listening-device box, the mix Outputs menu, the channel Inputs menu, the
    AddDialog and the patchbay card list; still in `devices` (marked) and still routable by name; a hidden device that
    is IN USE stays visible where it is used. Unhide from the window's dialog handler → back everywhere."""
    from test_service_cli import make_fake_sink, make_fake_source
    make_fake_sink(stack, "fake.hide.out", "Hideable Out"); make_fake_source(stack, "fake.hide.in", "Hideable In"); time.sleep(0.8)
    def picker_state():
        g = kde(stack, "--probe", "listeningDeviceBox.count", "--probe", "hearingBar.deviceNames", "--probe", "mixHeader/stream.outputDeviceNames",
                "--probe", "channelHeader/voice.inputDeviceNames", "--probe", "patchbay.cardIds", open_page="patchbay")
        return g
    try:
        before = picker_state()
        assert "Hideable Out" in before["hearingBar.deviceNames"] and "Hideable Out" in before["mixHeader/stream.outputDeviceNames"]
        assert "Hideable In" in before["channelHeader/voice.inputDeviceNames"] and "dev/fake.hide.in" in before["patchbay.cardIds"] and "outdev/fake.hide.out" in before["patchbay.cardIds"]
        # hide both via CLI
        stack.cli("devices", "hide", "fake.hide.out"); stack.cli("devices", "hide", "fake.hide.in")
        assert set(stack.cli("devices", "hidden").stdout.split()) == {"fake.hide.out", "fake.hide.in"}
        assert "(hidden)" in [l for l in stack.cli("devices").stdout.splitlines() if "fake.hide.out" in l][0]
        assert "fake.hide.out" in stack.cli("devices", json_out=True), "hidden ≠ removed: still a device"
        hidden = picker_state()
        for k in ("hearingBar.deviceNames", "mixHeader/stream.outputDeviceNames"): assert "Hideable Out" not in hidden[k], (k, hidden[k])
        assert "Hideable In" not in hidden["channelHeader/voice.inputDeviceNames"]
        assert "dev/fake.hide.in" not in hidden["patchbay.cardIds"] and "outdev/fake.hide.out" not in hidden["patchbay.cardIds"]
        assert int(hidden["listeningDeviceBox.count"]) == int(before["listeningDeviceBox.count"]) - 1
        # still routable by name, and once in use it is visible where it is used
        stack.cli("mix", "output-add", "stream", "fake.hide.out")
        assert "fake.hide.out" in next(m for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Outputs"]
        tone = stack.pw.play_into("kmixdeck.channel.game")
        try:
            assert wait_level(lambda: stack.pw.level_at("fake.hide.out"), lambda v: v > -50, tries=12) > -50, "a hidden device must still carry audio"
        finally:
            tone.kill(); tone.wait()
        inuse = picker_state()
        assert "Hideable Out" in inuse["mixHeader/stream.outputDeviceNames"] and "outdev/fake.hide.out" in inuse["patchbay.cardIds"], "in use → visible where used"
        stack.cli("mix", "output-remove", "stream", "fake.hide.out")
        # survives a daemon restart
        stack.restart_daemon(); time.sleep(1.5)
        assert set(stack.cli("devices", "hidden").stdout.split()) == {"fake.hide.out", "fake.hide.in"}
        # unhide through the window (the dialog's "Show again" handler) and via the tray's hidden-count
        assert kde(stack, "--gesture", "hide:fake.hide.out|0")[0].endswith("-> ok")
        stack.cli("devices", "unhide", "fake.hide.in")
        wait_for(lambda: not stack.cli("devices", "hidden").stdout.strip(), timeout=3.0, what="not stack.cli('devices', 'hidden').stdout.strip()")
        after = picker_state()
        assert "Hideable Out" in after["hearingBar.deviceNames"] and "Hideable In" in after["channelHeader/voice.inputDeviceNames"]
        assert "dev/fake.hide.in" in after["patchbay.cardIds"] and "outdev/fake.hide.out" in after["patchbay.cardIds"]
        r = stack.cli("devices", "hide", "kmixdeck.channel.game", check=False); assert r.returncode != 0, "our own nodes are not devices"
    finally:
        stack.cli("devices", "unhide", "fake.hide.out", check=False); stack.cli("devices", "unhide", "fake.hide.in", check=False)
        stack.cli("mix", "output-remove", "stream", "fake.hide.out", check=False)


def test_ux5_german_catalog_covers_every_string_and_window_speaks_it(stack):
    """UX-5: English is the source language, German is the second — via KDE's i18n (KI18n): po/kmixdeck.pot is
    extracted from every i18n*() call, po/de/kmixdeck.po translates all of them (no fuzzy, no empty), the catalog is
    built and installed by `ki18n_install(po)`, and the window shows German text when the user's language is German.
    The CLI stays untranslated on purpose (it is scripting surface; its output is parsed)."""
    import re
    root = Path(__file__).resolve().parents[2]
    pot = (root / "po/kmixdeck.pot").read_text(); po = (root / "po/de/kmixdeck.po").read_text()
    # extract fresh and compare: a string added to the QML without running tools/extract-messages.sh is a red test
    fresh = subprocess.run(["sh", str(root / "tools/extract-messages.sh")], capture_output=True, text=True, cwd=root)
    assert fresh.returncode == 0, fresh.stderr
    assert (root / "po/kmixdeck.pot").read_text().count("\nmsgid ") == pot.count("\nmsgid "), "po/kmixdeck.pot is stale — run tools/extract-messages.sh and translate the new strings"
    stats = subprocess.run(["msgfmt", "--check", "--statistics", "-o", "/dev/null", str(root / "po/de/kmixdeck.po")], capture_output=True, text=True)
    assert stats.returncode == 0, stats.stderr
    assert "untranslated" not in stats.stderr and "fuzzy" not in stats.stderr, f"German catalog incomplete: {stats.stderr.strip()}"
    assert (BIN.parent / "locale/de/LC_MESSAGES/kmixdeck.mo").exists(), "ki18n_install(po) did not build the catalog"
    # every %1-style placeholder of the source survives in the translation (a dropped %1 is a broken UI string)
    for m in re.finditer(r'msgid "(.*)"\nmsgstr "(.+)"', po):
        src, dst = m.group(1), m.group(2)
        assert set(re.findall(r"%\d", src)) == set(re.findall(r"%\d", dst)), f"placeholder mismatch: {src!r} → {dst!r}"
    en = kde(stack, "--probe", "hearingBar.hearLabelText", "--probe", "channelHeader/voice.inputDeviceNames", open_page="mixer", lang="en")
    de = kde(stack, "--probe", "hearingBar.hearLabelText", open_page="mixer", lang="de")
    assert en["hearingBar.hearLabelText"] == "I hear:", en
    assert de["hearingBar.hearLabelText"] == "Ich höre:", f"window did not switch to German: {de}"


def test_ux3_first_run_wizard_wires_defaults_from_cli_and_window():
    """UX-3: the daemon owns the first-run logic (FirstRunPlan/FirstRunApply). Fresh config dir → FirstRun is true;
    the plan names the session's default sink/source (WirePlumber's "default" metadata) and where each running app
    would go (by media.role); Apply routes Monitor → default sink, listens there, Voice ← default mic, apps → channels;
    CLI `kmixdeck setup` prints the same plan, the window's dialog opens on first run and reads the same fields."""
    from test_service_cli import make_fake_sink, make_fake_source, start_fake_app
    from pw_sandbox import start_private_pipewire
    # own stack: the module-wide one has a layout.json written by every test before this one → FirstRun is false there
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream"); stack = Stack(pw)
    try:
        _ux3_body(stack, prop_of(stack), make_fake_sink, make_fake_source, start_fake_app)
    finally:
        stack.close(); pw.close()


def _sample_wav(stack):
    """The sandbox's own 120 s tone — long enough to still be sounding on the LAST surface.

    Measured 2026-09-22: probing the window takes 6.1 s, the tray 6.3 s and the browser 3.4 s, 15.7 s in total,
    because each one starts its own process. A 12 s file was silent by the time Chrome rendered the pad, and the
    row then failed on the web stage only — which reads like a web bug and is not one.
    """
    return stack.pw.tone()



def prop_of(stack):
    return lambda name: stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", name).stdout.strip()


def _ux3_body(stack, prop, make_fake_sink, make_fake_source, start_fake_app):
    assert prop("FirstRun") == "b true", "a sandbox has no layout.json → the daemon must report FirstRun"
    make_fake_sink(stack, "fake.desk", "Desk Speakers"); make_fake_source(stack, "fake.usbmic", "USB Microphone"); time.sleep(0.8)
    subprocess.run(["pw-metadata", "0", "default.audio.sink", '{"name":"fake.desk"}', "Spa:String:JSON"], env=stack.env, capture_output=True)
    subprocess.run(["pw-metadata", "0", "default.audio.source", '{"name":"fake.usbmic"}', "Spa:String:JSON"], env=stack.env, capture_output=True)
    for _ in range(50):
        plan = stack.cli("setup", json_out=True)
        if plan.get("defaultSink") == "fake.desk" and plan.get("defaultSource") == "fake.usbmic": break
        time.sleep(0.1)
    assert plan["defaultSink"] == "fake.desk" and plan["sinkKnown"] is True and plan["sinkDescription"] == "Desk Speakers", plan
    assert plan["defaultSource"] == "fake.usbmic" and plan["sourceKnown"] is True, plan
    p, app = start_fake_app(stack)
    try:
        for _ in range(50):
            plan = stack.cli("setup", json_out=True)
            if any(a["name"] == "FakeGame" for a in plan["apps"]): break
            time.sleep(0.1)
        # CH-4 auto-route may already have parked the new stream on the default channel — the plan must say so
        # (assigned=True, channel=where it is) instead of proposing to move it. Unassigned → by role → game.
        fg = next(a for a in plan["apps"] if a["name"] == "FakeGame")
        if fg["assigned"]:
            assert fg["channel"] in stack.cli("app", "list", json_out=True)[0]["Channels"] or fg["channel"] in ("system", "game", "voice"), fg
        else:
            assert fg["channel"] == "game", fg
        human = stack.cli("setup").stdout
        assert "first run" in human and "fake.desk" in human and "Desk Speakers" in human and "FakeGame" in human, human
        assert ("-> game" in human) or ("already on" in human), human
        # the window opens the wizard by itself on first run and shows the very same plan
        g = kde(stack, "--probe", "firstRunBody.dialogVisible", "--probe", "firstRunBody.summary", "--probe", "firstRunSink.text", "--probe", "firstRunSource.text", "--probe", "firstRunApps.text", open_page="mixer")
        assert g["firstRunBody.dialogVisible"] == "true", g
        assert g["firstRunBody.summary"].startswith("plan:fake.desk|fake.usbmic|apps=1"), g
        assert "Desk Speakers" in g["firstRunSink.text"] and "USB Microphone" in g["firstRunSource.text"] and "FakeGame" in g["firstRunApps.text"], g
        # apply through the window's handler (what the "Set up" button calls)
        r = kde(stack, "--gesture", "firstrun:apply", "--probe", "firstRunBody.summary", open_page="mixer")
        assert r["firstRunBody.summary"].startswith("done:") and "monitorOutput" in r["firstRunBody.summary"] and "voiceInput" in r["firstRunBody.summary"], r
        wait_for(lambda: prop("FirstRun") == "b false" and prop("ListeningDevice") == 's "fake.desk"', timeout=5.0, what="prop('FirstRun') == 'b false' and prop('ListeningDevice') == 's 'fake.")
        assert prop("FirstRun") == "b false"
        st = stack.cli("status", json_out=True)
        assert stack.cli("listen").stdout.strip().startswith("fake.desk")
        assert next(m for m in st["mixes"] if m["Slug"] == "monitor")["Outputs"] == ["fake.desk"]
        assert stack.cli("channel", "inputs", "voice", json_out=True) in (["fake.usbmic"], ["fake.usbmic:FL"], ["fake.usbmic:FR"]), "the default mic feeds Voice"
        assert next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"], "the running app must be in some channel after setup"
        # and the tone really reaches the desk speakers via Monitor
        assert wait_level(lambda: stack.pw.level_at("fake.desk"), lambda v: v > -50, tries=12) > -50
        # second run: no wizard (layout exists), setup is idempotent (fills gaps only, changes nothing here)
        again = stack.cli("setup", "--apply", json_out=True)
        assert again.get("monitorOutput") is None and again.get("voiceInput") is None and again.get("apps") == [], again
        g2 = kde(stack, "--probe", "firstRunBody.dialogVisible", "--probe", "hearingBar.deviceNames", open_page="mixer")
        assert g2["firstRunBody.dialogVisible"] in ("false", "<not found: firstRunBody>"), "the wizard must not come back once a layout exists"
        # no default sink → Apply refuses with a reason instead of doing half a job
        subprocess.run(["pw-metadata", "0", "default.audio.sink", '{"name":"does.not.exist"}', "Spa:String:JSON"], env=stack.env, capture_output=True); time.sleep(0.5)
        rej = stack.cli("setup", "--apply", check=False)
        assert rej.returncode != 0 and "default output" in rej.stderr, rej
    finally:
        p.kill(); p.wait()
        subprocess.run(["pw-metadata", "0", "default.audio.sink", '{"name":"fake.desk"}', "Spa:String:JSON"], env=stack.env, capture_output=True)


def test_ch8_channel_groups_move_together_in_cli_window_and_tray(stack):
    tray_probe = globals()["tray"]
    """CH-8: channels sharing a group move together — trim as one dB delta for every member (their balance survives),
    mute mirrored; leaving the group stops it. Set from CLI, seen in window (badge on each member) and tray (⛓);
    a fader move in the WINDOW on one member moves the other in the daemon; group names persist over a restart."""
    import math
    db = lambda v: -99.0 if v <= 0 else 20 * math.log10(v)
    def trims(): return {c["Slug"]: (round(db(c["Trim"]), 1), c["Muted"]) for c in stack.cli("channel", "list", json_out=True)}
    for c in ("game", "system", "voice"): stack.cli("channel", "group", c, "none"); stack.cli("channel", "mute", c, "off")
    stack.cli("channel", "trim", "game", "-6dB"); stack.cli("channel", "trim", "system", "-12dB"); stack.cli("channel", "trim", "voice", "0dB"); time.sleep(0.5)
    # --- CLI: group two of three, move one → the other follows by the same delta, the third stays
    stack.cli("channel", "group", "game", "Media"); stack.cli("channel", "group", "system", "Media")
    assert stack.cli("channel", "groups", json_out=True) == {"Media": ["game", "system"]}
    stack.cli("channel", "trim", "game", "-9dB"); time.sleep(0.5)
    t = trims(); assert t["game"][0] == -9.0 and abs(t["system"][0] + 15.0) < 0.3 and t["voice"][0] == 0.0, t
    stack.cli("channel", "mute", "system", "on"); time.sleep(0.4)
    t = trims(); assert t["game"][1] is True and t["system"][1] is True and t["voice"][1] is False, "mute must mirror inside the group only"
    stack.cli("channel", "mute", "system", "off"); time.sleep(0.3)
    # --- window: badge on both members, none on voice; the group menu gesture joins voice; the badge appears
    g = kde(stack, "--probe", "channelGroupBadge.visible", "--gesture", "group:voice|Media", "--probe", "channelGroupBadge.groupName", open_page="mixer")
    assert g["channelGroupBadge.visible"] == "true", g
    assert g["channelGroupBadge.groupName"] == "Media"
    wait_for(lambda: stack.cli("channel", "group", "voice").stdout.strip() == "Media", timeout=3.0, what="stack.cli('channel', 'group', 'voice').stdout.strip() == 'Media'")
    assert stack.cli("channel", "groups", json_out=True) == {"Media": ["game", "system", "voice"]}
    # --- a fader move in the window on ONE member moves the others in the daemon (trim dial of game: −9 → −3 = +6 dB)
    before = trims()
    r = kde(stack, "--gesture", "trim:game|" + str(round(_cubic(-3), 4)), "--probe", "channelTrimText/game.text", open_page="mixer")   # dial is cubic (CH-7)
    for _ in range(30):
        t = trims()
        if abs(t["game"][0] + 3.0) < 0.3: break
        time.sleep(0.1)
    assert abs(t["game"][0] + 3.0) < 0.3, (before, t, r)
    assert abs(t["system"][0] - (before["system"][0] + 6.0)) < 0.4, f"system should follow by +6 dB: {before['system']} → {t['system']}"
    assert abs(t["voice"][0] - min(0.0, before["voice"][0] + 6.0)) < 0.4, f"voice (was 0 dB) is clamped at 0: {t['voice']}"
    # --- tray: ⛓ on every member's name
    tr = tray_probe(stack, "trayChannelName/game.text", "trayChannelName/system.text", "trayChannelName/voice.text")
    assert all("⛓" in tr[k] for k in tr), tr
    # --- leaving: voice out, game move no longer touches voice
    stack.cli("channel", "group", "voice", "none"); stack.cli("channel", "trim", "game", "-9dB"); time.sleep(0.5)
    t2 = trims(); assert t2["voice"][0] == t["voice"][0], "a channel that left the group must not follow any more"
    tray2 = tray_probe(stack, "trayChannelName/voice.text"); assert "⛓" not in tray2["trayChannelName/voice.text"]
    # --- persists
    stack.restart_daemon(); time.sleep(1.0)
    assert stack.cli("channel", "groups", json_out=True) == {"Media": ["game", "system"]}
    # --- refused: a slash (would break slugs/paths in the tray keys) — and the CLI reports it
    r = stack.cli("channel", "group", "game", "a/b", check=False); assert r.returncode != 0 and "group name" in r.stderr
    for c in ("game", "system"): stack.cli("channel", "group", c, "none")

def test_fx8_the_kde_window_can_open_the_fx_panel_and_greys_out_missing_plugins(stack):
    """FX-8 im KDE-Fenster — und der Grund, warum es diesen Test gibt.

    Zwei Funde vom 2026-09-21, beide vorher von KEINEM Test beruehrt:

    1. Das Effekt-Panel liess sich im Fenster GAR NICHT oeffnen. FxPanel.qml hatte eine
       Kirigami.FormLayout als Wurzel, Main.qml schob sie mit pushDialogLayer() — das
       verlangt eine Page. verifyPages() lehnte ab, PageRow.qml starb an "Value is null",
       und im Fenster passierte sichtbar nichts. Der Klick auf "Effects…" im Kanalkopf war
       tot. Gemerkt hat es niemand, weil die FX-Tests ueber CLI und Browser liefen und
       --self-test die Datei nur laedt, ohne sie zu pushen.
    2. Ein Effekt ohne installiertes LADSPA-Plugin sah wie jeder andere waehlbar aus.

    Der Test geht durch die Shell wie ein Nutzer: Panel oeffnen, Dropdown aufklappen,
    Eintraege lesen. Faellt (1) zurueck, findet der Probe das Dropdown nicht mehr; faellt
    (2) zurueck, ist `enabled` true oder der Paketname fehlt.
    """
    katalog = {t["type"]: t for t in json.loads(stack.cli("fx", "types").stdout)}
    fehlende = [typ for typ, s in katalog.items() if s.get("available") is False]
    vorhandene = [typ for typ, s in katalog.items() if s.get("available") is not False]
    assert vorhandene, "kein einziger Effekt verfuegbar — dann prueft der Test unten nichts"

    specs = ["fxAddType.count"]
    for typ in fehlende + vorhandene[:2]:
        specs += [f"fxAddType/{typ}.enabled", f"fxAddType/{typ}.text"]
    g = kde(stack, "--open", "fx/channel/voice", "--gesture", "fxaddopen",
            *sum((["--probe", s] for s in specs), []))

    # (1) Das Panel ist offen und das Dropdown gefuellt — sonst waere jeder Probe "<not found>".
    assert g["fxAddType.count"] == str(len(katalog)), (
        f"Dropdown zeigt {g['fxAddType.count']} Eintraege, der Daemon kennt {len(katalog)}: {g}")

    # (2) Fehlendes Plugin: gesperrt, mit Paketname am Eintrag.
    for typ in fehlende:
        assert g[f"fxAddType/{typ}.enabled"] == "false", f"{typ} ist waehlbar, obwohl das Plugin fehlt"
        paket = katalog[typ]["package"].split()[0]
        assert paket in g[f"fxAddType/{typ}.text"], (
            f"Paketname {paket!r} fehlt am Eintrag: {g[f'fxAddType/{typ}.text']!r}")
    # Gegengewicht: vorhandene Effekte MUESSEN waehlbar bleiben, sonst wuerde ein generell
    # kaputtes Dropdown die Pruefung oben gruen faerben.
    for typ in vorhandene[:2]:
        assert g[f"fxAddType/{typ}.enabled"] == "true", f"{typ} ist gesperrt, obwohl das Plugin da ist"


def Mixer_name(stack, slug):
    """Der Anzeigename eines Kanals, wie ihn das Fenster zeigt (die ComboBox listet Namen, nicht Slugs)."""
    return next(c["Name"] for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == slug)


def test_fx9_ducking_panel_opens_in_the_window_and_configures_the_strength(stack):
    """FX-9 im Fenster (Regel 2): das Ducking-Panel oeffnet sich per pushDialogLayer, die vier Regler
    tragen die Grenzen des Daemons, und was dort steht ist dasselbe, was die CLI sieht.

    Der Test geht wie ein Nutzer durch die Shell — Panel oeffnen, Werte lesen. Faellt die Wurzel von
    ScrollablePage auf FormLayout zurueck (der FxPanel-Fehler vom 2026-09-21), findet der Probe die
    Regler nicht mehr und jeder Wert ist "<not found>". qmllint merkt das NICHT: die Datei ist
    syntaktisch einwandfrei, sie laesst sich nur nicht pushen.
    """
    stack.cli("duck", "set", "game", "--by", "voice", "--depth", "-18", "--threshold", "-25",
              "--attack", "30", "--release", "400")
    cli = stack.cli("--json", "duck", "show", "game", json_out=True)

    # duckPanelPushed.visible ist der einzige Probe, der beweist, dass das Panel WIRKLICH auf dem
    # Layer-Stack liegt: ein per createObject erzeugtes Panel haengt am Fenster und ist damit probebar,
    # auch wenn pushDialogLayer es abgelehnt hat (genau die Luecke, die FxPanel monatelang verdeckte).
    specs = ["duckPanelPushed.text", "duckBy.count", "duckReduction.text", "duckClear.enabled"]
    for key in ("depth", "threshold", "attack", "release"):
        specs += [f"duckParam/{key}.value", f"duckParam/{key}.from", f"duckParam/{key}.to",
                  f"duckParam/{key}.enabled"]
    g = kde(stack, "--open", "duck/game", *sum((["--probe", s] for s in specs), []))

    assert g["duckPanelPushed.text"] == "yes", (
        "pushDialogLayer hat das Panel NICHT gepusht — ist die Wurzel von DuckPanel.qml eine Page? " + str(g))

    # (1) Das Panel ist offen: die Trigger-Liste ist gefuellt — "Not ducked" plus die anderen Kanaele,
    #     der eigene Kanal fehlt (der Daemon lehnt Selbst-Ducking ab).
    kanaele = stack.cli("channel", "list", json_out=True)
    erwartet = 1 + len([c for c in kanaele if c["Slug"] != "game"])
    assert g["duckBy.count"] == str(erwartet), f"Trigger-Liste zeigt {g['duckBy.count']}, erwartet {erwartet}: {g}"

    # (2) Die eingestellte Staerke steht in den Reglern — dieselben Zahlen, die die CLI liefert.
    for key in ("depth", "threshold", "attack", "release"):
        assert float(g[f"duckParam/{key}.value"]) == cli[key], (
            f"{key}: Fenster {g[f'duckParam/{key}.value']}, CLI {cli[key]}")
        assert g[f"duckParam/{key}.enabled"] == "true", f"{key} ist gesperrt, obwohl ein Trigger gesetzt ist"

    # (3) Die Grenzen sind die des Daemons — ein Regler, der einen abgelehnten Wert erzeugen kann, ist kaputt.
    #     Gegenprobe zu den Zahlen: die CLI muss jeden Randwert annehmen und alles dahinter ablehnen.
    for key, (von, bis) in (("depth", (-60, 0)), ("threshold", (-60, 0)), ("attack", (2, 400)), ("release", (2, 800))):
        assert float(g[f"duckParam/{key}.from"]) == von and float(g[f"duckParam/{key}.to"]) == bis, (
            f"{key}: Regler {g[f'duckParam/{key}.from']}..{g[f'duckParam/{key}.to']}, Daemon {von}..{bis}")
        for rand in (von, bis):
            r = stack.cli("duck", "set", "game", f"--{key}", str(rand), check=False)
            assert r.returncode == 0, f"{key}={rand} ist Regler-Rand, aber die CLI lehnt ab: {r.stdout}{r.stderr}"
        r = stack.cli("duck", "set", "game", f"--{key}", str(von - 1 if von < bis else bis + 1), check=False)
        assert r.returncode == 4, f"{key} ausserhalb des Regler-Bereichs wurde angenommen: {r.stdout}{r.stderr}"

    # Der eigene Kanal darf NICHT in der Trigger-Liste stehen: der Daemon lehnt Selbst-Ducking ab, also
    # soll die Oberflaeche es nicht anbieten. duckBy.count oben zaehlt nur — dieser Probe liest die Namen.
    namen = kde(stack, "--open", "duck/game", "--probe", "duckBy.model")["duckBy.model"]
    assert Mixer_name(stack, "game") not in namen, f"eigener Kanal steht in der Trigger-Liste: {namen}"
    assert Mixer_name(stack, "voice") in namen, f"Trigger-Kanal fehlt in der Liste: {namen}"

    assert g["duckClear.enabled"] == "true"
    assert "dB" in g["duckReduction.text"], g["duckReduction.text"]
    stack.cli("duck", "clear", "game")


def test_fx9_badge_says_who_ducks_and_how_much_in_all_three_frontends(stack):
    """FX-9, Spec-Wortlaut: der abgesenkte Kanal MUSS ein "ducked by <channel>"-Abzeichen und die aktuelle
    Reduktion zeigen. Dreifach-Paritaet heisst hier: CLI-Baum, Web-Kanalzeile und KDE-Kanalkopf nennen
    denselben Trigger — sonst zeigt ein Frontend etwas anderes an als die anderen zwei.
    """
    stack.cli("duck", "set", "game", "--by", "voice", "--depth", "-18")
    try:

        # CLI: Baum (Text) und --json
        # Das Abzeichen haengt an der KANALZEILE (…"Game (game)  v voice"), nicht in einer eigenen Zeile.
        baum = stack.cli("tree").stdout
        gamezeile = next(z for z in baum.splitlines() if "(game)" in z)
        assert "v voice" in gamezeile, f"CLI-Baum nennt den Trigger nicht an der Kanalzeile: {gamezeile!r}"
        assert "v " not in next(z for z in baum.splitlines() if "(voice)" in z), (
            "der Trigger-Kanal selbst traegt ein Abzeichen")
        j = stack.cli("--json", "tree", json_out=True)
        kj = next(k for k in j["channels"] if k["slug"] == "game")
        assert kj["duckedBy"] == "voice" and kj["duckDepth"] == -18, f"JSON-Baum: {kj}"
        assert "duckReduction" in kj, f"laufende Reduktion fehlt im JSON: {kj}"

        # KDE-UI: Abzeichen am Kanalkopf
        g = kde(stack, "--probe", "channelDuckBadge/game.visible")
        assert g["channelDuckBadge/game.visible"] == "true", f"KDE-Abzeichen unsichtbar: {g}"

        # (Die Web-Kanalzeile prueft test_web.py::test_fx9_badge_in_the_channel_row — dort steht der Browser.)

        # Gegengewicht: ein NICHT abgesenkter Kanal zeigt kein Abzeichen — sonst waere oben jede Zeile gruen.
        g2 = kde(stack, "--probe", "channelDuckBadge/voice.visible")
        assert g2["channelDuckBadge/voice.visible"] in ("false", "<not found>"), (
            f"nicht abgesenkter Kanal traegt ein Abzeichen: {g2}")
    finally:
        stack.cli("duck", "clear", "game")

def test_ct8_soundboard_is_the_same_in_all_three_frontends(stack):
    """CT-8 (core → this file is required, see the header): one sample, three frontends, one state.

    Each frontend is asked the SAME question — is the pad there and does it say "playing" — and the answer has to
    match the daemon. The KDE side is probed through the real window (`--open soundboard --probe`), which is what
    catches a panel that compiles but never loads: FxPanel shipped broken for months because nothing ever opened it.
    """
    # The stack fixture is module-scoped, so the board may already exist from the CT-8 row in CORE — reuse it.
    # A SECOND board is not an option: the panel shows one board at a time (it picks the first, Main.qml:516), so
    # probing a pad on a second board finds nothing. Isolation therefore runs over the sample NAME, and the list
    # assertion below checks that this name is present rather than that it is alone.
    vorhanden = [c["Slug"] for c in stack.cli("status", json_out=True)["channels"] if c.get("Kind") == "soundboard"]
    slug = vorhanden[0] if vorhanden else stack.cli("channel", "add", "--soundboard", "Board", json_out=True)["slug"]
    wav = stack.pw.tone()
    stack.cli("sample", "add", slug, str(wav), "--name", "solo", check=False)
    try:
        # (1) CLI
        zeilen = stack.cli("--json", "sample", "list", slug, json_out=True)
        meine = [z for z in zeilen if z["name"] == "solo"]
        assert len(meine) == 1, zeilen
        assert meine[0]["sounding"] is False

        # (2) KDE window: the panel must actually LOAD and show this pad
        w = window(stack, "soundboardPanel.visible", f"samplePad/{slug}:solo.text",
                   f"samplePad/{slug}:solo.highlighted", open_page="soundboard")
        assert w[f"samplePad/{slug}:solo.text"] == "solo", w
        assert w["soundboardPanel.visible"] == "true", w
        assert w[f"samplePad/{slug}:solo.highlighted"] == "false", "the pad looked like it was playing before play"

        # (3) web UI
        b = browser(stack, f"samplePad:{slug}:solo.textContent", view="soundboard")
        assert "solo" in b[f"samplePad:{slug}:solo.textContent"], b

        # now play it and ask all three again — one state, three views
        stack.cli("sample", "play", "solo")
        wait_for(lambda: stack.cli("--json", "sample", "list", slug, json_out=True)[0]["sounding"] is True,
                 timeout=8.0, what="the CLI to see the sample sounding")
        # `highlighted`, not the label: --probe returns one line per probe, so a label with a newline in it comes
        # back truncated (measured 2026-09-22 — the probe showed just "solo" for a pad that read "solo\n▶ playing").
        w2 = window(stack, f"samplePad/{slug}:solo.highlighted", open_page="soundboard")
        assert w2[f"samplePad/{slug}:solo.highlighted"] == "true", w2
        b2 = browser(stack, f"samplePad:{slug}:solo.className", view="soundboard")
        assert "sounding" in b2[f"samplePad:{slug}:solo.className"], b2
    finally:
        # Stop THIS sample, not every sample: `sample stop ""` is stop-all and the stack is shared with the CORE
        # row, which has its own solo sounding. Then drop the board so the list assertions above stay true if
        # this test is ever run twice against one daemon.
        # Stop and unregister only MY sample: the board is shared with the CT-8 row in CORE, and `sample stop ""`
        # would be stop-all. Removing the sample keeps the shared board usable for whoever runs next.
        stack.cli("sample", "stop", "solo", check=False)
        stack.cli("sample", "remove", slug, "solo", check=False)


def test_ux18_loudness_is_visible_in_every_frontend(stack):
    """UX-18: the R128 numbers and the target are readable in all four frontends, not just over the bus.

    This row exists because the requirement sat on ✅ for two days while the UI side was missing entirely:
    daemon, bus, CLI and a test were all green, and `grep -ri loudness src/qml web/static` returned nothing.
    A bus property nobody can see is not a loudness meter — so each frontend is asked for the actual NUMBER
    here, and the reference is measured with ffmpeg (via tone_at_known_loudness) instead of assumed. A wrong
    unit, a wrong array index or a frontend reading M where it should read I therefore shows up as a number
    that disagrees with the reference, not as "some text arrived".
    """
    # Own mix, so switching the analyser on cannot disturb the other rows sharing this module-scoped stack.
    stack.cli("mix", "add", "R128", check=False)
    try:
        stack.cli("mix", "loudness", "r128", "on")
        stack.cli("mix", "loudness", "r128", "-16")
        # route the tone: a mix with no cell fed from a channel measures silence forever
        stack.cli("cell", "set", "voice", "r128", "1.0", check=False)

        tone, want = stack.pw.tone_at_known_loudness()
        play = subprocess.Popen(["pw-play", "-P", '{ application.name = "Ux18Parity" node.name = "ux18-parity" '
                                 'target.object = "kmixdeck.channel.voice" }', str(tone)],
                                env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            # (1) CLI — the reference path. The wait condition is STABILITY, not "a number arrived": measured
            # 2026-09-22, the first I above the -70 floor appears at t=0.25 s reading -26.72 LUFS, 6.7 LU below the
            # reference, because BS.1770 integrates over gated blocks and has barely any of them yet. It passes 1 LU
            # at t≈1.1 s and settles near -20.1. A wait on "I > -70" therefore samples mid-integration and the
            # frontend assertions below inherit a number the analyser itself would disown a second later.
            def stabil():
                a = json.loads(stack.cli("--json", "loudness", "--once").stdout).get("r128", [-70] * 4)[2]
                if a <= -70:
                    return False
                time.sleep(0.4)
                b = json.loads(stack.cli("--json", "loudness", "--once").stdout).get("r128", [-70] * 4)[2]
                return b > -70 and abs(b - a) < 0.3

            wait_for(stabil, timeout=25.0, what="the integrated loudness of mix r128 to settle (BS.1770 gating)")
            lu = json.loads(stack.cli("--json", "loudness", "--once").stdout)["r128"]
            m, s, i, tp = lu
            assert abs(i - want) < 2.0, f"integrated {i:.2f} LUFS should be near the measured reference {want:.2f} LUFS"

            # (2) KDE window: the readout row must be there AND carry the same numbers, plus the target line
            w = window(stack, "lufsRow/r128.visible", "lufsI/r128.text", "lufsM/r128.text",
                       "lufsTP/r128.text", "lufsTarget/r128.text", "loudnessTarget/r128.visible")
            assert w["lufsRow/r128.visible"] == "true", f"the analyser is on but the KDE readout is hidden: {w}"
            assert w["loudnessTarget/r128.visible"] == "true", f"target line missing on the meter: {w}"
            assert w["lufsTarget/r128.text"] == "target -16", w
            # 0.5 LU between a frontend and the bus, not 3: the readouts are fed by the same signal at 25 Hz, so
            # anything larger is a unit error or the wrong array index, which is exactly what this row must catch.
            assert abs(float(w["lufsI/r128.text"]) - i) < 0.5, f"KDE shows I={w['lufsI/r128.text']}, bus says {i:.2f}"
            assert abs(float(w["lufsM/r128.text"]) - m) < 2.0, f"KDE shows M={w['lufsM/r128.text']}, bus says {m:.2f}"
            assert w["lufsTP/r128.text"].startswith("TP "), w

            # (3) tray: the integrated value against the target, one glance
            tr = tray(stack, "trayLufs/r128.visible", "trayLufs/r128.text")
            assert tr["trayLufs/r128.visible"] == "true", f"analyser on but tray line hidden: {tr}"
            assert "target -16" in tr["trayLufs/r128.text"], tr
            assert abs(float(tr["trayLufs/r128.text"].split()[0]) - i) < 0.5, f"tray disagrees with the bus: {tr}"

            # (4) web UI
            b = browser(stack, "lufsRow/r128.textContent", "lufsI/r128.textContent", "lufsTP/r128.textContent",
                        "loudnessTarget/r128.dataset.lufs")
            assert "target -16" in b["lufsRow/r128.textContent"], b
            assert b["loudnessTarget/r128.dataset.lufs"] == "-16", f"web target line at the wrong value: {b}"
            assert abs(float(b["lufsI/r128.textContent"]) - i) < 0.5, f"web shows I={b['lufsI/r128.textContent']}, bus says {i:.2f}"
        finally:
            play.terminate()

        # switching it off must remove the readout everywhere — a frozen last reading is worse than no reading,
        # because it looks like a measurement of the current signal.
        stack.cli("mix", "loudness", "r128", "off")
        time.sleep(1.0)
        w3 = window(stack, "lufsRow/r128.visible")
        assert w3["lufsRow/r128.visible"] == "false", f"analyser off but the KDE readout is still there: {w3}"
        tr3 = tray(stack, "trayLufs/r128.visible")
        assert tr3["trayLufs/r128.visible"] == "false", f"analyser off but the tray line is still there: {tr3}"
    finally:
        stack.cli("mix", "remove", "r128", check=False)


def test_ux18_silence_after_a_tone_reads_as_the_floor_not_as_minus_2432_db(stack):
    """After the signal stops, every loudness value must sit at the -70 LUFS floor — not at -253 or -2432 dB.

    Found 2026-09-22 by watching M across the end of a 12 s tone: it went -20.25 → -253.54 → -2432.19 dB and
    those numbers went out over the bus into all four frontends. The guard in meters.cpp was `std::isfinite()`,
    which is true for -2432, because libebur128 only promises -HUGE_VAL for actual negative infinity and returns
    finite garbage for near-silence. ffmpeg's ebur128 — the reference tool — prints M:-163.2 for digital silence
    but reports I: -70.0 LUFS, the BS.1770 absolute gate, and that is the clamp this asserts.

    This is its own row because the parity test above measures while the tone PLAYS and never looks at what
    happens when it stops: with the clamp reverted, that test stayed green (verified 2026-09-22).
    """
    stack.cli("mix", "add", "Floor", check=False)
    try:
        stack.cli("mix", "loudness", "floor", "on")
        stack.cli("cell", "set", "voice", "floor", "1.0", check=False)
        tone, _ = stack.pw.tone_at_known_loudness()
        subprocess.run(["pw-play", "-P", '{ application.name = "Ux18Floor" node.name = "ux18-floor" '
                        'target.object = "kmixdeck.channel.voice" }', str(tone)],
                       env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=40)
        # the tone has ended here; M's 400 ms window and S's 3 s window both run dry within ~4 s
        schlimmster = {}
        for _ in range(45):
            time.sleep(0.2)
            lu = json.loads(stack.cli("--json", "loudness", "--once").stdout).get("floor")
            if not lu:
                continue
            for name, v in zip(("M", "S", "I", "TP"), lu, strict=True):
                if v < schlimmster.get(name, 0.0):
                    schlimmster[name] = v
        assert schlimmster, "the analyser reported nothing at all after the tone"
        for name, v in schlimmster.items():
            assert v >= -70.0, f"{name} fell to {v:.2f} below the -70 LUFS floor after the tone ended: {schlimmster}"
        # The frontends must show the floor as a dash — but only for the values that HAVE a floor. Measured while
        # writing this: I stayed at -20.1 through the silence, and that is correct, not a bug. Integrated loudness
        # is cumulative over the whole programme per BS.1770; it is the number a streamer checks at the end of a
        # session, so it must NOT reset when nobody talks for a moment. M (400 ms) and S (3 s) are the sliding
        # windows that do run dry, so they are what the dash applies to.
        w = window(stack, "lufsM/floor.text", "lufsI/floor.text")
        assert w["lufsM/floor.text"] == "–", f"momentary shows {w['lufsM/floor.text']!r} for silence, expected a dash"
        assert float(w["lufsI/floor.text"]) < -10.0, f"integrated should still hold the programme value: {w}"
    finally:
        stack.cli("mix", "remove", "floor", check=False)
