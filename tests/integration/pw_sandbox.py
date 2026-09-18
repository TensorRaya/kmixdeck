# SPDX-License-Identifier: GPL-3.0-or-later
"""Test fixture: a private, throw-away PipeWire daemon per test session.

Pattern borrowed from PipeWire's own `pwtest` (test/pwtest.c: fresh XDG_RUNTIME_DIR per test,
PIPEWIRE_REMOTE pointed at it) and plasma-pa's appium tests (spawn pipewire + wireplumber in the
test, never touch the user's session). No sound card needed — everything is null sinks.
"""
from __future__ import annotations
import json, os, re, shutil, subprocess, tempfile, time
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROTOTYPE_CONF = REPO / "prototype" / "kmixdeck-prototype.conf"


@dataclass
class PwDaemon:
    runtime_dir: Path
    env: dict
    procs: list

    # ---- introspection
    def dump(self) -> list[dict]:
        out = subprocess.run(["pw-dump"], env=self.env, capture_output=True, text=True, check=True).stdout
        return json.loads(out)

    def node(self, name: str) -> dict | None:
        for o in self.dump():
            if o.get("info", {}).get("props", {}).get("node.name") == name:
                return o
        return None

    def node_names(self) -> set[str]:
        """ONE pw-dump for a whole set of names. Polling node() per name scales with (names × graph size): with 32
        channels the loop itself took 25 s and was read as \"PipeWire needs 0.7 s per loopback\" (2026-09-17). The
        graph had every edge after 2 s. Measure with this, not with node() in a loop."""
        return {o.get("info", {}).get("props", {}).get("node.name") for o in self.dump() if o.get("type", "").endswith("Node")}

    def wait_nodes(self, names, timeout: float = 30.0) -> float:
        """Wait until every name exists; returns the seconds it took. Raises with the missing set on timeout."""
        want = set(names); t0 = time.time()
        while time.time() - t0 < timeout:
            missing = want - self.node_names()
            if not missing: return time.time() - t0
            time.sleep(0.05)
        raise AssertionError(f"nodes never appeared within {timeout}s: {sorted(want - self.node_names())[:6]}")

    def node_id(self, name: str) -> int:
        n = self.node(name)
        assert n is not None, f"node {name} not found"
        return n["id"]

    def wait_node(self, name: str, timeout: float = 5.0) -> dict:
        t0 = time.time()
        while time.time() - t0 < timeout:
            n = self.node(name)
            if n is not None:
                return n
            time.sleep(0.1)
        raise AssertionError(f"node {name} did not appear within {timeout}s")

    def props(self, name: str) -> dict:
        """channelVolumes[0] and mute from the node's Props param."""
        n = self.node(name)
        assert n is not None, name
        for p in n["info"].get("params", {}).get("Props", []):
            if "channelVolumes" in p:
                return {"volume": p["channelVolumes"][0], "mute": p["mute"]}
        raise AssertionError(f"{name}: no Props with channelVolumes")

    # ---- control (same calls the app makes)
    def set_volume(self, name: str, linear: float, mute: bool = False) -> None:
        nid = self.node_id(name)
        subprocess.run(["pw-cli", "set-param", str(nid), "Props",
                        f"{{ channelVolumes: [ {linear}, {linear} ], mute: {'true' if mute else 'false'} }}"],
                       env=self.env, check=True, capture_output=True)
        time.sleep(0.3)

    def restart(self, wait_for: str | None = "kmixdeck.mix.stream") -> None:
        """Restart pipewire + wireplumber inside the sandbox (persistence tests). wait_for=None: just come up."""
        self._stop_procs()
        time.sleep(0.3)
        self._start_procs()
        self.wait_node(wait_for or "kmixdeck.null", timeout=8.0)

    # ---- audio measurement
    def rms_db(self, wav: Path) -> float:
        out = subprocess.run(["ffmpeg", "-hide_banner", "-i", str(wav), "-af",
                              "astats=measure_overall=RMS_level:measure_perchannel=0", "-f", "null", "-"],
                             capture_output=True, text=True).stderr
        m = re.search(r"RMS level dB: (-?[0-9.]+|-inf)", out)
        assert m, out[-400:]
        return float("-inf") if m.group(1) == "-inf" else float(m.group(1))

    def tone(self) -> Path:
        p = self.runtime_dir / "tone.wav"
        if not p.exists():
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "quiet", "-y", "-f", "lavfi", "-i",
                            "sine=frequency=1000:sample_rate=48000", "-t", "120", "-ac", "2", str(p)], check=True)
        return p

    def hot_tone(self) -> Path:
        """CH-7 clip test: a sine that reaches full scale (float WAV: ffmpeg's sine sits at −21 dBFS, +24 dB → samples at 1.4; pw-play keeps floats, so samples
        pass 1.0 and the daemon's clip counter sees them)."""
        p = self.runtime_dir / "hot.wav"
        if not p.exists():
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "quiet", "-y", "-f", "lavfi", "-i",
                            "sine=frequency=1000:sample_rate=48000", "-t", "120", "-ac", "2", "-af", "volume=24dB", "-c:a", "pcm_f32le", str(p)], check=True)
        return p

    def play_into(self, sink: str, wav: Path | None = None) -> subprocess.Popen:
        """Play the test tone into `sink` with explicit port links (autoconnect off — deterministic)."""
        p = subprocess.Popen(["pw-play", "-P", "{ node.autoconnect = false }", str(wav or self.tone())],
                             env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.6)
        for ch in ("FL", "FR"):
            subprocess.run(["pw-link", f"pw-play:output_{ch}", f"{sink}:playback_{ch}"], env=self.env, capture_output=True)
        time.sleep(0.5)
        return p

    # ---- ADR 0009 port-level helpers (fake multichannel devices: a sink's ports are playback_<POS>/monitor_<POS>,
    # a source's ports are capture_<POS>)
    def play_into_port(self, node: str, port: str) -> subprocess.Popen:
        """Left channel of the tone → exactly ONE port (`<node>:<port>`, e.g. fake.ui24r:playback_AUX2)."""
        p = subprocess.Popen(["pw-play", "-P", "{ node.autoconnect = false }", str(self.tone())],
                             env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.6)
        subprocess.run(["pw-link", "pw-play:output_FL", f"{node}:{port}"], env=self.env, capture_output=True)
        time.sleep(0.5)
        return p

    def _record(self, out: Path, channels: int, links: list[tuple[str, str]], seconds: float, what: str) -> Path:
        """Start pw-record unconnected, link the given (source_port, record_port) pairs, THEN record `seconds`.
        No hard `timeout` around pw-record: under load (full ctest run, 2026-09-16) it was killed before the ports
        were linked and the file stayed empty — six routing tests went red although the audio path was fine."""
        env = dict(self.env, PW_LATENCY="1024/48000")
        # A unique node.name per recorder. The links below address the recorder BY NAME; with the default name
        # "pw-record" a previous recorder that PipeWire has not yet torn down (terminate() returns before the node is
        # gone) is an equally valid target — and two back-to-back measurements then read the SAME stream. Seen 2026-09-18
        # as two different ports reporting the identical -24.206796 dB (ctest44, DV-21).
        name = f"kmixdeck-rec-{time.time_ns()}"
        rec = subprocess.Popen(["pw-record", "-P", "{ node.autoconnect = false node.name = %s }" % name,
                                "--rate", "48000", "--channels", str(channels), "--format", "s16", str(out)],
                               env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        links = [(src, dst.replace("pw-record:", name + ":", 1)) for src, dst in links]
        linked = False
        for _ in range(80):
            time.sleep(0.1)
            ok = 0
            for src, dst in links:
                r = subprocess.run(["pw-link", src, dst], env=self.env, capture_output=True, text=True)
                if r.returncode == 0 or "exists" in (r.stderr + r.stdout): ok += 1
            if ok == len(links): linked = True; break
        if not linked:
            rec.kill(); rec.wait()
            raise AssertionError(f"could not link pw-record to {what}")
        time.sleep(seconds)
        rec.terminate()
        try: rec.wait(timeout=3)
        except subprocess.TimeoutExpired: rec.kill(); rec.wait()
        assert out.exists() and out.stat().st_size > 1000, f"recording from {what} is empty"
        return out

    def record_port(self, node: str, port: str, seconds: float = 1.5) -> Path:
        """Mono recording of ONE output port (`monitor_AUX2` of a sink, `capture_AUX2` of a source)."""
        out = self.runtime_dir / f"rec-{node}-{port}-{time.time_ns()}.wav"
        return self._record(out, 1, [(f"{node}:{port}", "pw-record:input_MONO")], seconds, f"{node}:{port}")

    def level_at_port(self, node: str, port: str) -> float:
        return self.rms_db(self.record_port(node, port))

    def record_monitor(self, sink: str, seconds: float = 1.5) -> Path:
        """Record `sink`'s monitor ports. NOTE: never use `--target`, it may pick the wrong port (see ADR 0002)."""
        out = self.runtime_dir / f"rec-{sink}-{time.time_ns()}.wav"
        return self._record(out, 2, [(f"{sink}:monitor_{ch}", f"pw-record:input_{ch}") for ch in ("FL", "FR")], seconds, sink)

    def level_at(self, sink: str) -> float:
        return self.rms_db(self.record_monitor(sink))

    # ---- lifecycle
    def _start_procs(self) -> None:
        pw = subprocess.Popen(["pipewire"], env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.8)
        wp = subprocess.Popen(["wireplumber"], env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.procs = [pw, wp]
        for _ in range(50):
            if subprocess.run(["pw-cli", "info", "0"], env=self.env, capture_output=True).returncode == 0:
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("private pipewire did not come up")
        time.sleep(1.0)  # let WirePlumber restore state + config modules load

    def start_pulse(self) -> None:
        """CT-5: what the Plasma volume applet talks to. pipewire-pulse on our private socket; PULSE_SERVER for clients."""
        sock = self.runtime_dir / "pulse" / "native"; (self.runtime_dir / "pulse").mkdir(exist_ok=True)
        self.env["PULSE_SERVER"] = f"unix:{sock}"; self.env["PULSE_RUNTIME_PATH"] = str(self.runtime_dir / "pulse")
        # the socket path comes from PULSE_RUNTIME_PATH (module-protocol-pulse default: $PULSE_RUNTIME_PATH/native)
        p = subprocess.Popen(["pipewire-pulse"], env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.procs.append(p)
        for _ in range(60):
            if sock.exists(): break
            time.sleep(0.1)
        else: raise RuntimeError("pipewire-pulse did not come up")
        time.sleep(0.5)

    def _stop_procs(self) -> None:
        for p in reversed(self.procs):
            p.terminate()
        for p in self.procs:
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
        self.procs = []

    def close(self) -> None:
        self._stop_procs()
        shutil.rmtree(self.runtime_dir, ignore_errors=True)


def start_private_pipewire(extra_conf: Path | None = PROTOTYPE_CONF, session_conf: str | None = None) -> PwDaemon:
    """session_conf: extra PipeWire context.properties text (DV-4: e.g. a 44.1 kHz / 256 quantum session)."""
    rt = Path(tempfile.mkdtemp(prefix="kmixdeck-pw-"))
    (rt / "pipewire.conf.d").mkdir()
    if session_conf: (rt / "pipewire.conf.d" / "10-session.conf").write_text(session_conf)
    state = rt / "state"; state.mkdir()
    env = dict(os.environ)
    env.update({
        "XDG_RUNTIME_DIR": str(rt),
        "XDG_STATE_HOME": str(state),        # WirePlumber state (stream-properties) lives here
        "XDG_CONFIG_HOME": str(rt / "config"),
        "PIPEWIRE_CONFIG_DIR": str(rt),      # our pipewire.conf.d is picked up from here
        "PIPEWIRE_RUNTIME_DIR": str(rt),
        "PIPEWIRE_REMOTE": "pipewire-0",
        "PIPEWIRE_LOG_SYSTEMD": "false",
        "DISABLE_RTKIT": "1",
    })
    env.pop("PULSE_SERVER", None); env.pop("DBUS_SESSION_BUS_ADDRESS", None)
    # PIPEWIRE_CONFIG_DIR *replaces* the search path — for the daemon AND every client (client.conf,
    # pipewire-pulse.conf, ...). So mirror the stock config dir and layer our conf.d on top.
    stock = Path("/usr/share/pipewire")
    for f in stock.glob("*.conf"):
        shutil.copy(f, rt / f.name)
    for sub in stock.glob("*.conf.d"):
        dst = rt / sub.name; dst.mkdir(exist_ok=True)
        for f in sub.glob("*.conf"): shutil.copy(f, dst / f.name)
    if extra_conf:
        shutil.copy(extra_conf, rt / "pipewire.conf.d" / "90-kmixdeck.conf")
    # kmixdeckd writes $XDG_CONFIG_HOME/pipewire/pipewire.conf.d/90-kmixdeck.conf; PIPEWIRE_CONFIG_DIR replaces the
    # search path, so point that directory at our conf.d — what the daemon writes is what the daemon loads.
    (rt / "config" / "pipewire").mkdir(parents=True)
    (rt / "config" / "pipewire" / "pipewire.conf.d").symlink_to(rt / "pipewire.conf.d")
    # WirePlumber reads XDG_CONFIG_HOME/wireplumber and /usr/share/wireplumber — leave it on stock.
    d = PwDaemon(runtime_dir=rt, env=env, procs=[])
    d._start_procs()
    return d
