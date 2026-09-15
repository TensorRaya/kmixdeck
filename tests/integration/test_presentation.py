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
