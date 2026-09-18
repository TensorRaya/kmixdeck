"""Drive the web UI in headless Chrome over CDP (no selenium/playwright — python `websockets` + chrome's own protocol).

Lifecycle matters here: chrome is a process TREE (launcher, zygote, renderers, gpu). `terminate()` on the launcher alone
left the children alive and the profile dir busy — 61 orphaned processes and 8 × ~100 MB profiles after one ctest run
filled /tmp (tmpfs) on 2026-09-18; a second round left 136 processes and load 39. Hence: own process group, killpg on
close, atexit reaper, context manager, and close() ASSERTS the profile is gone so a leak turns a test red instead of
filling the disk.
"""
import asyncio, atexit, json, os, shutil, signal, subprocess, tempfile, time, urllib.request

import websockets

CHROME = shutil.which("google-chrome") or shutil.which("chromium") or shutil.which("chromium-browser")

_LIVE = set()


def _reap():
    for c in list(_LIVE):
        try: c.close()
        except Exception: pass


atexit.register(_reap)


class Chrome:
    """`with Chrome(url) as ch:` — close() runs even when an assertion fails halfway."""

    def __init__(self, url, size=(1280, 800)):
        self.tmp = tempfile.mkdtemp(prefix="kmix-chrome-")
        self._closed = False
        self.proc = subprocess.Popen([CHROME, "--headless=new", "--remote-debugging-port=0", f"--user-data-dir={self.tmp}", "--no-first-run",
                                      "--disable-gpu", "--hide-scrollbars", "--force-dark-mode", "--enable-features=WebContentsForceDark:inversion_method/cielab_based/image_behavior/none", f"--window-size={size[0]},{size[1]}", "--remote-allow-origins=*", url],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, start_new_session=True)
        _LIVE.add(self)
        port = None; t0 = time.time()
        while time.time() - t0 < 15:
            line = self.proc.stderr.readline()
            if "DevTools listening on" in line: port = line.split(":")[-1].split("/")[0].strip(); break
        if not port:
            self.close(); raise AssertionError("chrome did not start")
        page = None
        for _ in range(50):
            try:
                tabs = json.loads(urllib.request.urlopen(f"http://127.0.0.1:{port}/json", timeout=2).read())
                page = next(t for t in tabs if t["type"] == "page"); break
            except Exception: time.sleep(0.1)
        if not page:
            self.close(); raise AssertionError("chrome has no page tab")
        self.ws_url = page["webSocketDebuggerUrl"]; self._id = 0

    def __enter__(self): return self
    def __exit__(self, *exc): self.close(); return False

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

    def answer_dialogs(self, prompt_answer=None, confirm=True):
        """Make window.prompt() return `prompt_answer` and window.confirm() return `confirm` from now on (page-side shim;
        headless chrome would otherwise auto-dismiss both). Call again to change the answer."""
        self.eval(f"(() => {{ window.prompt = () => {json.dumps(prompt_answer)}; window.confirm = () => {json.dumps(confirm)}; }})()", False)

    def shot(self, path):
        async def go():
            async with websockets.connect(self.ws_url, max_size=1 << 26) as ws:
                r = await self._cmd(ws, "Page.captureScreenshot", format="png", captureBeyondViewport=True)
                import base64; open(path, "wb").write(base64.b64decode(r["data"]))
        asyncio.run(go()); return path

    def close(self):
        _LIVE.discard(self)
        if self._closed: return
        self._closed = True
        try: os.killpg(self.proc.pid, signal.SIGTERM)
        except ProcessLookupError: pass
        try: self.proc.wait(5)
        except subprocess.TimeoutExpired:
            try: os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError: pass
            self.proc.wait(5)
        for _ in range(30):                          # renderers may take a moment to let go of the profile
            shutil.rmtree(self.tmp, ignore_errors=True)
            if not os.path.exists(self.tmp): break
            time.sleep(0.1)
        assert not os.path.exists(self.tmp), f"chrome profile {self.tmp} could not be removed — a chrome process is still holding it"
