# SPDX-FileCopyrightText: 2026 Raya Elena Solano
# SPDX-License-Identifier: GPL-3.0-or-later
"""AR-12, second half: frontend parity for the features that bring their own panel or measurement.

Split out of test_frontends_sync.py on 2026-10-07. That suite had grown to 28 tests and ~745 s under load
(389 s on 2026-09-22), so the 600 s ctest timeout killed it twice in a row with no failing test at all. The
core table (CORE, read by tools/sot-audit.py) and the small parity checks stay there; the long per-feature
tests live here: eight mixes, hidden devices, the FX and ducking panels, the soundboard, loudness and the
CT-6 capture switch. Same fixtures, same helpers, one more ctest entry.
"""
import json, subprocess, time
import pytest
from test_service_cli import make_fake_sink  # noqa: F401
from test_presentation import kde, stack  # noqa: F401
from test_ports import wait_level
from test_frontends_sync import tray, window, browser
from waiting import wait_for


def test_mx5_eight_mixes_all_faders_visible_and_bad_colour_refused(stack):
    """MX-5: eight mixes at once — every master fader and every cell fader of a channel is on screen (no hidden
    faders); the window's own scroll area may scroll, but each fader item has a size and is on the page. A colour
    that is not #rrggbb is refused by the daemon, the previous colour stays."""
    slugs = []
    try:
        for i in range(6):   # + monitor + stream = 8
            stack.cli("mix", "add", f"Mix {i + 3}")
        mixes = [m["Slug"] for m in stack.cli("status", json_out=True)["mixes"]]
        slugs = [m for m in mixes if m not in ("monitor", "stream")]
        stack.pw.wait_nodes([f"kmixdeck.mix.{sl}" for sl in slugs]); time.sleep(0.8)
        assert len(mixes) >= 8, mixes
        probes = []
        for m in mixes: probes += [f"mixFader/{m}.visible", f"mixFader/{m}.width", f"cell/voice/{m}.visible", f"cell/voice/{m}.height"]
        g = kde(stack, *sum((["--probe", p] for p in probes), []))
        hidden = [p for p in probes if p.endswith(".visible") and g.get(p) != "true"]
        flat = [p for p in probes if (p.endswith(".width") or p.endswith(".height")) and float(g.get(p, "0") or 0) < 8]
        assert not hidden and not flat, f"hidden: {hidden} flat: {flat}"
        # MX-5: columns share the width instead of scrolling. At 1280 px
        # six mixes fit without a scrollbar (headers fold to two rows), at 1600 px all eight do; the folded header keeps
        # every control (mute, master, listen, ⋮) and the master fader stays wide enough to grab.
        for sl in slugs[-2:]: stack.cli("mix", "remove", sl)
        six = [m for m in mixes if m not in slugs[-2:]]; time.sleep(0.8)
        g6 = kde(stack, "--size", "1280x760", "--probe", "mixStrip.needed", "--probe", "mixStrip.availableWidth", "--probe", "mixHeader/monitor.narrow",
                 "--probe", "mixFader/monitor.width", "--probe", "mixMute/monitor.visible", "--probe", "mixHeader/monitor.height", "--probe", "channelHeader/game.width")
        assert int(g6["mixStrip.needed"]) <= int(g6["mixStrip.availableWidth"]), f"six mixes must fit at 1280 px without scrolling: {g6}"
        assert g6["mixHeader/monitor.narrow"] == "true" and float(g6["mixFader/monitor.width"]) >= 60, g6
        assert g6["mixMute/monitor.visible"] == "true"
        g8 = kde(stack, "--size", "1600x760", "--probe", "mixStrip.needed", "--probe", "mixStrip.availableWidth") if len(six) == 6 else None
        for i in range(2): stack.cli("mix", "add", f"Mix {i + 7}")
        slugs = [m["Slug"] for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] not in ("monitor", "stream")]
        stack.pw.wait_nodes([f"kmixdeck.mix.{sl}" for sl in slugs]); time.sleep(0.8)
        g8 = kde(stack, "--size", "1600x760", "--probe", "mixStrip.needed", "--probe", "mixStrip.availableWidth")
        assert int(g8["mixStrip.needed"]) <= int(g8["mixStrip.availableWidth"]), f"eight mixes must fit at 1600 px: {g8}"
        for sl in slugs: stack.cli("mix", "remove", sl)
        slugs = []; time.sleep(0.8)
        gw = kde(stack, "--size", "1280x760", "--probe", "mixHeader/monitor.narrow", "--probe", "mixHeader/monitor.width")
        assert gw["mixHeader/monitor.narrow"] == "false", f"two mixes: the header is back to one row: {gw}"
        # bad colour
        stack.cli("mix", "color", "stream", "#3daee9")
        r = stack.cli("mix", "color", "stream", "blue", check=False); assert r.returncode != 0, r
        r = stack.cli("mix", "color", "stream", "#12345", check=False); assert r.returncode != 0, r
        assert next(m for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Color"] == "#3daee9"
        stack.cli("mix", "color", "stream", "none")
        assert next(m for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Color"] == ""
    finally:
        for sl in slugs: stack.cli("mix", "remove", sl, check=False)
        stack.cli("mix", "color", "stream", "none", check=False); stack.cli("channel", "color", "voice", "none", check=False)


def test_ch11_hidden_device_leaves_every_picker_stays_routable_and_comes_back(stack):
    """CH-11: hide a device → gone from the listening-device box, the mix Outputs menu, the channel Inputs menu, the
    AddDialog and the patchbay card list; still in `devices` (marked) and still routable by name; a hidden device that
    is IN USE stays visible where it is used. Unhide from the window's dialog handler → back everywhere."""
    from test_service_cli import make_fake_sink, make_fake_source
    make_fake_sink(stack, "fake.hide.out", "Hideable Out"); make_fake_source(stack, "fake.hide.in", "Hideable In"); time.sleep(0.8)
    def picker_state():
        g = kde(stack, "--probe", "listeningDeviceBox.count", "--probe", "hearingBar.deviceNames", "--probe", "mixHeader/stream.outputDeviceNames",
                "--probe", "channelHeader/voice.inputDeviceNames", "--probe", "patchbay.cardIds", open_page="patchbay")
        return g
    try:
        before = picker_state()
        assert "Hideable Out" in before["hearingBar.deviceNames"] and "Hideable Out" in before["mixHeader/stream.outputDeviceNames"]
        assert "Hideable In" in before["channelHeader/voice.inputDeviceNames"] and "dev/fake.hide.in" in before["patchbay.cardIds"] and "outdev/fake.hide.out" in before["patchbay.cardIds"]
        # hide both via CLI
        stack.cli("devices", "hide", "fake.hide.out"); stack.cli("devices", "hide", "fake.hide.in")
        assert set(stack.cli("devices", "hidden").stdout.split()) == {"fake.hide.out", "fake.hide.in"}
        assert "(hidden)" in [l for l in stack.cli("devices").stdout.splitlines() if "fake.hide.out" in l][0]
        assert "fake.hide.out" in stack.cli("devices", json_out=True), "hidden ≠ removed: still a device"
        hidden = picker_state()
        for k in ("hearingBar.deviceNames", "mixHeader/stream.outputDeviceNames"): assert "Hideable Out" not in hidden[k], (k, hidden[k])
        assert "Hideable In" not in hidden["channelHeader/voice.inputDeviceNames"]
        assert "dev/fake.hide.in" not in hidden["patchbay.cardIds"] and "outdev/fake.hide.out" not in hidden["patchbay.cardIds"]
        assert int(hidden["listeningDeviceBox.count"]) == int(before["listeningDeviceBox.count"]) - 1
        # still routable by name, and once in use it is visible where it is used
        stack.cli("mix", "output-add", "stream", "fake.hide.out")
        assert "fake.hide.out" in next(m for m in stack.cli("status", json_out=True)["mixes"] if m["Slug"] == "stream")["Outputs"]
        tone = stack.pw.play_into("kmixdeck.channel.game")
        try:
            assert wait_level(lambda: stack.pw.level_at("fake.hide.out"), lambda v: v > -50, tries=12) > -50, "a hidden device must still carry audio"
        finally:
            tone.kill(); tone.wait()
        inuse = picker_state()
        assert "Hideable Out" in inuse["mixHeader/stream.outputDeviceNames"] and "outdev/fake.hide.out" in inuse["patchbay.cardIds"], "in use → visible where used"
        stack.cli("mix", "output-remove", "stream", "fake.hide.out")
        # survives a daemon restart
        stack.restart_daemon(); time.sleep(1.5)
        assert set(stack.cli("devices", "hidden").stdout.split()) == {"fake.hide.out", "fake.hide.in"}
        # unhide through the window (the dialog's "Show again" handler) and via the tray's hidden-count
        assert kde(stack, "--gesture", "hide:fake.hide.out|0")[0].endswith("-> ok")
        stack.cli("devices", "unhide", "fake.hide.in")
        wait_for(lambda: not stack.cli("devices", "hidden").stdout.strip(), timeout=3.0, what="not stack.cli('devices', 'hidden').stdout.strip()")
        after = picker_state()
        assert "Hideable Out" in after["hearingBar.deviceNames"] and "Hideable In" in after["channelHeader/voice.inputDeviceNames"]
        assert "dev/fake.hide.in" in after["patchbay.cardIds"] and "outdev/fake.hide.out" in after["patchbay.cardIds"]
        r = stack.cli("devices", "hide", "kmixdeck.channel.game", check=False); assert r.returncode != 0, "our own nodes are not devices"
    finally:
        stack.cli("devices", "unhide", "fake.hide.out", check=False); stack.cli("devices", "unhide", "fake.hide.in", check=False)
        stack.cli("mix", "output-remove", "stream", "fake.hide.out", check=False)


def test_fx8_the_kde_window_can_open_the_fx_panel_and_greys_out_missing_plugins(stack):
    """FX-8 im KDE-Fenster — und der Grund, warum es diesen Test gibt.

    Zwei Funde vom 2026-09-21, beide vorher von KEINEM Test beruehrt:

    1. Das Effekt-Panel liess sich im Fenster GAR NICHT oeffnen. FxPanel.qml hatte eine
       Kirigami.FormLayout als Wurzel, Main.qml schob sie mit pushDialogLayer() — das
       verlangt eine Page. verifyPages() lehnte ab, PageRow.qml starb an "Value is null",
       und im Fenster passierte sichtbar nichts. Der Klick auf "Effects…" im Kanalkopf war
       tot. Gemerkt hat es niemand, weil die FX-Tests ueber CLI und Browser liefen und
       --self-test die Datei nur laedt, ohne sie zu pushen.
    2. Ein Effekt ohne installiertes LADSPA-Plugin sah wie jeder andere waehlbar aus.

    Der Test geht durch die Shell wie ein Nutzer: Panel oeffnen, Dropdown aufklappen,
    Eintraege lesen. Faellt (1) zurueck, findet der Probe das Dropdown nicht mehr; faellt
    (2) zurueck, ist `enabled` true oder der Paketname fehlt.
    """
    katalog = {t["type"]: t for t in json.loads(stack.cli("fx", "types").stdout)}
    fehlende = [typ for typ, s in katalog.items() if s.get("available") is False]
    vorhandene = [typ for typ, s in katalog.items() if s.get("available") is not False]
    assert vorhandene, "kein einziger Effekt verfuegbar — dann prueft der Test unten nichts"

    specs = ["fxAddType.count"]
    for typ in fehlende + vorhandene[:2]:
        specs += [f"fxAddType/{typ}.enabled", f"fxAddType/{typ}.text"]
    g = kde(stack, "--open", "fx/channel/voice", "--gesture", "fxaddopen",
            *sum((["--probe", s] for s in specs), []))

    # (1) Das Panel ist offen und das Dropdown gefuellt — sonst waere jeder Probe "<not found>".
    assert g["fxAddType.count"] == str(len(katalog)), (
        f"Dropdown zeigt {g['fxAddType.count']} Eintraege, der Daemon kennt {len(katalog)}: {g}")

    # (2) Fehlendes Plugin: gesperrt, mit Paketname am Eintrag.
    for typ in fehlende:
        assert g[f"fxAddType/{typ}.enabled"] == "false", f"{typ} ist waehlbar, obwohl das Plugin fehlt"
        paket = katalog[typ]["package"].split()[0]
        assert paket in g[f"fxAddType/{typ}.text"], (
            f"Paketname {paket!r} fehlt am Eintrag: {g[f'fxAddType/{typ}.text']!r}")
    # Gegengewicht: vorhandene Effekte MUESSEN waehlbar bleiben, sonst wuerde ein generell
    # kaputtes Dropdown die Pruefung oben gruen faerben.
    for typ in vorhandene[:2]:
        assert g[f"fxAddType/{typ}.enabled"] == "true", f"{typ} ist gesperrt, obwohl das Plugin da ist"


def Mixer_name(stack, slug):
    """Der Anzeigename eines Kanals, wie ihn das Fenster zeigt (die ComboBox listet Namen, nicht Slugs)."""
    return next(c["Name"] for c in stack.cli("channel", "list", json_out=True) if c["Slug"] == slug)


def test_fx9_ducking_panel_opens_in_the_window_and_configures_the_strength(stack):
    """FX-9 im Fenster (Regel 2): das Ducking-Panel oeffnet sich per pushDialogLayer, die vier Regler
    tragen die Grenzen des Daemons, und was dort steht ist dasselbe, was die CLI sieht.

    Der Test geht wie ein Nutzer durch die Shell — Panel oeffnen, Werte lesen. Faellt die Wurzel von
    ScrollablePage auf FormLayout zurueck (der FxPanel-Fehler vom 2026-09-21), findet der Probe die
    Regler nicht mehr und jeder Wert ist "<not found>". qmllint merkt das NICHT: die Datei ist
    syntaktisch einwandfrei, sie laesst sich nur nicht pushen.
    """
    stack.cli("duck", "set", "game", "--by", "voice", "--depth", "-18", "--threshold", "-25",
              "--attack", "30", "--release", "400")
    cli = stack.cli("--json", "duck", "show", "game", json_out=True)

    # duckPanelPushed.visible ist der einzige Probe, der beweist, dass das Panel WIRKLICH auf dem
    # Layer-Stack liegt: ein per createObject erzeugtes Panel haengt am Fenster und ist damit probebar,
    # auch wenn pushDialogLayer es abgelehnt hat (genau die Luecke, die FxPanel monatelang verdeckte).
    specs = ["duckPanelPushed.text", "duckBy.count", "duckReduction.text", "duckClear.enabled"]
    for key in ("depth", "threshold", "attack", "release"):
        specs += [f"duckParam/{key}.value", f"duckParam/{key}.from", f"duckParam/{key}.to",
                  f"duckParam/{key}.enabled"]
    g = kde(stack, "--open", "duck/game", *sum((["--probe", s] for s in specs), []))

    assert g["duckPanelPushed.text"] == "yes", (
        "pushDialogLayer hat das Panel NICHT gepusht — ist die Wurzel von DuckPanel.qml eine Page? " + str(g))

    # (1) Das Panel ist offen: die Trigger-Liste ist gefuellt — "Not ducked" plus die anderen Kanaele,
    #     der eigene Kanal fehlt (der Daemon lehnt Selbst-Ducking ab).
    kanaele = stack.cli("channel", "list", json_out=True)
    erwartet = 1 + len([c for c in kanaele if c["Slug"] != "game"])
    assert g["duckBy.count"] == str(erwartet), f"Trigger-Liste zeigt {g['duckBy.count']}, erwartet {erwartet}: {g}"

    # (2) Die eingestellte Staerke steht in den Reglern — dieselben Zahlen, die die CLI liefert.
    for key in ("depth", "threshold", "attack", "release"):
        assert float(g[f"duckParam/{key}.value"]) == cli[key], (
            f"{key}: Fenster {g[f'duckParam/{key}.value']}, CLI {cli[key]}")
        assert g[f"duckParam/{key}.enabled"] == "true", f"{key} ist gesperrt, obwohl ein Trigger gesetzt ist"

    # (3) Die Grenzen sind die des Daemons — ein Regler, der einen abgelehnten Wert erzeugen kann, ist kaputt.
    #     Gegenprobe zu den Zahlen: die CLI muss jeden Randwert annehmen und alles dahinter ablehnen.
    for key, (von, bis) in (("depth", (-60, 0)), ("threshold", (-60, 0)), ("attack", (2, 400)), ("release", (2, 800))):
        assert float(g[f"duckParam/{key}.from"]) == von and float(g[f"duckParam/{key}.to"]) == bis, (
            f"{key}: Regler {g[f'duckParam/{key}.from']}..{g[f'duckParam/{key}.to']}, Daemon {von}..{bis}")
        for rand in (von, bis):
            r = stack.cli("duck", "set", "game", f"--{key}", str(rand), check=False)
            assert r.returncode == 0, f"{key}={rand} ist Regler-Rand, aber die CLI lehnt ab: {r.stdout}{r.stderr}"
        r = stack.cli("duck", "set", "game", f"--{key}", str(von - 1 if von < bis else bis + 1), check=False)
        assert r.returncode == 4, f"{key} ausserhalb des Regler-Bereichs wurde angenommen: {r.stdout}{r.stderr}"

    # Der eigene Kanal darf NICHT in der Trigger-Liste stehen: der Daemon lehnt Selbst-Ducking ab, also
    # soll die Oberflaeche es nicht anbieten. duckBy.count oben zaehlt nur — dieser Probe liest die Namen.
    namen = kde(stack, "--open", "duck/game", "--probe", "duckBy.model")["duckBy.model"]
    assert Mixer_name(stack, "game") not in namen, f"eigener Kanal steht in der Trigger-Liste: {namen}"
    assert Mixer_name(stack, "voice") in namen, f"Trigger-Kanal fehlt in der Liste: {namen}"

    assert g["duckClear.enabled"] == "true"
    assert "dB" in g["duckReduction.text"], g["duckReduction.text"]
    stack.cli("duck", "clear", "game")


def test_fx9_badge_says_who_ducks_and_how_much_in_all_three_frontends(stack):
    """FX-9, Spec-Wortlaut: der abgesenkte Kanal MUSS ein "ducked by <channel>"-Abzeichen und die aktuelle
    Reduktion zeigen. Dreifach-Paritaet heisst hier: CLI-Baum, Web-Kanalzeile und KDE-Kanalkopf nennen
    denselben Trigger — sonst zeigt ein Frontend etwas anderes an als die anderen zwei.
    """
    stack.cli("duck", "set", "game", "--by", "voice", "--depth", "-18")
    try:

        # CLI: Baum (Text) und --json
        # Das Abzeichen haengt an der KANALZEILE (…"Game (game)  v voice"), nicht in einer eigenen Zeile.
        baum = stack.cli("tree").stdout
        gamezeile = next(z for z in baum.splitlines() if "(game)" in z)
        assert "v voice" in gamezeile, f"CLI-Baum nennt den Trigger nicht an der Kanalzeile: {gamezeile!r}"
        assert "v " not in next(z for z in baum.splitlines() if "(voice)" in z), (
            "der Trigger-Kanal selbst traegt ein Abzeichen")
        j = stack.cli("--json", "tree", json_out=True)
        kj = next(k for k in j["channels"] if k["slug"] == "game")
        assert kj["duckedBy"] == "voice" and kj["duckDepth"] == -18, f"JSON-Baum: {kj}"
        assert "duckReduction" in kj, f"laufende Reduktion fehlt im JSON: {kj}"

        # KDE-UI: Abzeichen am Kanalkopf
        g = kde(stack, "--probe", "channelDuckBadge/game.visible")
        assert g["channelDuckBadge/game.visible"] == "true", f"KDE-Abzeichen unsichtbar: {g}"

        # (Die Web-Kanalzeile prueft test_web.py::test_fx9_badge_in_the_channel_row — dort steht der Browser.)

        # Gegengewicht: ein NICHT abgesenkter Kanal zeigt kein Abzeichen — sonst waere oben jede Zeile gruen.
        g2 = kde(stack, "--probe", "channelDuckBadge/voice.visible")
        assert g2["channelDuckBadge/voice.visible"] in ("false", "<not found>"), (
            f"nicht abgesenkter Kanal traegt ein Abzeichen: {g2}")
    finally:
        stack.cli("duck", "clear", "game")


def test_ct8_soundboard_is_the_same_in_all_three_frontends(stack):
    """CT-8 (core → this file is required, see the header): one sample, three frontends, one state.

    Each frontend is asked the SAME question — is the pad there and does it say "playing" — and the answer has to
    match the daemon. The KDE side is probed through the real window (`--open soundboard --probe`), which is what
    catches a panel that compiles but never loads: FxPanel shipped broken for months because nothing ever opened it.
    """
    # The stack fixture is module-scoped, so the board may already exist from the CT-8 row in CORE — reuse it.
    # A SECOND board is not an option: the panel shows one board at a time (it picks the first, Main.qml:516), so
    # probing a pad on a second board finds nothing. Isolation therefore runs over the sample NAME, and the list
    # assertion below checks that this name is present rather than that it is alone.
    vorhanden = [c["Slug"] for c in stack.cli("status", json_out=True)["channels"] if c.get("Kind") == "soundboard"]
    slug = vorhanden[0] if vorhanden else stack.cli("channel", "add", "--soundboard", "Board", json_out=True)["slug"]
    wav = stack.pw.tone()
    stack.cli("sample", "add", slug, str(wav), "--name", "solo", check=False)
    try:
        # (1) CLI
        zeilen = stack.cli("--json", "sample", "list", slug, json_out=True)
        meine = [z for z in zeilen if z["name"] == "solo"]
        assert len(meine) == 1, zeilen
        assert meine[0]["sounding"] is False

        # (2) KDE window: the panel must actually LOAD and show this pad
        w = window(stack, "soundboardPanel.visible", f"samplePad/{slug}:solo.text",
                   f"samplePad/{slug}:solo.highlighted", open_page="soundboard")
        assert w[f"samplePad/{slug}:solo.text"] == "solo", w
        assert w["soundboardPanel.visible"] == "true", w
        assert w[f"samplePad/{slug}:solo.highlighted"] == "false", "the pad looked like it was playing before play"

        # (3) web UI
        b = browser(stack, f"samplePad:{slug}:solo.textContent", view="soundboard")
        assert "solo" in b[f"samplePad:{slug}:solo.textContent"], b

        # now play it and ask all three again — one state, three views
        stack.cli("sample", "play", "solo")
        wait_for(lambda: stack.cli("--json", "sample", "list", slug, json_out=True)[0]["sounding"] is True,
                 timeout=8.0, what="the CLI to see the sample sounding")
        # `highlighted`, not the label: --probe returns one line per probe, so a label with a newline in it comes
        # back truncated (measured 2026-09-22 — the probe showed just "solo" for a pad that read "solo\n▶ playing").
        w2 = window(stack, f"samplePad/{slug}:solo.highlighted", open_page="soundboard")
        assert w2[f"samplePad/{slug}:solo.highlighted"] == "true", w2
        b2 = browser(stack, f"samplePad:{slug}:solo.className", view="soundboard")
        assert "sounding" in b2[f"samplePad:{slug}:solo.className"], b2
    finally:
        # Stop THIS sample, not every sample: `sample stop ""` is stop-all and the stack is shared with the CORE
        # row, which has its own solo sounding. Then drop the board so the list assertions above stay true if
        # this test is ever run twice against one daemon.
        # Stop and unregister only MY sample: the board is shared with the CT-8 row in CORE, and `sample stop ""`
        # would be stop-all. Removing the sample keeps the shared board usable for whoever runs next.
        stack.cli("sample", "stop", "solo", check=False)
        stack.cli("sample", "remove", slug, "solo", check=False)


def test_ux18_loudness_is_visible_in_every_frontend(stack):
    """UX-18: the R128 numbers and the target are readable in all four frontends, not just over the bus.

    This row exists because the requirement sat on ✅ for two days while the UI side was missing entirely:
    daemon, bus, CLI and a test were all green, and `grep -ri loudness src/qml web/static` returned nothing.
    A bus property nobody can see is not a loudness meter — so each frontend is asked for the actual NUMBER
    here, and the reference is measured with ffmpeg (via tone_at_known_loudness) instead of assumed. A wrong
    unit, a wrong array index or a frontend reading M where it should read I therefore shows up as a number
    that disagrees with the reference, not as "some text arrived".
    """
    # Own mix, so switching the analyser on cannot disturb the other rows sharing this module-scoped stack.
    stack.cli("mix", "add", "R128", check=False)
    try:
        stack.cli("mix", "loudness", "r128", "on")
        stack.cli("mix", "loudness", "r128", "-16")
        # route the tone: a mix with no cell fed from a channel measures silence forever
        stack.cli("cell", "set", "voice", "r128", "1.0", check=False)

        tone, want = stack.pw.tone_at_known_loudness()
        play = subprocess.Popen(["pw-play", "-P", '{ application.name = "Ux18Parity" node.name = "ux18-parity" '
                                 'target.object = "kmixdeck.channel.voice" }', str(tone)],
                                env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            # (1) CLI — the reference path. The wait condition is STABILITY, not "a number arrived": measured
            # 2026-09-22, the first I above the -70 floor appears at t=0.25 s reading -26.72 LUFS, 6.7 LU below the
            # reference, because BS.1770 integrates over gated blocks and has barely any of them yet. It passes 1 LU
            # at t≈1.1 s and settles near -20.1. A wait on "I > -70" therefore samples mid-integration and the
            # frontend assertions below inherit a number the analyser itself would disown a second later.
            def stabil():
                a = json.loads(stack.cli("--json", "loudness", "--once").stdout).get("r128", [-70] * 4)[2]
                if a <= -70:
                    return False
                time.sleep(0.4)
                b = json.loads(stack.cli("--json", "loudness", "--once").stdout).get("r128", [-70] * 4)[2]
                return b > -70 and abs(b - a) < 0.3

            wait_for(stabil, timeout=25.0, what="the integrated loudness of mix r128 to settle (BS.1770 gating)")
            lu = json.loads(stack.cli("--json", "loudness", "--once").stdout)["r128"]
            m, s, i, tp = lu
            assert abs(i - want) < 2.0, f"integrated {i:.2f} LUFS should be near the measured reference {want:.2f} LUFS"

            def bus_i(): return json.loads(stack.cli("--json", "loudness", "--once").stdout)["r128"][2]

            def within(shown, lo, hi, tol=0.5):
                # I still creeps while a frontend starts up (measured 2026-09-28: bus -20.60 when read, KDE -20.1 a few
                # seconds later, a 0.503 LU "mismatch" that was only time). The frontend must lie inside the bus
                # values read right before and right after it, plus the unit-error tolerance.
                return min(lo, hi) - tol < shown < max(lo, hi) + tol

            # (2) KDE window: the readout row must be there AND carry the same numbers, plus the target line
            i0 = bus_i()
            w = window(stack, "lufsRow/r128.visible", "lufsI/r128.text", "lufsM/r128.text",
                       "lufsTP/r128.text", "lufsTarget/r128.text", "loudnessTarget/r128.visible")
            assert w["lufsRow/r128.visible"] == "true", f"the analyser is on but the KDE readout is hidden: {w}"
            assert w["loudnessTarget/r128.visible"] == "true", f"target line missing on the meter: {w}"
            assert w["lufsTarget/r128.text"] == "target -16", w
            # 0.5 LU between a frontend and the bus, not 3: the readouts are fed by the same signal at 25 Hz, so
            # anything larger is a unit error or the wrong array index, which is exactly what this row must catch.
            i1 = bus_i()
            assert within(float(w["lufsI/r128.text"]), i0, i1), f"KDE shows I={w['lufsI/r128.text']}, bus said {i0:.2f} → {i1:.2f}"
            assert abs(float(w["lufsM/r128.text"]) - m) < 2.0, f"KDE shows M={w['lufsM/r128.text']}, bus says {m:.2f}"
            assert w["lufsTP/r128.text"].startswith("TP "), w

            # (3) tray: the integrated value against the target, one glance
            i0 = bus_i()
            tr = tray(stack, "trayLufs/r128.visible", "trayLufs/r128.text")
            assert tr["trayLufs/r128.visible"] == "true", f"analyser on but tray line hidden: {tr}"
            assert "target -16" in tr["trayLufs/r128.text"], tr
            i1 = bus_i()
            assert within(float(tr["trayLufs/r128.text"].split()[0]), i0, i1), f"tray disagrees with the bus ({i0:.2f} → {i1:.2f}): {tr}"

            # (4) web UI
            i0 = bus_i()
            b = browser(stack, "lufsRow/r128.textContent", "lufsI/r128.textContent", "lufsTP/r128.textContent",
                        "loudnessTarget/r128.dataset.lufs")
            assert "target -16" in b["lufsRow/r128.textContent"], b
            assert b["loudnessTarget/r128.dataset.lufs"] == "-16", f"web target line at the wrong value: {b}"
            i1 = bus_i()
            assert within(float(b["lufsI/r128.textContent"]), i0, i1), f"web shows I={b['lufsI/r128.textContent']}, bus said {i0:.2f} → {i1:.2f}"
        finally:
            play.terminate()

        # switching it off must remove the readout everywhere — a frozen last reading is worse than no reading,
        # because it looks like a measurement of the current signal.
        stack.cli("mix", "loudness", "r128", "off")
        time.sleep(1.0)
        w3 = window(stack, "lufsRow/r128.visible")
        assert w3["lufsRow/r128.visible"] == "false", f"analyser off but the KDE readout is still there: {w3}"
        tr3 = tray(stack, "trayLufs/r128.visible")
        assert tr3["trayLufs/r128.visible"] == "false", f"analyser off but the tray line is still there: {tr3}"
    finally:
        stack.cli("mix", "remove", "r128", check=False)


def test_ux18_silence_after_a_tone_reads_as_the_floor_not_as_minus_2432_db(stack):
    """After the signal stops, every loudness value must sit at the -70 LUFS floor — not at -253 or -2432 dB.

    Found 2026-09-22 by watching M across the end of a 12 s tone: it went -20.25 → -253.54 → -2432.19 dB and
    those numbers went out over the bus into all four frontends. The guard in meters.cpp was `std::isfinite()`,
    which is true for -2432, because libebur128 only promises -HUGE_VAL for actual negative infinity and returns
    finite garbage for near-silence. ffmpeg's ebur128 — the reference tool — prints M:-163.2 for digital silence
    but reports I: -70.0 LUFS, the BS.1770 absolute gate, and that is the clamp this asserts.

    This is its own row because the parity test above measures while the tone PLAYS and never looks at what
    happens when it stops: with the clamp reverted, that test stayed green (verified 2026-09-22).
    """
    stack.cli("mix", "add", "Floor", check=False)
    try:
        stack.cli("mix", "loudness", "floor", "on")
        stack.cli("cell", "set", "voice", "floor", "1.0", check=False)
        tone, _ = stack.pw.tone_at_known_loudness()
        subprocess.run(["pw-play", "-P", '{ application.name = "Ux18Floor" node.name = "ux18-floor" '
                        'target.object = "kmixdeck.channel.voice" }', str(tone)],
                       env=stack.pw.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=40)
        # the tone has ended here; M's 400 ms window and S's 3 s window both run dry within ~4 s
        schlimmster = {}
        for _ in range(45):
            time.sleep(0.2)
            lu = json.loads(stack.cli("--json", "loudness", "--once").stdout).get("floor")
            if not lu:
                continue
            for name, v in zip(("M", "S", "I", "TP"), lu, strict=True):
                if v < schlimmster.get(name, 0.0):
                    schlimmster[name] = v
        assert schlimmster, "the analyser reported nothing at all after the tone"
        for name, v in schlimmster.items():
            assert v >= -70.0, f"{name} fell to {v:.2f} below the -70 LUFS floor after the tone ended: {schlimmster}"
        # The frontends must show the floor as a dash — but only for the values that HAVE a floor. Measured while
        # writing this: I stayed at -20.1 through the silence, and that is correct, not a bug. Integrated loudness
        # is cumulative over the whole programme per BS.1770; it is the number a streamer checks at the end of a
        # session, so it must NOT reset when nobody talks for a moment. M (400 ms) and S (3 s) are the sliding
        # windows that do run dry, so they are what the dash applies to.
        w = window(stack, "lufsM/floor.text", "lufsI/floor.text")
        assert w["lufsM/floor.text"] == "–", f"momentary shows {w['lufsM/floor.text']!r} for silence, expected a dash"
        assert float(w["lufsI/floor.text"]) < -10.0, f"integrated should still hold the programme value: {w}"
    finally:
        stack.cli("mix", "remove", "floor", check=False)


def test_ct6_channel_capture_switch_in_the_window_the_routing_page_and_the_web(stack):
    """CT-6 is tier full: CLI (test_routing), KDE window and web UI. The switch is flipped through each frontend's own
    menu entry and read back from the bus, so a menu that only looks right fails here."""
    from test_web import Web, _click, _menu_click
    from chrome_driver import Chrome, CHROME
    src = "kmixdeck.chsource.voice"
    try:
        # window: the row menu item turns it on, the bus and the graph agree
        g = kde(stack, "--gesture", "capture:voice", open_page="mixer")
        assert g == ["gesture capture:voice -> ok"], g
        wait_for(lambda: stack.cli("channel", "capture", "voice", json_out=True)["capture"] is True, timeout=4.0, what="Capture on after the window click")
        stack.pw.wait_node(src)
        # routing page: the source has its own row, named after the channel
        r = window(stack, "routingChannelCapture/voice.title", "routingChannelCapture/voice.subtitle", open_page="routing")
        assert "Voice" in r.get("routingChannelCapture/voice.title", ""), r
        # the same menu item turns it off again
        assert kde(stack, "--gesture", "capture:voice", open_page="mixer") == ["gesture capture:voice -> ok"]
        wait_for(lambda: stack.pw.node(src) is None, timeout=4.0, what="source gone after the second window click")
        assert stack.cli("channel", "capture", "voice", json_out=True)["capture"] is False
        assert window(stack, "routingChannelCapture/voice.title", open_page="routing").get("routingChannelCapture/voice.title", "<not found>").startswith("<not found")
        # web UI: the channel menu entry does the same
        if not CHROME: pytest.skip("no chrome/chromium for the web UI")
        web = Web(stack, token="")
        try:
            with Chrome(web.url, size=(1280, 800)) as ch:
                ch.wait("window.kmixdeck && window.kmixdeck.state.connected && document.querySelector('[data-probe=\"channelMenuButton/voice\"]')", 15)
                _click(ch, "channelMenuButton/voice"); _menu_click(ch, "Separate capture source")
                wait_for(lambda: stack.cli("channel", "capture", "voice", json_out=True)["capture"] is True, timeout=4.0, what="Capture on after the web click")
                stack.pw.wait_node(src)
                _click(ch, "channelMenuButton/voice"); _menu_click(ch, "Separate capture source")
                wait_for(lambda: stack.pw.node(src) is None, timeout=4.0, what="source gone after the second web click")
        finally:
            web.close()
    finally:
        stack.cli("channel", "capture", "voice", "off", check=False)


def test_ct10_playback_switch_and_stop_in_the_window_and_the_web(stack, tmp_path):
    """CT-10 is tier full: CLI (test_playback), KDE window and web UI. "Accept playback" is flipped through each
    frontend's own menu entry and read back from the bus; "Stop playback" ends a track that is really sounding
    (level on the channel sink before, silence and result 'stopped' after), and the title of the sounding track is
    what the window row and the web channel show."""
    from test_web import Web, _click, _menu_click
    from chrome_driver import Chrome, CHROME
    from test_service_cli import BIN
    slug, node = "voice", "kmixdeck.channel.voice"
    clip = tmp_path / "long.wav"
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=1000:duration=30:sample_rate=48000",
                    "-ac", "2", str(clip)], check=True)
    playback = lambda: stack.cli("channel", "playback", slug, json_out=True)   # noqa: E731

    def start_track(title):
        p = subprocess.Popen([str(BIN / "kmixdeck"), "channel", "play", slug, str(clip), "--title", title, "--wait"],
                             env=stack.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert p.stdout.readline().strip().isdigit()
        wait_level(lambda: stack.pw.level_at(node), lambda v: v > -50, what=f"'{title}' on {node}")
        return p

    def ended_stopped(p):
        _out, err = p.communicate(timeout=10)
        assert p.returncode == 4 and "stopped" in err, (p.returncode, err)
        wait_level(lambda: stack.pw.level_at(node), lambda v: v < -60, what="silence after Stop playback")

    p = None
    try:
        # window: the row menu item turns it on
        g = kde(stack, "--gesture", f"playback:{slug}", open_page="mixer")
        assert g == [f"gesture playback:{slug} -> ok"], g
        wait_for(lambda: playback()["playback"] is True, timeout=4.0, what="Playback on after the window click")
        # Stop is disabled while nothing plays — the gesture says so instead of clicking a dead item
        assert kde(stack, "--gesture", f"stopplayback:{slug}", open_page="mixer") != [f"gesture stopplayback:{slug} -> ok"]
        p = start_track("Window song")
        # the row knows the title (tooltip + the enabled Stop item come from it; a menu item exists only while open,
        # so the gesture below is what proves Stop is enabled: it refuses a disabled item)
        r = window(stack, f"channelHeader/{slug}.nowPlaying", open_page="mixer")
        assert r.get(f"channelHeader/{slug}.nowPlaying") == "Window song", r
        assert kde(stack, "--gesture", f"stopplayback:{slug}", open_page="mixer") == [f"gesture stopplayback:{slug} -> ok"]
        ended_stopped(p); p = None
        # the same item turns it off again
        assert kde(stack, "--gesture", f"playback:{slug}", open_page="mixer") == [f"gesture playback:{slug} -> ok"]
        wait_for(lambda: playback()["playback"] is False, timeout=4.0, what="Playback off after the second window click")

        # web UI: the channel menu entries do the same
        if not CHROME: pytest.skip("no chrome/chromium for the web UI")
        web = Web(stack, token="")
        try:
            with Chrome(web.url, size=(1280, 800)) as ch:
                ch.wait("window.kmixdeck && window.kmixdeck.state.connected && document.querySelector('[data-probe=\"channelMenuButton/voice\"]')", 15)
                _click(ch, f"channelMenuButton/{slug}"); _menu_click(ch, "Accept playback")
                wait_for(lambda: playback()["playback"] is True, timeout=4.0, what="Playback on after the web click")
                p = start_track("Web song")
                ch.wait("document.querySelector('[data-probe=\"channelNowPlaying/voice\"]') && "
                        "document.querySelector('[data-probe=\"channelNowPlaying/voice\"]').textContent.includes('Web song')", 5)
                _click(ch, f"channelMenuButton/{slug}"); _menu_click(ch, "Stop playback")
                ended_stopped(p); p = None
                ch.wait("!document.querySelector('[data-probe=\"channelNowPlaying/voice\"]')", 5)
                _click(ch, f"channelMenuButton/{slug}"); _menu_click(ch, "Accept playback")
                wait_for(lambda: playback()["playback"] is False, timeout=4.0, what="Playback off after the second web click")
        finally:
            web.close()
    finally:
        if p is not None and p.poll() is None:
            stack.cli("channel", "stop", slug, check=False); p.wait(10)
        stack.cli("channel", "playback", slug, "off", check=False)
