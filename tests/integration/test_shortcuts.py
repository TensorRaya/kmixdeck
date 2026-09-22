"""CT-1: global shortcuts, proven with a REAL key press instead of a D-Bus call.

The older CT-1 tests check two things: the atomic ToggleMute on the bus, and that the KDE
frontend asks for its actions (watched on the private bus). Neither presses a key. Between
`invokeShortcut` and a finger on the keyboard sit three pieces nobody tested: the X server's
key grab, kglobalacceld resolving keycode+modifiers to an action, and the frontend actually
executing it.

This file closes that gap. The chain is real end to end:

    xdotool (XTEST)  ->  Xvfb  ->  kglobalacceld (XGrabKey)  ->  kmixdeck-kde
        ->  org.kmixdeck1.Channel.ToggleMute  ->  kmixdeckd  ->  PipeWire node mute

XTEST events are indistinguishable from hardware at the X protocol level: xev reports
`synthetic NO` for them, i.e. the server does not mark them as sent by SendEvent. What
remains untestable from here is the hardware below X (USB/evdev/libinput) — for that a
human still has to press a key once, see CT-1 in docs/spec/requirements.md.

Why the shortcut is seeded into kglobalshortcutsrc instead of set over D-Bus:
kglobalacceld 6.6.5 CRASHES in setShortcutKeys/setForeignShortcutKeys under a bare Xvfb
(KCrash, "X11 connection broke"), reproducible with both busctl and gdbus, with and without
a running frontend. Seeding the file is also what System Settings effectively produces, and
it exercises the load path that a real session uses after a reboot.
"""
import pathlib
import shutil
import subprocess
import time

import pytest

from pw_sandbox import start_private_pipewire
from test_service_cli import BIN, Stack

KGLOBALACCELD = "/usr/lib/x86_64-linux-gnu/libexec/kglobalacceld"
# Qt::Key_F9 | Shift | Control | Alt — what KGlobalAccel stores as an int
QT_CTRL_SHIFT_ALT_F9 = 0x01000038 | 0x02000000 | 0x04000000 | 0x08000000
XDO_COMBO = "ctrl+shift+alt+F9"
ACTION_ID = ["4", "kmixdeck", "mute-channel-game", "kmixdeck", "Mute channel: Game"]


def _freies_display():
    for n in range(96, 140):
        if not pathlib.Path(f"/tmp/.X{n}-lock").exists():
            return f":{n}"
    pytest.skip("no free X display")


class ShortcutStack:
    """kmixdeckd + Xvfb + kglobalacceld + kmixdeck-kde, all private to this test."""

    def __init__(self, tmp_path):
        self.procs = []
        self.pw = start_private_pipewire()
        self.pw.wait_node("kmixdeck.mix.stream")
        self.stack = Stack(self.pw)
        self.log = tmp_path / "kglobalacceld.log"

        # the shortcut on disk, in the format a real session has after System Settings
        cfg = tmp_path / "xdgconfig"
        if cfg.exists():
            shutil.rmtree(cfg)
        cfg.mkdir(parents=True)
        (cfg / "kglobalshortcutsrc").write_text(
            "[kmixdeck]\n"
            "_k_friendly_name=kmixdeck\n"
            "mute-channel-game=Ctrl+Shift+Alt+F9,none,Mute channel: Game\n"
        )

        self.env = dict(self.stack.env)
        self.env["XDG_CONFIG_HOME"] = str(cfg)
        self.display = _freies_display()
        self._start(["Xvfb", self.display, "-screen", "0", "1280x800x24"])
        time.sleep(1.5)
        self.env["DISPLAY"] = self.display
        self.env["XDG_SESSION_TYPE"] = "x11"

        kenv = dict(self.env)
        kenv["QT_LOGGING_RULES"] = "kf.globalaccel*=true"
        self._start([KGLOBALACCELD], env=kenv, log=self.log)
        time.sleep(2.5)

    def _start(self, cmd, env=None, log=None):
        p = subprocess.Popen(cmd, env=env or self.env, text=True,
                             stdout=open(log, "w") if log else subprocess.DEVNULL,
                             stderr=subprocess.STDOUT)
        self.procs.append(p)
        return p

    def start_frontend(self):
        uenv = dict(self.env)
        uenv["QT_QPA_PLATFORM"] = "xcb"          # a real X client, offscreen has no key path
        self._start([str(BIN / "kmixdeck-kde")], env=uenv)
        time.sleep(6.0)
        # a window that holds the focus; without one the X server drops the keys
        self._start(["xmessage", "-geometry", "200x80+5+5", "focus"])
        time.sleep(1.0)

    def kga(self, *args):
        r = subprocess.run(["busctl", "--user", "call", "org.kde.kglobalaccel", "/kglobalaccel",
                            "org.kde.KGlobalAccel", *args], env=self.env,
                           capture_output=True, text=True)
        return (r.stdout or r.stderr).strip()

    def press(self, combo=XDO_COMBO):
        subprocess.run(["xdotool", "key", "--clearmodifiers", combo], env=self.env, check=False)
        time.sleep(2.0)

    def key_presses_seen(self):
        return self.log.read_text().count("XKeyPress")

    def close(self):
        for p in reversed(self.procs):
            p.terminate()
            try:
                p.wait(timeout=4)
            except subprocess.TimeoutExpired:
                p.kill()
        self.stack.close()
        self.pw.close()


