"""AR-12 / ADR 0010 D5: a core-tier feature is done only when EVERY frontend shows it.

One table row per core feature: the change is made through the CLI (rule 1: the API is the feature), then read
back through (a) the CLI itself, (b) the KDE window (`--probe`), (c) the tray popover (`--gesture trayclick:1
--probe`). Adding a core feature = adding a row here; tools/sot-audit.py requires this file for every ✅ core row.
"""
import subprocess, time
import pytest
from test_service_cli import BIN, Stack, make_fake_sink  # noqa: F401
from test_presentation import kde, stack  # noqa: F401


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
