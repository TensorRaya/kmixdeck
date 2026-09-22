"""CT-8 soundboard — integration tests.

The point of every test here is ACOUSTIC: it is not enough that the bus accepts PlaySample and the daemon logs
something. A soundboard is only correct if the sample is measurable on the channel sink and, through the normal
routing, in the mix — and if it stays out where the user muted it. "The call returned 0" proves nothing; a silent
button fails live, on air, in front of an audience.

On patience: every level_at() is a ~1.5 s recording, so tries=6 is ~10 s of real waiting (see wait_level's
docstring — raising it blew the 600 s ctest limit once). The samples here are 3 s long, well inside that window.
"""
import json
import re
import subprocess
import time
from pathlib import Path

import pytest

from pw_sandbox import start_private_pipewire
from test_ports import wait_level
from test_service_cli import Stack

REPO = Path(__file__).resolve().parents[2]


def make_audio(path: Path, seconds: float = 3.0, freq: int = 440, rate: int = 48000) -> Path:
    """A real file on disk, written by ffmpeg — not a fixture the sampler might treat specially.

    3 s is deliberately longer than one measurement: a 0.2 s blip could slip between two recordings and then the
    test would prove nothing about whether audio actually flows.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    codec = {".wav": "pcm_s16le", ".flac": "flac", ".ogg": "libvorbis", ".mp3": "libmp3lame"}[path.suffix]
    r = subprocess.run(["ffmpeg", "-y", "-f", "lavfi",
                        "-i", f"sine=frequency={freq}:duration={seconds}:sample_rate={rate}",
                        "-c:a", codec, str(path)], capture_output=True, text=True)
    assert r.returncode == 0, f"ffmpeg could not write {path}: {r.stderr[-400:]}"
    assert path.exists() and path.stat().st_size > 500, f"{path} is empty"
    return path


def samples_prop(stack):
    """The Mixer.Samples property straight off the bus — what the web UI and the KDE client actually subscribe to.
    A manual stop that never reaches this property leaves every client showing "playing"."""
    roh = stack.busctl("get-property", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "Samples").stdout.strip()
    # busctl prints `aa{sv} 1 6 "channel" s "board" ... "sounding" b true` — read that one field, not any "true"
    # in the row (a path or a name could contain it).
    return re.findall(r'"sounding" b (true|false)', roh)


def _drain(proc):
    """Everything the bus monitor has printed so far, without blocking. os.read on a non-blocking fd: the monitor
    keeps running, so a plain .read() would hang until it exits."""
    import fcntl, os
    fd = proc.stdout.fileno()
    fcntl.fcntl(fd, fcntl.F_SETFL, fcntl.fcntl(fd, fcntl.F_GETFL) | os.O_NONBLOCK)
    out = []
    while True:
        try:
            teil = os.read(fd, 65536)
            if not teil: break
            out.append(teil.decode(errors="replace"))
        except BlockingIOError: break
    return "".join(out)


def wait_link(fn, pred, tries=60, dt=0.1, what="link"):
    """Poll a cheap graph query until pred holds; raise with the last value. Like test_lifecycle's wait(), but it
    reports WHAT it last saw — a bare False tells you nothing when a link lands on the wrong node."""
    zuletzt = None
    for _ in range(tries):
        zuletzt = fn()
        if pred(zuletzt): return zuletzt
        time.sleep(dt)
    raise AssertionError(f"{what} never held in {tries * dt:.0f}s; last value {zuletzt!r}")


@pytest.fixture(scope="module")
def board(tmp_path_factory):
    """One daemon, one soundboard channel with one sample, routed into the stream mix at unity."""
    pw = start_private_pipewire()
    pw.wait_node("kmixdeck.mix.stream")
    stack = Stack(pw)
    tmp = tmp_path_factory.mktemp("ct8")
    wav = make_audio(tmp / "jingle.wav")
    slug = stack.cli("channel", "add", "--soundboard", "Board", json_out=True)["slug"]
    stack.cli("sample", "add", slug, str(wav), "--name", "jingle")
    # Unity into the stream mix: a level on the mix then proves the sample travels the NORMAL signal path
    # (channel sink → cell → mix), not some private shortcut of the sampler.
    stack.cli("cell", "set", slug, "stream", "0dB")
    stack.pw.wait_node(f"kmixdeck.channel.{slug}")
    yield stack, slug, wav, tmp
    stack.close()
    pw.close()


def test_ct8_sample_is_audible_on_channel_and_in_the_mix(board):
    """CT-8: a registered sample produces measurable audio on the channel AND in the routed mix."""
    stack, slug, _wav, _tmp = board
    node = f"kmixdeck.channel.{slug}"
    still = stack.pw.level_at(node)
    assert still < -60, f"channel {node} was not silent before playback ({still:.2f} dB)"

    stack.cli("sample", "play", "jingle")
    ch = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what=f"sample on {node}")
    assert ch > -50, f"sample 'jingle' produced no audio on {node} (last reading {ch:.2f} dB)"

    stack.cli("sample", "play", "jingle")   # the 3 s file has run out by now — play again for the mix reading
    mx = wait_level(lambda: stack.pw.level_at("kmixdeck.mix.stream"), lambda v: v > -60, what="sample in stream mix")
    assert mx > -60, f"sample 'jingle' never reached the stream mix (last reading {mx:.2f} dB)"

    stack.cli("sample", "stop", "jingle")
    quiet = wait_level(lambda: stack.pw.level_at(node), lambda v: v < -60, what=f"silence on {node}")
    assert quiet < -60, f"channel {node} kept sounding after 'sample stop' (last reading {quiet:.2f} dB)"


def test_ct8_a_muted_cell_keeps_the_sample_out_of_that_mix(board):
    """CT-8's whole design claim: a sample obeys the per-mix cell like any other source.

    Mute the board's cell and the jingle must NOT be in that mix, while still sounding on the channel itself.
    That is what "samples go through the channel" has to mean in practice.
    """
    stack, slug, _wav, _tmp = board
    node = f"kmixdeck.channel.{slug}"
    stack.cli("cell", "mute", slug, "stream", "on")
    try:
        stack.cli("sample", "play", "jingle")
        ch = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what=f"sample on {node}")
        assert ch > -50, f"sample did not sound on the channel at all (last reading {ch:.2f} dB)"
        mx = stack.pw.level_at("kmixdeck.mix.stream")
        assert mx < -60, f"the muted cell still leaked the sample into the stream mix ({mx:.2f} dB)"
    finally:
        stack.cli("sample", "stop", "")
        stack.cli("cell", "mute", slug, "stream", "off")


def test_ct8_soundboard_is_opt_in(board):
    """CT-8: the kind is opt-in. A normal channel has no sample behaviour and refuses it with a real reason."""
    stack, _slug, wav, _tmp = board
    plain = stack.cli("channel", "add", "Plain", json_out=True)["slug"]
    try:
        r = stack.cli("sample", "add", plain, str(wav), "--name", "nope", check=False)
        assert r.returncode != 0, "registering a sample on a normal channel must fail, not silently succeed"
        assert "not a soundboard" in (r.stderr + r.stdout), (
            f"unhelpful refusal: {r.stderr.strip() or r.stdout.strip()}")
        # And it must not show up as a board, so no UI would draw one for it.
        assert plain not in [s["channel"] for s in stack.cli("sample", "list", json_out=True)], (
            f"plain channel {plain} appeared in the sample list")
        assert stack.cli("channel", "list", json_out=True), "channel list broke"
        kind = next(c.get("Kind", "") for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == plain)
        assert kind == "", f"a normal channel reports Kind={kind!r}, so every UI would treat it as a board"
    finally:
        stack.cli("channel", "remove", plain)


def test_ct8_an_unplayable_file_is_refused_at_registration(board):
    """A dead button is the worst failure mode, so a file that cannot decode is refused up front, not on press."""
    stack, slug, _wav, tmp = board
    junk = tmp / "notaudio.wav"
    junk.write_text("this is not audio")
    r = stack.cli("sample", "add", slug, str(junk), "--name", "junk", check=False)
    assert r.returncode != 0, "an undecodable file must be refused at registration"
    assert str(junk) in (r.stderr + r.stdout), f"the refusal does not name the file: {(r.stderr + r.stdout).strip()}"

    r2 = stack.cli("sample", "add", slug, str(tmp / "gone.wav"), "--name", "gone", check=False)
    assert r2.returncode != 0, "a missing file must be refused at registration"
    assert "no such file" in (r2.stderr + r2.stdout).lower(), (
        f"the refusal does not say the file is missing: {(r2.stderr + r2.stdout).strip()}")


@pytest.mark.parametrize("suffix", [".wav", ".flac", ".ogg", ".mp3"])
def test_ct8_every_required_format_actually_sounds(board, suffix):
    """CT-8 names wav/flac/ogg/mp3. Registering is not proof — each one has to be AUDIBLE.

    A format that decodes to silence (wrong sample format, wrong planar handling) would pass a registration-only
    test and fail on stage, which is exactly the failure this file exists to prevent.
    """
    stack, slug, _wav, tmp = board
    name = f"fmt{suffix[1:]}"
    f = make_audio(tmp / f"{name}{suffix}")
    stack.cli("sample", "add", slug, str(f), "--name", name)
    node = f"kmixdeck.channel.{slug}"
    try:
        stack.cli("sample", "play", name)
        lvl = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what=f"{suffix} sample on {node}")
        assert lvl > -50, f"{suffix} registered but produced no audio (last reading {lvl:.2f} dB)"
    finally:
        stack.cli("sample", "stop", "")
        stack.cli("sample", "remove", slug, name)


def test_ct8_samples_survive_a_daemon_restart(board):
    """The registry lives in the layout, so samples must come back after a restart — and still make sound."""
    stack, slug, _wav, _tmp = board
    before = stack.cli("sample", "list", json_out=True)
    assert any(s["name"] == "jingle" for s in before), f"'jingle' missing before the restart: {before}"
    stack.restart_daemon()
    stack.pw.wait_node(f"kmixdeck.channel.{slug}")
    after = stack.cli("sample", "list", json_out=True)
    assert any(s["name"] == "jingle" for s in after), f"'jingle' did not survive the restart: {after}"
    # Being listed is not enough — it has to still play.
    node = f"kmixdeck.channel.{slug}"
    stack.cli("sample", "play", "jingle")
    lvl = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what=f"'jingle' after restart on {node}")
    assert lvl > -50, f"'jingle' was listed after the restart but produced no audio (last reading {lvl:.2f} dB)"
    stack.cli("sample", "stop", "jingle")


def test_ct8_play_by_name_needs_no_channel_and_unknown_names_are_errors(board):
    """Mixer.PlaySample(name) is the form a Stream Deck or Home Assistant button uses: a name, no channel.

    An unknown name must come back as a bus ERROR, not a no-op — otherwise the button silently lies.
    """
    stack, _slug, _wav, _tmp = board
    r = stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer", "PlaySample", "s", "jingle")
    assert r.returncode == 0, f"PlaySample('jingle') failed on the bus: {r.stderr.strip()}"
    stack.cli("sample", "stop", "")

    bad = stack.busctl("call", "org.kmixdeck1", "/org/kmixdeck1", "org.kmixdeck1.Mixer",
                       "PlaySample", "s", "doesnotexist")
    assert bad.returncode != 0, "PlaySample with an unknown name returned success — the button would lie"
    assert "doesnotexist" in bad.stderr, f"the error does not name the sample: {bad.stderr.strip()}"


def test_ct8_all_three_frontends_expose_the_soundboard(board):
    """Triple parity (the project's ground rule): CLI, the bus/web API and the KDE window all know CT-8."""
    stack, slug, _wav, _tmp = board
    # 1) CLI
    listed = stack.cli("sample", "list", json_out=True)
    assert any(s["name"] == "jingle" and s["channel"] == slug for s in listed), f"CLI lists nothing: {listed}"
    # 2) the bus property the web UI renders from (it reacts to PropertiesChanged, it never polls)
    r = stack.busctl("get-property", "--json=short", "org.kmixdeck1", "/org/kmixdeck1",
                     "org.kmixdeck1.Mixer", "Samples")
    assert r.returncode == 0, f"the Mixer.Samples property failed: {r.stderr.strip()}"
    payload = json.loads(r.stdout)
    assert payload["type"] == "aa{sv}", f"Samples is on the wire as {payload['type']}, the XML promises aa{{sv}}"
    names = [row["name"]["data"] for row in payload["data"]]
    assert "jingle" in names, f"Mixer.Samples does not contain the sample: {names}"
    # 3) the web asset has to be WIRED IN, not just present — a file nobody imports is dead code, not a feature
    web = REPO / "web" / "static"
    assert (web / "soundboard.js").exists(), "web/static/soundboard.js is missing"
    assert "soundboard.js" in (web / "app.js").read_text(), "soundboard.js is imported nowhere in app.js"
    assert 'data-view="soundboard"' in (web / "index.html").read_text(), "no soundboard tab in index.html"
    # 4) same for the KDE UI
    qml = REPO / "src" / "qml" / "SoundboardPanel.qml"
    assert qml.exists(), "SoundboardPanel.qml is missing — the KDE window would have no soundboard"
    assert "SoundboardPanel {}" in (REPO / "src" / "qml" / "Main.qml").read_text(), (
        "SoundboardPanel.qml exists but Main.qml never instantiates it")
    assert "qml/SoundboardPanel.qml" in (REPO / "src" / "CMakeLists.txt").read_text(), (
        "SoundboardPanel.qml is not registered in the QML module, so the window cannot load it at runtime")

def test_ct8_samples_run_through_the_channels_fx_chain(board):
    """The spec's words: "Samples play through the channel's FX chain ... like any source."

    Checks the LINK, not the level. Measured 2026-09-22 (pw-link -l): for a channel the chain sits in front of the
    plain sink — kmixdeck.sample → kmixdeck.fx.<slug> → kmixdeck.fx.<slug>.out → kmixdeck.channel.<slug> — so the
    sink is post-chain whichever node the voice entered, and a level reading cannot tell the two apart. Two earlier
    level-based versions of this test stayed green with the fix reverted; this one goes red (verified by
    counter-check), because with the bug the voice is linked straight to kmixdeck.channel.<slug>.

    Root cause it locks down: Mixer::playSample targeted Names::channelNode() unconditionally, and a sounding voice
    cannot be retargeted later — it is created with node.dont-reconnect, and the PipeWire docs say such a node "is
    initially linked to target.object ... If the target is removed, the node is destroyed", so moveStream() (which
    only rewrites WirePlumber's target.object metadata) has no effect on it. Hence fxTarget() on start plus
    Mixer::restartSoundingSamples() when a chain is switched on underneath a sounding sample.
    """
    stack, slug, _wav, tmp = board
    node = f"kmixdeck.channel.{slug}"
    entry = f"kmixdeck.fx.{slug}"
    lang = make_audio(tmp / "fxprobe.wav", seconds=10.0)
    stack.cli("sample", "add", slug, str(lang), "--name", "fxprobe")
    try:
        # (a) chain already active when the sample starts → the voice must enter the chain, not the sink
        stack.cli("fx", "set", "channel", slug,
                  '{"enabled":true,"chain":[{"type":"gate","enabled":true,'
                  '"params":{"threshold":-40,"range":-90,"hold":10,"decay":50}}]}')
        wait_link(lambda: entry in stack.pw.node_names(), lambda d: d, what=f"{entry} to appear")
        stack.cli("sample", "play", "fxprobe")
        ziel = wait_link(lambda: stack.pw.sink_of("kmixdeck.sample"), lambda z: z != "",
                        what="the sampler voice to be linked anywhere")
        assert ziel == entry, (
            f"the sample bypasses the FX chain: its output is linked to {ziel!r}, expected {entry!r}. "
            f"Mixer::playSample must target the fx entry while a chain is active (ADR 0008).")
        # and it is really audible through that chain, not just wired up
        pegel = wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50,
                           what=f"sample audible through the chain on {node}")
        assert pegel > -50, f"wired through the chain but silent ({pegel:.2f} dB)"
        stack.cli("sample", "stop", "")

        # (b) chain switched on WHILE the sample sounds → the voice has to be replayed into the chain
        stack.cli("fx", "set", "channel", slug, '{"enabled":false,"chain":[]}')
        wait_link(lambda: entry not in stack.pw.node_names(), lambda d: d, what=f"{entry} to disappear")
        stack.cli("sample", "play", "fxprobe")
        assert wait_link(lambda: stack.pw.sink_of("kmixdeck.sample"), lambda z: z == node,
                        what="the voice to start on the plain sink") == node
        stack.cli("fx", "set", "channel", slug,
                  '{"enabled":true,"chain":[{"type":"gate","enabled":true,'
                  '"params":{"threshold":-40,"range":-90,"hold":10,"decay":50}}]}')
        danach = wait_link(lambda: stack.pw.sink_of("kmixdeck.sample"), lambda z: z == entry,
                          what="the sounding voice to be replayed into the new chain")
        assert danach == entry, (
            f"switching the chain on left the sounding sample outside it (linked to {danach!r}). "
            f"A dont-reconnect voice cannot be retargeted, so it must be replayed into {entry!r}.")
    finally:
        stack.cli("sample", "stop", "")
        stack.cli("fx", "set", "channel", slug, '{"enabled":false,"chain":[]}')
        stack.cli("sample", "remove", slug, "fxprobe")

