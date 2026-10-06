#!/usr/bin/env python3
"""A private Hyprland session with kmixdeck in it, for measuring the KDE frontend outside Plasma (ADR 0014).

Usage: tools/hyprland-sandbox.py [--session hyprland|kwin] [--max-seconds N] [--theme NAME] [--ready-file PATH]

Starts, all private to this process and removed on exit (SIGTERM/SIGINT or after --max-seconds):
PipeWire + WirePlumber (tests/integration/pw_sandbox.py), a session bus, a host compositor, Hyprland as its client,
xdg-desktop-portal(-hyprland) via bus activation, mako, Waybar with only the `tray` module, kmixdeckd and
kmixdeck-kde from build/bin. Prints `READY <runtime dir>` and writes `<runtime dir>/env.sh`; source it to talk to the
session (`hyprctl clients`, `hyprctl layers`, `hyprctl globalshortcuts`, `busctl --user ...`, `grim`).

Why a host compositor: Hyprland needs an output. Run by anyone but the seat owner, aquamarine's DRM backend has no
session and its headless backend no allocator (`CBackend::create() failed`, Hyprland 0.56.2). weston's headless
backend offers wl_compositor v5, Hyprland binds v6. `kwin_wayland --virtual` works; it gets its OWN session bus,
because on the shared one it owns org.kde.kglobalaccel, which a real Hyprland session does not have.

`--session kwin` is the control group: the same stack with KWin as THE compositor (on the session bus, so it owns
org.kde.kglobalaccel like in Plasma) and no Hyprland. Waybar stands in for plasmashell as the tray host.

Nothing touches the logged-in user's session: own XDG_RUNTIME_DIR, own buses, no seat, no systemd export.
"""
import argparse
import json
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BIN = REPO / "build" / "bin"
sys.path.insert(0, str(REPO / "tests" / "integration"))
from pw_sandbox import start_private_pipewire  # noqa: E402

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("--session", choices=("hyprland", "kwin"), default="hyprland")
ap.add_argument("--max-seconds", type=int, default=1800)
ap.add_argument("--theme", default="", help="QT_QPA_PLATFORMTHEME for kmixdeck-kde (default: unset)")
ap.add_argument("--ready-file", type=Path, help="also write the runtime dir here once the session is up")
args = ap.parse_args()

procs = []
pw = None


def stop(*_):
    for p in reversed(procs):
        p.terminate()
    for p in procs:
        try:
            p.wait(timeout=4)
        except subprocess.TimeoutExpired:
            p.kill()
    if pw is not None:
        pw.close()
    sys.exit(0)


signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)


