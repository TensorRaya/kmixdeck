"""Drive the web UI in headless Chrome over CDP (no selenium/playwright — python `websockets` + chrome's own protocol)."""
import json, subprocess, tempfile, time, asyncio, urllib.request, shutil

import websockets

CHROME = shutil.which("google-chrome") or shutil.which("chromium") or shutil.which("chromium-browser")


class Chrome:
    def __init__(self, url, size=(1280, 800)):
        self.tmp = tempfile.mkdtemp(prefix="kmix-chrome-")
        self.proc = subprocess.Popen([CHROME, "--headless=new", "--remote-debugging-port=0", f"--user-data-dir={self.tmp}", "--no-first-run",
                                      "--disable-gpu", "--hide-scrollbars", "--force-dark-mode", "--enable-features=WebContentsForceDark:inversion_method/cielab_based/image_behavior/none", f"--window-size={size[0]},{size[1]}", "--remote-allow-origins=*", url],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        port = None; t0 = time.time()
        while time.time() - t0 < 15:
            line = self.proc.stderr.readline()
            if "DevTools listening on" in line: port = line.split(":")[-1].split("/")[0].strip(); break
        assert port, "chrome did not start"
        for _ in range(50):
            try:
                tabs = json.loads(urllib.request.urlopen(f"http://127.0.0.1:{port}/json", timeout=2).read())
                page = next(t for t in tabs if t["type"] == "page"); break
            except Exception: time.sleep(0.1)
        self.ws_url = page["webSocketDebuggerUrl"]; self._id = 0

    async def _cmd(self, ws, method, **params):
        self._id += 1; await ws.send(json.dumps({"id": self._id, "method": method, "params": params}))
        while True:
            m = json.loads(await asyncio.wait_for(ws.recv(), 20))
            if m.get("id") == self._id:
                if "error" in m: raise RuntimeError(m["error"])
                return m.get("result", {})

    def eval(self, expr, await_promise=True):
        async def go():
            async with websockets.connect(self.ws_url, max_size=1 << 26) as ws:
                r = await self._cmd(ws, "Runtime.evaluate", expression=expr, awaitPromise=await_promise, returnByValue=True)
                if "exceptionDetails" in r: raise RuntimeError(r["exceptionDetails"].get("exception", {}).get("description", str(r["exceptionDetails"])))
                return r.get("result", {}).get("value")
        return asyncio.run(go())

    def wait(self, expr, timeout=10):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.eval(f"!!({expr})"): return True
            time.sleep(0.1)
        raise TimeoutError(expr)

    def probe(self, name, attr="dataset.value"):
        """attr is a JS property path on the element; 'computed.<css-prop>' reads getComputedStyle (colours come back as rgb(...))."""
        if attr.startswith("computed."):
            return self.eval(f'(() => {{ const e = window.kmixdeck.probe({json.dumps(name)}); return e ? getComputedStyle(e).getPropertyValue({json.dumps(attr[9:])}) : "<not found: {name}>"; }})()', False)
        return self.eval(f'(() => {{ const e = window.kmixdeck.probe({json.dumps(name)}); return e ? String(e.{attr}) : "<not found: {name}>"; }})()', False)

    def click(self, name):
        return self.eval(f'(() => {{ const e = window.kmixdeck.probe({json.dumps(name)}); if (!e) return "<not found>"; e.click(); return "ok"; }})()', False)

    def shot(self, path):
        async def go():
            async with websockets.connect(self.ws_url, max_size=1 << 26) as ws:
                r = await self._cmd(ws, "Page.captureScreenshot", format="png", captureBeyondViewport=True)
                import base64; open(path, "wb").write(base64.b64decode(r["data"]))
        asyncio.run(go()); return path

    def close(self):
        self.proc.terminate()
        try: self.proc.wait(5)
        except subprocess.TimeoutExpired: self.proc.kill()
        shutil.rmtree(self.tmp, ignore_errors=True)