def test_ct8_a_manual_stop_is_announced_to_every_client(board):
    """A manual stop has to be ANNOUNCED, not merely be true when someone asks.

    The Samples property is computed on every read, so polling it looks fine even when the daemon stays silent —
    measured 2026-09-22, which is why the first version of this test passed with the fix reverted. The clients do
    not poll: the web UI and the KDE client render from PropertiesChanged. Sampler::stop() destroyed the voice
    without emitting finished(), so no signal went out and every pad kept showing "playing" until something else
    happened to trigger a refresh. This test listens on the bus, exactly like a client does.
    """
    stack, slug, _wav, _tmp = board
    node = f"kmixdeck.channel.{slug}"
    mon = subprocess.Popen(["busctl", "--user", "monitor", "--match",
                            "type='signal',interface='org.freedesktop.DBus.Properties',"
                            "member='PropertiesChanged',path='/org/kmixdeck1'"],
                           env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        time.sleep(0.8)                              # let the monitor attach before anything happens
        stack.cli("sample", "play", "jingle")
        wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what="the sample to be audible")
        vor = _drain(mon)
        assert vor, "starting a sample announced nothing on the bus"

        stack.cli("sample", "stop", "")
        nach = wait_link(lambda: _drain(mon), lambda s: s != "", tries=40,
                         what="a PropertiesChanged after the manual stop")
        assert "Samples" in nach, f"the stop was not announced as a Samples change: {nach[:400]!r}"
        pegel = stack.pw.level_at(node)
        assert pegel < -60, f"announced as stopped but still audible ({pegel:.2f} dB)"
    finally:
        mon.terminate(); mon.wait(timeout=5)
        stack.cli("sample", "stop", "")
