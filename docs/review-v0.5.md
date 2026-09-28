# Code-Review v0.5 — Zeiger + Journal

Stand: 2026-09-28, Basis `db7f2cd` (release/0.3). Ohne Squash, ohne Release — das kommt danach.

## Zeiger

| # | Punkt | Status | Befund |
|---|---|---|---|
| 1 | Sauberer Code | **ok nach Fix** | ruff 0; qmllint mit Importpfad 491 Warnungen: 482 `unqualified` (i18n aus dem Kontext, `modelData` ohne `required`), 3 `comma` (bewusster Abhängigkeitstrick `devicePortsVersion, …`), 6 `missing-property`. Davon 1 echt: doppelte `accessibleName`-Zuweisung am Mix-Master mit nicht existierendem `header.title` → entfernt. Rest: qmllint kennt den Root-Typ einer eigenen Datei nicht (ColorMenu) bzw. Popup-Kinder (MixHeader:377). Tote Variable `took` in `tools/measure-dv30c.py` entfernt. |
| 2 | Keine internen Referenzen | **ok nach Fix** | 4 Personenzitate in Code-Kommentaren (ChannelHeader, Main, LevelMeter, test_layout) neutralisiert; in requirements.md „Michel“ → „owner“. Ui24R/RØDECaster bleiben: öffentliche Produkte als Beispielgeräte. ADR-Owner-Felder bleiben (üblich). Neuer Dauercheck `tools/pruefe-hygiene.py` (ctest `hygiene`, Label fast). |
| 3 | Portierbarkeit | **ok** | XDG über `QStandardPaths`; LADSPA-Pfade `LADSPA_PATH` + lib/lib64/local + Multiarch-Triplet vom Compiler; systemd-Unit-Dir per CMake; keine x86-Annahmen im Code. |
| 4 | Best Practices | **ok** | QSaveFile für layout.json, pipewire.conf, CLI-Export; Layout-Versionsfeld mit Downgrade-Schutz (PS-2); Debounce + Hash statt mtime am Watcher. |
| 5 | Tests | **ok, dünn im Unit-Teil** | 3 Unit-Suiten (129 QVERIFY/QCOMPARE), 17 fast-Tests grün. Der Großteil der Logik ist nur integrativ belegt — kein Blocker, aber Kandidat nach v0.1. |
| 6 | Integrationstest | **ok, 2 Flakes offen** | 14 Suiten, 199 Tests, Vollgate 2026-09-28 GATEDONE (alle grün, ports ×2); dv30c 10/10 unter Last. Gegenprobe nach den Fixes: je 1 Einzelrot in `test_web::test_ar8_web_reconnects_after_the_bridge_restarts` (RuntimeError) und `test_frontends_sync::test_ux18_silence_after_a_tone…`; Wiederholung web 4× 16/16, frontends_sync 27/27. Beide berühren keinen geänderten Pfad — Flakes, Ursache noch nicht gefunden. |
| 7 | Anforderungen | **ok** | sot-audit: 123 ✅ · 0 🔶 · 0 📝; 8 `tier:core` in allen Frontends belegt; `pruefe-suiten`: alle 14 Suiten in ctest. |
| 8 | Persistenzschicht | **2 Fehler, gefixt** | `Mixer::saveScene` und `MixerClient::exportToFile` schrieben mit `QFile` + Truncate → Absturz beim Schreiben hinterlässt halbe Datei. Beide auf `QSaveFile` + `commit()`-Prüfung. Check war vorher rot (2 Treffer), jetzt grün. |
| 9 | Plug & Play | **ok** | D-Bus-Aktivierung (`org.kmixdeck1.service` → `SystemdService=kmixdeckd.service`); fehlendes layout.json → Starter-Layout (test_lifecycle `…default_layout_on_missing_file`); externe Änderung ohne Neustart übernommen, eigene Writes ohne Schleife, kaputte Datei abgewiesen (B2-Tests); Hotplug DV-13. |
| 10 | Doku | **offen für Release** | Sprachmix: CONTRIBUTING, cli.md, ADR 0012/0013 teils deutsch, Rest englisch. Interne Arbeitsdokumente im Repo: `docs/schlachtplan-v0.4.md`, `docs/review-*`. Für v0.1 entscheiden: vereinheitlichen und Arbeitsdokumente raus. |

## Journal

- FAKT: `ruff check .` → 0 Befunde.
- FAKT: `qmllint` ohne `-I build/bin` → 1017 Warnungen (Import nicht gefunden); mit → 491, Verteilung s. Zeiger 1.
- FAKT: `MixHeader.qml:277` `accessibleName: i18n(…, header.title)` — `header` hat kein `title`; Zeile 288 setzt `Accessible.name` korrekt. test_presentation prüft den Namen über Zeile 288 → Entfernen ändert nichts am Verhalten.
- FAKT: `git grep -i michel` in src/web/streamdeck/interfaces/data/tools/tests → 4 Code-Kommentare + requirements.md; nach Fix 0 im Code.
- FAKT: `pruefe-hygiene.py` erster Lauf: rot, 2 QFile-Writer in src (+3 Test-Fixtures, bewusst ausgenommen). Nach Fix: ok.
- FAKT: Build + `ctest -L fast` → 17/17 (vorher 16, +hygiene).
- FAKT: Gegenprobe (service_cli 44/44, web 15/16, presentation 16/16, layout 7/7, frontends_sync 26/27). Rot: ar8-reconnect (RuntimeError aus chrome_driver `_cmd`/`eval`), ux18-floor.
- FAKT: Wiederholung unter Last < 3: web 16/16 ×4, frontends_sync 27/27. HYPOTHESE: ar8 = Chrome-DevTools-Aufruf während die Seite neu verbindet (eval wirft statt False); ux18 = Timing der 400-ms-Fenster. Nicht belegt → offen.
