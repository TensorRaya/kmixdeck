"""CT-10 playback into a channel — integration tests.

A client hands the daemon an audio stream (a file descriptor) and the daemon plays it into one channel, one track
at a time per channel. Every test here is ACOUSTIC where the requirement is about sound: a level measured on the
channel sink or in a mix, or the frequency content of a recording (two different tones tell "one after the other"
from "at the same time"). "The call returned 0" proves nothing.

On patience: every level_at() is a ~1.5 s recording (see wait_level), so the tones are 4 s or longer.
"""
import array
import json
import math
import subprocess
import threading
import time
import wave
from pathlib import Path

import pytest

from pw_sandbox import start_private_pipewire
from test_ports import wait_level
from test_service_cli import BIN, Stack
from waiting import wait_for

REPO = Path(__file__).resolve().parents[2]
SLUG = "music"


def tone(path: Path, freq: int, seconds: float, rate: int = 48000) -> Path:
    """A real file written by ffmpeg; the suffix picks the container."""
    path.parent.mkdir(parents=True, exist_ok=True)
    codec = {".wav": "pcm_s16le", ".flac": "flac", ".ogg": "libvorbis", ".mp3": "libmp3lame"}[path.suffix]
    r = subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", f"sine=frequency={freq}:duration={seconds}:sample_rate={rate}",
                        "-ac", "2", "-c:a", codec, str(path)], capture_output=True, text=True)
    assert r.returncode == 0 and path.stat().st_size > 500, f"ffmpeg could not write {path}: {r.stderr[-400:]}"
    return path