def start(cmd, env, log):
    p = subprocess.Popen(cmd, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    procs.append(p)
    return p


def wait(pred, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return
        time.sleep(0.2)
    print(f"timeout waiting for {what}", flush=True)
    stop()


def session_bus(env, address):
    p = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address=1", f"--address={address}"],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, env=env)
    procs.append(p)
    return p.stdout.readline().strip()


# Hyprland puts its IPC sockets at $XDG_RUNTIME_DIR/hypr/<40 hex>_<10 digits>_<up to 10 digits>/.socket2.sock, 82
# characters after the runtime dir, and a unix socket path has at most 107 (sun_path is 108 bytes with the NUL). Past
# that Hyprland logs "Socket2 path is too long. IPC will not work." and the sandbox times out waiting for the socket.
# Measured 2026-10-06 (Hyprland 0.56.2): /tmp/kmixdeck-pw-XXXXXXXX gave exactly 107, so a TMPDIR of /var/tmp broke it.
# So the runtime dir comes from a short fixed parent, whatever TMPDIR says, and the budget is checked before starting.
HYPR_SOCKET_TAIL = len("/hypr/") + 40 + 1 + 10 + 1 + 10 + len("/.socket2.sock")
runtime = Path(tempfile.mkdtemp(prefix="kmx-hy-", dir="/tmp"))
if len(str(runtime)) + HYPR_SOCKET_TAIL > 107:
    sys.exit(f"runtime dir {runtime} is too long for Hyprland's IPC socket ({len(str(runtime)) + HYPR_SOCKET_TAIL} > 107)")
pw = start_private_pipewire(runtime_dir=runtime)
pw.wait_node("kmixdeck.mix.stream")
rt = pw.runtime_dir
env = dict(pw.env)
for k in ("WAYLAND_DISPLAY", "DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE", "QT_QPA_PLATFORMTHEME", "XDG_CURRENT_DESKTOP"):
    env.pop(k, None)
# xdg-desktop-portal resolves a host app id through its .desktop file ("App info not found" otherwise); an installed
# kmixdeck has it in /usr/share/applications, this one from data/ via a private applications dir. Set BEFORE the bus
# starts: bus-activated services (the portals) inherit the bus's environment, not ours.
apps = rt / "share" / "applications"
apps.mkdir(parents=True)
(apps / "org.kmixdeck.kmixdeck.desktop").write_text(
    (REPO / "data" / "org.kmixdeck.kmixdeck.desktop").read_text().replace("Exec=kmixdeck-kde", f"Exec={BIN}/kmixdeck-kde"))
env["XDG_DATA_DIRS"] = f"{rt / 'share'}:" + env.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share")
env["DBUS_SESSION_BUS_ADDRESS"] = session_bus(env, f"unix:dir={rt}")

if args.session == "kwin":
    kenv = dict(env, KWIN_COMPOSE="O2ES", XDG_CURRENT_DESKTOP="KDE", QT_LOGGING_RULES="kwin_scripting=true")
else:
    kenv = dict(env, KWIN_COMPOSE="O2ES", DBUS_SESSION_BUS_ADDRESS=session_bus(env, f"unix:path={rt}/host-bus"))
start(["kwin_wayland", "--virtual", "--width", "1920", "--height", "1080", "--no-lockscreen", "--socket", "wl-host"],
      kenv, rt / "host-kwin.log")
wait(lambda: (rt / "wl-host").exists(), 20, "host compositor socket")

if args.session == "kwin":
    wl, sig, desktop = "wl-host", "", "KDE"
else:
    wl, sig, desktop = None, None, "Hyprland"
cfg = rt / "hyprland.lua"
cfg.write_text("""
hl.monitor({ output = "", mode = "1920x1080@60", position = "0x0", scale = 1 })
hl.config({
    animations = { enabled = false },
    misc = { disable_hyprland_logo = true, disable_splash_rendering = true },
    ecosystem = { no_update_news = true, no_donation_nag = true },
    xwayland = { enabled = false },
})
""")


def hypr_socket():
    return next((p.name for p in rt.iterdir() if p.name.startswith("wayland-") and not p.name.endswith(".lock")), None)


if args.session == "hyprland":
    henv = dict(env, WAYLAND_DISPLAY="wl-host", HYPRLAND_NO_SD_VARS="1", HYPRLAND_NO_RT="1",
                HYPRLAND_NO_CRASHREPORTER="1", XDG_CURRENT_DESKTOP="Hyprland", XDG_SESSION_TYPE="wayland")
    start(["Hyprland", "--config", str(cfg)], henv, rt / "hyprland.log")
    hypr = rt / "hypr"
    wait(lambda: hypr.is_dir() and any((d / ".socket.sock").exists() for d in hypr.iterdir()), 30,
         "Hyprland IPC socket")
    sig = next(d.name for d in hypr.iterdir() if (d / ".socket.sock").exists())
    wait(hypr_socket, 10, "Hyprland Wayland socket")
    wl = hypr_socket()
    # IPC and Wayland socket up does not mean Hyprland has its output yet. 10 of 109 starts (2026-10-06, Blade,
    # aquamarine 0.15.1) had none: `hyprctl monitors all` empty, every window got configure 0x0 and was never mapped
    # (ADR 0014, open points). Ready means a monitor.
    t0 = time.monotonic()
    henv_ipc = dict(henv, HYPRLAND_INSTANCE_SIGNATURE=sig, XDG_RUNTIME_DIR=str(rt))

    def monitors():
        r = subprocess.run(["hyprctl", "monitors", "-j"], env=henv_ipc, capture_output=True, text=True)
        return json.loads(r.stdout or "[]") if r.returncode == 0 else []
    end = time.monotonic() + 20
    while not monitors() and time.monotonic() < end:
        time.sleep(0.1)
    if not monitors():
        keep = Path("/var/tmp") / f"hyprland-no-monitor-{rt.name}"
        keep.mkdir(exist_ok=True)
        shutil.copy(rt / "hyprland.log", keep / "stdout.log")
        shutil.copy(rt / "host-kwin.log", keep / "host-kwin.log")
        for f in (rt / "hypr" / sig).glob("hyprland.log*"):   # Hyprland's own log once stdout logging is off
            shutil.copy(f, keep / f"hypr-{f.name}")
        r = subprocess.run(["hyprctl", "monitors", "all", "-j"], env=henv_ipc, capture_output=True, text=True)
        (keep / "monitors-all.json").write_text(r.stdout + r.stderr)
        print(f"timeout waiting for a Hyprland monitor; logs kept in {keep}", flush=True)
        stop()
    print(f"monitor after {time.monotonic() - t0:.2f}s", flush=True)

denv = dict(env, WAYLAND_DISPLAY=wl, XDG_CURRENT_DESKTOP=desktop, XDG_SESSION_TYPE="wayland", QT_QPA_PLATFORM="wayland")
if sig:
    denv["HYPRLAND_INSTANCE_SIGNATURE"] = sig
# the bus activates the portals with ITS environment, so it needs the compositor's
subprocess.run(["dbus-update-activation-environment", "WAYLAND_DISPLAY=" + wl, "HYPRLAND_INSTANCE_SIGNATURE=" + (sig or ""),
                "XDG_CURRENT_DESKTOP=" + desktop, "XDG_SESSION_TYPE=wayland"], env=denv, check=True)

start(["mako"], denv, rt / "mako.log")
(rt / "waybar.json").write_text('{"layer":"top","position":"top","height":30,"modules-right":["tray"],'
                                '"tray":{"spacing":8}}')
start(["waybar", "--config", str(rt / "waybar.json")], denv, rt / "waybar.log")
start([str(BIN / "kmixdeckd")], denv, rt / "kmixdeckd.log")
wait(lambda: subprocess.run([str(BIN / "kmixdeck"), "status"], env=denv, capture_output=True).returncode == 0, 20,
     "kmixdeckd on the private bus")
# Qt logs to journald, not stderr, when stderr is not a terminal (measured: the log stayed at 0 bytes without this)
fenv = dict(denv, QT_FORCE_STDERR_LOGGING="1",
            QT_LOGGING_RULES="kf.globalaccel*=true;kf.statusnotifieritem*=true;qt.qpa.wayland*=true;kf.notifications*=true")
if args.theme:
    fenv["QT_QPA_PLATFORMTHEME"] = args.theme
start([str(BIN / "kmixdeck-kde")], fenv, rt / "frontend.log")

with open(rt / "env.sh", "w") as f:
    for k in ("XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS", "PIPEWIRE_RUNTIME_DIR", "PIPEWIRE_REMOTE",
              "XDG_CONFIG_HOME", "XDG_STATE_HOME", "PIPEWIRE_CONFIG_DIR"):
        if k in env:
            f.write(f"export {k}='{env[k]}'\n")
    f.write(f"export WAYLAND_DISPLAY='{wl}' HYPRLAND_INSTANCE_SIGNATURE='{sig or ''}' XDG_CURRENT_DESKTOP={desktop}\n")
if args.ready_file:
    args.ready_file.write_text(str(rt))
print("READY", rt, flush=True)

time.sleep(args.max_seconds)
stop()
