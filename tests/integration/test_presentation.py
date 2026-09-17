# SPDX-License-Identifier: GPL-3.0-or-later
"""UX-8 icons and UX-9 order: pure presentation state that must survive a daemon restart and never touch the graph.

Runs against a private PipeWire + private session bus (see test_service_cli.Stack)."""
import json, time
from pathlib import Path
import pytest
from test_service_cli import Stack, BIN
from pw_sandbox import start_private_pipewire


@pytest.fixture(scope="module")
def stack():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream")
    s = Stack(pw)
    yield s
    s.close(); pw.close()


def layout(stack):
    return json.loads((Path(stack.pw.runtime_dir) / "config" / "kmixdeck" / "layout.json").read_text())


def wait(pred, tries=50, dt=0.1):
    for _ in range(tries):
        v = pred()
        if v: return v
        time.sleep(dt)
    return pred()


def prop(stack, path, iface, name):
    r = stack.busctl("get-property", "org.kmixdeck1", path, iface, name)
    assert r.returncode == 0, r.stderr
    return r.stdout.strip()


# ------------------------------------------------------------------------------------------------ UX-8 icons
def test_ux8_icon_is_persisted_and_survives_restart(stack):
    stack.cli("channel", "icon", "game", "input-gaming")
    stack.cli("mix", "icon", "stream", "camera-video")
    assert prop(stack, "/org/kmixdeck1/channel/game", "org.kmixdeck1.Channel", "Icon") == 's "input-gaming"'
    assert prop(stack, "/org/kmixdeck1/mix/stream", "org.kmixdeck1.Mix", "Icon") == 's "camera-video"'
    lay = layout(stack)
    assert next(c for c in lay["channels"] if c["slug"] == "game")["icon"] == "input-gaming"
    assert next(m for m in lay["mixes"] if m["slug"] == "stream")["icon"] == "camera-video"
    stack.restart_daemon()   # the bus answers before reconcile() has merged the layout (registry replay first)
    assert wait(lambda: prop(stack, "/org/kmixdeck1/channel/game", "org.kmixdeck1.Channel", "Icon") == 's "input-gaming"')
    assert prop(stack, "/org/kmixdeck1/mix/stream", "org.kmixdeck1.Mix", "Icon") == 's "camera-video"'


def test_ux8_icon_none_clears_to_frontend_default(stack):
    stack.cli("channel", "icon", "game", "none")
    assert prop(stack, "/org/kmixdeck1/channel/game", "org.kmixdeck1.Channel", "Icon") == 's ""'


def test_ux8_icon_may_be_an_absolute_path(stack):
    stack.cli("mix", "icon", "monitor", "/tmp/somewhere/logo.png")   # the daemon stores, the frontend renders
    assert prop(stack, "/org/kmixdeck1/mix/monitor", "org.kmixdeck1.Mix", "Icon") == 's "/tmp/somewhere/logo.png"'
    stack.cli("mix", "icon", "monitor", "none")


# ------------------------------------------------------------------------------------------------ UX-9 order
def order(stack, what):
    r = stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", what)
    assert r.returncode == 0, r.stderr
    parts = r.stdout.strip().split()            # 'as 3 "game" "system" "voice"'
    return [p.strip('"') for p in parts[2:]]


def cell_volumes(stack):
    st = stack.cli("status", json_out=True)
    return {c["Path"] if "Path" in c else (c["Channel"], c["Mix"]): c["Volume"] for c in st["cells"]}


def test_ux9_move_changes_order_only(stack):
    before_cells = cell_volumes(stack)
    ch = order(stack, "ChannelOrder"); assert len(ch) >= 3
    first = ch[0]
    stack.cli("channel", "move", first, "bottom")
    assert order(stack, "ChannelOrder") == ch[1:] + [first]
    assert cell_volumes(stack) == before_cells, "a move must not touch any fader"
    assert [c["slug"] for c in layout(stack)["channels"]] == ch[1:] + [first]


def test_ux9_move_up_down_and_clamp(stack):
    ch = order(stack, "ChannelOrder")
    last = ch[-1]
    stack.cli("channel", "move", last, "up")
    assert order(stack, "ChannelOrder")[-2] == last
    stack.cli("channel", "move", last, "down")
    assert order(stack, "ChannelOrder")[-1] == last
    stack.cli("channel", "move", last, "99")          # clamped, not refused
    assert order(stack, "ChannelOrder")[-1] == last
    stack.cli("channel", "move", last, "top")
    assert order(stack, "ChannelOrder")[0] == last


