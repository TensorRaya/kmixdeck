# Schlachtplan bis „alles fertig" (2026-09-20, Michels Auftrag)

Reihenfolge von Michel entschieden: **Gate grün → v0.3.0 taggen → CLI v0.4 →
Parität → Features.** Kein Squash, kein Tag, solange das Gate nicht grün ist.

Grundregel für jede Zeile hier: eine Anforderung gilt erst als fertig, wenn sie
in **allen drei Oberflächen** vorhanden ist, in denen sie Sinn hat — KDE (QML),
Web, CLI. Das ist der Auftrag von Michel („im Prinzip drei UIs"). Ausnahme: rein
CLI-eigene Punkte (man page, Completion, Exit-Codes) haben keine UI-Entsprechung.

## Phase 0 — Gate grün (läuft)

| # | Aufgabe | Status |
|---|---|---|
| 0.1 | dv7 Wettlauf: `wait_props()` wartet auf WERT statt Existenz | ✅ 7/7 grün |
| 0.2 | dv30b: FD_CLAMP raus, deterministischer Hebel (`PIPEWIRE_MODULE_DIR` nur für kmixdeckd) | ✅ im Verband grün |
| 0.3 | dv30c Flakiness (`kmixdeck.out.r3`, ~1 von 2) | 🚧 Reproduktion mit Fehlermoment-Diagnose läuft |
| 0.4 | Voller ctest 17/17 grün, zweimal hintereinander | ⬜ |
| 0.5 | Diagnosezeilen/Tempfiles raus, ruff+clang sauber | ⬜ |

## Phase 1 — v0.3.0 abschließen

| # | Aufgabe |
|---|---|
| 1.1 | 14 Branch-Commits zu einem squashen, Message nach Vorlage |
| 1.2 | Tag `v0.3.0` (annotated) auf den Squash |
| 1.3 | `docs/review-v0.3.md` als Journal einchecken (inkl. B6–B8 + widerlegte Hypothesen) |
| 1.4 | 9-Punkt-Release-Checkliste abhaken, Ergebnis dokumentieren |

## Phase 2 — CLI v0.4 (Sektion 5b, CL-1…CL-9)

Reihenfolge nach Abhängigkeit, nicht nach Nummer:

| # | Punkt | Warum hier |
|---|---|---|
| 2.1 | **CL-1** eine Doku-Quelle → man page + `--help` generiert, Drift-Check im Build | Fundament für 2.2–2.4 und 2.9 |
| 2.2 | **CL-4** `Usage: kmixdeck …` statt argv[0] | Einzeiler, fällt aus CL-1 |
| 2.3 | **CL-2** Help ohne Daemon, `kmixdeck help`, Pager am TTY | der Punkt, der mich am meisten stört |
| 2.4 | **CL-3** Pro-Kommando-Hilfe mit Beispiel | |
| 2.5 | **CL-8** stderr-Prefix + ein Test **pro** Exit-Code (0–4 existieren) | billig, hohe Wirkung |
| 2.6 | **CL-9** Test: jedes Kommando in Hilfe, jedes Hilfe-Kommando existiert | hätte `scene` in v0.3.0 verhindert |
| 2.7 | **CL-5** `kmixdeck tree` + `--json`, 80 Spalten, ASCII-Fallback, NO_COLOR | größter Brocken, größter Alltagsnutzen |
| 2.8 | **CL-6** `kmixdeck patch` RFC 7386, `-`, `--dry-run`, alles-oder-nichts | Merge-Logik aus der Web-Bridge (AR-8) wiederverwenden |
| 2.9 | **CL-7** Completion bash+zsh, dynamisch aus dem Daemon, statischer Fallback | |

## Phase 3 — Paritätslücken (gemessen 2026-09-20, `/var/tmp/paritaet.py`)

Das sind Features, die als ✅ gelten, aber in einer Oberfläche fehlen.

| # | Feature | CLI | KDE | Web | zu tun |
|---|---|---|---|---|---|
| 3.1 | **Loudness/LUFS (UX-18)** | ✅ | ❌ | ❌ | M/S/I neben dem Meter + Ziellinie + Toggle pro Mix, in BEIDEN Frontends. Spec-Status war zu früh ✅ — korrigiert |
| 3.2 | Virtuelle Geräte (DV-23) | ✅ | ✅ | ❌ | Anlegen/Entfernen + Portliste in der Web-UI |
| 3.3 | Watch/Events | ✅ | ✅ | ❌ | Live-Updates in der Web-UI (SSE/EventSource) |

## Phase 4 — Offene Features, jeweils dreifach

| # | Punkt | KDE | Web | CLI |
|---|---|---|---|---|
| 4.1 | **CT-1** Rest: volume-up/down + listen-next-mix als Global Shortcuts | ⬜ | – | – |
| 4.2 | **FX-8** Noise Suppression (rnnoise), aus + Paketname wenn fehlend | ⬜ | ⬜ | ⬜ |
| 4.3 | **FX-9** Ducking (Side-Chain), aus, Badge „ducked by X" | ⬜ | ⬜ | ⬜ |
| 4.4 | **CT-8** Soundboard (`PlaySample` im Daemon per `pw_stream`) | ⬜ | ⬜ | ⬜ |

Opt-in-Regel der Spec (Zeile 13) gilt für alle vier: **aus by default**, zur
Laufzeit schaltbar, und die UI versteckt die Bedienelemente eines abgeschalteten
Features bis auf den einen Schalter.

## Zählstand (gemessen, nicht geschätzt)

121 Anforderungen: **108 ✅ · 1 🔶 (CT-1) · 12 📝**, davon 9 die CLI-Sektion.
Plus 3 Paritätslücken bei Punkten, die ✅ tragen → effektiv **16 offene Posten**.
