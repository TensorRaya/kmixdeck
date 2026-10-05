"""CT-1 under Wayland: a key press through KWin mutes the channel on the PipeWire node.

test_shortcuts.py proves the chain on X11 (XTEST -> Xvfb -> kglobalacceld). CT-1 asks for
Wayland, and there the key grab is not a separate daemon: kwin_wayland itself owns
org.kde.kglobalaccel and filters global shortcuts inside its input redirection. This file
runs that path end to end:

    kwin-fake-key  ->  KWin (org_kde_kwin_fake_input)  ->  KWin input redirection
        ->  KWin global-shortcut filter  ->  kmixdeck-kde  ->  Channel.ToggleMute
        ->  kmixdeckd  ->  PipeWire node mute

Why fake_input and not uinput/ydotool or wtype (measured 2026-09-22 on KWin 6.6.6):
- `kwin_wayland --virtual` has no libinput backend, so uinput keys never reach it
  (ydotool returned rc=0, KWin logged nothing, mute stayed False).
- KWin does not offer zwp_virtual_keyboard_v1; wtype fails with "Compositor does not
  support the virtual keyboard protocol".
fake_input is KWin's own interface for synthetic input; its key events enter the same
input redirection as a keyboard, so the shortcut filter cannot tell them apart.

Still NOT covered here: USB/HID, evdev and libinput below KWin — see CT-1 in
docs/spec/requirements.md.

The shortcut is seeded into kglobalshortcutsrc, the same file System Settings writes,
for the same reason as in test_shortcuts.py.
"""
import os
import pathlib
import shutil
import subprocess
import time

import pytest

from pw_sandbox import start_private_pipewire
from test_service_cli import BIN, Stack

KWIN = shutil.which("kwin_wayland")
FAKE_KEY = BIN / "kwin-fake-key"
# evdev keycodes: KEY_LEFTCTRL 29, KEY_LEFTSHIFT 42, KEY_LEFTALT 56, KEY_F9 67, KEY_F10 68
CTRL_SHIFT_ALT_F9 = ["29", "42", "56", "67"]
CTRL_SHIFT_ALT_F10 = ["29", "42", "56", "68"]
NODE = "kmixdeck.channel.game"


def _wait(pred, timeout, what):
    ende = time.monotonic() + timeout
    while time.monotonic() < ende:
        if pred():
            return
        time.sleep(0.1)
    raise AssertionError(f"{what} did not happen within {timeout}s")


