#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kmixdeck actions for OpenAction servers (OpenDeck, Tacto) and — via the compatible subset — the Elgato Stream Deck
software. CT-3: Stream Deck support on top of CT-2, without our own HID stack.

Design (ADR 0010, rule 1): this plugin holds NO mixer logic. It is a thin client of the kmixdeck D-Bus API, reached
through the `kmixdeck` CLI in --json mode (so a third-party host needs nothing of our code but that binary): every
press becomes one CLI call, every displayed state comes from `kmixdeck watch`. Window, tray, CLI and this deck all
show the same truth because they all read the same daemon.

Protocol: https://openaction.amankhanna.me — the server starts us with
    plugin.py -port <port> -pluginUUID <uuid> -registerEvent <event> -info <json>
we open ws://localhost:<port>, register, then react to willAppear / keyDown / dialRotate / dialDown /
didReceiveSettings and answer with setTitle / setState / showAlert / sendToPropertyInspector.

Actions (settings per instance, chosen in the property inspector):
  me.kmixdeck.mute     {target: "channel:game" | "mix:stream" | "cell:game/stream"}          key: toggle mute
  me.kmixdeck.fader    {target, step_db: 3}      key: ±step; encoder: ticks × step_db/3, press = mute toggle
  me.kmixdeck.listen   {device: "<node.name>"}   key: hear on that device (UX-2)