def play(stack, path, *extra, slug=SLUG, wait=True, stdin=None):
    """`kmixdeck channel play` as a background process. With --wait it ends when the track ends."""
    cmd = [str(BIN / "kmixdeck"), "channel", "play", slug, str(path), *extra] + (["--wait"] if wait else [])
    return subprocess.Popen(cmd, env=stack.env, stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def track_id(proc, timeout=10.0):
    """The id `channel play` prints first (with --wait it then keeps running until the track ends)."""
    line = proc.stdout.readline().strip()
    assert line.isdigit(), f"channel play printed no id: {line!r} / {proc.stderr.read() if proc.poll() is not None else ''}"
    return int(line)


def finish(proc, timeout=30.0):
    out, err = proc.communicate(timeout=timeout)
    return proc.returncode, out, err


def props(stack, slug=SLUG):
    return next(c for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == slug)


def goertzel(samples, rate, freq):
    """Amplitude of one frequency in a window (Hann-weighted Goertzel), 1.0 = full-scale sine."""
    n = len(samples)
    if n == 0: return 0.0
    k = 2.0 * math.cos(2.0 * math.pi * freq / rate)
    s1 = s2 = 0.0
    wsum = 0.0
    for i, x in enumerate(samples):
        w = 0.5 - 0.5 * math.cos(2.0 * math.pi * i / (n - 1))
        wsum += w
        s0 = x * w + k * s1 - s2
        s2, s1 = s1, s0
    power = s1 * s1 + s2 * s2 - k * s1 * s2
    return 2.0 * math.sqrt(max(power, 0.0)) / wsum


def spectrogram(wav_path, freqs, win=0.05):
    """Per window of `win` seconds: {freq: amplitude} of the left channel."""
    with wave.open(str(wav_path)) as w:
        rate, ch, frames = w.getframerate(), w.getnchannels(), w.readframes(w.getnframes())
    pcm = array.array("h", frames)
    left = [v / 32768.0 for v in pcm[::ch]]
    step = int(rate * win)
    return [{f: goertzel(left[i:i + step], rate, f) for f in freqs} for i in range(0, len(left) - step + 1, step)]


def record_in_background(stack, node, seconds):
    """Record `node`'s monitor for `seconds` in a thread; returns (thread, result dict). Wait for 'linked' before
    starting the audio — pw-record only records once its ports are linked (pw_sandbox._record)."""
    res = {}
    def go():
        try: res["wav"] = stack.pw.record_monitor(node, seconds=seconds)
        except Exception as e: res["error"] = e  # noqa: BLE001 — reported by the caller
    t = threading.Thread(target=go, daemon=True); t.start()
    return t, res


@pytest.fixture(scope="module")
def desk(tmp_path_factory):
    """One daemon with an extra channel `music`, unity into the stream mix, playback still OFF (opt-in)."""
    pw = start_private_pipewire()
    pw.wait_node("kmixdeck.mix.stream")
    stack = Stack(pw)
    tmp = tmp_path_factory.mktemp("ct10")
    stack.cli("channel", "add", "Music")
    stack.pw.wait_node(f"kmixdeck.channel.{SLUG}")
    stack.cli("cell", "set", SLUG, "stream", "0dB")
    yield stack, tmp
    stack.close()
    pw.close()


def test_ct10_playback_is_opt_in_and_refused_with_the_reason(desk):
    """Opt-in rule: Playback is off on every channel, Play is refused with the reason AND the command that turns it
    on, and layout.json carries no key for it."""
    stack, tmp = desk
    for c in stack.cli("channel", "list", json_out=True):
        assert c["Playback"] is False, f"channel {c['Slug']} accepts playback by default: {c}"
        assert c["NowPlaying"] == "" and c["PlayQueue"] == [], c
    layout = json.loads((Path(stack.env["XDG_CONFIG_HOME"]) / "kmixdeck" / "layout.json").read_text())
    assert not any("playback" in c for c in layout["channels"]), "an off switch must write nothing to layout.json"
    wav = tone(tmp / "optin.wav", 440, 2)
    r = stack.cli("channel", "play", SLUG, str(wav), check=False)
    assert r.returncode == 4, f"Play on a channel with playback off must be refused (rc 4), got {r.returncode}: {r.stderr}"
    assert "playback is off" in r.stderr and f"kmixdeck channel playback {SLUG} on" in r.stderr, r.stderr
    # straight on the bus too: a named error, not a silent 0
    bad = stack.busctl("call", "org.kmixdeck1", f"/org/kmixdeck1/channel/{SLUG}", "org.kmixdeck1.Channel", "StopPlayback", "u", "0")
    assert bad.returncode == 0, f"StopPlayback(0) on an idle channel must be a harmless no-op: {bad.stderr}"
    assert stack.pw.level_at(f"kmixdeck.channel.{SLUG}") < -60, "a refused Play made sound anyway"
    assert stack.cli("channel", "playback", SLUG, "on").stdout.strip() == "on"
    assert stack.cli("channel", "playback", "game", json_out=True)["playback"] is False, "switching one channel switched another"
    assert stack.cli("channel", "playback", SLUG, "maybe", check=False).returncode == 1


def test_ct10_a_wav_is_audible_on_the_channel_and_in_the_mix_and_a_muted_cell_keeps_it_out(desk):
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    node = f"kmixdeck.channel.{SLUG}"
    wav = tone(tmp / "five.wav", 1000, 5)
    p = play(stack, wav, "--title", "Five seconds")
    tid = track_id(p)
    ch = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what=f"the track on {node}")
    assert ch > -50
    assert props(stack)["NowPlaying"] == "Five seconds", props(stack)
    mx = wait_level(lambda: stack.pw.level_at("kmixdeck.mix.stream"), lambda v: v > -60, what="the track in the stream mix")
    assert mx > -60
    rc, out, err = finish(p)
    assert rc == 0 and out.strip() == "played", f"track {tid}: rc={rc} out={out!r} err={err!r}"
    wait_for(lambda: props(stack)["NowPlaying"] == "", timeout=3.0, what="NowPlaying cleared after the end")

    stack.cli("cell", "mute", SLUG, "stream", "on")
    try:
        p = play(stack, wav)
        track_id(p)
        ch = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what=f"the track on {node} (cell muted)")
        mx = stack.pw.level_at("kmixdeck.mix.stream")
        assert mx < -60, f"the muted cell leaked the track into the stream mix ({mx:.2f} dB, channel {ch:.2f} dB)"
    finally:
        stack.cli("channel", "stop", SLUG)
        finish(p)
        stack.cli("cell", "mute", SLUG, "stream", "off")


