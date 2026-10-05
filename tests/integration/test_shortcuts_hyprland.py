"""ADR 0014 HY-1: under Hyprland a real key press reaches kmixdeck through the GlobalShortcuts portal.

Hyprland has no org.kde.kglobalaccel, so KGlobalAccel registrations go nowhere (measured in phase 0: every
shortcut dead). The frontend binds the same action ids through org.freedesktop.portal.GlobalShortcuts instead, and
the user binds a key to them with `hl.dsp.global("org.kmixdeck.kmixdeck:<id>")`. This file runs that chain:

    wtype (zwp_virtual_keyboard_v1)  ->  Hyprland bind  ->  xdg-desktop-portal-hyprland
        ->  xdg-desktop-portal GlobalShortcuts.Activated  ->  kmixdeck-kde  ->  Channel.ToggleMute
        ->  kmixdeckd  ->  PipeWire node mute

The session (private runtime dir, buses, portals, PipeWire, Waybar, the daemon and the frontend) comes from
tools/hyprland-sandbox.py; nothing touches the logged-in user's session.

resolve_binds_by_sym: wtype uploads its own keymap with ad-hoc keycodes, and Hyprland resolves a bind's key by
keycode through ITS layout unless told to use the keysym. Measured 2026-10-05 (Hyprland 0.56.2): a bind on F8 stayed
silent for `wtype -k F8` with the default and fired with resolve_binds_by_sym = true. A real keyboard needs neither.
"""
import json
import os
import pathlib
import shlex
import shutil
import signal
import subprocess
import sys
import time

import pytest

from test_service_cli import BIN
from test_shortcuts_wayland import _wait

REPO = pathlib.Path(__file__).resolve().parents[2]
SANDBOX = REPO / "tools" / "hyprland-sandbox.py"
XDPH_PORTAL = pathlib.Path("/usr/share/xdg-desktop-portal/portals/hyprland.portal")
NEEDED = ("Hyprland", "hyprctl", "kwin_wayland", "waybar", "mako", "wtype", "pw-dump")
APP = "org.kmixdeck.kmixdeck"
CHORD = ["-M", "ctrl", "-M", "shift", "-M", "alt"]
UNCHORD = ["-m", "alt", "-m", "shift", "-m", "ctrl"]


