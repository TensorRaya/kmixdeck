"""ADR 0014 HY-3: the shipped data/hyprland/kmixdeck.lua, loaded the way docs tell a user to, does what it says.

The sandbox's hyprland.lua gets exactly the two documented lines (tools/hyprland-sandbox.py --require-kmixdeck):

    package.path = "<repo>/data/hyprland/?.lua;" .. package.path
    local kmixdeck = require("kmixdeck")

Measured before the rule existed (Hyprland 0.56.2): the main window is tiled; with a float rule it opens centred at
the size it asks for (gridUnit * 64 x 36). A rule that also matched initial_title floated the same window at the full
monitor size instead, which is what test_hy3_the_main_window_floats_at_its_own_size_centred guards against.

Shares the session helpers with test_shortcuts_hyprland.py; nothing touches the logged-in user's session.
"""
import json
import subprocess
import time

import pytest

from test_service_cli import BIN
from test_shortcuts_hyprland import APP, HyprlandSession, skip_without_hyprland
from test_shortcuts_wayland import _wait


@pytest.fixture(scope="module")
def hy(tmp_path_factory):
    skip_without_hyprland()
    s = HyprlandSession(tmp_path_factory.mktemp("hy3"), "--require-kmixdeck")
    s.hypr_eval("hl.config({ input = { resolve_binds_by_sym = true } })")
    yield s
    s.close()


def clients(hy, pid=None) -> list[dict]:
    return [c for c in json.loads(hy.run("hyprctl", "clients", "-j"))
            if c["class"] == APP and (pid is None or c["pid"] == pid)]


def wait_clients(hy, what: str, pid=None, count=1, timeout=20, proc=None) -> list[dict]:
    """Like _wait, but a timeout says what Hyprland DID see: every client, the frontend's last lines, the process'
    stderr. One run in fourteen once timed out here with no trace (2026-10-06); the next one must leave one."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = clients(hy, pid)
        if len(found) >= count:
            return found
        time.sleep(0.1)
    every = [(c["pid"], c["class"], c["title"], c["floating"], c["size"]) for c in json.loads(hy.run("hyprctl", "clients", "-j"))]
    err = ""
    if proc is not None:
        if proc.poll() is None:
            proc.kill()
        err = proc.communicate(timeout=5)[1][-1500:]
    tail = "\n".join(hy.frontend_log().splitlines()[-15:])
    monitors = [(m["name"], m["width"], m["height"], m.get("disabled")) for m in json.loads(hy.run("hyprctl", "monitors", "all", "-j"))]
    hlog = [z for z in (hy.rt / "hyprland.log").read_text(errors="replace").splitlines()
            if any(k in z for k in ("ERR", "WARN", "onitor", "output", "Output"))][-25:]
    raise AssertionError(f"{what}: not {count} window(s) of pid {pid} within {timeout}s\n"
                         f"hyprctl clients: {every}\nhyprctl monitors all: {monitors}\nprocess stderr: {err}\n"
                         f"frontend.log tail:\n{tail}\nhyprland.log (ERR/WARN/monitor/output):\n" + "\n".join(hlog))


def work_area(hy) -> tuple[int, int, int, int]:
    """x, y, width, height of the monitor minus what bars reserve."""
    m = json.loads(hy.run("hyprctl", "monitors", "-j"))[0]
    left, top, right, bottom = m["reserved"]
    return m["x"] + left, m["y"] + top, m["width"] - left - right, m["height"] - top - bottom


def open_window(hy, *args):
    """A second kmixdeck-kde with its own main window; returns (process, its windows once they are mapped).
    Always headless (--probe): a plain second launch hands over to the running instance (KDBusService::Unique) and
    exits without a window of its own."""
    p = subprocess.Popen([str(BIN / "kmixdeck-kde"), *args, "--probe", "mixStrip.needed"], env=hy.env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    wait_clients(hy, "a kmixdeck window from the new process", pid=p.pid, proc=p)
    time.sleep(1.0)   # the first configure is applied; size and position are final
    return p, clients(hy, p.pid)


def close(p):
    p.terminate()
    try:
        p.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
        p.communicate(timeout=5)


def test_hy3_the_main_window_floats_at_its_own_size_centred(hy):
    """The resident window, started by the session after the config (= login autostart)."""
    wait_clients(hy, "kmixdeck's main window", timeout=30)
    time.sleep(1.0)
    [w] = clients(hy)
    x, y, width, height = work_area(hy)
    assert w["floating"], f"main window tiled although kmixdeck.lua is loaded: {w}"
    ww, wh = w["size"]
    assert ww < width and wh < height, f"main window floats at the full work area {width}x{height}: {w['size']}"
    cx, cy = w["at"][0] + ww / 2, w["at"][1] + wh / 2
    assert abs(cx - (x + width / 2)) <= 2 and abs(cy - (y + height / 2)) <= 2, \
        f"main window not centred in the work area {x},{y} {width}x{height}: at {w['at']} size {w['size']}"


def test_hy3_a_dialog_floats_inside_the_screen(hy):
    """Dialogs are their own toplevels with the same class; the rule must not push them to the full screen size."""
    p, _ = open_window(hy, "--open", "duck/voice")
    try:
        wait_clients(hy, "the dialog next to the main window", pid=p.pid, count=2, timeout=10)
        x, y, width, height = work_area(hy)
        for w in clients(hy, p.pid):
            assert w["floating"], f"a kmixdeck window is tiled: {w}"
            assert w["size"][0] < width and w["size"][1] < height, f"window as large as the work area: {w}"
            assert x <= w["at"][0] and y <= w["at"][1] and w["at"][0] + w["size"][0] <= x + width \
                and w["at"][1] + w["size"][1] <= y + height, f"window not inside the work area: {w}"
    finally:
        close(p)


def test_hy3_one_line_turns_the_rule_off(hy):
    """The documented `kmixdeck.rules.window:set_enabled(false)`; `require` returns the module the config loaded."""
    hy.hypr_eval('require("kmixdeck").rules.window:set_enabled(false)')
    try:
        p, wins = open_window(hy)
        try:
            assert wins and not any(w["floating"] for w in wins), f"window floats with the rule disabled: {wins}"
        finally:
            close(p)
    finally:
        hy.hypr_eval('require("kmixdeck").rules.window:set_enabled(true)')


def test_hy3_kmixdeck_bind_reaches_the_portal_action(hy):
    """`kmixdeck.bind(chord, id)` is hl.bind + hl.dsp.global with the app id filled in: a key press mutes."""
    _wait(lambda: "mute-channel-game" in hy.portal_ids(), 20, "the portal binding")
    hy.hypr_eval('require("kmixdeck").bind("CTRL + SHIFT + ALT + F10", "mute-channel-game")')
    before = hy.mute("kmixdeck.channel.game")
    hy.press("F10")
    _wait(lambda: hy.mute("kmixdeck.channel.game") != before, 5, "the game channel's mute changing")
    hy.press("F10")
    _wait(lambda: hy.mute("kmixdeck.channel.game") == before, 5, "the game channel's mute returning")