@pytest.fixture(scope="module")
def sc(tmp_path_factory):
    if not pathlib.Path(KGLOBALACCELD).exists():
        pytest.skip("kglobalacceld not installed")
    if not shutil.which("xdotool") or not shutil.which("Xvfb"):
        pytest.skip("Xvfb/xdotool missing")
    if not (BIN / "kmixdeck-kde").exists():
        pytest.skip("kmixdeck-kde not built")
    s = ShortcutStack(tmp_path_factory.mktemp("ct1"))
    yield s
    s.close()


def test_ct1_grab_is_registered_for_the_key(sc):
    """kglobalacceld must own Ctrl+Shift+Alt+F9 for component kmixdeck after loading the file."""
    sc.start_frontend()
    treffer = sc.kga("getGlobalShortcutsByKey", "i", str(QT_CTRL_SHIFT_ALT_F9))
    assert "mute-channel-game" in treffer, f"no grab for the key: {treffer}"
    assert '"kmixdeck"' in treffer, treffer
    gespeichert = sc.kga("shortcutKeys", "as", *ACTION_ID)
    assert str(QT_CTRL_SHIFT_ALT_F9) in gespeichert, f"key not stored: {gespeichert}"


def test_ct1_a_real_key_press_mutes_the_channel(sc):
    """The whole chain: XTEST key -> X -> kglobalacceld -> kmixdeck-kde -> daemon -> PipeWire."""
    vorher = sc.stack.pw.props("kmixdeck.channel.game")["mute"]
    presses_vorher = sc.key_presses_seen()

    sc.press()

    nachher = sc.stack.pw.props("kmixdeck.channel.game")["mute"]
    assert sc.key_presses_seen() > presses_vorher, "kglobalacceld saw no XKeyPress at all"
    assert nachher != vorher, (
        f"key press did not reach the daemon: PipeWire mute stayed {vorher}; "
        f"kglobalacceld saw {sc.key_presses_seen()} presses"
    )
    # and the daemon agrees with PipeWire, not just the graph
    assert sc.stack.cli("channel", "list", json_out=True)[0]["Slug"] == "game"

    sc.press()   # toggle back, so the order of tests in this file does not matter
    assert sc.stack.pw.props("kmixdeck.channel.game")["mute"] == vorher


def test_ct1_an_unassigned_key_does_nothing(sc):
    """Counter-probe: without this the test above would also pass if ANY key muted."""
    vorher = sc.stack.pw.props("kmixdeck.channel.game")["mute"]
    sc.press("ctrl+shift+alt+F10")
    assert sc.stack.pw.props("kmixdeck.channel.game")["mute"] == vorher, \
        "an unassigned key changed the mute state"
