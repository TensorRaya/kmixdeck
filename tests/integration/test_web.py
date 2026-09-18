# SPDX-FileCopyrightText: 2026 Raya Elena Solano
# SPDX-License-Identifier: GPL-3.0-or-later
"""AR-8/AR-9 — the web bridge speaks only D-Bus, serves one JSON view model over a WebSocket, and refuses everything
that is not an allowlisted org.kmixdeck1 property/method (ADR 0011).

The bridge runs under the system python (Gio bindings); the test client runs under pytest's python (`websockets`)."""
import asyncio, json, os, shutil, subprocess, sys, time
import websockets
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
from test_service_cli import Stack, start_fake_app, stack, make_fake_sink, make_fake_source  # noqa: E402,F401 — the fixture
from chrome_driver import Chrome  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "web" / "kmixdeck-web"
SYSTEM_PYTHON = "/usr/bin/python3"

websockets = pytest.importorskip("websockets")
if not shutil.which(SYSTEM_PYTHON) or subprocess.run([SYSTEM_PYTHON, "-c", "import gi, websockets"], capture_output=True).returncode:
    pytest.skip("web bridge needs python3-gi and python3-websockets in the system python", allow_module_level=True)


class Web:
    def __init__(self, stack, token="test-token", lan=False, port=0):
        args = [SYSTEM_PYTHON, str(BRIDGE), "--port", str(port), "--token", token] + (["--lan"] if lan else [])
        self.proc = subprocess.Popen(args, env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        lines = []
        for _ in range(5):                                  # --lan prints its warning first; never .read() (blocks)
            lines.append(self.proc.stdout.readline())
            if "http://" in lines[-1]: break
        line = lines[-1]
        assert "http://" in line, f"bridge did not start: {lines}"
        self.url = line.split("kmixdeck-web: ", 1)[1].split("#")[0].strip()
        self.port = int(self.url.rsplit(":", 1)[1].rstrip("/"))
        self.token = token

    def close(self):
        self.proc.terminate()
        try: self.proc.wait(5)
        except subprocess.TimeoutExpired: self.proc.kill()

    async def connect(self, token=None):
        ws = await websockets.connect(f"ws://127.0.0.1:{self.port}/ws")
        await ws.send(json.dumps({"op": "hello", "token": self.token if token is None else token}))
        return ws


def wait_for(pred, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if pred(): return True
        except Exception: pass
        time.sleep(0.1)
    return False


async def recv_until(ws, pred, timeout=5.0):
    deadline = time.monotonic() + timeout
    while True:
        m = json.loads(await asyncio.wait_for(ws.recv(), max(0.1, deadline - time.monotonic())))
        if pred(m): return m


def test_ar8_bridge_serves_the_object_tree_and_writes_land_in_the_daemon(stack):
    web = Web(stack)
    try:
        async def go():
            ws = await web.connect()
            snap = await recv_until(ws, lambda m: m["op"] == "snapshot")
            st = snap["state"]
            assert st["connected"] is True and "Version" in st["root"]
            by_iface = {}
            for path, obj in st["objects"].items(): by_iface.setdefault(obj["interface"], []).append(path)
            # AR-9: the same names the KDE window reads — channels, mixes, cells, straight from the D-Bus tree
            assert {"org.kmixdeck1.Channel", "org.kmixdeck1.Mix", "org.kmixdeck1.Cell"} <= set(by_iface)
            slugs = sorted(st["objects"][p]["Slug"] for p in by_iface["org.kmixdeck1.Channel"])
            assert slugs == sorted(c["Slug"] for c in stack.cli("status", json_out=True)["channels"])
            game = next(p for p in by_iface["org.kmixdeck1.Channel"] if st["objects"][p]["Slug"] == "game")
            # write through the bridge, read back through the CLI and as a merge patch
            await ws.send(json.dumps({"op": "set", "path": game, "property": "Trim", "value": 0.25, "id": 1}))
            res = await recv_until(ws, lambda m: m["op"] in ("result", "error") and m.get("id") == 1)
            assert res["op"] == "result", res
            patch = await recv_until(ws, lambda m: m["op"] == "patch" and game in m["state"].get("objects", {}))
            assert abs(patch["state"]["objects"][game]["Trim"] - 0.25) < 1e-6
            assert abs(next(c for c in stack.cli("status", json_out=True)["channels"] if c["Slug"] == "game")["Trim"] - 0.25) < 1e-6
            # a change made elsewhere (CLI) reaches the browser as a patch — one truth, both directions
            stack.cli("channel", "mute", "game", "on")
            patch = await recv_until(ws, lambda m: m["op"] == "patch" and m["state"].get("objects", {}).get(game, {}).get("Muted") is True)
            # a method call returns the new object's path and the object arrives as a patch (ObjectManager InterfacesAdded)
            await ws.send(json.dumps({"op": "call", "path": "/org/kmixdeck1", "method": "AddChannel", "args": ["Radio"], "id": 2}))
            res = await recv_until(ws, lambda m: m.get("id") == 2)
            assert res["op"] == "result" and res["value"].endswith("/channel/radio"), res
            added = await recv_until(ws, lambda m: m["op"] == "patch" and res["value"] in m["state"].get("objects", {}))
            assert added["state"]["objects"][res["value"]]["Slug"] == "radio"
            # …and its removal is an RFC 7386 null
            stack.cli("channel", "remove", "radio")
            await recv_until(ws, lambda m: m["op"] == "patch" and m["state"].get("objects", {}).get(res["value"], "x") is None)
            await ws.close()
        asyncio.run(go())
    finally:
        web.close()
        stack.cli("channel", "mute", "game", "off"); stack.cli("channel", "trim", "game", "0dB", check=False)


def test_ar8_bridge_refuses_what_the_allowlist_does_not_name(stack):
    web = Web(stack)
    try:
        async def go():
            ws = await web.connect()
            st = (await recv_until(ws, lambda m: m["op"] == "snapshot"))["state"]
            game = next(p for p, o in st["objects"].items() if o.get("Slug") == "game" and o["interface"] == "org.kmixdeck1.Channel")
            cases = [
                ({"op": "set", "path": game, "property": "Slug", "value": "x", "id": 1}, "read-only"),
                ({"op": "call", "path": game, "method": "Introspect", "args": [], "id": 2}, "Introspect"),
                ({"op": "call", "path": "/org/freedesktop/DBus", "method": "ListNames", "args": [], "id": 3}, ""),
                ({"op": "set", "path": game, "property": "Trim", "value": "loud", "id": 4}, ""),
                ({"op": "call", "path": "/org/kmixdeck1", "method": "AddChannel", "args": [], "id": 5}, "takes 1"),
                ({"op": "shell", "cmd": "id", "id": 6}, "unknown op"),
                # field missing: answered, connection lives on. A hand-written latency probe sent "prop" instead of
                # "property" and took the whole handler down (2026-09-18) — a stale tab must never kill the bridge.
                ({"op": "set", "path": game, "prop": "Trim", "value": 0.5, "id": 7}, "malformed"),
            ]
            for msg, needle in cases:
                await ws.send(json.dumps(msg))
                res = await recv_until(ws, lambda m, i=msg["id"]: m.get("id") == i)
                assert res["op"] == "error", f"{msg} must be refused, got {res}"
                assert needle in res["message"], (msg, res)
            # the bridge is still alive and still correct after all of that
            await ws.send(json.dumps({"op": "set", "path": game, "property": "Trim", "value": 1.0, "id": 7}))
            assert (await recv_until(ws, lambda m: m.get("id") == 7))["op"] == "result"
            await ws.close()
        asyncio.run(go())
    finally:
        web.close()


def test_ar8_lan_mode_needs_the_token_localhost_does_not(stack):
    web = Web(stack, token="s3cret", lan=True)
    try:
        async def go():
            ws = await web.connect(token="wrong")
            with pytest.raises(websockets.exceptions.ConnectionClosed) as e:
                await asyncio.wait_for(ws.recv(), 5)
            assert e.value.rcvd.code == 4001
            ws = await web.connect(token="s3cret")
            assert (await recv_until(ws, lambda m: m["op"] == "snapshot"))["state"]["connected"] is True
            await ws.close()
        asyncio.run(go())
    finally:
        web.close()
    # localhost without --lan: no token at all
    web = Web(stack, token="")
    try:
        async def go2():
            ws = await web.connect(token="")
            assert (await recv_until(ws, lambda m: m["op"] == "snapshot"))["state"]["connected"] is True
            await ws.close()
        asyncio.run(go2())
    finally:
        web.close()


def test_ar8_meters_arrive_over_the_websocket_at_the_daemon_rate(stack):
    web = Web(stack)
    p, app = start_fake_app(stack)
    try:
        async def go():
            ws = await web.connect()
            await recv_until(ws, lambda m: m["op"] == "snapshot")
            frames = []; t0 = None
            while len(frames) < 25:
                m = await recv_until(ws, lambda m: m["op"] == "meters" and m["peaks"], timeout=8)
                if t0 is None: t0 = time.monotonic()
                frames.append(m)
            dt = time.monotonic() - t0
            rate = 24 / dt if dt else 0.0
            assert 15 < rate < 40, f"expected ~25 Hz, got {rate:.1f} Hz"
            keys = set(frames[-1]["peaks"])
            assert any(k.startswith("app/") for k in keys), keys
            assert any(k.startswith("cell/") for k in keys) and any(k.startswith("mix/") for k in keys), keys
            assert max(v for k, v in frames[-1]["peaks"].items() if k.startswith("app/")) > 0.01, "the fake app plays a tone"
            await ws.close()
        asyncio.run(go())
    finally:
        p.kill(); p.wait(); web.close()


def test_ar8_static_files_are_served_and_the_tree_is_jailed(stack):
    import urllib.request, urllib.error
    web = Web(stack)
    try:
        html = urllib.request.urlopen(web.url, timeout=5).read().decode()
        assert "<title>kmixdeck" in html
        with pytest.raises(urllib.error.HTTPError) as e:
            urllib.request.urlopen(web.url + "../kmixdeck-web", timeout=5)
        assert e.value.code == 404
        with pytest.raises(urllib.error.HTTPError) as e:
            urllib.request.urlopen(web.url + "nope.js", timeout=5)
        assert e.value.code == 404
    finally:
        web.close()


def test_fx8_web_ui_edits_the_chain_the_window_and_cli_see(stack):
    """FX in the browser (rule 2): the drawer shows the daemon's catalog, adds a gate over WebSocket, the CLI sees the
    chain and the fx nodes exist; a CLI-side change shows up in the drawer; a live control write goes through
    SetFxControl without rebuilding the node; the FX button on the header lights up while a chain is active."""
    web = Web(stack, token="")   # the token handshake has its own test; here the browser must get in
    try:
        with Chrome(web.url, size=(1280, 800)) as ch:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected && document.querySelector('[data-probe=\"channelFx/voice\"]')", 15)
            assert ch.eval("document.querySelector('[data-probe=\"channelFx/voice\"]').classList.contains('on')") is False
            ch.click("channelFx/voice")
            ch.wait("!!document.querySelector('[data-probe=\"fxPanel\"]') && !document.getElementById('fx-drawer').hidden", 5)
            options = ch.eval("[...document.querySelectorAll('[data-probe=\"fxAddType\"] option')].map(o => o.value).filter(Boolean)")
            assert "gate" in options and "limiter" in options, options
            # add a gate from the drawer
            ch.eval("(() => { const s = document.querySelector('[data-probe=\"fxAddType\"]'); s.value = 'gate'; s.dispatchEvent(new Event('change')); })()")
            ch.wait("document.querySelector('[data-probe=\"fxList\"]')?.dataset.value === '1'", 8)
            got = json.loads(stack.cli("fx", "get", "channel", "voice").stdout)
            assert got["enabled"] and got["chain"][0]["type"] == "gate", got
            assert stack.pw.wait_nodes(["kmixdeck.fx.voice"], timeout=8)
            assert ch.eval("document.querySelector('[data-probe=\"channelFx/voice\"]').classList.contains('on')") is True
            # live control: slider input → SetFxControl, node ids unchanged
            before = stack.pw.node_id("kmixdeck.fx.voice")
            ch.eval("(() => { const s = document.querySelector('[data-probe^=\"fxParam/0/threshold\"]'); s.value = -12; s.dispatchEvent(new Event('input')); s.dispatchEvent(new Event('change')); })()")
            def saved():
                g = json.loads(stack.cli("fx", "get", "channel", "voice").stdout)
                return abs(g["chain"][0]["params"].get("threshold", 99) - (-12)) < 0.01
            assert wait_for(saved, 8), stack.cli("fx", "get", "channel", "voice").stdout
            assert stack.pw.node_id("kmixdeck.fx.voice") == before, "a control tweak must not rebuild the chain"
            # the other direction: CLI puts a limiter in front → the drawer shows two cards, limiter first
            chain = json.loads(stack.cli("fx", "get", "channel", "voice").stdout)
            chain["chain"].insert(0, {"type": "limiter", "enabled": True, "params": {"limit": -6}})
            assert stack.cli("fx", "set", "channel", "voice", json.dumps(chain)).returncode == 0
            ch.wait("document.querySelector('[data-probe=\"fxList\"]')?.dataset.value === '2' && document.querySelector('[data-probe=\"fxCard/0\"]')?.dataset.value === 'limiter'", 8)
            # bypass from the drawer (FX-5): enabled=false, chain kept
            ch.eval("(() => { const c = document.querySelector('[data-probe=\"fxEnabled\"]'); c.checked = false; c.dispatchEvent(new Event('change')); })()")
            assert wait_for(lambda: json.loads(stack.cli("fx", "get", "channel", "voice").stdout)["enabled"] is False, 8)
            assert len(json.loads(stack.cli("fx", "get", "channel", "voice").stdout)["chain"]) == 2
            assert ch.eval("document.querySelector('[data-probe=\"channelFx/voice\"]').classList.contains('on')") is False
            ch.shot("/tmp/web-fx.png")
            ch.close()
    finally:
        stack.cli("fx", "set", "channel", "voice", "{}", check=False)
        web.close()


# ---------------------------------------------------------------------------------------------------------------------
# Rule 2 for the rest of the mixer page: what UX-14 / UX-11 / DV-14 / CT-4 prove for the window, proven for the browser.
# Every step drives the UI element (click / pointer event) and reads the truth back from the CLI — never from the DOM alone.
# JS snippets use single quotes only, so they can live inside plain Python double-quoted strings.

def _q(probe): return "document.querySelector('[data-probe=\"%s\"]')" % probe
def _click(ch, probe): ch.eval(_q(probe) + ".click()", False)

def _menu_click(ch, regex):
    ch.wait("document.querySelector('.popover.menu')", 5)
    ch.eval("[...document.querySelectorAll('.popover.menu button')].find(b => /%s/.test(b.textContent)).click()" % regex, False)

SLIDE_JS = """
(() => {
  const f = FADER; const t = f.querySelector('.track').getBoundingClientRect();
  const y = t.top + t.height / 2, x0 = t.left + 14 + (t.width - 28) * (f._pos ?? 1), x1 = t.left + 14 + (t.width - 28) * CUBIC;
  const ev = (type, x) => f.dispatchEvent(new PointerEvent(type, { bubbles: true, clientX: x, clientY: y, pointerId: 1, button: 0, isPrimary: true }));
  ev('pointerdown', x0); ev('pointermove', x1); ev('pointerup', x1);
})()
"""
def _slide(ch, probe, cubic):
    """Move a web fader like a finger: pointerdown on the handle, pointermove to the target x, pointerup."""
    ch.eval(SLIDE_JS.replace("FADER", _q(probe)).replace("CUBIC", str(cubic)), False)

DRAG_JS = """
(() => {
  const a = document.querySelector('.jack.out[data-card=\"dev/SRC\"]');
  const b = document.querySelector('.jack.in[data-card=\"ch/voice\"][data-pos=\"L\"]');
  const ra = a.getBoundingClientRect(), rb = b.getBoundingClientRect();
  const ev = (t, el, r) => el.dispatchEvent(new PointerEvent(t, { bubbles: true, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2, pointerId: 1, button: 0, isPrimary: true }));
  ev('pointerdown', a, ra); ev('pointermove', b, rb); ev('pointerup', b, rb);
})()
"""

def _status(stack): return stack.cli("status", json_out=True)
def _mix(stack, slug): return next(m for m in _status(stack)["mixes"] if m["Slug"] == slug)
def _cell(stack, ch, mix): return next(c for c in _status(stack)["cells"] if c["Path"].endswith("/%s/%s" % (ch, mix)))
def _tab(ch, view): ch.eval("document.querySelector('#tabs [data-view=\"%s\"]').click()" % view, False)


def test_ux14_web_mix_end_to_end_from_the_browser_alone(stack):
    """A whole mix from the browser: add → rename → output → cell fader by pointer → cell mute → link → undo → remove.
    Each step is checked at the CLI, as UX-14 does for the window."""
    web = Web(stack, token="")
    sink = "fake.web.out"; make_fake_sink(stack, sink, "Web Speakers")
    try:
        with Chrome(web.url, size=(1280, 800)) as ch:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected && " + _q("addMix"), 15)
            ch.answer_dialogs("Web Mix"); _click(ch, "addMix")
            assert wait_for(lambda: any(m["Name"] == "Web Mix" for m in _status(stack)["mixes"]), 8), "AddMix from the browser"
            slug = next(m["Slug"] for m in _status(stack)["mixes"] if m["Name"] == "Web Mix")
            ch.wait(_q("mixHeader/" + slug), 8)
            ch.answer_dialogs("Renamed Mix"); _click(ch, "mixName/" + slug)
            assert wait_for(lambda: _mix(stack, slug)["Name"] == "Renamed Mix", 8), "rename via the name button"
            _click(ch, "mixOutput/" + slug)
            ch.wait(_q("outMenu/" + slug), 5)
            ch.eval("[...document.querySelectorAll('[data-probe=\"outMenu/%s\"] label')].find(l => l.textContent.includes('Web Speakers')).querySelector('input').click()" % slug, False)
            assert wait_for(lambda: sink in _mix(stack, slug)["Outputs"], 8), "AddOutput from the picker"
            stack.pw.wait_nodes(["kmixdeck.link.game." + slug])
            ch.wait(_q("cellFader/game/" + slug), 8); time.sleep(0.3)
            _slide(ch, "cellFader/game/" + slug, 0.5)
            assert wait_for(lambda: abs(_cell(stack, "game", slug)["Volume"] - 0.125) < 0.02, 8), "pointer drag → cubic 0.5 = lin 0.125, got %s" % _cell(stack, "game", slug)["Volume"]
            _click(ch, "cellMute/game/" + slug)
            assert wait_for(lambda: _cell(stack, "game", slug)["Muted"] is True, 8), "cell mute"
            _click(ch, "cellLink/system/" + slug); _menu_click(ch, "Stream$")
            assert wait_for(lambda: _cell(stack, "system", slug)["Follows"].endswith("/stream"), 8), "link via the cell menu"
            _click(ch, "cellLink/system/" + slug)                                        # linked → one click unlinks
            assert wait_for(lambda: _cell(stack, "system", slug)["Follows"] in ("", "/"), 8), "unlink via the same button"
            # CH-9: removing a mix is undoable — from the ⋮ menu, then the toolbar's undo button brings it back
            ch.answer_dialogs(None, confirm=True); _click(ch, "mixMenuButton/" + slug); _menu_click(ch, "^Remove mix")
            assert wait_for(lambda: all(m["Slug"] != slug for m in _status(stack)["mixes"]), 8), "RemoveMix from the browser"
            ch.wait("!document.getElementById('undo').hidden && /Renamed Mix/.test(document.getElementById('undo').textContent)", 5)
            ch.eval("document.getElementById('undo').click()", False)
            assert wait_for(lambda: any(m["Slug"] == slug for m in _status(stack)["mixes"]), 8), "Undo from the toolbar restores the mix"
            assert sink in _mix(stack, slug)["Outputs"], "undo restores the mix with its outputs"
            stack.cli("mix", "remove", slug)
            ch.close()
    finally:
        web.close()


def test_ux11_web_app_chip_assigns_and_unassigns(stack):
    """UX-11 in the browser: the Apps tab shows the running app; a channel chip adds that channel (accumulates, CH-12),
    a second tap removes it — checked at the daemon like the window's drop test."""
    web = Web(stack, token="")
    p, app = start_fake_app(stack)
    try:
        with Chrome(web.url, size=(1280, 800)) as ch:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected", 15)
            _tab(ch, "apps")
            ch.wait("document.querySelector('[data-probe^=\"appRunning/\"]')", 10)
            nid = ch.eval("document.querySelector('[data-probe^=\"appRunning/\"]').dataset.probe.split('/')[1]", False)
            assert ch.eval(_q("appRunning/" + nid) + ".dataset.value", False) == "true"
            def chans(): return next(a for a in stack.cli("app", "list", json_out=True) if a["Name"] == "FakeGame")["Channels"]
            before = chans(); assert "voice" not in before
            _click(ch, "appTo/%s/voice" % nid)
            assert wait_for(lambda: "voice" in chans() and all(c in chans() for c in before), 8), "chip must ADD voice and keep %s: %s" % (before, chans())
            ch.wait(_q("appTo/%s/voice" % nid) + ".classList.contains('on')", 5)
            _click(ch, "appTo/%s/voice" % nid)
            assert wait_for(lambda: "voice" not in chans(), 8), "second tap removes the channel again"
            ch.close()
    finally:
        p.kill(); p.wait(); web.close()


def test_dv24_web_patchbay_wire_menu_and_drag(stack):
    """DV-14/DV-24 in the browser: one wire per link; a wire's menu mutes/removes at the daemon; dragging from a device
    jack onto a channel jack calls AddInput."""
    web = Web(stack, token="")
    src = "fake.web.mic"; make_fake_source(stack, src, "Web Mic")
    try:
        with Chrome(web.url, size=(1280, 900)) as ch:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected", 15)
            _tab(ch, "patchbay")
            ch.wait("document.querySelectorAll('.wire').length > 0", 10)
            n0 = ch.eval("document.querySelectorAll('.wire').length", False)
            ch.eval(_q("wire/ch/game/L/mix/stream/L") + ".dispatchEvent(new MouseEvent('click', {bubbles: true}))", False)
            _menu_click(ch, "^Mute in this mix")
            assert wait_for(lambda: _cell(stack, "game", "stream")["Muted"] is True, 8), "wire menu mute"
            ch.wait("document.querySelector('.jack.out[data-card=\"dev/%s\"]')" % src, 8)
            ch.eval(DRAG_JS.replace("SRC", src), False)
            def inputs(): return next(c for c in _status(stack)["channels"] if c["Slug"] == "voice")["Inputs"]
            assert wait_for(lambda: any(src in i for i in inputs()), 8), "dragging a wire must call AddInput: %s" % inputs()
            # wait for THAT wire, not for the count: the count may rise from another patch a render tick earlier (flaked 1/12 in the full suite)
            find = "[...document.querySelectorAll('.wire.input')].find(w => w.dataset.probe.includes('dev/%s') && w.dataset.probe.includes('ch/voice'))" % src
            ch.wait(find, 8)
            assert ch.eval("document.querySelectorAll('.wire').length", False) > n0
            # find + click in ONE eval: the patchbay re-renders on every patch and the node found a tick ago may be gone
            r = ch.eval("(() => { const w = " + find + "; if (!w) return 'gone: ' + JSON.stringify({ wires: [...document.querySelectorAll('.wire')].map(x => x.dataset.probe), inputs: window.kmixdeck.state.objects['/org/kmixdeck1/channel/voice'].Inputs }); w.dispatchEvent(new MouseEvent('click', {bubbles: true})); return 'ok'; })()", False)
            assert r == 'ok', r
            _menu_click(ch, "^Remove wire")
            assert wait_for(lambda: not any(src in i for i in inputs()), 8), "wire menu remove"
            ch.close()
    finally:
        stack.cli("cell", "unmute", "game", "stream", check=False)
        web.close()


def test_ar8_web_reconnects_after_the_bridge_restarts(stack):
    """The browser survives a bridge restart: shows 'disconnected', reconnects with backoff, and the state is fresh
    (a change made while it was away is visible). What a phone does when the wifi drops for a moment."""
    web = Web(stack, token="")
    try:
        with Chrome(web.url, size=(1280, 800)) as ch:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected", 15)
            port = web.port
            web.proc.terminate(); web.proc.wait(5)
            ch.wait("!window.kmixdeck.state.connected && " + _q("disconnected"), 10)
            stack.cli("mix", "mute", "stream")                       # change while the browser is blind
            web2 = Web(stack, token="", port=port)
            try:
                ch.wait("window.kmixdeck.state.connected", 20)
                ch.wait(_q("mixMute/stream") + ".getAttribute('aria-pressed') === 'true'", 8)
            finally:
                web2.close()
            ch.close()
    finally:
        stack.cli("mix", "unmute", "stream", check=False)
        web.close()


def test_ct7_web_export_downloads_and_import_round_trips_and_refuses_garbage(stack):
    """CT-7 in the browser: Export returns the layout document (checked against `kmixdeck export`); Import of that
    document with a changed mix name lands in the daemon; garbage is refused with a toast and the layout is untouched."""
    web = Web(stack, token="")
    try:
        with Chrome(web.url, size=(1280, 800)) as ch:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected && " + _q("exportLayout"), 15)
            # Export: intercept the download (headless chrome has no download dir) — read the blob the button builds
            ch.eval("(() => { window.__dl = null; const o = URL.createObjectURL; URL.createObjectURL = (b) => { window.__dl = b; return o(b); }; })()", False)
            _click(ch, "exportLayout")
            ch.wait("window.__dl", 8)
            got = json.loads(ch.eval("window.__dl.text()", True))
            ref = json.loads(stack.cli("export").stdout)
            assert got.keys() == ref.keys() and got["mixes"] == ref["mixes"], "browser export must be the daemon's export"
            # Import: same document with a renamed mix, fed through the file input (no OS dialog in headless)
            doc = json.loads(json.dumps(ref)); doc["mixes"][0]["name"] = "Imported Web Mix"; slug = doc["mixes"][0]["slug"]
            ch.answer_dialogs(None, confirm=True)
            ch.eval("""(() => { const inp = document.createElement('input'); inp.type = 'file'; window.__inp = inp;
                const o = document.createElement; document.createElement = function (t) { const e = o.call(document, t); if (t === 'input' && !window.__hooked) { window.__hooked = true; return inp; } return e; }; })()""", False)
            _click(ch, "importLayout")
            ch.eval("""(() => { const f = new File([%s], 'layout.json', { type: 'application/json' }); const dt = new DataTransfer(); dt.items.add(f);
                window.__inp.files = dt.files; window.__inp.dispatchEvent(new Event('change')); })()""" % json.dumps(json.dumps(doc)), False)
            assert wait_for(lambda: _mix(stack, slug)["Name"] == "Imported Web Mix", 8), "Import from the browser"
            # garbage: refused, nothing changes
            before = stack.cli("export").stdout
            ch.eval("""(() => { const f = new File(['{"this is": "not a layout"'], 'bad.json'); const dt = new DataTransfer(); dt.items.add(f);
                window.__inp.files = dt.files; window.__inp.dispatchEvent(new Event('change')); })()""", False)
            ch.wait("/refused/.test(document.getElementById('toast').textContent) && document.getElementById('toast').classList.contains('error')", 8)
            assert stack.cli("export").stdout == before, "garbage import must leave the layout untouched"
    finally:
        web.close()


def test_dv1_web_input_picker_and_ux8_icon_from_the_browser(stack):
    """ChannelHeader.qml's input line and Icon… in the browser: the source line opens a picker listing the daemon's
    InputDevices; ticking calls AddInput, unticking RemoveInput; the header then names the device. Icon… writes UX-8's
    Icon property on channel and mix — all checked at the CLI."""
    web = Web(stack, token="")
    src = "fake.web.line"; make_fake_source(stack, src, "Web Line In")
    try:
        with Chrome(web.url, size=(1280, 800)) as ch:
            ch.wait("window.kmixdeck && window.kmixdeck.state.connected && " + _q("channelSource/voice"), 15)
            ch.wait("Object.keys(window.kmixdeck.state.root.InputDevices || {}).includes('%s')" % src, 8)
            _click(ch, "channelSource/voice")
            ch.wait(_q("inMenu/voice"), 5)
            ch.eval("[...document.querySelectorAll('[data-probe=\"inMenu/voice\"] label')].find(l => l.textContent.includes('Web Line In')).querySelector('input').click()", False)
            def inputs(): return next(c for c in _status(stack)["channels"] if c["Slug"] == "voice")["Inputs"]
            assert wait_for(lambda: any(i.startswith(src) for i in inputs()), 8), "tick → AddInput: %s" % inputs()
            ch.wait(_q("channelSource/voice") + ".textContent.includes('Web Line In')", 8)
            _click(ch, "channelSource/voice"); ch.wait(_q("inMenu/voice"), 5)
            ch.eval("[...document.querySelectorAll('[data-probe=\"inMenu/voice\"] label')].find(l => l.textContent.includes('Web Line In')).querySelector('input').click()", False)
            assert wait_for(lambda: not any(i.startswith(src) for i in inputs()), 8), "untick → RemoveInput"
            # UX-8 icon via the ⋮ menus
            ch.answer_dialogs("audio-input-microphone"); _click(ch, "channelMenuButton/voice"); _menu_click(ch, "^Icon")
            assert wait_for(lambda: next(c for c in _status(stack)["channels"] if c["Slug"] == "voice")["Icon"] == "audio-input-microphone", 8), "channel Icon"
            ch.answer_dialogs("audio-headphones"); _click(ch, "mixMenuButton/stream"); _menu_click(ch, "^Icon")
            assert wait_for(lambda: _mix(stack, "stream")["Icon"] == "audio-headphones", 8), "mix Icon"
            ch.answer_dialogs(""); _click(ch, "channelMenuButton/voice"); _menu_click(ch, "^Icon")
            assert wait_for(lambda: next(c for c in _status(stack)["channels"] if c["Slug"] == "voice")["Icon"] == "", 8), "empty → default"
    finally:
        stack.cli("mix", "icon", "stream", "none", check=False)
        web.close()


def test_bp1_websocket_refuses_a_foreign_origin_and_takes_its_own(stack):
    """A page from another site must not get a WebSocket to the bridge (it would drive the mixer from any tab on
    localhost, where no token is required). Same-origin and non-browser clients (no Origin header) are fine."""
    import websockets
    web = Web(stack, token="")
    try:
        async def attempt(origin):
            kw = {"additional_headers": {"Origin": origin}} if origin else {}
            try:
                async with websockets.connect(f"ws://127.0.0.1:{web.port}/ws", **kw) as ws:
                    await ws.send(json.dumps({"op": "hello"})); return json.loads(await asyncio.wait_for(ws.recv(), 5))["op"]
            except websockets.exceptions.InvalidStatus as e:
                return e.response.status_code
        assert asyncio.run(attempt("http://evil.example")) == 403
        assert asyncio.run(attempt("null")) == 403                     # file:// pages send "null"
        assert asyncio.run(attempt(f"http://127.0.0.1:{web.port}")) == "snapshot"
        assert asyncio.run(attempt(None)) == "snapshot"                 # scripts, the tests themselves
    finally:
        web.close()
