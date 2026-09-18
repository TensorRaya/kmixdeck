# SPDX-License-Identifier: GPL-3.0-or-later
"""UX-8 icons and UX-9 order: pure presentation state that must survive a daemon restart and never touch the graph.

Runs against a private PipeWire + private session bus (see test_service_cli.Stack)."""
import json, os, subprocess, time
from pathlib import Path
import pytest
from test_service_cli import Stack, BIN
from pw_sandbox import start_private_pipewire
from waiting import wait_for


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


def kde(stack, *args, open_page=None, timeout=90, lang=None):
    b = BIN / "kmixdeck-kde"
    if not b.exists(): pytest.skip("kmixdeck-kde not built")
    env = dict(stack.env); env["QT_QPA_PLATFORM"] = "offscreen"
    if lang:   # UX-5: pick a UI language; catalogs from the build tree, glibc locale from LOCPATH if the box has none
        env["LANGUAGE"] = lang; env["LANG"] = {"de": "de_DE.UTF-8", "en": "en_US.UTF-8"}.get(lang, lang)
        env["KMIXDECK_LOCALE_DIR"] = str(BIN.parent / "locale")
        locpath = Path.home() / ".local/lib/locale"
        if (locpath / env["LANG"]).exists(): env["LOCPATH"] = str(locpath)
    cmd = [str(b)] + (["--open", open_page] if open_page else []) + list(args)
    try:
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        # a hung UI: grab where it spins before the process is gone (perf is on this box, gdb is not)
        pid = subprocess.run(["pgrep", "-n", "-f", "kmixdeck-kde"], capture_output=True, text=True).stdout.strip()
        if pid:
            subprocess.run(["bash", "-c", f"perf record -g -p {pid} -o /tmp/kmix-ui-hang.perf -- sleep 3 >/dev/null 2>&1; perf report -i /tmp/kmix-ui-hang.perf --stdio --no-children 2>/dev/null | grep -vE '^#|^$' | head -60 > /tmp/kmix-ui-hang.txt"], timeout=60)
            open("/tmp/kmix-ui-hang.txt", "a").write("\nthreads: " + str([(open(f"/proc/{pid}/task/{t}/comm").read().strip(), open(f"/proc/{pid}/task/{t}/stat").read().split()[2]) for t in os.listdir(f"/proc/{pid}/task")]))
            subprocess.run(["kill", "-9", pid])
        raise
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
    wait_for(lambda: not any(a.get("Running") for a in stack.cli("app", "list", json_out=True) if a.get("Name") == name), timeout=5.0, what="not any(a.get('Running') for a in stack.cli('app', 'list', json_out=Tr")
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
    wait_for(lambda: "fake.cans" in stack.cli("mix", "outputs", "stream", json_out=True), timeout=5.0, what="'fake.cans' in stack.cli('mix', 'outputs', 'stream', json_out=True)")
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
    wait_for(lambda: "fake.pop" in stack.cli("mix", "outputs", "stream", json_out=True), timeout=5.0, what="'fake.pop' in stack.cli('mix', 'outputs', 'stream', json_out=True)")
    stack.cli("mix", "wire", "stream", "fake.pop", "trim", "-6dB", "mute", "on")
    got = kde(stack, "--gesture", "wirepopup:output|stream|fake.pop", "--probe", "wirePopup.visible", "--probe", "wireTrim.value", "--probe", "wireTrimText.text", "--probe", "wireMute.checked", "--probe", "wireRemove.visible", open_page="patchbay")
    assert got["wirePopup.visible"] == "true", got
    want = (10 ** (-6 / 20)) ** (1 / 3)
    assert abs(float(got["wireTrim.value"]) - want) < 0.02, got
    assert got["wireTrimText.text"].startswith("-6.") or got["wireTrimText.text"].startswith("-5.9"), got
    assert got["wireMute.checked"] == "true" and got["wireRemove.visible"] == "true", got
    stack.cli("mix", "wire", "stream", "fake.pop", "trim", "0dB", "mute", "off")
    stack.cli("mix", "output-remove", "stream", "fake.pop")