class WaylandShortcutStack:
    """kmixdeckd + kwin_wayland (virtual backend) + kmixdeck-kde, all private to this test."""

    def __init__(self, tmp_path):
        self.procs = []
        self.pw = start_private_pipewire()
        self.pw.wait_node("kmixdeck.mix.stream")
        self.stack = Stack(self.pw)
        self.kwin_log = tmp_path / "kwin.log"

        cfg = tmp_path / "xdgconfig"
        cfg.mkdir(parents=True)
        (cfg / "kglobalshortcutsrc").write_text(
            "[kmixdeck]\n"
            "_k_friendly_name=kmixdeck\n"
            "mute-channel-game=Ctrl+Shift+Alt+F9,none,Mute channel: Game\n"
        )

        self.socket = f"kmixdeck-ct1-{os.getpid()}"
        self.env = dict(self.stack.env)
        self.env.pop("DISPLAY", None)
        self.env.update(XDG_CONFIG_HOME=str(cfg), XDG_SESSION_TYPE="wayland",
                        WAYLAND_DISPLAY=self.socket)

        # KWin hides restricted interfaces (fake_input among them) from any client whose .desktop file
        # does not list them in X-KDE-Wayland-Interfaces. A test helper has no .desktop file, so the
        # check is switched off for this private compositor only — it never touches the user's session.
        kenv = dict(self.env, KWIN_COMPOSE="Q", KWIN_WAYLAND_NO_PERMISSION_CHECKS="1",
                    QT_LOGGING_RULES="kf.globalaccel*=true;kwin_scripting=true")
        self._start([KWIN, "--virtual", "--width", "1280", "--height", "800",
                     "--no-lockscreen", "--socket", self.socket], env=kenv, log=self.kwin_log)
        # the sandbox has its own XDG_RUNTIME_DIR; the socket appears there, not in /run/user
        sock = pathlib.Path(self.env["XDG_RUNTIME_DIR"]) / self.socket
        _wait(sock.exists, 20, f"KWin socket {sock}")
        _wait(lambda: "kwin_wayland" in self.kga_owner(), 20, "KWin owning org.kde.kglobalaccel")

    def _start(self, cmd, env=None, log=None):
        p = subprocess.Popen(cmd, env=env or self.env, text=True,
                             stdout=open(log, "w") if log else subprocess.DEVNULL,
                             stderr=subprocess.STDOUT)
        self.procs.append(p)
        return p

    def kga_owner(self) -> str:
        r = subprocess.run(["busctl", "--user", "--no-pager", "list"], env=self.env,
                           capture_output=True, text=True)
        return next((z for z in r.stdout.splitlines() if z.startswith("org.kde.kglobalaccel ")), "")

    def log(self) -> str:
        return self.kwin_log.read_text(errors="replace")

    def start_frontend(self):
        self._start([str(BIN / "kmixdeck-kde")], env=dict(self.env, QT_QPA_PLATFORM="wayland"))
        _wait(lambda: 'Registering key "Ctrl+Alt+Shift+F9" for "kmixdeck" : "mute-channel-game"' in self.log(),
              30, "KWin registering the kmixdeck shortcut")

    def window_ids(self, tmp_path) -> list[str]:
        """desktopFileName|resourceClass of every normal window, read by a KWin script (print() lands in the log)."""
        js = tmp_path / "window-ids.js"
        js.write_text('print("WINDOW-IDS " + workspace.windowList().filter(w => w.normalWindow)'
                      '.map(w => w.desktopFileName + "|" + w.resourceClass).join(","));\n')
        bus = ["busctl", "--user", "--no-pager"]
        r = subprocess.run([*bus, "call", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript", "ss",
                            str(js), f"window-ids-{time.monotonic_ns()}"], env=self.env, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        sid = r.stdout.split()[-1]
        r = subprocess.run([*bus, "call", "org.kde.KWin", f"/Scripting/Script{sid}", "org.kde.kwin.Script", "run"],
                           env=self.env, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        _wait(lambda: "WINDOW-IDS " in self.log(), 5, "the KWin script's output")
        line = [z for z in self.log().splitlines() if "WINDOW-IDS " in z][-1]
        return [x for x in line.split("WINDOW-IDS ", 1)[1].strip().strip('"').split(",") if x]

    def press(self, keys):
        r = subprocess.run([str(FAKE_KEY), *keys], env=self.env, capture_output=True, text=True)
        assert r.returncode == 0, f"kwin-fake-key rc={r.returncode}: {r.stderr.strip()}"

    def mute(self) -> bool:
        return self.pw.props(NODE)["mute"]

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
def wl(tmp_path_factory):
    if not KWIN:
        pytest.skip("kwin_wayland not installed")
    if not FAKE_KEY.exists():
        pytest.skip("kwin-fake-key not built (needs plasma-wayland-protocols + wayland-client)")
    if not (BIN / "kmixdeck-kde").exists():
        pytest.skip("kmixdeck-kde not built")
    s = WaylandShortcutStack(tmp_path_factory.mktemp("ct1wl"))
    s.start_frontend()
    yield s
    s.close()


def test_ct1_wayland_kwin_owns_the_shortcut(wl):
    """Under Wayland the grab is KWin's: it owns org.kde.kglobalaccel and registered our key."""
    assert "kwin_wayland" in wl.kga_owner(), wl.kga_owner()
    assert 'Loading group  "kmixdeck"' in wl.log(), "KWin did not read kglobalshortcutsrc"
    assert 'Registering key "Ctrl+Alt+Shift+F9" for "kmixdeck" : "mute-channel-game"' in wl.log()


def test_ct1_wayland_a_key_press_mutes_the_channel(wl):
    """The whole chain: fake_input key -> KWin -> kmixdeck-kde -> daemon -> PipeWire."""
    vorher = wl.mute()
    wl.press(CTRL_SHIFT_ALT_F9)
    _wait(lambda: wl.mute() != vorher, 5, f"PipeWire mute changing from {vorher}")
    assert 'Processed key "Ctrl+Alt+Shift+F9"' in wl.log(), "KWin did not see the key"

    wl.press(CTRL_SHIFT_ALT_F9)   # toggle back, so the order of tests in this file does not matter
    _wait(lambda: wl.mute() == vorher, 5, f"PipeWire mute returning to {vorher}")


def test_ct1_wayland_an_unassigned_key_does_nothing(wl):
    """Counter-probe: without this the test above would also pass if ANY key muted."""
    vorher = wl.mute()
    wl.press(CTRL_SHIFT_ALT_F10)
    time.sleep(2.0)   # the positive test sees the change in well under 1 s
    assert wl.mute() == vorher, "an unassigned key changed the mute state"


def test_wayland_app_id_is_the_desktop_file_name(wl, tmp_path):
    """The Wayland app id must equal the .desktop basename. Compositors match .desktop files, window rules
    (Hyprland `class`), task-bar icons and portal app ids (GlobalShortcuts) on it. KAboutData::setApplicationData()
    silently replaced it with "org.kde.kmixdeck" (measured in Hyprland 2026-09-28)."""
    _wait(lambda: wl.window_ids(tmp_path), 10, "a kmixdeck window")
    assert wl.window_ids(tmp_path) == ["org.kmixdeck.kmixdeck|org.kmixdeck.kmixdeck"]