class HyprlandSession:
    def __init__(self, tmp_path):
        ready = tmp_path / "ready"
        self.log = tmp_path / "sandbox.log"
        self.proc = subprocess.Popen([sys.executable, str(SANDBOX), "--session", "hyprland", "--max-seconds", "900",
                                      "--ready-file", str(ready)],
                                     stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        try:
            _wait(lambda: ready.exists() or self.proc.poll() is not None, 90, "the Hyprland sandbox")
            assert ready.exists(), f"sandbox exited: {self.log.read_text()}"
        except BaseException:
            self.close()
            raise
        self.rt = pathlib.Path(ready.read_text())
        self.env = dict(os.environ)
        for line in (self.rt / "env.sh").read_text().splitlines():
            for word in shlex.split(line.removeprefix("export ")):
                k, _, v = word.partition("=")
                self.env[k] = v
        self.env.pop("DISPLAY", None)
        self.binds = {}

    def bind(self, chord: str, action: str):
        """Once per chord: Hyprland keeps every hl.bind, so a second identical bind fires twice and two toggles cancel
        out (measured: the last test here bound F9 again and the mute did not change)."""
        if self.binds.get(chord) == action:
            return
        assert chord not in self.binds, f"{chord} already bound to {self.binds[chord]}"
        self.hypr_eval(f'hl.bind("{chord}", hl.dsp.global("{APP}:{action}"))')
        self.binds[chord] = action

    def run(self, *cmd) -> str:
        r = subprocess.run(list(cmd), env=self.env, capture_output=True, text=True, timeout=30)
        assert r.returncode == 0, f"{cmd}: rc={r.returncode} {r.stderr.strip()}"
        return r.stdout

    def hypr_eval(self, lua: str):
        assert self.run("hyprctl", "eval", lua).strip() == "ok", lua

    def portal_ids(self) -> list[str]:
        return [line.split(" -> ")[0].removeprefix(APP + ":").strip()
                for line in self.run("hyprctl", "globalshortcuts").splitlines() if line.startswith(APP + ":")]

    def press(self, key: str):
        self.run("wtype", *CHORD, "-k", key, *UNCHORD)

    def mute(self, node: str) -> bool:
        for o in json.loads(self.run("pw-dump")):
            info = o.get("info") or {}
            if (info.get("props") or {}).get("node.name") == node:
                for p in (info.get("params") or {}).get("Props") or []:
                    if "mute" in p:
                        return p["mute"]
        raise AssertionError(f"{node} has no mute property")

    def frontend_log(self) -> str:
        return (self.rt / "frontend.log").read_text(errors="replace")

    def close(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.proc.kill()


@pytest.fixture(scope="module")
def hy(tmp_path_factory):
    missing = [t for t in NEEDED if not shutil.which(t)]
    if missing:
        pytest.skip(f"needs {', '.join(missing)}")
    if not XDPH_PORTAL.exists():
        pytest.skip("xdg-desktop-portal-hyprland not installed")
    if not (BIN / "kmixdeck-kde").exists():
        pytest.skip("kmixdeck-kde not built")
    s = HyprlandSession(tmp_path_factory.mktemp("hy1"))
    s.hypr_eval("hl.config({ input = { resolve_binds_by_sym = true } })")
    yield s
    s.close()


def test_hy1_every_action_is_bound_through_the_portal(hy):
    """Same ids as the KGlobalAccel path (test_ct1_kde_frontend_registers_global_shortcuts), so a bind survives."""
    _wait(lambda: "mute-channel-game" in hy.portal_ids(), 20, "kmixdeck's ids in `hyprctl globalshortcuts`")
    ids = hy.portal_ids()
    for name in ("mute-channel-game", "mute-channel-system", "mute-channel-voice", "mute-mix-monitor", "mute-mix-stream",
                 "volume-up-mix-monitor", "volume-down-mix-stream", "listen-next-mix"):
        assert name in ids, f"{name} not bound through the portal: {ids}"
    assert "org.kde.kglobalaccel" not in hy.run("busctl", "--user", "--no-pager", "list"), \
        "something owns org.kde.kglobalaccel; this would not be a Hyprland session"


def test_hy1_a_real_key_press_mutes_the_channel(hy):
    node = "kmixdeck.channel.game"
    _wait(lambda: "mute-channel-game" in hy.portal_ids(), 20, "the portal binding")
    before = hy.mute(node)
    if "CTRL + SHIFT + ALT + F9" not in hy.binds:
        hy.press("F9")   # counter-probe: no bind yet, so the key must not reach the action
        time.sleep(1.5)
        assert hy.mute(node) == before, "the key changed the mute without a bind"

    hy.bind("CTRL + SHIFT + ALT + F9", "mute-channel-game")
    hy.press("F9")
    _wait(lambda: hy.mute(node) != before, 5, f"PipeWire mute changing from {before}")
    hy.press("F9")   # toggle back, so the order of tests in this file does not matter
    _wait(lambda: hy.mute(node) == before, 5, f"PipeWire mute returning to {before}")


def test_hy1_an_unbound_key_does_nothing(hy):
    node = "kmixdeck.channel.game"
    before = hy.mute(node)
    hy.press("F10")
    time.sleep(2.0)   # the positive test sees the change in well under 1 s
    assert hy.mute(node) == before, "an unbound key changed the mute state"


def test_hy1_a_channel_added_later_gets_its_shortcut_too(hy):
    """BindShortcuts works once per portal session; a new channel needs a new session, the old binds must survive."""
    _wait(lambda: "mute-channel-game" in hy.portal_ids(), 20, "the portal binding")
    hy.run(str(BIN / "kmixdeck"), "channel", "add", "Chat")
    _wait(lambda: "mute-channel-chat" in hy.portal_ids(), 10, "mute-channel-chat in `hyprctl globalshortcuts`")

    hy.bind("CTRL + SHIFT + ALT + F11", "mute-channel-chat")
    before = hy.mute("kmixdeck.channel.chat")
    hy.press("F11")
    _wait(lambda: hy.mute("kmixdeck.channel.chat") != before, 5, "the new channel's mute changing")

    hy.bind("CTRL + SHIFT + ALT + F9", "mute-channel-game")
    game = hy.mute("kmixdeck.channel.game")
    hy.press("F9")
    _wait(lambda: hy.mute("kmixdeck.channel.game") != game, 5, "the old channel's shortcut after the rebind")
    hy.press("F9")
    _wait(lambda: hy.mute("kmixdeck.channel.game") == game, 5, "the old channel's mute returning")
    failures = [z for z in hy.frontend_log().splitlines() if "through the portal" in z and "failed" in z]
    assert not failures, failures