def test_ct10_queued_tracks_play_one_after_the_other_never_together(desk):
    """Two tones (440 Hz, then 1000 Hz). The recording must show the first, then the second, and no stretch where
    both sound — the spectral content separates "sequential" from "mixed" where a level cannot."""
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    node = f"kmixdeck.channel.{SLUG}"
    a, b = tone(tmp / "a440.wav", 440, 2.5), tone(tmp / "b1000.wav", 1000, 2.5)
    th, res = record_in_background(stack, node, 9.0)
    time.sleep(1.5)                                   # the recorder links its ports first
    pa = play(stack, a, "--title", "A"); ida = track_id(pa)
    pb = play(stack, b, "--title", "B"); idb = track_id(pb)
    assert idb != ida
    pr = props(stack)
    assert pr["NowPlaying"] == "A" and pr["PlayQueue"] == ["B"], f"B must wait behind A: {pr}"
    assert finish(pa)[0] == 0
    wait_for(lambda: props(stack)["NowPlaying"] == "B", timeout=3.0, what="B to start after A")
    rc, out, err = finish(pb)
    assert rc == 0 and out.strip() == "played", (rc, out, err)
    th.join(15)
    assert "wav" in res, f"recording failed: {res.get('error')}"
    win = spectrogram(res["wav"], (440, 1000))
    on = 0.01                                         # -40 dB; ffmpeg's sine sits at 1/8 (-18 dBFS)
    with_a = [i for i, w in enumerate(win) if w[440] > on]
    with_b = [i for i, w in enumerate(win) if w[1000] > on]
    both = [i for i, w in enumerate(win) if w[440] > on and w[1000] > on]
    assert len(with_a) >= 30 and len(with_b) >= 30, f"tones missing from the recording: A in {len(with_a)}, B in {len(with_b)} windows of 50 ms"
    assert len(both) <= 2, f"A and B sounded together in {len(both)} windows of 50 ms (allowed: the hand-over window): {both[:10]}"
    assert max(with_a) <= min(with_b) + 2, f"B started before A ended (A until window {max(with_a)}, B from {min(with_b)})"


def test_ct10_stopping_a_queued_track_means_it_never_sounds(desk):
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    node = f"kmixdeck.channel.{SLUG}"
    a, b = tone(tmp / "qa440.wav", 440, 3), tone(tmp / "qb1000.wav", 1000, 3)
    th, res = record_in_background(stack, node, 8.0)
    time.sleep(1.5)
    pa = play(stack, a, "--title", "first"); track_id(pa)
    pb = play(stack, b, "--title", "second"); idb = track_id(pb)
    assert props(stack)["PlayQueue"] == ["second"]
    stack.cli("channel", "stop", SLUG, str(idb))
    rc, out, err = finish(pb)
    assert rc == 4 and f"track {idb}: stopped" in err, f"a stopped queued track must end 'stopped': rc={rc} err={err!r}"
    assert props(stack)["PlayQueue"] == [], props(stack)
    assert finish(pa)[0] == 0
    th.join(15)
    assert "wav" in res, res
    win = spectrogram(res["wav"], (440, 1000))
    assert sum(w[440] > 0.01 for w in win) >= 30, "the first track did not sound"
    leaked = [i for i, w in enumerate(win) if w[1000] > 0.01]
    assert not leaked, f"the stopped queued track sounded anyway in {len(leaked)} windows"