"""
import asyncio, json, math, os, shutil, subprocess, sys

KMIXDECK = os.environ.get("KMIXDECK_CLI") or shutil.which("kmixdeck") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "kmixdeck")
PLUGIN_UUID = "me.kmixdeck"
LOG = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "plugin.log"), "a", buffering=1)


def log(*a):
    print(*a, file=LOG)


# ---- daemon access: every read and write is one CLI call ---------------------------------------------------------
def cli(*args, check=True):
    r = subprocess.run([KMIXDECK, "--json", *args], capture_output=True, text=True, timeout=10)
    if check and r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or r.stdout.strip() or f"kmixdeck rc={r.returncode}")
    try:
        return json.loads(r.stdout) if r.stdout.strip() else None
    except json.JSONDecodeError:
        return r.stdout.strip()


def parse_target(t):
    """'channel:game' → ('channel', ['game']); 'cell:game/stream' → ('cell', ['game', 'stream'])."""
    kind, _, rest = (t or "").partition(":")
    parts = [p for p in rest.split("/") if p]
    if kind not in ("channel", "mix", "cell") or (kind == "cell" and len(parts) != 2) or (kind != "cell" and len(parts) != 1):
        raise ValueError(f"bad target {t!r}")
    return kind, parts


def path_of(t):
    kind, p = parse_target(t)
    return "/org/kmixdeck1/" + kind + "/" + "/".join(p)


def state_of(t):
    """{'Muted': bool, 'Volume': linear 0..1, 'Name': str} straight from the daemon."""
    kind, p = parse_target(t)
    if kind == "cell":
        c = cli("cell", "get", *p); return {"Muted": c["Muted"], "Volume": c["Volume"], "Name": f"{p[0]}→{p[1]}"}
    lst = cli(kind, "list") or []
    o = next((x for x in lst if x["Slug"] == p[0]), None)
    if o is None: raise RuntimeError(f"no {kind} '{p[0]}'")
    # a channel's own fader is its trim (CH-7); a mix's is its master volume
    return {"Muted": o["Muted"], "Volume": o["Trim"] if kind == "channel" else o["Volume"], "Name": o["Name"]}


def set_mute(t, on):
    kind, p = parse_target(t)
    cli(kind, "mute", *p, "on" if on else "off")


def set_db(t, db):
    kind, p = parse_target(t)
    level = "-inf" if db <= -60 else f"{db:+.1f}dB"
    if kind == "cell": cli("cell", "set", *p, level)
    elif kind == "channel": cli("channel", "trim", p[0], level)
    else: cli("mix", "volume", p[0], level)


def to_db(linear):
    return -math.inf if linear <= 0 else 20 * math.log10(linear)


def title_for(st):
    db = to_db(st["Volume"])
    return f"{st['Name']}\n" + ("MUTE" if st["Muted"] else ("-∞" if db == -math.inf else f"{db:+.0f} dB"))


# ---- the plugin ---------------------------------------------------------------------------------------------------
class Plugin:
    def __init__(self, ws):
        self.ws = ws
        self.instances = {}      # context → {"action": uuid, "settings": {...}}
        self.watch = None

    async def send(self, **msg):
        await self.ws.send(json.dumps(msg))

    async def refresh(self, context):
        inst = self.instances.get(context)
        if not inst: return
        try:
            if inst["action"] == f"{PLUGIN_UUID}.listen":
                dev = inst["settings"].get("device", "")
                cur = cli("listen"); cur = cur if isinstance(cur, dict) else {}
                on = bool(dev) and cur.get("device") == dev
                devs = cli("devices") or {}
                label = (devs.get(dev) if isinstance(devs, dict) else None) or dev or "listen"
                await self.send(event="setState", context=context, payload={"state": 1 if on else 0})
                await self.send(event="setTitle", context=context, payload={"title": "hear\n" + label})
                return
            t = inst["settings"].get("target")
            if not t:
                await self.send(event="setTitle", context=context, payload={"title": "choose\ntarget"}); return
            st = state_of(t)
            await self.send(event="setTitle", context=context, payload={"title": title_for(st)})
            await self.send(event="setState", context=context, payload={"state": 1 if st["Muted"] else 0})
        except Exception as e:  # daemon gone, unknown slug, …
            log("refresh", context, e)
            await self.send(event="setTitle", context=context, payload={"title": "kmixdeck\n?"})
            await self.send(event="showAlert", context=context)

    async def refresh_all_for(self, path):
        for ctx, inst in list(self.instances.items()):
            t = inst["settings"].get("target")
            if inst["action"] == f"{PLUGIN_UUID}.listen" or (t and path_of(t) == path):
                await self.refresh(ctx)

    async def handle(self, msg):
        ev, ctx = msg.get("event"), msg.get("context")
        payload = msg.get("payload", {}) or {}
        if ev == "willAppear":
            self.instances[ctx] = {"action": msg["action"], "settings": payload.get("settings", {}) or {}}
            await self.refresh(ctx)
        elif ev == "willDisappear":
            self.instances.pop(ctx, None)
        elif ev == "didReceiveSettings":
            if ctx in self.instances: self.instances[ctx]["settings"] = payload.get("settings", {}) or {}
            await self.refresh(ctx)
        elif ev == "propertyInspectorDidAppear":
            await self.send_choices(ctx)
        elif ev == "sendToPlugin" and (payload.get("request") == "choices"):
            await self.send_choices(ctx)
        elif ev in ("keyDown", "dialDown"):
            await self.press(ctx, payload)
        elif ev == "dialRotate":
            inst = self.instances.get(ctx)
            if inst and inst["action"] == f"{PLUGIN_UUID}.fader":
                step = float(inst["settings"].get("step_db", 3)) / 3.0   # one detent = a third of the key step
                await self.nudge(ctx, inst, payload.get("ticks", 0) * step)

    async def send_choices(self, ctx):
        """The property inspector cannot reach D-Bus — it asks us for the live list of channels, mixes and devices."""
        try:
            chans = cli("channel", "list") or []; mixes = cli("mix", "list") or []; devs = cli("devices") or {}   # devices: {node.name: description}
            targets = [{"value": f"channel:{c['Slug']}", "label": f"Channel {c['Name']}"} for c in chans]
            targets += [{"value": f"mix:{m['Slug']}", "label": f"Mix {m['Name']}"} for m in mixes]
            targets += [{"value": f"cell:{c['Slug']}/{m['Slug']}", "label": f"{c['Name']} in {m['Name']}"} for c in chans for m in mixes]
            outs = [{"value": n, "label": d or n} for n, d in (devs.items() if isinstance(devs, dict) else [])]
            inst = self.instances.get(ctx, {})
            await self.send(event="sendToPropertyInspector", action=inst.get("action", ""), context=ctx, payload={"targets": targets, "devices": outs})
        except Exception as e:
            log("choices", e)

    async def press(self, ctx, payload):
        inst = self.instances.get(ctx)
        if not inst: return
        a = inst["action"]
        try:
            if a == f"{PLUGIN_UUID}.mute" or (a == f"{PLUGIN_UUID}.fader" and payload.get("controller") == "Encoder"):
                t = inst["settings"]["target"]; set_mute(t, not state_of(t)["Muted"])
            elif a == f"{PLUGIN_UUID}.fader":
                await self.nudge(ctx, inst, float(inst["settings"].get("step_db", 3)) * (1 if inst["settings"].get("direction", "up") == "up" else -1))
                return
            elif a == f"{PLUGIN_UUID}.listen":
                cli("listen", inst["settings"]["device"])
            await self.send(event="showOk", context=ctx)
        except Exception as e:
            log("press", a, e); await self.send(event="showAlert", context=ctx)
        await self.refresh(ctx)

    async def nudge(self, ctx, inst, delta_db):
        try:
            t = inst["settings"]["target"]; st = state_of(t)
            cur = to_db(st["Volume"]); cur = -60 if cur == -math.inf else cur
            set_db(t, max(-60.0, min(0.0, cur + delta_db)))
        except Exception as e:
            log("nudge", e); await self.send(event="showAlert", context=ctx)
        await self.refresh(ctx)

    async def watch_daemon(self):
        """`kmixdeck --json watch` streams PropertiesChanged; whatever changes — from the window, the tray, the CLI —
        the deck shows it. That is the 'one truth' the SoT asks for, seen from the fourth frontend."""
        while True:
            try:
                proc = await asyncio.create_subprocess_exec(KMIXDECK, "--json", "watch", stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
                while True:
                    line = await proc.stdout.readline()
                    if not line: break
                    try: ev = json.loads(line)
                    except json.JSONDecodeError: continue
                    if ev.get("event") == "changed" and ev.get("path"):
                        await self.refresh_all_for(ev["path"])
                    elif ev.get("event") in ("added", "removed"):
                        for ctx in list(self.instances): await self.refresh(ctx)
            except Exception as e:
                log("watch", e)
            await asyncio.sleep(2)   # daemon restart → reconnect


async def main(argv):
    import websockets
    args = dict(zip(argv[1::2], argv[2::2], strict=False))
    port, uuid, reg = args["-port"], args["-pluginUUID"], args["-registerEvent"]
    async with websockets.connect(f"ws://localhost:{port}", max_size=None) as ws:
        await ws.send(json.dumps({"event": reg, "uuid": uuid}))
        p = Plugin(ws)
        watcher = asyncio.create_task(p.watch_daemon())
        try:
            async for raw in ws:
                try: msg = json.loads(raw)
                except json.JSONDecodeError: continue
                await p.handle(msg)
        finally:
            watcher.cancel()


if __name__ == "__main__":
    try:
        asyncio.run(main(sys.argv))
    except KeyboardInterrupt:
        pass
