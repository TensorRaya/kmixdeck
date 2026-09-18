"""AR-8/AR-9 — the web bridge speaks only D-Bus, serves one JSON view model over a WebSocket, and refuses everything
that is not an allowlisted org.kmixdeck1 property/method (ADR 0011).

The bridge runs under the system python (Gio bindings); the test client runs under pytest's python (`websockets`)."""
import asyncio, json, os, shutil, subprocess, sys, time
import websockets
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
from test_service_cli import Stack, start_fake_app, stack  # noqa: E402,F401 — the fixture
from chrome_driver import Chrome  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "web" / "kmixdeck-web"
SYSTEM_PYTHON = "/usr/bin/python3"

websockets = pytest.importorskip("websockets")
if not shutil.which(SYSTEM_PYTHON) or subprocess.run([SYSTEM_PYTHON, "-c", "import gi, websockets"], capture_output=True).returncode:
    pytest.skip("web bridge needs python3-gi and python3-websockets in the system python", allow_module_level=True)


class Web:
    def __init__(self, stack, token="test-token", lan=False):
        args = [SYSTEM_PYTHON, str(BRIDGE), "--port", "0", "--token", token] + (["--lan"] if lan else [])
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
        ch = Chrome(web.url, size=(1280, 800))
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