def test_ux9_mix_order_survives_restart(stack):
    mx = order(stack, "MixOrder"); assert len(mx) >= 2
    stack.cli("mix", "move", mx[-1], "top")
    want = [mx[-1]] + mx[:-1]
    assert order(stack, "MixOrder") == want
    stack.restart_daemon()
    assert wait(lambda: order(stack, "MixOrder") == want)
    assert [m["slug"] for m in layout(stack)["mixes"]] == want


def test_ux9_unknown_slug_is_refused(stack):
    r = stack.cli("channel", "move", "nope", "top", check=False)
    assert r.returncode != 0
    r = stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "MoveMix", "oi", "/org/kmixdeck1/mix/nope", "0")
    assert r.returncode != 0 and "no such mix" in r.stderr


def test_ux9_order_is_in_get_managed_objects(stack):
    r = stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects")
    assert '"ChannelOrder"' in r.stdout and '"MixOrder"' in r.stdout


# ---- UI-side proofs via `kmixdeck-kde --probe` / `--gesture` (VF-2: the picture is a claim until a test reads it)
import subprocess, os
from test_service_cli import start_fake_app


def kde(stack, *args, open_page=None, timeout=30):
    b = BIN / "kmixdeck-kde"
    if not b.exists(): pytest.skip("kmixdeck-kde not built")
    env = dict(stack.env); env["QT_QPA_PLATFORM"] = "offscreen"
    cmd = [str(b)] + (["--open", open_page] if open_page else []) + list(args)
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
    return {l.split(" = ", 1)[0].split(" ", 1)[1]: l.split(" = ", 1)[1] for l in r.stdout.splitlines() if l.startswith("probe ")} \
        if "--probe" in args else [l for l in r.stdout.splitlines() if l.startswith("gesture ")]


def test_mx10_muted_mix_header_is_red_and_says_so(stack):
    """MX-10: a muted mix is unmistakable — the header background takes the negative colour, the title says 'muted'."""
    stack.cli("mix", "mute", "stream", "off")
    off = kde(stack, "--probe", "mixHeaderBg/stream.color", "--probe", "mixHeaderTitle/stream.text", "--probe", "mixHeaderBg/monitor.color")
    stack.cli("mix", "mute", "stream", "on")
    try:
        on = kde(stack, "--probe", "mixHeaderBg/stream.color", "--probe", "mixHeaderTitle/stream.text", "--probe", "mixHeaderBg/monitor.color")
    finally:
        stack.cli("mix", "mute", "stream", "off")
    assert off["mixHeaderTitle/stream.text"] == "Stream" and "muted" in on["mixHeaderTitle/stream.text"].lower(), (off, on)
    assert on["mixHeaderBg/stream.color"] != off["mixHeaderBg/stream.color"], "muted header must change colour"
    assert on["mixHeaderBg/monitor.color"] == off["mixHeaderBg/monitor.color"], "the OTHER mix must not change"
    # red-ish: in Breeze light the negative background is a pink/red tint — red channel dominant
    c = on["mixHeaderBg/stream.color"].lstrip("#"); r, g, b = int(c[-6:-4], 16), int(c[-4:-2], 16), int(c[-2:], 16)
    assert r > g and r > b, f"muted colour should be red-dominant, got {on['mixHeaderBg/stream.color']}"


def test_ux10_app_row_shows_its_icon_and_running_state(stack):
    """UX-10: the Applications page shows the app's own icon and whether it is playing right now."""
    p, app = start_fake_app(stack)
    try:
        name = app["Name"] if isinstance(app, dict) else "FakeGame"
        got = kde(stack, "--probe", f"appIcon/{name}.source", "--probe", f"appRunning/{name}.opacity", open_page="apps")
        assert got.get(f"appIcon/{name}.source") not in (None, "", "<not found: appIcon/%s>" % name), got
        assert float(got[f"appRunning/{name}.opacity"]) >= 0.5, f"a playing app must not be shown as silent: {got}"
    finally:
        p.kill(); p.wait()
    for _ in range(50):
        if not any(a.get("Running") for a in stack.cli("app", "list", json_out=True) if a.get("Name") == name): break
        time.sleep(0.1)
    got = kde(stack, "--probe", f"appRunning/{name}.opacity", open_page="apps")
    # after the app is gone the row either disappears (not found) or shows silent (≤ 0.25)
    v = got.get(f"appRunning/{name}.opacity", "<not found>")
    assert v.startswith("<not found") or float(v) <= 0.26, got


