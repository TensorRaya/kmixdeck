# SPDX-License-Identifier: GPL-3.0-or-later
"""CT-3: the Stream Deck plugin (streamdeck/me.kmixdeck.sdPlugin) against the real daemon, driven by a fake OpenAction
server that behaves like OpenDeck: it launches the plugin with -port/-pluginUUID/-registerEvent/-info, waits for the
registration event, then sends willAppear / keyDown / dialRotate / didReceiveSettings and records what the plugin
sends back (setTitle, setState, showOk, showAlert, sendToPropertyInspector).

What is proven: a key press changes the daemon (measured over the CLI), the key's title/state follow the daemon
(also when the change comes from somewhere else — CLI standing in for window/tray), the property inspector gets the
live list of controls, and a bad target alerts instead of crashing. No hardware needed; the protocol is the contract."""
import asyncio, json, os, subprocess, sys, time
from pathlib import Path

import pytest
from test_service_cli import BIN, Stack, make_fake_sink
from pw_sandbox import start_private_pipewire

PLUGIN_DIR = Path(__file__).resolve().parents[2] / "streamdeck" / "me.kmixdeck.sdPlugin"
websockets = pytest.importorskip("websockets")


class FakeOpenAction:
    """The bits of an OpenAction server a plugin can observe."""
    def __init__(self, env):
        self.env = env; self.inbox = asyncio.Queue(); self.ws = None; self.proc = None; self.server = None; self.port = None

    async def __aenter__(self):
        self.server = await websockets.serve(self._on_connect, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]
        info = {"application": {"font": "", "language": "en", "platform": "linux", "platformVersion": "", "version": "OpenDeck 2.5.0 (fake, kmixdeck tests)"},
                "plugin": {"uuid": "me.kmixdeck", "version": "0.1.0"}, "devices": [{"id": "fake0", "name": "Fake Deck", "size": {"rows": 2, "columns": 3}}]}
        env = dict(self.env); env["KMIXDECK_CLI"] = str(BIN / "kmixdeck")
        self.proc = subprocess.Popen([str(PLUGIN_DIR / "run.sh"), "-port", str(self.port), "-pluginUUID", "plugin-ctx-1", "-registerEvent", "registerPlugin", "-info", json.dumps(info)],
                                     env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        reg = await asyncio.wait_for(self.inbox.get(), 15)
        assert reg == {"event": "registerPlugin", "uuid": "plugin-ctx-1"}, reg
        return self

    async def __aexit__(self, *exc):
        if self.proc: self.proc.terminate()
        try: self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired: self.proc.kill()
        self.server.close(); await self.server.wait_closed()

    async def _on_connect(self, ws):
        self.ws = ws
        async for raw in ws: await self.inbox.put(json.loads(raw))

    async def send(self, **msg): await self.ws.send(json.dumps(msg))

    async def expect(self, event, context=None, timeout=8, where=None):
        """Next message of that event (for that context) — everything else is kept aside, not lost."""
        deadline = time.time() + timeout; kept = []
        try:
            while time.time() < deadline:
                try: m = await asyncio.wait_for(self.inbox.get(), max(0.05, deadline - time.time()))
                except asyncio.TimeoutError: break
                if m.get("event") == event and (context is None or m.get("context") == context) and (where is None or where(m)): return m
                kept.append(m)
        finally:
            for m in kept: self.inbox.put_nowait(m)
        raise AssertionError(f"no {event} for {context} within {timeout}s; got {[ (m.get('event'), m.get('context'), m.get('payload')) for m in kept ][-6:]}\nplugin stderr: {self._stderr()}")

    def _stderr(self):
        try: return open(PLUGIN_DIR / "plugin.log").read()[-800:]
        except OSError: return ""

    async def appear(self, ctx, action, settings, controller="Keypad"):
        await self.send(event="willAppear", action=action, context=ctx, device="fake0",
                        payload={"settings": settings, "coordinates": {"row": 0, "column": 0}, "controller": controller, "state": 0, "isInMultiAction": False})

    async def key(self, ctx, action, settings):
        for ev in ("keyDown", "keyUp"):
            await self.send(event=ev, action=action, context=ctx, device="fake0", payload={"settings": settings, "coordinates": {"row": 0, "column": 0}, "state": 0, "isInMultiAction": False})

    async def dial(self, ctx, action, settings, ticks):
        await self.send(event="dialRotate", action=action, context=ctx, device="fake0",
                        payload={"settings": settings, "coordinates": {"row": 0, "column": 0}, "controller": "Encoder", "ticks": ticks, "pressed": False})

    async def dial_press(self, ctx, action, settings):
        await self.send(event="dialDown", action=action, context=ctx, device="fake0", payload={"settings": settings, "coordinates": {"row": 0, "column": 0}, "controller": "Encoder"})
        await self.send(event="dialUp", action=action, context=ctx, device="fake0", payload={"settings": settings, "coordinates": {"row": 0, "column": 0}, "controller": "Encoder"})


def cell(stack, ch, mx): return stack.cli("cell", "get", ch, mx, json_out=True)


def test_ct3_manifest_has_every_field_opendeck_requires():
    """OpenDeck's serde structs (plugins/manifest.rs, shared.rs) reject a manifest with a missing required field — the
    plugin then silently never shows up. tools/check-openaction-manifest.py mirrors those rules."""
    r = subprocess.run([sys.executable, str(PLUGIN_DIR.parents[1] / "tools" / "check-openaction-manifest.py"), str(PLUGIN_DIR / "manifest.json")], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    m = json.loads((PLUGIN_DIR / "manifest.json").read_text())
    assert (PLUGIN_DIR / m["CodePath"]).exists() and os.access(PLUGIN_DIR / m["CodePath"], os.X_OK)
    for a in m["Actions"]:
        assert a["UUID"].startswith("me.kmixdeck."), a["UUID"]
        for st in a["States"]:
            assert any((PLUGIN_DIR / (st["Image"] + ext)).exists() for ext in (".svg", ".png", "@2x.png")), st["Image"]
    assert (PLUGIN_DIR / m["PropertyInspectorPath"]).exists()


def test_ct3_stream_deck_plugin_drives_and_mirrors_the_daemon():
    pw = start_private_pipewire(); pw.wait_node("kmixdeck.mix.stream"); stack = Stack(pw)
    try:
        make_fake_sink(stack, "fake.cans", "Fake Headphones"); time.sleep(0.6)
        asyncio.run(_run(stack))
    finally:
        stack.close(); pw.close()


async def _run(stack):
    MUTE, FADER, LISTEN = "me.kmixdeck.mute", "me.kmixdeck.fader", "me.kmixdeck.listen"
    mute_s = {"target": "cell:game/stream"}
    fader_s = {"target": "mix:stream", "step_db": 3, "direction": "down"}
    listen_s = {"device": "fake.cans"}
    async with FakeOpenAction(stack.env) as deck:
        # --- willAppear: the key shows the daemon's current state (unmuted, 0 dB)
        await deck.appear("k1", MUTE, mute_s)
        t = await deck.expect("setTitle", "k1"); assert t["payload"]["title"] == "game→stream\n+0 dB", t
        s = await deck.expect("setState", "k1"); assert s["payload"]["state"] == 0
        # --- keyDown mutes IN THE DAEMON, and the key shows it
        await deck.key("k1", MUTE, mute_s)
        await deck.expect("showOk", "k1")
        assert cell(stack, "game", "stream")["Muted"] is True, "the deck's mute key must mute the cell in the daemon"
        s = await deck.expect("setState", "k1", where=lambda m: m["payload"]["state"] == 1)
        t = await deck.expect("setTitle", "k1", where=lambda m: "MUTE" in m["payload"]["title"])
        # --- a change from ELSEWHERE (CLI = stand-in for window/tray) reaches the deck via `kmixdeck watch`
        stack.cli("cell", "mute", "game", "stream", "off")
        s = await deck.expect("setState", "k1", where=lambda m: m["payload"]["state"] == 0)
        assert cell(stack, "game", "stream")["Muted"] is False
        # --- fader key: −3 dB per press on the stream mix master; title follows; floor at −60 not below
        await deck.appear("k2", FADER, fader_s)
        await deck.expect("setTitle", "k2", where=lambda m: m["payload"]["title"] == "Stream\n+0 dB")
        await deck.key("k2", FADER, fader_s)
        t = await deck.expect("setTitle", "k2", where=lambda m: m["payload"]["title"] == "Stream\n-3 dB")
        v = next(m for m in stack.cli("mix", "list", json_out=True) if m["Slug"] == "stream")["Volume"]
        assert abs(20 * __import__("math").log10(v) + 3) < 0.3, f"mix master should be -3 dB, is {v}"
        # --- the same action on an encoder: 2 detents up = +2 dB (a third of the step each), press = mute
        await deck.appear("d1", FADER, fader_s, controller="Encoder")
        await deck.expect("setTitle", "d1")
        await deck.dial("d1", FADER, fader_s, ticks=2)
        await deck.expect("setTitle", "d1", where=lambda m: m["payload"]["title"] == "Stream\n-1 dB")
        await deck.dial_press("d1", FADER, fader_s)
        await deck.expect("setState", "d1", where=lambda m: m["payload"]["state"] == 1)
        assert next(m for m in stack.cli("mix", "list", json_out=True) if m["Slug"] == "stream")["Muted"] is True
        # both faces of the same mix (key k2 and dial d1) show the mute — one daemon, one truth
        await deck.expect("setState", "k2", where=lambda m: m["payload"]["state"] == 1)
        stack.cli("mix", "mute", "stream", "off")
        # --- listen action: press → the daemon's listening device changes (UX-2)
        await deck.appear("k3", LISTEN, listen_s)
        await deck.expect("setState", "k3", where=lambda m: m["payload"]["state"] == 0)
        await deck.key("k3", LISTEN, listen_s)
        await deck.expect("showOk", "k3")
        assert stack.cli("listen", json_out=True)["device"] == "fake.cans"
        await deck.expect("setState", "k3", where=lambda m: m["payload"]["state"] == 1)
        await deck.expect("setTitle", "k3", where=lambda m: "Fake Headphones" in m["payload"]["title"])
        # --- property inspector asks for choices → live lists from the daemon
        await deck.send(event="propertyInspectorDidAppear", action=MUTE, context="k1", device="fake0")
        c = await deck.expect("sendToPropertyInspector", "k1")
        values = {t["value"] for t in c["payload"]["targets"]}
        assert {"channel:game", "channel:voice", "mix:stream", "mix:monitor", "cell:game/stream"} <= values, values
        assert {"value": "fake.cans", "label": "Fake Headphones"} in c["payload"]["devices"]
        # --- settings changed in the inspector → the key re-reads (now a channel: its trim)
        stack.cli("channel", "trim", "voice", "-6dB")
        await deck.send(event="didReceiveSettings", action=MUTE, context="k1", device="fake0", payload={"settings": {"target": "channel:voice"}, "coordinates": {"row": 0, "column": 0}, "isInMultiAction": False})
        await deck.expect("setTitle", "k1", where=lambda m: m["payload"]["title"] == "Voice\n-6 dB")
        # --- a target that does not exist: alert, no crash, plugin still answers
        await deck.appear("k9", MUTE, {"target": "channel:nope"})
        await deck.expect("showAlert", "k9")
        await deck.expect("setTitle", "k9", where=lambda m: "?" in m["payload"]["title"])
        await deck.key("k1", MUTE, {"target": "channel:voice"})
        await deck.expect("showOk", "k1")
        assert next(c for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == "voice")["Muted"] is True
        assert deck.proc.poll() is None, "plugin must still be running"