def test_ct10_a_pipe_streams_and_a_stalled_pipe_can_be_stopped(desk):
    """The music case: another program writes the audio into a pipe, the daemon plays it while it arrives."""
    stack, _tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    node = f"kmixdeck.channel.{SLUG}"
    gen = subprocess.Popen(["ffmpeg", "-v", "error", "-re", "-f", "lavfi", "-i", "sine=frequency=1000:duration=6:sample_rate=48000",
                            "-ac", "2", "-f", "ogg", "-c:a", "libvorbis", "-"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    assert gen.stdout is not None
    p = play(stack, "-", "--title", "piped", stdin=gen.stdout)
    gen.stdout.close()
    track_id(p)
    lvl = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what="audio from the pipe")
    assert lvl > -50
    rc, out, err = finish(p, timeout=30)
    gen.wait(5)
    assert rc == 0 and out.strip() == "played", (rc, out, err)

    # a pipe that delivers NOTHING: Stop must still end the track at once (poll() with a timeout in the read callback)
    staller = subprocess.Popen(["sleep", "60"], stdout=subprocess.PIPE)
    assert staller.stdout is not None
    p = play(stack, "-", "--title", "stalled", stdin=staller.stdout)
    staller.stdout.close()
    sid = track_id(p)
    wait_for(lambda: props(stack)["NowPlaying"] == "stalled", timeout=3.0, what="the stalled track to be sounding")
    t0 = time.monotonic()
    stack.cli("channel", "stop", SLUG)
    rc, _out, err = finish(p, timeout=10)
    took = time.monotonic() - t0
    staller.kill(); staller.wait()
    assert rc == 4 and f"track {sid}" in err and "stopped" in err, (rc, err)
    assert took < 3.0, f"stopping a stalled pipe took {took:.1f} s"


def test_ct10_undecodable_input_ends_with_error_and_the_next_track_still_plays(desk):
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    junk = tmp / "notaudio.wav"; junk.write_text("this is not audio at all, just text\n" * 50)
    good = tone(tmp / "after.wav", 1000, 4)
    pj = play(stack, junk, "--title", "junk"); track_id(pj)
    pg = play(stack, good, "--title", "good"); track_id(pg)
    rc, _out, err = finish(pj)
    assert rc == 4 and "error:" in err, f"undecodable input must end with 'error: …', got rc={rc} err={err!r}"
    lvl = wait_level(lambda: stack.pw.level_at(f"kmixdeck.channel.{SLUG}"), lambda v: v > -50, what="the track after the broken one")
    assert lvl > -50
    rc, out, err = finish(pg)
    assert rc == 0 and out.strip() == "played", (rc, out, err)


def test_ct10_an_fx_change_while_playing_moves_the_stream_without_a_restart(desk):
    """A -12 dB EQ switched on while a 1 kHz track plays: the level drops ~12 dB, the stream is linked to the chain's
    entry, the same track id keeps sounding (no restart, no PlaybackEnded) and it ends when the FILE ends."""
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    node, entry, stream = f"kmixdeck.channel.{SLUG}", f"kmixdeck.fx.{SLUG}", f"kmixdeck.playback.{SLUG}"
    long = tone(tmp / "long.wav", 1000, 14)
    t0 = time.monotonic()
    p = play(stack, long, "--title", "long")
    tid = track_id(p)
    try:
        before = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what="the long track")
        assert stack.pw.sink_of(stream) == node
        stack.cli("fx", "set", "channel", SLUG,
                  '{"enabled":true,"chain":[{"type":"eq","enabled":true,"params":{"midGain":-12,"midFreq":1000}}]}')
        wait_for(lambda: stack.pw.sink_of(stream) == entry, timeout=6.0, what=f"the playing stream to move onto {entry}")
        after = wait_level(lambda: stack.pw.level_at(node), lambda v: before - 15 < v < before - 9,
                           what=f"the level to drop by ~12 dB through the new chain (before {before:.1f} dB)")
        assert props(stack)["NowPlaying"] == "long", "the track was replaced instead of moved"
        assert p.poll() is None, "the track ended when the chain changed"
        # and back: chain off → plain sink, level back
        stack.cli("fx", "set", "channel", SLUG, '{"enabled":false,"chain":[]}')
        wait_for(lambda: stack.pw.sink_of(stream) == node, timeout=6.0, what="the stream to move back onto the plain sink")
        wait_level(lambda: stack.pw.level_at(node), lambda v: abs(v - before) < 2, what=f"the level back at {before:.1f} dB")
        rc, out, err = finish(p, timeout=30)
        took = time.monotonic() - t0
        assert rc == 0 and out.strip() == "played", (rc, out, err)
        assert took < 14 + 4, f"the 14 s track took {took:.1f} s — it was restarted somewhere ({after:.1f} dB after the EQ)"
    finally:
        stack.cli("channel", "stop", SLUG, str(tid), check=False)
        stack.cli("fx", "set", "channel", SLUG, '{"enabled":false,"chain":[]}', check=False)