def test_ux11_drop_on_channel_row_assigns_the_app(stack):
    """UX-11: dropping an app onto a channel row adds that channel (accumulates, CH-12) — driven through the same
    call the DropArea makes, proven at the daemon."""
    p, app = start_fake_app(stack)
    try:
        path = next(a["Path"] for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")
        before = next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"]
        out = kde(stack, "--gesture", f"drop:{path}|voice")
        assert out and out[0].endswith("-> ok"), out
        for _ in range(50):
            now = next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"]
            if "voice" in now: break
            time.sleep(0.1)
        if not ("voice" in now and all(c in now for c in before)):
            from test_service_cli import daemon_log_tail
            raise AssertionError(f"drop must ADD voice, keep {before}: {now}\n--- daemon log ---\n" + daemon_log_tail(stack, ("assign", "new application", "routed \"FakeGame\"")))
    finally:
        p.kill(); p.wait()


def test_dv27_monitors_column_is_hidden_by_default_and_shows_listening_device_with_its_mixes(stack):
    """DV-27: the patchbay's Monitors column is opt-in; once shown it names the listening device (UX-2) and lists
    every mix routed there with its master level; a mix NOT routed there is absent; no device → hint."""
    from test_service_cli import make_fake_sink
    stack.cli("listen", "none", check=False)
    hidden = kde(stack, "--probe", "monitorsColumn.visible", open_page="patchbay")
    assert hidden["monitorsColumn.visible"] == "false", hidden
    none = kde(stack, "--gesture", "monitors:on", "--probe", "monitorsColumn.visible", "--probe", "monitorsDevice.text", open_page="patchbay")
    assert none["monitorsColumn.visible"] == "true" and "No listening device" in none["monitorsDevice.text"], none
    make_fake_sink(stack, "fake.cans", "Fake Cans")
    stack.cli("listen", "fake.cans")
    stack.cli("mix", "output", "stream", "fake.cans"); stack.cli("mix", "volume", "stream", "0.5")
    for _ in range(50):
        if "fake.cans" in stack.cli("mix", "outputs", "stream", json_out=True): break
        time.sleep(0.1)
    got = kde(stack, "--gesture", "monitors:on", "--probe", "monitorsDevice.text", "--probe", "monitorsMix/stream.text", "--probe", "monitorsVolume/stream.value", "--probe", "monitorsMix/monitor.text", open_page="patchbay")
    assert got["monitorsDevice.text"] == "Fake Cans", got
    assert got["monitorsMix/stream.text"] == "Stream", got
    assert got["monitorsMix/monitor.text"].startswith("<not found"), f"monitor mix is not routed to the cans: {got}"
    # the slider value is the same cubic level the mixer page shows (MixerClient::mixVolume)
    ui_level = float(got["monitorsVolume/stream.value"]); daemon_lin = 0.5
    assert abs(ui_level ** 3 - daemon_lin) < 0.05, f"slider {ui_level} → linear {ui_level ** 3} ≠ {daemon_lin}"
    stack.cli("mix", "volume", "stream", "1.0"); stack.cli("mix", "output", "stream", "none"); stack.cli("listen", "none", check=False)


def test_dv14_wire_popover_shows_trim_and_mute_of_that_wire(stack):
    """DV-14 in the patchbay: clicking a device wire opens a popover whose slider/dB text/mute reflect THAT wire's trim
    (set via the CLI), not the channel's fader; the popover offers Remove."""
    from test_service_cli import make_fake_sink
    make_fake_sink(stack, "fake.pop", "Pop Sink")
    stack.cli("mix", "output-add", "stream", "fake.pop")
    for _ in range(50):
        if "fake.pop" in stack.cli("mix", "outputs", "stream", json_out=True): break
        time.sleep(0.1)
    stack.cli("mix", "wire", "stream", "fake.pop", "trim", "-6dB", "mute", "on")
    got = kde(stack, "--gesture", "wirepopup:output|stream|fake.pop", "--probe", "wirePopup.visible", "--probe", "wireTrim.value", "--probe", "wireTrimText.text", "--probe", "wireMute.checked", "--probe", "wireRemove.visible", open_page="patchbay")
    assert got["wirePopup.visible"] == "true", got
    want = (10 ** (-6 / 20)) ** (1 / 3)
    assert abs(float(got["wireTrim.value"]) - want) < 0.02, got
    assert got["wireTrimText.text"].startswith("-6.") or got["wireTrimText.text"].startswith("-5.9"), got
    assert got["wireMute.checked"] == "true" and got["wireRemove.visible"] == "true", got
    stack.cli("mix", "wire", "stream", "fake.pop", "trim", "0dB", "mute", "off")
    stack.cli("mix", "output-remove", "stream", "fake.pop")
