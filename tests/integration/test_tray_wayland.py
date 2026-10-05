"""UX-17 / ADR 0014 HY-2 under Wayland: a tray click shows the popover as a wlr-layer-shell surface.

The popover used to be a Qt.Popup window, i.e. an xdg_popup. On Wayland that needs an input serial from one of the
app's own surfaces, and a click on a tray belongs to the panel, so QtWayland refused ("Failed to create grabbing popup")
and nothing appeared at all. Measured 2026-10-05 under Hyprland 0.56.2 and KWin 6.7.5 alike. This file runs the real
path: KWin (virtual backend, offers zwlr_layer_shell_v1) -> kmixdeck-kde -> StatusNotifierItem.Activate over D-Bus,
the same call Waybar and plasmashell make -> the popover is mapped as a layer surface.

How a layer surface is told apart from a toplevel in KWin's script API: a toplevel carries the app id
(desktopFileName = org.kmixdeck.kmixdeck, see test_wayland_app_id_is_the_desktop_file_name); a layer surface has no app
id, and KWin stacks it above the normal layer.
"""
import json
import subprocess
import time

import pytest

from test_service_cli import BIN
from test_shortcuts_wayland import KWIN, WaylandShortcutStack, _wait

APP_ID = "org.kmixdeck.kmixdeck"
NORMAL_LAYER = 2   # KWin::Layer::NormalLayer; layer-shell "top" lands above it


class TrayStack(WaylandShortcutStack):
    def start_frontend(self):
        self.frontend_log = self.kwin_log.with_name("frontend.log")
        self.frontend = self._start([str(BIN / "kmixdeck-kde")], env=dict(self.env, QT_QPA_PLATFORM="wayland"),
                                    log=self.frontend_log)
        self._sni = ""
        _wait(self.sni_name, 30, "kmixdeck's StatusNotifierItem on the bus")

    def sni_name(self) -> str:
        """The item lives on a SECOND connection of the frontend (KStatusNotifierItemDBus connects its own), known
        only by its unique name; a tray host learns it from RegisterStatusNotifierItem. Without a host in this
        sandbox, find it the way a host could: the frontend's connection that serves /StatusNotifierItem."""
        if self._sni:
            return self._sni
        r = subprocess.run(["busctl", "--user", "--no-pager", "list", "--unique"], env=self.env,
                           capture_output=True, text=True)
        for z in r.stdout.splitlines():
            f = z.split()
            if len(f) > 1 and f[0].startswith(":") and f[1] == str(self.frontend.pid):
                q = subprocess.run(["busctl", "--user", "get-property", f[0], "/StatusNotifierItem",
                                    "org.kde.StatusNotifierItem", "Id"], env=self.env, capture_output=True, text=True)
                if q.returncode == 0:
                    self._sni = f[0]
        return self._sni

    def activate(self, x, y):
        r = subprocess.run(["busctl", "--user", "call", self.sni_name(), "/StatusNotifierItem",
                            "org.kde.StatusNotifierItem", "Activate", "ii", str(x), str(y)],
                           env=self.env, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr

    def windows(self, tmp_path) -> list[dict]:
        js = tmp_path / f"windows-{time.monotonic_ns()}.js"
        js.write_text('print("WINDOWS " + JSON.stringify(workspace.windowList().map(w => ({app: w.desktopFileName,'
                      ' cls: w.resourceClass, layer: w.layer, x: w.frameGeometry.x, y: w.frameGeometry.y,'
                      ' w: w.frameGeometry.width, h: w.frameGeometry.height}))));\n')
        bus = ["busctl", "--user", "--no-pager"]
        before = self.log().count("WINDOWS ")
        r = subprocess.run([*bus, "call", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript", "ss",
                            str(js), js.stem], env=self.env, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        subprocess.run([*bus, "call", "org.kde.KWin", f"/Scripting/Script{r.stdout.split()[-1]}", "org.kde.kwin.Script",
                        "run"], env=self.env, check=True, capture_output=True)
        _wait(lambda: self.log().count("WINDOWS ") > before, 5, "the KWin script's output")
        line = [z for z in self.log().splitlines() if "WINDOWS " in z][-1]
        return json.loads(line.split("WINDOWS ", 1)[1].strip().strip('"').replace('\\"', '"'))


@pytest.fixture(scope="module")
def tray(tmp_path_factory):
    if not KWIN:
        pytest.skip("kwin_wayland not installed")
    if not (BIN / "kmixdeck-kde").exists():
        pytest.skip("kmixdeck-kde not built")
    s = TrayStack(tmp_path_factory.mktemp("ux17wl"))
    s.start_frontend()
    yield s
    s.close()


def _popovers(windows):
    return [w for w in windows if w["app"] != APP_ID and w["cls"] == "kmixdeck-kde"]


def test_ux17_wayland_tray_click_shows_the_popover_as_a_layer_surface(tray, tmp_path):
    _wait(lambda: any(w["app"] == APP_ID for w in tray.windows(tmp_path)), 15, "the main window")
    assert _popovers(tray.windows(tmp_path)) == []
    tray.activate(1180, 10)   # a click on a bar at the top right
    _wait(lambda: _popovers(tray.windows(tmp_path)), 5, "a popover after the tray click")
    windows = tray.windows(tmp_path)
    [pop] = _popovers(windows)
    assert pop["layer"] > NORMAL_LAYER, f"popover stacked like a normal window: {pop}"
    assert [w["app"] for w in windows].count(APP_ID) == 1, f"the click opened a second toplevel: {windows}"
    # anchored to the top edge with the gap, centred on the click and kept on screen (1280 wide)
    assert pop["y"] == 8 and pop["x"] + pop["w"] <= 1280 and pop["x"] <= 1180 <= pop["x"] + pop["w"], pop
    assert "Failed to create grabbing popup" not in tray.frontend_log.read_text(errors="replace")

    time.sleep(1.0)   # past the double-click interval, or the second click would open the window instead
    tray.activate(1180, 10)
    _wait(lambda: not _popovers(tray.windows(tmp_path)), 5, "the popover closing on the second click")


def test_ux17_wayland_bottom_bar_anchors_the_popover_to_the_bottom(tray, tmp_path):
    tray.activate(100, 795)   # a click on a bar at the bottom left (screen 1280x800)
    _wait(lambda: _popovers(tray.windows(tmp_path)), 5, "a popover after the tray click")
    [pop] = _popovers(tray.windows(tmp_path))
    assert pop["y"] + pop["h"] == 800 - 8 and pop["x"] == 0, pop
    time.sleep(1.0)
    tray.activate(100, 795)
    _wait(lambda: not _popovers(tray.windows(tmp_path)), 5, "the popover closing on the second click")