def test_ct10_the_switch_survives_restart_and_export_import_and_off_or_remove_stops(desk):
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    layout = json.loads((Path(stack.env["XDG_CONFIG_HOME"]) / "kmixdeck" / "layout.json").read_text())
    assert next(c for c in layout["channels"] if c["slug"] == SLUG).get("playback") is True
    stack.restart_daemon()
    stack.pw.wait_node(f"kmixdeck.channel.{SLUG}")
    assert stack.cli("channel", "playback", SLUG, json_out=True)["playback"] is True, "Playback lost by a restart"
    doc = tmp / "export.json"
    stack.cli("export", str(doc))
    stack.cli("channel", "playback", SLUG, "off")
    stack.cli("import", str(doc))
    wait_for(lambda: stack.cli("channel", "playback", SLUG, json_out=True)["playback"] is True, timeout=5.0, what="Playback back from the import")

    node = f"kmixdeck.channel.{SLUG}"
    long = tone(tmp / "offlong.wav", 1000, 20)
    # off while sounding → stopped, silent, and the queue is gone too
    p1 = play(stack, long, "--title", "one"); track_id(p1)
    p2 = play(stack, long, "--title", "two"); track_id(p2)
    wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what="the track before the switch goes off")
    stack.cli("channel", "playback", SLUG, "off")
    for p in (p1, p2):
        rc, _o, err = finish(p, timeout=10)
        assert rc == 4 and "stopped" in err, (rc, err)
    wait_level(lambda: stack.pw.level_at(node), lambda v: v < -60, what="silence after Playback off")
    assert props(stack)["NowPlaying"] == "" and props(stack)["PlayQueue"] == []

    # remove while sounding → stopped; undo brings the switch back
    stack.cli("channel", "playback", SLUG, "on")
    p = play(stack, long, "--title", "doomed"); track_id(p)
    wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what="the track before the channel goes")
    stack.cli("channel", "remove", SLUG)
    rc, _o, err = finish(p, timeout=10)
    assert rc == 4 and "stopped" in err, f"removing the channel must end its track 'stopped': rc={rc} err={err!r}"
    stack.cli("undo")
    stack.pw.wait_node(node)
    wait_for(lambda: stack.cli("channel", "playback", SLUG, json_out=True)["playback"] is True, timeout=5.0, what="Playback restored by undo")
    stack.cli("cell", "set", SLUG, "stream", "0dB")


def test_ct10_the_queue_is_bounded(desk):
    """8 tracks per channel (sounding + waiting); the 9th is refused with the reason, nothing is dropped silently."""
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    long = tone(tmp / "q.wav", 440, 20)
    ids = []
    try:
        for i in range(8):
            r = stack.cli("channel", "play", SLUG, str(long), "--title", f"t{i}")
            ids.append(int(r.stdout.strip()))
        assert len(props(stack)["PlayQueue"]) == 7
        r = stack.cli("channel", "play", SLUG, str(long), check=False)
        assert r.returncode == 4 and "queue full" in r.stderr, (r.returncode, r.stderr)
    finally:
        stack.cli("channel", "stop", SLUG)
    wait_for(lambda: props(stack)["NowPlaying"] == "" and props(stack)["PlayQueue"] == [], timeout=5.0, what="queue empty after stop")


# ---- web bridge play endpoint (kmixdeck-web --play) ----------------------------------------------------------------
import http.client  # noqa: E402
import socket  # noqa: E402
import urllib.parse  # noqa: E402

BRIDGE = REPO / "web" / "kmixdeck-web"
SYSTEM_PYTHON = "/usr/bin/python3"
PLAY_TOKEN = "play-test-token"


def _bridge_usable():
    return Path(SYSTEM_PYTHON).exists() and subprocess.run([SYSTEM_PYTHON, "-c", "import gi, websockets"], capture_output=True).returncode == 0


class PlayBridge:
    """kmixdeck-web with --play on a free port, under the system python (Gio)."""
    def __init__(self, stack, max_bytes=None):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0)); self.port = s.getsockname()[1]
        args = [SYSTEM_PYTHON, str(BRIDGE), "--port", "0", "--token", "ui-token", "--play", f"127.0.0.1:{self.port}", "--play-token", PLAY_TOKEN]
        if max_bytes: args += ["--play-max-bytes", str(max_bytes)]
        self.proc = subprocess.Popen(args, env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        lines = []
        for _ in range(6):
            lines.append(self.proc.stdout.readline())
            if "play endpoint" in lines[-1]: break
        assert "play endpoint" in lines[-1], f"bridge did not start its play endpoint: {lines}"

    def request(self, method, path, body=None, token=PLAY_TOKEN, headers=None, timeout=60):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=timeout)
        h = dict(headers or {})
        if token is not None: h["Authorization"] = f"Bearer {token}"
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, (json.loads(data) if data else None)

    def close(self):
        self.proc.terminate()
        try: self.proc.wait(5)
        except subprocess.TimeoutExpired: self.proc.kill(); self.proc.wait()