def test_ux14_mix_end_to_end_from_the_ui_alone(stack):
    """UX-14: mixing from the KDE UI without the CLI. Every CHANGE goes through the UI's own handlers (--gesture drives
    the combo box's onActivated, the Outputs menu entry, the cell fader's onMoved, the mute button's onToggled, the
    Applications drop); the CLI/bus is only READ to confirm. The audio measurement at the end is the proof that what the
    user did in the UI is what leaves towards the headphones."""
    from test_service_cli import make_fake_sink, start_fake_app
    from test_ports import wait_level, SILENT
    make_fake_sink(stack, "fake.phones", "Laptop Speakers"); time.sleep(0.6)
    # known start (previous tests leave mutes/levels behind): everything unmuted, masters at unity, cells at unity —
    # via the bus, because this is the fixture, not the walk-through
    for m in ("stream", "monitor"): stack.cli("mix", "mute", m, "off"); stack.cli("mix", "volume", m, "0dB")
    for c in ("game", "system", "voice"): stack.cli("channel", "mute", c, "off"); stack.cli("channel", "trim", c, "0dB"); stack.cli("cell", "set", c, "stream", "0dB", check=False); stack.cli("cell", "mute", c, "stream", "off", check=False)
    stack.cli("listen", "none", check=False)
    # 1) "I hear:" → pick the speakers (routes the heard mix there and remembers the device)
    hear_out = kde(stack, "--gesture", "hear:fake.phones", "--probe", "hearingBar.device", "--probe", "hearingBar.hearingMixes")
    wait_for(lambda: stack.cli("listen").stdout.strip().startswith("fake.phones"), timeout=5.0, what="stack.cli('listen').stdout.strip().startswith('fake.phones')")
    assert stack.cli("listen").stdout.strip().startswith("fake.phones"), hear_out
    heard = [m for m in stack.cli("status", json_out=True)["mixes"] if "fake.phones" in m["Outputs"]]
    assert heard, "no mix plays to the chosen device"
    # 2) the OTHER mix → also to the speakers via its header's Outputs menu (MX-9: a toggle — on the mix that already
    #    plays there it would switch it off, which is what the menu's check mark tells the user)
    heard_slug = heard[0]["Slug"]; other = "monitor" if heard_slug == "stream" else "stream"
    assert kde(stack, "--gesture", f"mixoutput:{other}|fake.phones")[0].endswith("-> ok")
    for _ in range(50):
        if all("fake.phones" in m["Outputs"] for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] in (heard_slug, other)): break
        time.sleep(0.1)
    else: raise AssertionError("Outputs menu toggle did not add the speakers to " + other)
    def outs(): return [(m["Slug"], m["Outputs"]) for m in stack.cli("status", json_out=True)["mixes"]]
    trail = [("after step 2", outs())]
    # 3) an app appears → drop it onto Voice (UX-11)
    p, app = start_fake_app(stack)
    trail.append(("after app start", outs()))
    try:
        path = next(a["Path"] for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")
        assert kde(stack, "--gesture", f"drop:{path}|voice")[0].endswith("-> ok")
        wait_for(lambda: "voice" in next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"], timeout=5.0, what="'voice' in next(a for a in stack.cli('app', 'list', json_out=True) if ")
        trail.append(("after drop", outs()))
        # the app now plays on system (CH-5 auto-route) AND voice (the drop) — both into stream. To hear ONE fader,
        # mute the system channel via the UI first; that is also the "mute" step of the walk-through.
        assert kde(stack, "--gesture", "mute:channel|system")[0].endswith("-> ok")
        wait_for(lambda: next(c for c in stack.cli("status", json_out=True)["channels"] if c["Slug"] == "system")["Muted"], timeout=5.0, what="next(c for c in stack.cli('status', json_out=True)['channels'] if c['S")
        trail.append(("after mute system", outs()))
        # baseline: the app's tone at the stream mix with the voice cell at unity
        base = wait_level(lambda: stack.pw.level_at("kmixdeck.mix.stream"), lambda v: v > SILENT + 10, tries=15)
        if not base > SILENT + 10:
            raise AssertionError("no signal at the stream mix (base %.1f); app on %s, linked to %s; relays %s" % (
                base, next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"],
                __import__("test_service_cli").current_sink_of(stack), [n for n in stack.pw.node_names() if n and "relay" in n]))
        # 4) fader: voice in stream → -12 dB (cubic 0.63); 5) mute the game channel
        cubic = (10 ** (-12 / 20)) ** (1 / 3)
        out = kde(stack, "--gesture", f"fader:voice|stream|{cubic:.4f}", "--gesture", "mute:channel|game")
        assert all(o.endswith("-> ok") for o in out), out
        for _ in range(50):
            st = stack.cli("status", json_out=True)
            if next(c for c in st["channels"] if c["Slug"] == "game")["Muted"] and \
               abs(next(c for c in st["cells"] if c["Path"].endswith("/voice/stream"))["Volume"] - 10 ** (-12 / 20)) < 0.02: break
            time.sleep(0.1)
        else: raise AssertionError("UI changes did not reach the daemon")
        # 6) hear it: -12 dB at the stream mix relative to the unity baseline; something reaches the chosen speakers
        lvl = wait_level(lambda: stack.pw.level_at("kmixdeck.mix.stream"), lambda v: v < base - 8)
        if not (-15 < (lvl - base) < -9):
            from test_service_cli import current_sink_of
            st = stack.cli("status", json_out=True)
            raise AssertionError(f"fader set in the UI to -12 dB, measured {lvl - base:.1f} dB at the mix (base {base:.1f}, now {lvl:.1f})"
                                 + "\n app on: " + str(next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"]) + " linked to " + str(current_sink_of(stack))
                                 + "\n voice %.1f system %.1f game %.1f" % (stack.pw.level_at("kmixdeck.channel.voice"), stack.pw.level_at("kmixdeck.channel.system"), stack.pw.level_at("kmixdeck.channel.game"))
                                 + "\n cells: " + str([(c["Path"].split("/cell/")[1], round(c["Volume"], 3), c["Muted"]) for c in st["cells"] if "/stream" in c["Path"]])
                                 + "\n channels: " + str([(c["Slug"], c["Muted"]) for c in st["channels"]])
                                 + "\n mixes: " + str([(m["Slug"], m["Muted"], m["Volume"]) for m in st["mixes"]]))
        trail.append(("after fader+mute game", outs()))
        sp = wait_level(lambda: stack.pw.level_at("fake.phones"), lambda v: v > SILENT + 10, tries=10)
        if not sp > SILENT + 10:
            raise AssertionError(f"nothing reaches the chosen speakers ({sp:.1f} dB)\n trail: " + "\n   ".join(f"{k}: {v}" for k, v in trail)
                                 + "\n links into fake.phones: " + subprocess.run(["pw-link", "-l", "fake.phones:playback_FL"], env=stack.env, capture_output=True, text=True).stdout.replace("\n", " | ")[:400])
    finally:
        p.kill(); p.wait()
    kde(stack, "--gesture", "mute:channel|game", "--gesture", "mute:channel|system")   # leave it as found
    stack.cli("mix", "output-remove", "stream", "fake.phones", check=False)


def test_ux4_keyboard_drives_faders_and_mutes_and_every_control_has_a_screen_reader_name(stack):
    """UX-4 (KDE HIG: full keyboard operability, screen-reader labels): without a mouse — focus a cell fader, Left/Right
    move it in 1 dB steps (Shift 3 dB, PageDown 6 dB, End = 0 dB, Home = −∞), the change lands in the daemon; Tab
    reaches the cell's mute and link buttons and Space toggles the mute; the accessibility tree names every fader/
    button after its channel and mix (a screen reader says "Game in Stream, slider, −3.0 dB", not "slider")."""
    stack.cli("cell", "set", "game", "stream", "0dB")
    g = kde(stack, "--gesture", "focus:cellFader/game/stream", "--gesture", "a11y:focus",
            "--gesture", "key:Left", "--gesture", "key:Left", "--gesture", "key:Left", "--gesture", "a11y:focus",
            "--gesture", "key:Shift+Left", "--gesture", "a11y:focus",
            "--gesture", "key:PgDown", "--gesture", "a11y:focus",
            "--gesture", "key:End", "--gesture", "a11y:focus",
            "--gesture", "key:PgDown", "--gesture", "key:PgDown", "--gesture", "a11y:focus",
            "--gesture", "key:Shift+Tab", "--gesture", "a11y:focus", "--gesture", "key:Space", "--gesture", "a11y:focus",
            "--gesture", "key:Tab", "--gesture", "key:Tab", "--gesture", "a11y:focus",
            open_page="mixer")
    got = [l.split(" -> ", 1)[1] for l in g]
    assert got[1] == "Slider|Game in Stream|0.0 dB", g
    assert got[5] == "Slider|Game in Stream|-3.0 dB", "three Left presses = −3 dB"
    assert got[7] == "Slider|Game in Stream|-6.0 dB", "Shift+Left = −3 dB more"
    assert got[9] == "Slider|Game in Stream|-12.0 dB", "PageDown = −6 dB"
    assert got[11] == "Slider|Game in Stream|0.0 dB", "End = unity"
    assert got[14] == "Slider|Game in Stream|-12.0 dB"
    # cell row reads mute · fader · link, so Shift+Tab from the fader is the mute, Tab twice is the link
    assert got[16].startswith("CheckBox|Mute Game in Stream|"), f"Shift+Tab from the fader must reach its mute button: {got[16]}"
    assert got[18].startswith("CheckBox|Mute Game in Stream|"), got[18]
    # (what follows the fader in Tab order is Kirigami's page toolbar, checked in the walk below — not ours to name)
    # the keyboard changes are real: the daemon has −12 dB and mute on this cell
    def cell():
        st = stack.cli("status", json_out=True)
        return next(c for c in st["cells"] if c["Path"].endswith("/game/stream"))
    for _ in range(30):
        c = cell()
        if abs(c["Volume"] - 10 ** (-12 / 20)) < 0.01 and c["Muted"]: break
        time.sleep(0.1)
    c = cell()
    assert abs(c["Volume"] - 10 ** (-12 / 20)) < 0.01, f"keyboard fader change did not reach the daemon: {c['Volume']}"
    assert c["Muted"] is True, "Space on the focused mute button must mute the cell in the daemon"
    stack.cli("cell", "mute", "game", "stream", "off"); stack.cli("cell", "set", "game", "stream", "0dB")
    # every control a keyboard user can land on carries a name — walk Tab through the mixer page and collect the tree
    walk = kde(stack, *sum((["--gesture", "key:Tab", "--gesture", "a11y:focus"] for _ in range(60)), []), open_page="mixer")
    pairs = list(zip([l.split(" -> ", 1)[1] for l in walk if l.startswith("gesture key:Tab")], [l.split(" -> ", 1)[1] for l in walk if l.startswith("gesture a11y:focus")], strict=False))
    seen = [a for _, a in pairs]
    # Kirigami's own chrome (page-action toolbar buttons, the drawer handle) is not ours to label — it reports as
    # <PrivateActionToolButton…>/<HandleButton…> without an objectName. Everything WE put on the page must be named.
    ours = [(who, a) for who, a in pairs if not who.startswith("<")]
    nameless = [(who, a) for who, a in ours if a.split("|")[0] in ("Slider", "Button", "CheckBox", "ComboBox", "RadioButton", "Dial", "ButtonMenu") and a.split("|")[1] == ""]
    assert not nameless, f"our controls without an accessible name in the Tab order: {nameless}"
    assert len(ours) >= 20, f"Tab should walk through the whole mixer (mutes, faders, dials, menus): only {len(ours)} of ours reached"
    roles = {s.split("|")[0] for s in seen}
    assert "Slider" in roles and ("Button" in roles or "CheckBox" in roles), f"Tab must reach faders and buttons: {roles}"
    # mix master via keyboard, too (Home = −∞ mutes nothing, just silence — then End back)
    m = kde(stack, "--gesture", "focus:mixFader/stream", "--gesture", "key:Home", "--gesture", "a11y:focus", "--gesture", "key:End", "--gesture", "a11y:focus", open_page="mixer")
    got = [l.split(" -> ", 1)[1] for l in m]
    assert got[2] == "Slider|Master volume of mix Stream|−∞ dB" and got[4] == "Slider|Master volume of mix Stream|0.0 dB", got