@pytest.fixture()
def bridge(desk):
    if not _bridge_usable(): pytest.skip("the web bridge needs python3-gi and python3-websockets in the system python")
    stack, _tmp = desk
    b = PlayBridge(stack)
    yield b
    b.close()


def test_ct10_web_play_endpoint_plays_and_answers_after_the_clip(desk, bridge):
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    clip = tone(tmp / "web.flac", 1000, 4).read_bytes()
    out = {}
    def post():
        out["t0"] = time.monotonic()
        # a literal "%2B" in the title: decoded exactly once ("+" would mean it was decoded twice)
        out["r"] = bridge.request("POST", f"/channel/{SLUG}/play?title=" + urllib.parse.quote("TTS: 50%2B hello"), body=clip,
                                  headers={"Content-Type": "audio/flac"})
        out["t1"] = time.monotonic()
    th = threading.Thread(target=post, daemon=True); th.start()
    lvl = wait_level(lambda: stack.pw.level_at(f"kmixdeck.channel.{SLUG}"), lambda v: v > -50, what="the uploaded clip on the channel")
    assert lvl > -50
    assert "r" not in out, "the request answered before the clip ended"
    assert props(stack)["NowPlaying"] == "TTS: 50%2B hello", props(stack)["NowPlaying"]
    th.join(30)
    status, body = out["r"]
    assert status == 200 and body["ok"] is True and body["result"] == "played" and isinstance(body["id"], int), (status, body)
    assert out["t1"] - out["t0"] >= 3.5, f"200 came {out['t1'] - out['t0']:.1f} s after the POST — before a 4 s clip could have ended"
    assert bridge.request("GET", "/healthz", token=None) == (200, {"ok": True})


def test_ct10_web_play_endpoint_refuses_before_reading_the_body(desk, bridge):
    """401/404/409/411/413 are answered from the headers alone: we announce a large body and send none of it."""
    stack, _tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    def head_only(path, token=PLAY_TOKEN, length=10 << 20, extra=b""):
        with socket.create_connection(("127.0.0.1", bridge.port), timeout=5) as s:
            req = f"POST {path} HTTP/1.1\r\nHost: x\r\n"
            if token is not None: req += f"Authorization: Bearer {token}\r\n"
            if length is not None: req += f"Content-Length: {length}\r\n"
            s.sendall(req.encode() + extra + b"\r\n")
            data = b""
            while b"\r\n\r\n" not in data or len(data.split(b"\r\n\r\n", 1)[1]) == 0:
                chunk = s.recv(4096)
                if not chunk: break
                data += chunk
        status = int(data.split(b" ", 2)[1])
        return status, json.loads(data.split(b"\r\n\r\n", 1)[1])
    st, body = head_only(f"/channel/{SLUG}/play", token="wrong")
    assert st == 401 and body["ok"] is False, (st, body)
    st, body = head_only(f"/channel/{SLUG}/play", token=None)
    assert st == 401, (st, body)
    # the UI token is not a play token (least privilege)
    assert head_only(f"/channel/{SLUG}/play", token="ui-token")[0] == 401
    assert head_only("/channel/nosuch/play")[0] == 404
    assert head_only(f"/channel/{SLUG}/play", length=None)[0] == 411
    assert head_only(f"/channel/{SLUG}/play", length=None, extra=b"Transfer-Encoding: chunked\r\n")[0] == 411
    st, body = head_only(f"/channel/{SLUG}/play", length=(64 << 20) + 1)
    assert st == 413 and "too large" in body["error"], (st, body)
    stack.cli("channel", "playback", SLUG, "off")
    try:
        wait_for(lambda: head_only(f"/channel/{SLUG}/play")[0] == 409, timeout=3.0, what="409 once Playback is off")
        st, body = head_only(f"/channel/{SLUG}/play")
        assert "playback is off" in body["error"], body
    finally:
        stack.cli("channel", "playback", SLUG, "on")
    # the bridge is still serving after all of that
    assert bridge.request("GET", "/healthz", token=None)[0] == 200


def test_ct10_web_play_endpoint_maps_results(desk, bridge):
    """decode error → 422 error: …; stopped → 410."""
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    st, body = bridge.request("POST", f"/channel/{SLUG}/play", body=b"definitely not audio " * 100)
    assert st == 422 and body["ok"] is False and body["result"].startswith("error:"), (st, body)
    clip = tone(tmp / "stopme.wav", 440, 10).read_bytes()
    out = {}
    th = threading.Thread(target=lambda: out.setdefault("r", bridge.request("POST", f"/channel/{SLUG}/play", body=clip)), daemon=True)
    th.start()
    wait_for(lambda: props(stack)["NowPlaying"] != "", timeout=5.0, what="the web clip to sound")
    stack.cli("channel", "stop", SLUG)
    th.join(10)
    st, body = out["r"]
    assert st == 410 and body["result"] == "stopped", (st, body)


def test_ct10_web_queue_full_is_the_daemons_answer(desk, bridge):
    """The queue limit lives in the daemon only. A full queue refused there reaches the client as 503 with the daemon's
    reason — mapped from the D-Bus error NAME (org.kmixdeck1.Error.QueueFull), not from a copy of the limit in the
    bridge and not from the wording of the message."""
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    long = tone(tmp / "full.wav", 440, 20)
    try:
        for i in range(8):
            stack.cli("channel", "play", SLUG, str(long), "--title", f"t{i}")
        assert len(props(stack)["PlayQueue"]) == 7
        st, body = bridge.request("POST", f"/channel/{SLUG}/play", body=tone(tmp / "ninth.wav", 1000, 1).read_bytes())
        assert st == 503 and body["ok"] is False and "queue full" in body["error"], (st, body)
        assert len(props(stack)["PlayQueue"]) == 7, "the refused clip must not have been queued"
    finally:
        stack.cli("channel", "stop", SLUG)
    wait_for(lambda: props(stack)["NowPlaying"] == "" and props(stack)["PlayQueue"] == [], timeout=5.0, what="queue empty after stop")


def test_ct10_web_client_that_disconnects_while_queued_is_never_played(desk, bridge):
    """A TTS client that gives up (timeout) while its clip waits behind another track: the bridge stops it, so it is
    never played late. Proven acoustically: the queued clip is a 1 kHz tone, the track in front of it 440 Hz."""
    stack, tmp = desk
    stack.cli("channel", "playback", SLUG, "on")
    node = f"kmixdeck.channel.{SLUG}"
    front = tone(tmp / "front440.wav", 440, 4)
    late = tone(tmp / "late1000.wav", 1000, 3).read_bytes()
    th, res = record_in_background(stack, node, 10.0)
    time.sleep(1.5)
    pf = play(stack, front, "--title", "front"); track_id(pf)
    s = socket.create_connection(("127.0.0.1", bridge.port), timeout=5)
    s.sendall((f"POST /channel/{SLUG}/play?title=late HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer {PLAY_TOKEN}\r\n"
               f"Content-Length: {len(late)}\r\n\r\n").encode() + late)
    wait_for(lambda: props(stack)["PlayQueue"] == ["late"], timeout=5.0, what="the web clip queued behind the front track")
    s.close()                                         # the client gives up
    wait_for(lambda: props(stack)["PlayQueue"] == [], timeout=3.0, what="the bridge to stop the abandoned clip")
    assert finish(pf)[0] == 0
    th.join(15)
    assert "wav" in res, res
    win = spectrogram(res["wav"], (440, 1000))
    assert sum(w[440] > 0.01 for w in win) >= 30, "the front track did not sound"
    leaked = [i for i, w in enumerate(win) if w[1000] > 0.01]
    assert not leaked, f"the abandoned clip was played late ({len(leaked)} windows of 50 ms)"


def test_ct10_web_healthz_follows_the_daemon(desk, bridge):
    stack, _tmp = desk
    assert bridge.request("GET", "/healthz", token=None) == (200, {"ok": True})
    stack.daemon.terminate(); stack.daemon.wait(10)
    try:
        wait_for(lambda: bridge.request("GET", "/healthz", token=None)[0] == 503, timeout=5.0, what="healthz 503 without the daemon")
        st, body = bridge.request("POST", f"/channel/{SLUG}/play", body=b"x" * 100)
        assert st == 503, (st, body)
    finally:
        stack.restart_daemon()
        stack.pw.wait_node(f"kmixdeck.channel.{SLUG}")
    wait_for(lambda: bridge.request("GET", "/healthz", token=None)[0] == 200, timeout=8.0, what="healthz back with the daemon")
