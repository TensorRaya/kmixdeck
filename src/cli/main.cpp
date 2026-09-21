// SPDX-License-Identifier: GPL-3.0-or-later
// kmixdeck — CLI. A pure D-Bus client of org.kmixdeck1; proves AR-3 (everything reachable from a shell).
#include <QCoreApplication>
#include "kmixdeck_version.h"
#include <QCommandLineParser>

#include "hilfe_text.h"   // CL-1: generiert aus docs/kmixdeck.md
#include "kommando_hilfe.h"   // CL-3: Hilfe je Kommando, gleiche Quelle
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QProcess>       // CL-2: $PAGER am TTY
#include <unistd.h>       // isatty()
#include <QTimer>
#include <QMap>
#include <cmath>
#include <cstdio>
#include <QDBusMessage>

using InterfaceMap = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceMap>;
using StringMap = QMap<QString, QString>;
using PortMap = QMap<QString, QStringList>;
Q_DECLARE_METATYPE(StringMap)
Q_DECLARE_METATYPE(PortMap)
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

namespace {
constexpr const char *BUS = "org.kmixdeck1";
constexpr const char *ROOT = "/org/kmixdeck1";
enum Exit { Ok = 0, Usage = 1, NoService = 2, NotFound = 3, Rejected = 4 };
QTextStream out(stdout), err(stderr);
bool g_json = false;
bool g_once = false;   // FX-10: with --once the JSON stream must stay ONE object — see Watcher::peaks()

QVariant unwrap(const QVariant &v) {
    if (v.canConvert<QDBusVariant>()) return unwrap(v.value<QDBusVariant>().variant());
    if (v.canConvert<QDBusObjectPath>() && v.userType() == qMetaTypeId<QDBusObjectPath>()) return v.value<QDBusObjectPath>().path();
    return v;
}
QVariantMap plain(const QVariantMap &m) { QVariantMap r; for (auto it = m.cbegin(); it != m.cend(); ++it) r[it.key()] = unwrap(it.value()); return r; }

struct Objects { QMap<QString, QVariantMap> channels, mixes, cells, apps; QVariantMap mixer; };

/// CL-8: jeder Fehler geht nach STDERR, mit stabilem Prefix, benanntem Objekt
/// und dem Exit-Code als Zahl.
///
/// Warum stderr auch im JSON-Modus: bis 2026-09-21 schrieb fail() das
/// Fehlerobjekt nach stdout. Damit landete es im DATENSTROM — `kmixdeck --json
/// status | jq '.mixes'` bekam bei nicht erreichbarem Dienst
/// `{"code":2,"error":"no session bus"}` in dieselbe Pipe wie die Nutzdaten.
/// Gemessen: exit=2, stdout trug das Objekt, stderr war leer. Ein Skript kann
/// so nicht zwischen Ergebnis und Fehler trennen, ohne den Inhalt zu raten.
///
/// Das Textformat `kmixdeck: <meldung>` bleibt wie es war (103 Aufrufstellen
/// haengen daran, und es ist die uebliche Form fuer Unix-Werkzeuge). Neu ist
/// nur, dass das JSON-Objekt ein Feld `kind` mit dem symbolischen Namen des
/// Codes traegt: eine Zahl allein zwingt jeden Aufrufer zu einer eigenen
/// Tabelle.
const char *codeName(Exit code) {
    switch (code) {
    case Ok: return "ok";
    case Usage: return "usage";
    case NoService: return "no-service";
    case NotFound: return "not-found";
    case Rejected: return "rejected";
    }
    return "unknown";
}

/// CL-8: die Namen der Kommandos, pruefbar OHNE Bus-Verbindung.
///
/// Warum getrennt von der Dispatch-Tabelle in Cli::run(): die Tabelle bildet auf
/// Cli-Methoden ab und braucht ein fertiges Cli-Objekt, also einen erreichbaren
/// Dienst. Ein Tippfehler ist aber ein Bedienfehler und kein Dienstproblem —
/// gemessen 2026-09-21 gab `kmixdeck quatschkommando` den Code 2 ("service not
/// reachable") samt Meldung ueber fehlende .service-Dateien. Der Benutzer sucht
/// dann an der falschen Stelle. Beide Listen haelt der Test cl9-hilfe-gegen-code
/// zusammen, der die Dispatch-Tabelle aus dieser Datei liest.
constexpr const char *KOMMANDO_NAMEN[] = {
    "status", "tree", "patch", "loudness", "streamdeck", "setup", "export", "import", "undo", "scene",
    "devices", "channel", "mix", "fx", "duck", "cell", "app", "listen", "audition", "levels", "watch",
    "complete",   // CL-7: kein Alltagskommando, aber `help complete` und die Doku-Pruefung brauchen den Namen
};

bool istKommando(const QString &name) {
    for (const char *k : KOMMANDO_NAMEN)
        if (name == QLatin1String(k)) return true;
    return false;
}

int fail(Exit code, const QString &msg) {
    if (g_json)
        err << QJsonDocument(QJsonObject{{"error", msg},
                                        {"code", int(code)},
                                        {"kind", QString::fromLatin1(codeName(code))}})
                       .toJson(QJsonDocument::Compact) << "\n";
    else
        err << "kmixdeck: " << msg << "\n";
    return code;
}

bool fetch(Objects &o, QString *error) {
    QDBusInterface om(BUS, ROOT, "org.freedesktop.DBus.ObjectManager", QDBusConnection::sessionBus());
    QDBusReply<ManagedObjects> r = om.call("GetManagedObjects");
    if (!r.isValid()) { *error = r.error().message(); return false; }
    for (auto it = r.value().cbegin(); it != r.value().cend(); ++it) {
        const QString p = it.key().path();
        for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
            const QVariantMap props = plain(jt.value());
            if (jt.key() == "org.kmixdeck1.Mixer") o.mixer = props;
            else if (jt.key() == "org.kmixdeck1.Channel") o.channels[p] = props;
            else if (jt.key() == "org.kmixdeck1.Mix") o.mixes[p] = props;
            else if (jt.key() == "org.kmixdeck1.Cell") o.cells[p] = props;
            else if (jt.key() == "org.kmixdeck1.App") o.apps[p] = props;
        }
    }
    return true;
}

QString db(double lin) { return lin <= 0 ? QStringLiteral("  -inf") : QString::number(20.0 * std::log10(lin), 'f', 1).rightJustified(6); }
QString cellPath(const QString &ch, const QString &mix) { return QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix); }

bool setProp(const QString &path, const QString &iface, const QString &name, const QVariant &v, QString *error) {
    QDBusInterface props(BUS, path, "org.freedesktop.DBus.Properties", QDBusConnection::sessionBus());
    QDBusMessage r = props.call("Set", iface, name, QVariant::fromValue(QDBusVariant(v)));
    if (r.type() == QDBusMessage::ErrorMessage) { *error = r.errorMessage(); return false; }
    return true;
}
/// Read one property straight off the bus. `o` is the snapshot fetch() took BEFORE the command ran, so
/// anything that reads back what the same invocation just wrote has to ask again (FX-9: `duck show`).
QVariant getProp(const QString &path, const QString &iface, const QString &name) {
    QDBusInterface props(BUS, path, "org.freedesktop.DBus.Properties", QDBusConnection::sessionBus());
    const QDBusMessage r = props.call("Get", iface, name);
    if (r.type() == QDBusMessage::ErrorMessage || r.arguments().isEmpty()) return {};
    return r.arguments().first().value<QDBusVariant>().variant();
}
// "0.25" (linear) | "-12dB" | "50%" (cubic, like the UI) → linear
// One rule for every syntax: nothing above unity is silently clamped — the bus refuses it, so do we.
// (Gain above 0 dB is a channel-trim/FX question, not a fader question; ADR 0002.)
bool parseLevel(const QString &s, double *lin) {
    bool ok = false;
    if (s.endsWith("dB", Qt::CaseInsensitive)) { double d = s.chopped(2).toDouble(&ok); if (ok) *lin = std::pow(10.0, d / 20.0); }
    else if (s.endsWith('%')) { double p = s.chopped(1).toDouble(&ok) / 100.0; if (ok) *lin = p * p * p; }
    else { *lin = s.toDouble(&ok); }
    return ok && *lin >= 0.0 && *lin <= 1.0 + 1e-9;
}
bool parseBool(const QStringList &a, int i, bool *b) { if (a.size() <= i) { *b = true; return true; } const QString s = a[i].toLower(); if (s == "on" || s == "1" || s == "true") *b = true; else if (s == "off" || s == "0" || s == "false") *b = false; else return false; return true; }

/// CL-5: Zeichensatz fuer den Baum. Box-Zeichen nur, wenn das Locale UTF-8 kann.
///
/// Warum nicht immer Unicode: in einer POSIX/C-Locale oder auf einer seriellen Konsole
/// kommen `├──` als Fragezeichen oder Muell an, und dann ist der Baum unlesbar —
/// schlimmer als ASCII. Geprueft wird dieselbe Kette wie bei GNU tree(1): LC_ALL,
/// dann LC_CTYPE, dann LANG.
struct Baumzeichen {
    const char *ast;        // Verzweigung, es folgen weitere Geschwister
    const char *letzter;    // letzte Verzweigung auf dieser Ebene
    const char *strich;     // senkrechte Fortsetzung
    const char *leer;
};

Baumzeichen baumzeichen() {
    for (const char *var : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const QByteArray v = qgetenv(var);
        if (v.isEmpty()) continue;
        const QByteArray o = v.toUpper();
        return (o.contains("UTF-8") || o.contains("UTF8"))
                ? Baumzeichen{"├── ", "└── ", "│   ", "    "}
                : Baumzeichen{"|-- ", "`-- ", "|   ", "    "};
    }
    return Baumzeichen{"|-- ", "`-- ", "|   ", "    "};
}

/// CL-5: Farbe nur, wenn sie erwuenscht UND sinnvoll ist.
///
/// no-color.org: JEDE nicht-leere Belegung von NO_COLOR schaltet Farbe ab, unabhaengig
/// vom Wert. Zusaetzlich nie faerben, wenn stdout keine Konsole ist — sonst landen
/// Escape-Sequenzen in `kmixdeck tree > datei.txt` und in jeder Pipe.
bool farbeAn() {
    if (!qEnvironmentVariableIsEmpty("NO_COLOR")) return false;
    if (qgetenv("TERM") == "dumb") return false;
    return isatty(STDOUT_FILENO) != 0;
}

int cmdStatus(const Objects &o) {
    if (g_json) {
        QJsonObject j{{"version", o.mixer.value("Version").toString()}, {"connected", o.mixer.value("Connected").toBool()}, {"lastError", o.mixer.value("LastError").toString()}};
        QJsonArray ch, mx, cells;
        for (auto it = o.channels.cbegin(); it != o.channels.cend(); ++it) ch.append(QJsonObject::fromVariantMap(it.value()));
        for (auto it = o.mixes.cbegin(); it != o.mixes.cend(); ++it) mx.append(QJsonObject::fromVariantMap(it.value()));
        for (auto it = o.cells.cbegin(); it != o.cells.cend(); ++it) { auto m = it.value(); m["Path"] = it.key(); cells.append(QJsonObject::fromVariantMap(m)); }
        j["channels"] = ch; j["mixes"] = mx; j["cells"] = cells;
        out << QJsonDocument(j).toJson(QJsonDocument::Indented); return Ok;
    }
    out << "kmixdeckd " << o.mixer.value("Version").toString() << (o.mixer.value("Connected").toBool() ? "  PipeWire: connected\n" : "  PipeWire: NOT CONNECTED\n");
    if (const QString e = o.mixer.value("LastError").toString(); !e.isEmpty()) out << "!! " << e << "\n";
    if (o.channels.isEmpty() || o.mixes.isEmpty()) { out << "(no channels or mixes)\n"; return Ok; }
    out << QStringLiteral("%1").arg("", -14);
    for (const auto &m : o.mixes) out << QStringLiteral("%1").arg(m.value("Name").toString().left(12), -14);
    out << "\n";
    for (auto c = o.channels.cbegin(); c != o.channels.cend(); ++c) {
        const QString cs = c.value().value("Slug").toString();
        out << QStringLiteral("%1").arg(c.value().value("Name").toString().left(12), -14);
        for (auto m = o.mixes.cbegin(); m != o.mixes.cend(); ++m) {
            const auto cell = o.cells.value(cellPath(cs, m.value().value("Slug").toString()));
            if (cell.isEmpty()) out << QStringLiteral("%1").arg("—", -14);
            else out << QStringLiteral("%1 %2").arg(cell.value("Muted").toBool() ? QStringLiteral(" muted") : db(cell.value("Volume").toDouble()), -7).arg("dB", -6);
        }
        out << "\n";
    }
    return Ok;
}

/// FX-Kette lesbar machen: die Property ist ein JSON-String `{enabled, chain:[{type,…}]}`,
/// nicht eine Liste. Und sie haengt an Kanal UND Mix (service.h:86 und :136) — NICHT an der
/// Zelle. Das war mein erster Fehlgriff bei CL-5: `Cell` hat Volume, Muted, Follows und
/// sonst nichts. Ein Baum, der FX unter der Zelle zeigt, behauptet eine Struktur, die es
/// im Daemon nicht gibt.
QStringList fxNamen(const QString &json) {
    if (json.isEmpty()) return {};
    const auto d = QJsonDocument::fromJson(json.toUtf8()).object();
    if (!d.value("enabled").toBool(true)) return {};
    QStringList namen;
    for (const auto &v : d.value("chain").toArray()) {
        const auto o = v.toObject();
        const QString typ = o.value("type").toString();
        if (!typ.isEmpty()) namen << typ;
    }
    return namen;
}

/// CL-7: `kmixdeck complete <wort-index> <wort>...` — Kandidaten fuer bash und zsh.
///
/// 🔴 Warum das im CLI steckt und nicht im Shell-Skript: eine Completion, die die
/// Kommandotabelle abschreibt, driftet. `patch` und `tree` kamen heute dazu — ein
/// Skript mit eigener Liste haette sie nicht gekannt, und niemand haette es gemerkt,
/// weil eine fehlende Vervollstaendigung keinen Test rot macht. Darum ist HIER die
/// Quelle: die Namen kommen aus KOMMANDO_NAMEN (derselbe Array, den `istKommando`
/// benutzt), die Unterkommandos aus dem generierten Hilfetext (derselbe, den
/// `help <cmd>` zeigt, Quelle docs/kmixdeck.md). Die Shell-Skripte fragen nur.
///
/// Dynamische Werte (Slugs, Geraete, Szenen) kommen vom laufenden Daemon. Ist er
/// nicht da, bleibt es bei den statischen Kandidaten — die Anforderung verlangt
/// ausdruecklich, dass Completion ohne Daemon nicht bricht. Darum wird der Bus hier
/// NIE als Fehler behandelt: keine Antwort heisst leere Liste, nicht Exit ungleich 0.
/// Eine Completion, die Exit 2 liefert, macht die Shell stumm.
int cmdComplete(const QStringList &argv) {
    // argv: ["complete", "<index>", "kmixdeck", "<wort1>", ...] — Index zaehlt ab 0 auf
    // die Wortliste NACH dem Programmnamen, so wie COMP_CWORD es liefert.
    const int index = argv.size() > 1 ? argv[1].toInt() : 0;
    QStringList worte = argv.mid(2);            // ["kmixdeck", "channel", "mu"]
    if (!worte.isEmpty()) worte.removeFirst();  // Programmname weg
    const QString praefix = index >= 1 && index - 1 < worte.size() ? worte[index - 1] : QString();
    const QString kommando = worte.isEmpty() ? QString() : worte.first();

    QStringList kandidaten;

    // Wort 1: die Kommandos. Aus KOMMANDO_NAMEN, nicht abgeschrieben.
    if (index <= 1) {
        for (const char *k : KOMMANDO_NAMEN) kandidaten << QString::fromLatin1(k);
        kandidaten << QStringLiteral("help") << QStringLiteral("--help") << QStringLiteral("--version")
                   << QStringLiteral("--json");
    } else {
        // Wort 2: die Unterkommandos dieses Kommandos, aus dem generierten Hilfetext.
        // Synopsis-Zeilen sind mit ZWEI Leerzeichen eingerueckt, Prosa mit sechs —
        // sonst liest man "channel and" und "channel can" aus dem Fliesstext mit.
        if (index == 2) {
            for (const auto &kh : kmixdeck::KOMMANDO_HILFE) {
                if (kommando != QLatin1String(kh.name)) continue;
                for (const QString &zeile : QString::fromUtf8(kh.text).split(QLatin1Char('\n'))) {
                    // Nur Synopsis-Zeilen: zwei Leerzeichen Einrueckung, Prosa hat sechs.
                    // 🔴 Gemessen (2026-09-21): diese Pruefung allein aendert NICHTS am
                    // Ergebnis — Prosa beginnt nie mit dem Kommandonamen als ERSTEM Wort
                    // ("Pre-fader gain of the channel itself" hat "channel" in der Mitte),
                    // und darauf filtert `w.first() == kommando` unten schon. Sie bleibt
                    // trotzdem, weil sie die Absicht festhaelt und eine kuenftige
                    // Doku-Zeile wie "  channel groups move together" sonst durchkaeme.
                    // Der WIRKSAME Filter ist die Wortposition, nicht die Einrueckung.
                    if (!zeile.startsWith(QLatin1String("  ")) || zeile.startsWith(QLatin1String("   "))) continue;
                    // "  channel mute <slug> [on|off]" -> "mute"; "  channel list" -> "list".
                    // Mehrere Varianten pro Zeile trennt " · ".
                    for (const QString &teil : zeile.trimmed().split(QStringLiteral(" · "))) {
                        const QStringList w = teil.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        if (w.size() >= 2 && w.first() == kommando
                            && !w[1].startsWith(QLatin1Char('<')) && !w[1].startsWith(QLatin1Char('[')))
                            kandidaten << w[1];
                    }
                }
            }
        }

        // Dynamische Werte. Ab hier darf der Bus fehlen — dann bleibt die Liste kurz.
        const bool willSlugKanal = (kommando == QLatin1String("channel") && index == 3)
                                || (kommando == QLatin1String("cell") && index == 3)
                                || (kommando == QLatin1String("fx") && index == 4 && worte.size() > 2
                                    && worte[2] == QLatin1String("channel"));
        const bool willSlugMix = (kommando == QLatin1String("mix") && index == 3)
                              || (kommando == QLatin1String("cell") && index == 4)
                              || (kommando == QLatin1String("listen") && index == 2)
                              || (kommando == QLatin1String("fx") && index == 4 && worte.size() > 2
                                  && worte[2] == QLatin1String("mix"));
        const bool willGeraet = (kommando == QLatin1String("channel") && index == 4 && worte.size() > 1
                                 && (worte[1] == QLatin1String("input") || worte[1] == QLatin1String("input-add")
                                     || worte[1] == QLatin1String("input-remove") || worte[1] == QLatin1String("wire")))
                             || (kommando == QLatin1String("mix") && index == 4 && worte.size() > 1
                                 && worte[1] == QLatin1String("output"))
                             || (kommando == QLatin1String("devices") && index == 2);
        const bool willSzene = kommando == QLatin1String("scene") && index == 3;

        if (willSlugKanal || willSlugMix || willGeraet || willSzene) {
            // Ohne Daemon bleibt `o` leer und `fetch` meldet einen Fehler, den wir
            // ABSICHTLICH verwerfen: die Completion soll dann nur die statischen
            // Kandidaten zeigen, nicht abbrechen.
            Objects o; QString egal; fetch(o, &egal);
            if (willSlugKanal)
                for (const auto &c : o.channels) kandidaten << unwrap(c.value("Slug")).toString();
            if (willSlugMix)
                for (const auto &m : o.mixes) kandidaten << unwrap(m.value("Slug")).toString();
            if (willGeraet) {
                QDBusInterface mx(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
                // InputDevices/OutputDevices sind a{ss} (node.name -> Beschreibung): der
                // SCHLUESSEL ist der Name, den die Kommandos erwarten. Auspacken mit
                // qdbus_cast wie im ganzen Rest der Datei — `QDBusArgument >>` direkt auf
                // dem Property-Wert bricht mit "read from a write-only object".
                // Beide Listen stehen schon in `o.mixer`, also kein zweiter Bus-Aufruf.
                for (const char *prop : {"InputDevices", "OutputDevices"})
                    kandidaten << qdbus_cast<StringMap>(o.mixer.value(QString::fromLatin1(prop))).keys();
            }
            if (willSzene) {
                QDBusInterface mx(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
                kandidaten << unwrap(mx.property("Scenes")).toStringList();
            }
        }
    }

    kandidaten.removeAll(QString());
    kandidaten.removeDuplicates();
    kandidaten.sort();
    for (const QString &k : kandidaten)
        if (praefix.isEmpty() || k.startsWith(praefix)) out << k << "\n";
    return Ok;
}

/// CL-6: ist diese Property schreibbar? Aus der Introspection des Daemons, nicht geraten.
///
/// 🔴 Zwei gemessene Fehler, die ohne diese Pruefung bleiben (2026-09-21):
///   1. `--dry-run` behauptete "voice.Slug: voice -> anders", obwohl `Slug` CONSTANT ist.
///      Ein Trockenlauf, der etwas verspricht, was der echte Lauf ablehnt, ist schlimmer
///      als kein Trockenlauf — man plant damit.
///   2. Der gemischte Patch {Trim, Slug} liess Trim unveraendert, aber nur WEIL "Slug"
///      alphabetisch vor "Trim" steht (QJsonObject sortiert). Bei {Muted, Slug} waere
///      Muted schon geschrieben gewesen. Alles-oder-nichts darf nicht von der
///      Schluesselreihenfolge abhaengen.
///
/// Introspection wird pro Interface EINMAL geholt und gemerkt — ein Patch mit 50
/// Zuweisungen soll nicht 50 D-Bus-Runden fuer dieselbe Antwort drehen.
bool istSchreibbar(const QString &pfad, const QString &iface, const QString &prop) {
    static QMap<QString, QSet<QString>> merker;   // iface -> schreibbare Properties
    if (!merker.contains(iface)) {
        QSet<QString> schreibbar;
        QDBusInterface in(BUS, pfad, QStringLiteral("org.freedesktop.DBus.Introspectable"),
                          QDBusConnection::sessionBus());
        const QDBusReply<QString> xml = in.call(QStringLiteral("Introspect"));
        if (!xml.isValid()) return true;   // keine Auskunft: nicht vorschnell ablehnen,
                                           // der Daemon lehnt beim Schreiben selbst ab.
        // Nur den Block dieses Interfaces lesen, sonst erbt man fremde Property-Namen.
        const QString doc = xml.value();
        const int von = doc.indexOf(QStringLiteral("<interface name=\"%1\"").arg(iface));
        if (von < 0) return true;
        const int bis = doc.indexOf(QStringLiteral("</interface>"), von);
        const QString block = doc.mid(von, bis < 0 ? -1 : bis - von);
        static const QRegularExpression re(
            QStringLiteral("<property[^>]*name=\"([^\"]+)\"[^>]*access=\"([^\"]+)\""));
        for (auto m = re.globalMatch(block); m.hasNext();) {
            const auto t2 = m.next();
            if (t2.captured(2).contains(QStringLiteral("write"))) schreibbar.insert(t2.captured(1));
        }
        merker.insert(iface, schreibbar);
    }
    return merker.value(iface).contains(prop);
}

/// CL-6: `kmixdeck patch <datei|->` — die ganze Konfiguration nicht-interaktiv aendern.
///
/// Form: RFC 7386 Merge Patch gegen dieselbe Sicht, die die Web-Bridge schon spricht
/// (AR-8, ADR 0011):
///
///     {"objects": {"/org/kmixdeck1/channel/voice": {"Trim": 0.7, "Muted": false}},
///      "root":    {"ListeningDevice": "fake.headphones"}}
///
/// 🔴 Warum NICHT die `export`-Form: die traegt Arrays (`channels: [...]`, `levels: [...]`).
/// RFC 7386 ersetzt ein Array immer VOLLSTAENDIG — ein Patch, der einen Kanal aendern will,
/// muesste alle mitschicken und wuerde beim geringsten Versehen den Rest loeschen. Genau
/// dafuer gibt es `import`. Patchen braucht eine Sicht, in der jedes Ziel einen eigenen
/// Schluessel hat; die Bridge hat sie, also nehmen wir sie.
///
/// Alles-oder-nichts: die Anforderung verlangt, dass ein abgelehnter Patch NICHTS anfasst.
/// D-Bus kennt keine Transaktion, darum zwei Phasen — erst jede Zuweisung gegen die
/// Introspection pruefen (existiert die Property? ist sie schreibbar? passt der Typ?), und
/// nur wenn ALLE durchkommen, wird geschrieben. Ein Patch mit einem Tippfehler im letzten
/// Feld darf die ersten neun nicht schon gesetzt haben.
int cmdPatch(const Objects &o, const QString &quelle, bool trocken) {
    QByteArray roh;
    if (quelle == QLatin1String("-")) {
        QFile in; if (!in.open(stdin, QIODevice::ReadOnly)) return fail(Usage, "cannot read stdin");
        roh = in.readAll();
    } else {
        QFile f(quelle);
        if (!f.open(QIODevice::ReadOnly)) return fail(NotFound, "cannot read " + quelle);
        roh = f.readAll();
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(roh, &pe);
    if (pe.error != QJsonParseError::NoError)
        return fail(Usage, QStringLiteral("%1: not JSON at offset %2: %3")
                               .arg(quelle).arg(pe.offset).arg(pe.errorString()));
    if (!doc.isObject()) return fail(Usage, quelle + ": top level must be an object");
    const QJsonObject wurzel = doc.object();

    for (const QString &k : wurzel.keys())
        if (k != QLatin1String("objects") && k != QLatin1String("root"))
            return fail(Usage, QStringLiteral("unknown key '%1' (expected 'objects' or 'root')").arg(k));

    // --- Phase 1: pruefen. Sammelt Zuweisungen, schreibt nichts.
    struct Zuweisung { QString pfad, iface, prop; QVariant wert; QString zeigt; };
    QList<Zuweisung> plan;

    auto pruefeObjekt = [&](const QString &pfad, const QString &iface,
                            const QVariantMap &ist, const QJsonObject &will) -> QString {
        for (auto it = will.begin(); it != will.end(); ++it) {
            const QString prop = it.key();
            if (!ist.contains(prop))
                return QStringLiteral("%1: no property '%2' (has: %3)")
                           .arg(pfad, prop, QStringList(ist.keys()).join(", "));
            if (!istSchreibbar(pfad, iface, prop))
                return QStringLiteral("%1.%2 is read-only").arg(pfad, prop);
            const QVariant alt = unwrap(ist.value(prop));
            QVariant neu = it.value().toVariant();
            // Typ an den vorhandenen Wert anpassen: JSON kennt nur double, D-Bus nicht.
            if (!neu.convert(alt.metaType()))
                return QStringLiteral("%1.%2: cannot use %3 as %4")
                           .arg(pfad, prop, QString::fromUtf8(QJsonDocument(QJsonObject{{prop, it.value()}}).toJson(QJsonDocument::Compact)), QString::fromLatin1(alt.metaType().name()));
            plan.append({pfad, iface, prop, neu,
                         QStringLiteral("%1.%2: %3 -> %4").arg(pfad, prop, alt.toString(), neu.toString())});
        }
        return {};
    };

    const QJsonObject objekte = wurzel.value("objects").toObject();
    for (auto it = objekte.begin(); it != objekte.end(); ++it) {
        const QString pfad = it.key();
        if (!it.value().isObject())
            return fail(Usage, QStringLiteral("%1: value must be an object (RFC 7386 null-deletion is not supported here — use `channel remove`)").arg(pfad));
        const QVariantMap ist = o.channels.contains(pfad) ? o.channels.value(pfad)
                             : o.mixes.contains(pfad)     ? o.mixes.value(pfad)
                             : o.cells.contains(pfad)     ? o.cells.value(pfad)
                                                          : QVariantMap();
        if (ist.isEmpty())
            return fail(NotFound, QStringLiteral("no object '%1' (see `kmixdeck tree --json`)").arg(pfad));
        const QString iface = o.channels.contains(pfad) ? QStringLiteral("org.kmixdeck1.Channel")
                            : o.mixes.contains(pfad)    ? QStringLiteral("org.kmixdeck1.Mix")
                                                        : QStringLiteral("org.kmixdeck1.Cell");
        const QString fehler = pruefeObjekt(pfad, iface, ist, it.value().toObject());
        if (!fehler.isEmpty()) return fail(Rejected, fehler);
    }
    const QJsonObject rootWill = wurzel.value("root").toObject();
    if (!rootWill.isEmpty()) {
        const QString fehler = pruefeObjekt(QString::fromLatin1(ROOT), QStringLiteral("org.kmixdeck1.Mixer"),
                                            o.mixer, rootWill);
        if (!fehler.isEmpty()) return fail(Rejected, fehler);
    }

    if (plan.isEmpty()) { out << "nothing to change\n"; return Ok; }

    // --- Phase 2: anwenden (oder bei --dry-run nur zeigen).
    if (trocken) {
        if (g_json) {
            QJsonArray j;
            for (const auto &z : plan)
                j.append(QJsonObject{{"path", z.pfad}, {"property", z.prop},
                                     {"value", QJsonValue::fromVariant(z.wert)}});
            out << QJsonDocument(j).toJson();
        } else {
            for (const auto &z : plan) out << z.zeigt << "\n";
            out << plan.size() << " change(s), nothing written (--dry-run)\n";
        }
        return Ok;
    }

    QString e;
    for (const auto &z : plan)
        if (!setProp(z.pfad, z.iface, z.prop, z.wert, &e))
            // Hierhin kommt man nur, wenn der Daemon eine geprueft-gueltige Zuweisung
            // ablehnt (Wertebereich, Geraet weg). Dann ist der Patch halb angewendet —
            // darum nennt die Meldung die Stelle, ab der nichts mehr passiert ist.
            return fail(Rejected, QStringLiteral("%1.%2 rejected: %3 (earlier changes in this patch are applied)")
                                      .arg(z.pfad, z.prop, e));
    if (g_json) out << QJsonDocument(QJsonObject{{"changed", plan.size()}}).toJson(QJsonDocument::Compact) << "\n";
    else out << "applied " << plan.size() << " change(s)\n";
    return Ok;
}

/// CL-5: der Signalweg als Baum — was haengt an was, in einem Blick.
///
/// Warum ueberhaupt: `status` ist eine Matrix aus Kanaelen x Mixes. Die beantwortet
/// "wie laut ist Kanal X in Mix Y", aber nicht "wo kommt der Ton her und wo geht er
/// hin". Genau das ist die Frage, wenn etwas stumm ist. Der Baum zeigt die Kette
/// Geraet -> Kanal -> Zelle -> Mix -> Ausgang, dazu die Programme am Kanal und die
/// Effekte in der Zelle.
///
/// 80 Spalten: die Breite ist nicht willkuerlich, sondern die Vorgabe (CL-5). Namen
/// werden gekuerzt, nie umgebrochen — ein umgebrochener Baum ist unlesbar, weil die
/// Fortsetzungszeile wie ein neuer Knoten aussieht.
int cmdTree(const Objects &o) {
    if (g_json) {
        // JSON kennt keine Box-Zeichen: derselbe Inhalt als verschachtelte Struktur.
        // Wichtig fuer Skripte — die sollen den Baum nicht aus ASCII zurueckparsen.
        QJsonArray kanaele;
        for (auto c = o.channels.cbegin(); c != o.channels.cend(); ++c) {
            const QString slug = c.value().value("Slug").toString();
            // "ref" ist die Zeichenkette, mit der man DIESEN Knoten in einem anderen
            // Kommando adressiert (CL-5 verlangt das ausdruecklich). Bei Kanaelen ist das
            // `channel <slug>`, bei Zellen `cell <kanal> <mix>` — man soll den Wert
            // abschreiben koennen, ohne die Syntax nachzuschlagen.
            QJsonObject k{{"slug", slug}, {"name", c.value().value("Name").toString()},
                          {"path", c.key()}, {"ref", slug}};
            const QString gerVal = c.value().value("InputDevice").toString();
            if (!gerVal.isEmpty()) k["inputDevice"] = gerVal;
            const QStringList kfx = fxNamen(c.value().value("FxChain").toString());
            if (!kfx.isEmpty()) k["fx"] = QJsonArray::fromStringList(kfx);
            // FX-9: wer duckt und wie viel gerade — dieselben zwei Angaben wie die Badges in den UIs.
            const QJsonObject kduck = QJsonDocument::fromJson(c.value().value("Ducking").toString().toUtf8()).object();
            if (!kduck.value("duckedBy").toString().isEmpty()) {
                k["duckedBy"] = kduck.value("duckedBy").toString();
                k["duckDepth"] = kduck.value("depth").toDouble(-12);
                k["duckReduction"] = c.value().value("DuckReduction").toDouble();
            }

            QJsonArray zellen;
            for (auto m = o.mixes.cbegin(); m != o.mixes.cend(); ++m) {
                const QString mslug = m.value().value("Slug").toString();
                const auto zelle = o.cells.value(cellPath(slug, mslug));
                if (zelle.isEmpty()) continue;
                QJsonObject z{{"mix", mslug},
                              {"volume", zelle.value("Volume").toDouble()},
                              {"muted", zelle.value("Muted").toBool()},
                              {"ref", QStringLiteral("%1 %2").arg(slug, mslug)}};
                zellen.append(z);
            }
            k["cells"] = zellen;

            QJsonArray programme;
            for (auto a = o.apps.cbegin(); a != o.apps.cend(); ++a) {
                const QStringList alle = a.value().value("Channels").toStringList();
                const QString einer = a.value().value("Channel").toString().section('/', -1);
                if (!alle.contains(slug) && einer != slug) continue;
                programme.append(QJsonObject{{"name", a.value().value("Name").toString()},
                                             {"nodeId", a.value().value("NodeId").toString()},
                                             {"running", a.value().value("Running").toBool()}});
            }
            if (!programme.isEmpty()) k["apps"] = programme;
            kanaele.append(k);
        }

        QJsonArray mixe;
        for (auto m = o.mixes.cbegin(); m != o.mixes.cend(); ++m) {
            QJsonObject mo{{"slug", m.value().value("Slug").toString()},
                           {"name", m.value().value("Name").toString()},
                           {"ref", m.value().value("Slug").toString()}};
            const QStringList aus = m.value().value("Outputs").toStringList();
            if (!aus.isEmpty()) mo["outputs"] = QJsonArray::fromStringList(aus);
            const QStringList mfx = fxNamen(m.value().value("FxChain").toString());
            if (!mfx.isEmpty()) mo["fx"] = QJsonArray::fromStringList(mfx);
            mixe.append(mo);
        }
        // Szenen: CL-5 nennt sie ausdruecklich. Sie haengen am Mixer, nicht an einem
        // Kanal — im Baum darum eine eigene Wurzel, nicht unter einem Kanal versteckt.
        QJsonArray szenen;
        for (const QString &s : unwrap(o.mixer.value("Scenes")).toStringList())
            szenen.append(QJsonObject{{"name", s}, {"ref", s}});
        QJsonObject wurzel{{"channels", kanaele}, {"mixes", mixe}};
        if (!szenen.isEmpty()) wurzel["scenes"] = szenen;
        out << QJsonDocument(wurzel).toJson();
        return Ok;
    }

    const Baumzeichen bz = baumzeichen();
    const bool farbe = farbeAn();
    // Farbcodes nur, wenn farbeAn(). Sonst leere Strings — dann ist der Code unten
    // identisch und es gibt keinen zweiten Ausgabepfad, der auseinanderlaufen kann.
    const QString dim   = farbe ? QStringLiteral("\033[2m")  : QString();
    const QString stumm = farbe ? QStringLiteral("\033[33m") : QString();
    const QString aus   = farbe ? QStringLiteral("\033[0m")  : QString();

    if (o.channels.isEmpty()) {
        out << "(no channels configured — run `kmixdeck setup` or `kmixdeck channel add <name>`)\n";
        return Ok;
    }

    int kanalNr = 0;
    for (auto c = o.channels.cbegin(); c != o.channels.cend(); ++c, ++kanalNr) {
        const bool letzterKanal = (kanalNr == o.channels.size() - 1);
        const QString slug = c.value().value("Slug").toString();
        const QString gerName = c.value().value("InputDevice").toString();

        out << (letzterKanal ? bz.letzter : bz.ast)
            << c.value().value("Name").toString().left(20)
            << dim << " (" << slug.left(16) << ")" << aus;
        if (!gerName.isEmpty())
            out << dim << "  <- " << gerName.section('/', -1).left(30) << aus;
        // FX-9: WER duckt und WIE VIEL gerade — dieselben zwei Angaben wie die Badges in KDE-UI und Web-UI.
        // Die laufende Zahl steht nur dabei, wenn wirklich abgesenkt wird, sonst ist die Zeile im Ruhezustand
        // jedes Mal anders und der Baum flackert beim Hinsehen.
        {
            const QJsonObject dk = QJsonDocument::fromJson(c.value().value("Ducking").toString().toUtf8()).object();
            const QString wer = dk.value("duckedBy").toString();
            if (!wer.isEmpty()) {
                const double jetzt = c.value().value("DuckReduction").toDouble();
                out << stumm << "  v " << wer.left(16) << aus;
                if (jetzt < -0.1) out << stumm << " " << QString::number(jetzt, 'f', 1) << " dB" << aus;
            }
        }
        out << "\n";

        const QString tiefer = letzterKanal ? bz.leer : bz.strich;

        // Kinder sammeln, damit `letzter` stimmt: Programme zuerst (Quelle), dann Zellen.
        QStringList progZeilen;
        for (auto a = o.apps.cbegin(); a != o.apps.cend(); ++a) {
            const QStringList alle = a.value().value("Channels").toStringList();
            const QString einer = a.value().value("Channel").toString().section('/', -1);
            if (!alle.contains(slug) && einer != slug) continue;
            progZeilen << QStringLiteral("%1%2 %3")
                            .arg(a.value().value("Running").toBool() ? QStringLiteral("* ")
                                                                    : QStringLiteral("  "))
                            .arg(a.value().value("Name").toString().left(24))
                            .arg(dim + a.value().value("NodeId").toString() + aus);
        }

        struct ZeileZelle { QString mix, wert; QStringList mixFx; bool stummGeschaltet; };
        QList<ZeileZelle> zellZeilen;
        for (auto m = o.mixes.cbegin(); m != o.mixes.cend(); ++m) {
            const QString mslug = m.value().value("Slug").toString();
            const auto zelle = o.cells.value(cellPath(slug, mslug));
            if (zelle.isEmpty()) continue;
            zellZeilen.append({mslug,
                               zelle.value("Muted").toBool() ? QStringLiteral("muted")
                                                             : db(zelle.value("Volume").toDouble()).trimmed() + " dB",
                               fxNamen(m.value().value("FxChain").toString()),
                               zelle.value("Muted").toBool()});
        }

        // FX des Kanals: eigene Zeilen direkt unter dem Kanal — sie wirken auf ALLE
        // Zellen dieses Kanals, nicht auf eine. Die Einrueckung muss das zeigen.
        const QStringList kanalFx = fxNamen(c.value().value("FxChain").toString());
        const int kinder = progZeilen.size() + kanalFx.size() + zellZeilen.size();
        int nr = 0;
        for (const QString &z : progZeilen) {
            out << tiefer << (++nr == kinder ? bz.letzter : bz.ast) << z << "\n";
        }
        for (const QString &f : kanalFx) {
            out << tiefer << (++nr == kinder ? bz.letzter : bz.ast)
                << dim << "fx " << f.left(24) << aus << "\n";
        }
        for (const ZeileZelle &z : zellZeilen) {
            const bool letzteZelle = (++nr == kinder);
            out << tiefer << (letzteZelle ? bz.letzter : bz.ast)
                << (z.stummGeschaltet ? stumm : QString())
                << QStringLiteral("%1 %2").arg(z.mix.left(16), -18).arg(z.wert)
                << (z.stummGeschaltet ? aus : QString());

            // Ausgaenge des Mix direkt dahinter: ohne die weiss man nicht, wo der Ton landet.
            const QStringList mixAus = o.mixes.value(QStringLiteral("%1/mix/%2").arg(ROOT, z.mix))
                                           .value("Outputs").toStringList();
            if (!mixAus.isEmpty())
                out << dim << " -> " << mixAus.join(QStringLiteral(",")).left(22) << aus;
            out << "\n";

            for (int i = 0; i < z.mixFx.size(); ++i)
                out << tiefer << (letzteZelle ? bz.leer : bz.strich)
                    << (i == z.mixFx.size() - 1 ? bz.letzter : bz.ast)
                    << dim << "fx " << z.mixFx.at(i).left(24) << aus << "\n";
        }
    }

    // Szenen (CT-9) gehoeren zum Mixer, nicht zu einem Kanal: eigene Wurzel. Ein Baum,
    // der sie unter einem Kanal zeigt, behauptet eine Zugehoerigkeit, die es nicht gibt.
    const QStringList szenen = unwrap(o.mixer.value("Scenes")).toStringList();
    if (!szenen.isEmpty()) {
        out << "scenes\n";
        for (int i = 0; i < szenen.size(); ++i)
            out << (i == szenen.size() - 1 ? bz.letzter : bz.ast)
                << szenen.at(i).left(28)
                << dim << "   scene recall " << szenen.at(i).left(20) << aus << "\n";
    }
    return Ok;
}
} // namespace

class Watcher : public QObject {
    Q_OBJECT
    int m_maxKeys = 0, m_stableTicks = 0, m_ticks = 0;   // FX-10: see peaks() — the key set grows in stages
Q_SIGNALS:
    void gotPeaks();   // `levels --once` quits on the first tick
    void gotLoudness();   // `loudness --once` quits on the first reading
public Q_SLOTS:
    // Slot with a QDBusMessage parameter receives the raw message → we get the object path.
    void propertiesChanged(const QDBusMessage &msg) {
        const QString path = msg.path(); const auto args = msg.arguments();
        const QString iface = args.value(0).toString(); const QVariantMap changed = qdbus_cast<QVariantMap>(args.value(1));
        if (g_json) { QJsonObject j{{"event", "changed"}, {"path", path}, {"interface", iface}, {"properties", QJsonObject::fromVariantMap(plain(changed))}}; out << QJsonDocument(j).toJson(QJsonDocument::Compact) << "\n"; }
        else { out << path.mid(QString(ROOT).size() + 1) << ":"; const auto pl = plain(changed); for (auto it = pl.cbegin(); it != pl.cend(); ++it) out << " " << it.key() << "=" << it.value().toString(); out << "\n"; }
        out.flush();
    }
    // UX-18: mix slug → [M, S, I, TP]. The signal is a{sad}, so the slot takes the raw message and demarshals
    // the dictionary by hand — a QVariantMap slot would hand us un-demarshalled QDBusArgument values (null).
    void loudnessSig(const QDBusMessage &msg) {
        QMap<QString, QList<double>> p;
        const auto arg = msg.arguments().value(0).value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            QString k; QList<double> v;
            arg.beginMapEntry(); arg >> k >> v; arg.endMapEntry();
            p.insert(k, v);
        }
        arg.endMap();
        if (p.isEmpty()) return;
        Q_EMIT gotLoudness();
        if (g_json) {
            QJsonObject j;
            for (auto it = p.cbegin(); it != p.cend(); ++it) {
                QJsonArray vals;
                for (double d : it.value()) vals.append(d);
                j.insert(it.key(), vals);
            }
            out << QJsonDocument(j).toJson(QJsonDocument::Compact) << "\n"; out.flush(); return;
        }
        for (auto it = p.cbegin(); it != p.cend(); ++it) {
            const QList<double> &v = it.value();
            if (v.size() < 4) continue;
            out << QStringLiteral("%1  M %2  S %3  I %4 LUFS   TP %5 dBTP\n")
                       .arg(it.key(), -12).arg(v[0], 6, 'f', 1).arg(v[1], 6, 'f', 1).arg(v[2], 6, 'f', 1).arg(v[3], 6, 'f', 1);
        }
        out.flush();
    }
    void peaks(const QVariantMap &p) {
        // The first tick after Subscribe() is empty: the daemon is still building the peak streams for the
        // targets it just learned about. `--once` waits for a tick that actually carries readings.
        // 🔴 "not empty" was the wrong bar (fixed 2026-09-19 while chasing the FX-10 test). Building those
        // streams is NOT atomic and not even single-staged: channel/* report first, mix/* a few ticks later,
        // and out/* (the edge behind a mix's FX chain) later still. A reader that quits on the first non-empty
        // tick therefore sees a TRUNCATED set — FX-10's gr/<mix> needs both mix/ and out/, so it saw nothing
        // and concluded the limiter was broken. Waiting for one specific prefix only moves the goalpost, so
        // the bar is STABILITY: quit once the set of keys stopped growing for three consecutive ticks
        // (25 Hz → 120 ms of quiet), capped so a permanently growing graph cannot hang the caller.
        if (p.isEmpty()) return;
        const int n = p.size();
        if (n > m_maxKeys) { m_maxKeys = n; m_stableTicks = 0; } else { ++m_stableTicks; }
        const bool finalTick = m_stableTicks >= 3 || ++m_ticks >= 40;   // 40 ticks ≈ 1.6 s hard ceiling
        // With --once the caller parses ONE object (json.loads), so the ticks we skip while the set grows must
        // not be printed — otherwise this fix would trade a missing key for a parse error.
        if (g_once && !finalTick) return;
        if (finalTick) Q_EMIT gotPeaks();
        if (g_json) { out << QJsonDocument(QJsonObject::fromVariantMap(p)).toJson(QJsonDocument::Compact) << "\n"; out.flush(); return; }
        QStringList keys = p.keys(); keys.sort();
        QString line;
        for (const auto &k : keys) {
            if (k.startsWith(QLatin1String("rms/")) || k.startsWith(QLatin1String("clip/"))) continue;   // CH-7 companions, folded into their key's line
            const double v = p.value(k).toDouble(); const double db = v > 0 ? 20 * std::log10(v) : -90;
            const double r = p.value(QStringLiteral("rms/") + k).toDouble(); const double rdb = r > 0 ? 20 * std::log10(r) : -90;
            const int bar = std::clamp(static_cast<int>((db + 60) / 60 * 20), 0, 20);    // −60…0 dB → 0…20 chars: peak = '#'
            const int core = std::clamp(static_cast<int>((rdb + 60) / 60 * 20), 0, bar); // RMS = '=' inside the peak bar
            const bool clip = p.value(QStringLiteral("clip/") + k).toDouble() > 0;
            line += QStringLiteral("%1 [%2%3%4]%5 %6  ").arg(k, -16).arg(QString(core, QLatin1Char('='))).arg(QString(bar - core, QLatin1Char('#'))).arg(QString(20 - bar, QLatin1Char(' ')))
                        .arg(clip ? QStringLiteral("!") : QStringLiteral(" ")).arg(v > 0 ? QStringLiteral("%1 dB").arg(db, 6, 'f', 1) : QStringLiteral("   -inf"));
        }
        out << "\r" << line; out.flush();
    }
    void interfacesAdded(const QDBusObjectPath &path, const InterfaceMap &ifaces) {
        if (g_json) out << QJsonDocument(QJsonObject{{"event", "added"}, {"path", path.path()}, {"interfaces", QJsonArray::fromStringList(ifaces.keys())}}).toJson(QJsonDocument::Compact) << "\n";
        else out << "+ " << path.path() << "\n";
        out.flush();
    }
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &) {
        if (g_json) out << QJsonDocument(QJsonObject{{"event", "removed"}, {"path", path.path()}}).toJson(QJsonDocument::Compact) << "\n";
        else out << "- " << path.path() << "\n";
        out.flush();
    }
};

/// One CLI invocation: parsed arguments, the daemon's object tree, and one handler per command.
/// (CC-1, review: this was a single 500-line `main()`. The handlers are the former `if (cmd == …)` blocks
/// verbatim; behaviour is pinned by tests/integration/test_service_cli.py and the other suites.)
struct Cli {
    QCoreApplication &app;
    QCommandLineParser &p;
    QStringList a;                 // positional arguments: a[0] = command, a[1] = sub-command
    QString cmd, sub;
    Objects o;                     // the daemon's object tree (empty for offline commands)
    QString e;
    QDBusInterface mixer{BUS, ROOT, "org.kmixdeck1.Mixer", QDBusConnection::sessionBus()};

    Cli(QCoreApplication &app_, QCommandLineParser &p_, QStringList args)
        : app(app_), p(p_), a(std::move(args)), cmd(a[0]), sub(a.value(1)) {}

    bool need(int n) { if (a.size() < n) { fail(Usage, "missing arguments; see --help"); return false; } return true; }

    /// Obergrenze: ein Lesekommando darf ueberzaehlige Argumente NICHT schlucken.
    ///
    /// 🔴 Gemessen 2026-09-21: `kmixdeck mix outputs stream fake.headphones` gab Code 0 und
    /// tat nichts. `outputs` LIEST (Schreiben ist `output`/`output-add`), das vierte Argument
    /// fiel unter den Tisch. Der Benutzer sieht Erfolg, die Aenderung ist nie passiert — die
    /// schlimmste Fehlerart, weil sie sich nicht als Fehler zeigt. Dasselbe bei
    /// `channel inputs`, `devices hidden`, `channel groups`, `streamdeck path`: 5 von 6
    /// geprueften Lesekommandos.
    ///
    /// Verstoesst gegen CL-8 (jeder Fehler nennt Objekt und Regel) und gegen die Regel, dass
    /// Exit 0 "hat getan was du wolltest" bedeutet. Darum Code 1 (Usage, Tippfehler des
    /// Benutzers) und ein Text, der sagt WELCHES Kommando stattdessen schreibt.
    bool nurLesen(int n, const char *schreibt = nullptr) {
        if (a.size() <= n) return true;
        const QString hinweis = schreibt
            ? QStringLiteral("; use `kmixdeck %1 %2` to set it").arg(cmd, QString::fromLatin1(schreibt))
            : QStringLiteral("; see --help");
        fail(Usage, QStringLiteral("`%1 %2` only reads, but got %3 extra argument(s): %4%5")
                        .arg(cmd, sub).arg(a.size() - n).arg(a.mid(n).join(' '), hinweis));
        return false;
    }
    int printPath(const QDBusReply<QDBusObjectPath> &r) {
        if (!r.isValid()) return fail(Rejected, r.error().message());
        if (g_json) out << QJsonDocument(QJsonObject{{"path", r.value().path()}}).toJson(QJsonDocument::Compact); else out << r.value().path() << "\n"; return Ok; }
    int list(const QMap<QString, QVariantMap> &m) {
        if (g_json) { QJsonArray arr; for (auto it = m.cbegin(); it != m.cend(); ++it) { auto v = it.value(); v["Path"] = it.key(); arr.append(QJsonObject::fromVariantMap(v)); } out << QJsonDocument(arr).toJson(); }
        else for (auto it = m.cbegin(); it != m.cend(); ++it) out << QStringLiteral("%1  %2\n").arg(it.value().value("Slug").toString(), -16).arg(it.value().value("Name").toString());
        return Ok; }
    // ADR 0009: "node[:POS,POS]" — the daemon logs a refusal but a D-Bus property Set cannot carry an error, so
    // the CLI checks the reference against Mixer.DevicePorts first and verifies the write by reading back.
    /// Validates a ref and, on success, rewrites it to canonical form: positions as PipeWire names them (DV-31 — a user
    /// may type the 1-based label "USB 1"; the daemon stores and echoes AUX0, so the round-trip check below must compare
    /// against that).
    bool checkRef(QString &refInOut, bool source, QString *why) {
        QString ref = refInOut, sideSuffix;
        int arrow = ref.lastIndexOf(QLatin1Char('>')); if (arrow < 0) arrow = ref.lastIndexOf(QLatin1Char('<'));   // ADR 0009 A2 side
        bool hasSide = false;
        if (arrow > 0) { const QString side = ref.mid(arrow + 1).toUpper(); if (side != "L" && side != "R") { *why = QStringLiteral("side must be L or R: node:POS>L"); return false; } sideSuffix = ref.mid(arrow); ref = ref.left(arrow); hasSide = true; }
        if (hasSide && ref.count(QLatin1Char(',')) > 0) { *why = QStringLiteral("a side (>L / >R) takes exactly one port: node:POS>L"); return false; }
        const QString node = ref.section(QLatin1Char(':'), 0, 0);
        const StringMap devs = qdbus_cast<StringMap>(o.mixer.value(source ? "InputDevices" : "OutputDevices"));
        if (!devs.contains(node)) { *why = QStringLiteral("no %1 device '%2' (see `kmixdeck devices%3`)").arg(source ? "input" : "output", node, source ? " in" : ""); return false; }
        if (!ref.contains(QLatin1Char(':'))) return true;
        const QStringList want = ref.section(QLatin1Char(':'), 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (want.isEmpty() || want.size() > 2) { *why = QStringLiteral("a virtual device is mono (1 port) or stereo (2 ports): node:POS or node:POS,POS"); return false; }
        // DV-31: a ref may name a port by PipeWire position (AUX0) or by its label (USB 1); the daemon stores the position
        QStringList have, labels; for (const auto &t : qdbus_cast<PortMap>(o.mixer.value("DevicePorts")).value(node)) { have << t.section(QLatin1Char('|'), 0, 0); labels << t.section(QLatin1Char('|'), 3, 3); }
        QStringList canon;
        for (const auto &w : want) {
            const QString t = w.trimmed();
            if (have.contains(t)) { canon << t; continue; }
            int li = -1; for (int i = 0; i < labels.size(); ++i) if (labels[i].compare(t, Qt::CaseInsensitive) == 0) { li = i; break; }
            if (li < 0) { *why = QStringLiteral("device '%1' has no port '%2' — name it as PipeWire does (%3) or by label (%4); see `kmixdeck devices ports %1`").arg(node, t, have.value(0), labels.value(0)); return false; }
            canon << have[li];
        }
        refInOut = node + QLatin1Char(':') + canon.join(QLatin1Char(',')) + sideSuffix;
        return true;
    }

    int cmdStatus() { return ::cmdStatus(o); }
    int cmdTree() { return ::cmdTree(o); }
    int cmdComplete() { return ::cmdComplete(QStringList{QStringLiteral("complete")} + a.mid(1)); }
    int cmdPatch() {
        if (!need(2)) return Usage;
        // --dry-run steht als Positionsargument in `a`, weil der Parser Optionen nach dem
        // ersten Positionsargument als Positionen behandelt (damit "-12dB" ein Wert bleibt).
        const bool trocken = a.contains(QStringLiteral("--dry-run")) || a.contains(QStringLiteral("-n"));
        QStringList rest;
        for (int i = 1; i < a.size(); ++i)
            if (a[i] != QStringLiteral("--dry-run") && a[i] != QStringLiteral("-n")) rest << a[i];
        if (rest.isEmpty()) return fail(Usage, "patch <file>|- [--dry-run]");
        if (rest.size() > 1)
            return fail(Usage, QStringLiteral("patch takes one file, got %1: %2").arg(rest.size()).arg(rest.join(' ')));
        return ::cmdPatch(o, rest.first(), trocken);
    }
    int cmdStreamdeck() {   // CT-3: kmixdeck streamdeck install|uninstall|path — hook the OpenAction plugin into OpenDeck
        const QString sub = a.size() > 1 ? a[1] : QStringLiteral("path");
        // where the plugin lives: next to this binary in a build tree, else the installed data dir
        QStringList candidates{QCoreApplication::applicationDirPath() + QStringLiteral("/../../streamdeck/me.kmixdeck.sdPlugin")};
        for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) candidates << d + QStringLiteral("/kmixdeck/streamdeck/me.kmixdeck.sdPlugin");
        QString src; for (const QString &c : candidates) if (QFileInfo::exists(c + QStringLiteral("/manifest.json"))) { src = QDir(c).canonicalPath(); break; }
        if (src.isEmpty()) return fail(Rejected, "plugin directory not found (looked in " + candidates.join(", ") + ")");
        // OpenDeck: <app_config_dir>/plugins, identifier "opendeck" → ~/.config/opendeck/plugins (Flatpak: ~/.var/app/me.amankhanna.opendeck/config/opendeck/plugins)
        const QString home = QDir::homePath();
        QStringList targets{QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/opendeck/plugins")};
        if (QFileInfo::exists(home + QStringLiteral("/.var/app/me.amankhanna.opendeck"))) targets << home + QStringLiteral("/.var/app/me.amankhanna.opendeck/config/opendeck/plugins");
        if (sub == "path") { if (!nurLesen(2)) return Usage; out << src << "\n"; return Ok; }
        if (sub == "install") {
            for (const QString &t : targets) {
                QDir().mkpath(t);
                const QString link = t + QStringLiteral("/me.kmixdeck.sdPlugin");
                if (QFileInfo(link).isSymLink() || QFileInfo::exists(link)) { QFile::remove(link); QDir(link).removeRecursively(); }
                if (!QFile::link(src, link)) return fail(Rejected, "cannot link " + link);
                out << "linked " << link << " -> " << src << "\n";
            }
            out << "restart OpenDeck, then add 'kmixdeck' actions from its action list\n"; return Ok;
        }
        if (sub == "uninstall") { for (const QString &t : targets) { const QString link = t + QStringLiteral("/me.kmixdeck.sdPlugin"); if (QFile::remove(link)) out << "removed " << link << "\n"; } return Ok; }
        return fail(Usage, "streamdeck install|uninstall|path");
    }
    int cmdSetup() {   // UX-3: kmixdeck setup [--plan|--apply]  — the first-run wizard's brain, on the command line
        const bool apply = a.size() > 1 && a[1] == "--apply";
        const QDBusReply<QString> r = mixer.call(apply ? "FirstRunApply" : "FirstRunPlan");
        if (!r.isValid()) return fail(Rejected, r.error().message());
        const QJsonObject o = QJsonDocument::fromJson(r.value().toUtf8()).object();
        if (g_json) { out << r.value() << "\n"; return Ok; }
        if (!apply) {
            out << (o.value("firstRun").toBool() ? "first run: no layout on disk yet\n" : "layout exists (setup would only fill gaps)\n");
            const QString sink = o.value("defaultSink").toString(), src = o.value("defaultSource").toString();
            out << "default output: " << (sink.isEmpty() ? QStringLiteral("(none)") : sink + "  " + o.value("sinkDescription").toString()) << (o.value("sinkKnown").toBool() || sink.isEmpty() ? "" : "  [not seen yet]") << "\n";
            out << "default input:  " << (src.isEmpty() ? QStringLiteral("(none)") : src + "  " + o.value("sourceDescription").toString()) << (o.value("sourceKnown").toBool() || src.isEmpty() ? "" : "  [not seen yet]") << "\n";
            out << "would: Monitor -> default output, listen on it, Voice <- default input\n";
            for (const auto &av : o.value("apps").toArray()) { const auto ap = av.toObject(); out << "  app " << ap.value("name").toString() << (ap.value("assigned").toBool() ? "  (already on " : "  -> ") << ap.value("channel").toString() << (ap.value("assigned").toBool() ? ")" : "") << "\n"; }
            out << "run `kmixdeck setup --apply` to do it\n";
            return Ok;
        }
        if (o.contains("monitorOutput")) out << "Monitor mix -> " << o.value("monitorOutput").toString() << "\n";
        if (o.contains("listeningDevice")) out << "listening on " << o.value("listeningDevice").toString() << "\n";
        if (o.contains("voiceInput")) out << "Voice <- " << o.value("voiceInput").toString() << "\n";
        for (const auto &av : o.value("apps").toArray()) { const auto ap = av.toObject(); out << "app " << ap.value("name").toString() << " -> " << ap.value("channel").toString() << "\n"; }
        return Ok;
    }
    int cmdExport() {   // CT-7: kmixdeck export [file]  — stdout when no file
        const QDBusReply<QString> r = mixer.call("Export");
        if (!r.isValid()) return fail(Rejected, r.error().message());
        if (a.size() < 2 || a[1] == "-") { out << r.value(); if (!r.value().endsWith(QLatin1Char('\n'))) out << "\n"; return Ok; }
        // BP-7: a backup that is half-written is worse than none — QSaveFile writes next to the target and renames.
        QSaveFile f(a[1]); if (!f.open(QIODevice::WriteOnly)) return fail(Usage, "cannot write " + a[1]);
        f.write(r.value().toUtf8()); if (!f.commit()) return fail(Usage, "cannot write " + a[1] + ": " + f.errorString());
        out << "exported to " << a[1] << "\n"; return Ok;
    }
    int cmdImport() {   // CT-7: kmixdeck import <file>  — replaces the whole layout + levels
        if (!need(2)) return Usage;
        QFile f(a[1]); if (!f.open(QIODevice::ReadOnly)) return fail(NotFound, "cannot read " + a[1]);
        const QDBusMessage r = mixer.call("Import", QString::fromUtf8(f.readAll()));
        if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
        out << "imported " << a[1] << "\n"; return Ok;
    }
    int cmdScene() {   // CT-9: scene save|recall|list|delete <name>  [--add]
        if (!need(2)) return Usage;
        QDBusInterface mx(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
        if (!mx.isValid()) return fail(NoService, mx.lastError().message());
        const QString sub = a[1];
        if (sub == QLatin1String("list")) {
            const QStringList names = mx.property("Scenes").toStringList();
            if (g_json) { QJsonArray arr; for (const auto &n : names) arr.append(n); out << QJsonDocument(arr).toJson(QJsonDocument::Compact) << "\n"; }
            else for (const auto &n : names) out << n << "\n";
            out.flush();
            return Ok;
        }
        if (!need(3)) return Usage;
        const QString name = a[2];
        if (sub == QLatin1String("save")) {
            const QDBusMessage r = mx.call(QStringLiteral("SaveScene"), name);
            if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
            // The re-read stays on purpose: SaveScene now reports its own failures (2026-09-19), but this also
            // catches a scene that was written and then vanished — cheap, and it is the user's proof it is stored.
            if (!mx.property("Scenes").toStringList().contains(name)) return fail(Rejected, QStringLiteral("daemon did not store scene '%1' (see its log)").arg(name));
            return Ok;
        }
        if (sub == QLatin1String("recall")) {
            // Exclusive by default (qpwgraph's Activated/Exclusive pair): a mix the scene does not mention goes
            // back to unity, so the same scene always sounds the same. --add leaves the rest where it is.
            const bool exclusive = !a.contains(QStringLiteral("--add"));
            const QDBusMessage r = mx.call(QStringLiteral("RecallScene"), name, exclusive);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == QLatin1String("delete")) {
            const QDBusMessage r = mx.call(QStringLiteral("DeleteScene"), name);
            if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
            if (mx.property("Scenes").toStringList().contains(name)) return fail(NotFound, QStringLiteral("no scene '%1'").arg(name));
            return Ok;
        }
        return fail(Usage, QStringLiteral("scene: expected save|recall|list|delete, got '%1'").arg(sub));
    }
    int cmdUndo() {
        const QString what = unwrap(o.mixer.value("UndoDescription")).toString();
        if (what.isEmpty()) return fail(NotFound, "nothing to undo");
        const QDBusMessage r = mixer.call("Undo");
        if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
        out << "restored " << what << "\n"; return Ok;
    }
    int cmdDevices() {
        if (sub == "virtual") {   // DV-23: devices virtual list | add <name> [--in N] [--out N] | remove <slug|node>
            const QString op = a.value(2);
            if (op == "list" || op.isEmpty()) { for (const auto &n : unwrap(o.mixer.value("VirtualDevices")).toStringList()) out << n << "\n"; return Ok; }
            if (op == "add") {
                if (!need(4)) return Usage;
                int in = 8, outN = 8;
                for (int i = 4; i + 1 < a.size(); i += 2) { if (a[i] == "--in") in = a[i + 1].toInt(); else if (a[i] == "--out") outN = a[i + 1].toInt(); else return fail(Usage, "devices virtual add <name> [--in N] [--out N]"); }
                const QDBusMessage r = mixer.call("AddVirtualDevice", a[3], in, outN);
                if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
                out << r.arguments().value(0).toString() << "\n"; return Ok;
            }
            if (op == "remove") { if (!need(4)) return Usage; const QDBusMessage r = mixer.call("RemoveVirtualDevice", a[3]); return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok; }
            return fail(Usage, "devices virtual list|add|remove");
        }
        if (sub == "ports") {   // devices ports <node> — ADR 0009 D4: what "node:POS,POS" may name, and who uses it
            if (!need(3)) return Usage;
            const PortMap pm = qdbus_cast<PortMap>(o.mixer.value("DevicePorts"));
            if (!pm.contains(a[2])) return fail(NotFound, "no device '" + a[2] + "' (see `kmixdeck devices` / `devices in`)");
            QHash<QString, QStringList> users;   // position → "channel voice" / "mix stream"
            for (auto it = o.channels.cbegin(); it != o.channels.cend(); ++it) {
                const QString ref = unwrap(it.value().value("InputDevice")).toString();
                if (ref.section(QLatin1Char(':'), 0, 0) != a[2]) continue;
                const QString posList = ref.section(QLatin1Char(':'), 1);
                for (const auto &p2 : posList.split(QLatin1Char(','), Qt::SkipEmptyParts)) users[p2] << "channel " + it.key().section(QLatin1Char('/'), -1);
                if (posList.isEmpty()) users[QStringLiteral("*")] << "channel " + it.key().section(QLatin1Char('/'), -1);
            }
            for (auto it = o.mixes.cbegin(); it != o.mixes.cend(); ++it)
                for (const auto &ref : unwrap(it.value().value("Outputs")).toStringList()) {
                    if (ref.section(QLatin1Char(':'), 0, 0) != a[2]) continue;
                    const QString posList = ref.section(QLatin1Char(':'), 1);
                    for (const auto &p2 : posList.split(QLatin1Char(','), Qt::SkipEmptyParts)) users[p2] << "mix " + it.key().section(QLatin1Char('/'), -1);
                    if (posList.isEmpty()) users[QStringLiteral("*")] << "mix " + it.key().section(QLatin1Char('/'), -1);
                }
            if (g_json) {
                QJsonArray arr;
                for (const auto &t : pm.value(a[2])) { const auto f = t.split(QLatin1Char('|')); arr.append(QJsonObject{{"position", f.value(0)}, {"port", f.value(1)}, {"alias", f.value(2)}, {"label", f.value(3, f.value(0))}, {"usedBy", QJsonArray::fromStringList(users.value(f.value(0)) + users.value(QStringLiteral("*")))}}); }
                out << QJsonDocument(arr).toJson(); return Ok;
            }
            for (const auto &t : pm.value(a[2])) {
                const auto f = t.split(QLatin1Char('|'));
                const QStringList u = users.value(f.value(0)) + users.value(QStringLiteral("*"));
                const QString label = f.value(3, f.value(0));
                out << QStringLiteral("%1  %2  %3%4\n").arg(f.value(0), -8).arg(label == f.value(0) ? QString() : label, -8).arg(f.value(1), -28).arg(u.isEmpty() ? QString() : QStringLiteral("in use by ") + u.join(QStringLiteral(", ")));
            }
            return Ok;
        }
        if (sub == "hide" || sub == "unhide") {   // CH-11: devices hide|unhide <node.name>
            if (!need(3)) return Usage;
            QDBusMessage r = mixer.call("SetDeviceHidden", a[2], sub == "hide");
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "hidden") {
            if (!nurLesen(2, "hide <node>")) return Usage;
            for (const auto &n : unwrap(o.mixer.value("HiddenDevices")).toStringList()) out << n << "\n";
            return Ok;
        }
        const StringMap devs = qdbus_cast<StringMap>(o.mixer.value(sub == "in" ? "InputDevices" : "OutputDevices"));
        const QStringList hidden = unwrap(o.mixer.value("HiddenDevices")).toStringList();
        if (g_json) { QJsonObject j; for (auto it = devs.cbegin(); it != devs.cend(); ++it) j[it.key()] = it.value(); out << QJsonDocument(j).toJson(); return Ok; }
        for (auto it = devs.cbegin(); it != devs.cend(); ++it) out << QStringLiteral("%1  %2%3\n").arg(it.key(), -48).arg(it.value()).arg(hidden.contains(it.key()) ? QStringLiteral("  (hidden)") : QString());
        return Ok;
    }
    int cmdChannelMix() {
        const bool ch = cmd == "channel"; const auto &objs = ch ? o.channels : o.mixes; const QString iface = ch ? "org.kmixdeck1.Channel" : "org.kmixdeck1.Mix";
        auto pathOf = [&](const QString &slug) { return QStringLiteral("%1/%2/%3").arg(ROOT, ch ? "channel" : "mix", slug); };
        if (sub == "list") return list(objs);
        if (sub == "add") { if (!need(3)) return Usage; return printPath(QDBusReply<QDBusObjectPath>(mixer.call(ch ? "AddChannel" : "AddMix", a[2]))); }
        if (sub == "default" && ch) {   // channel default [<slug>|none]
            if (a.size() < 3) { const QString p = unwrap(o.mixer.value("DefaultChannel")).toString(); out << (p == "/" ? QStringLiteral("none") : p.section(QLatin1Char('/'), -1)) << "\n"; return Ok; }
            if (a[2] != "none" && !objs.contains(pathOf(a[2]))) return fail(NotFound, QStringLiteral("no channel '%1'").arg(a[2]));
            const QDBusObjectPath p(a[2] == "none" ? QStringLiteral("/") : pathOf(a[2]));
            return setProp(QString::fromLatin1(ROOT), "org.kmixdeck1.Mixer", "DefaultChannel", QVariant::fromValue(p), &e) ? Ok : fail(Rejected, e);
        }
        if (sub == "groups" && cmd == "channel") {   // CH-8: every group with its members
            if (!nurLesen(2, "group <slug> <name>")) return Usage;
            QMap<QString, QStringList> groups;
            Objects fresh; if (!fetch(fresh, &e)) return fail(NoService, e);   // `o` is the snapshot from startup; groups may have changed since
            for (auto it = fresh.channels.constBegin(); it != fresh.channels.constEnd(); ++it) { const QString g = it.value().value("Group").toString(); if (!g.isEmpty()) groups[g] << it.value().value("Slug").toString(); }
            if (g_json) { QJsonObject j; for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) j.insert(it.key(), QJsonArray::fromStringList(it.value())); out << QJsonDocument(j).toJson(QJsonDocument::Compact) << "\n"; return Ok; }
            for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) out << it.key() << ": " << it.value().join(", ") << "\n";
            return Ok;
        }
        if (!need(3)) return Usage;
        if (!objs.contains(pathOf(a[2]))) return fail(NotFound, QStringLiteral("no %1 '%2'").arg(cmd, a[2]));
        if (sub == "remove") { QDBusMessage r = mixer.call(ch ? "RemoveChannel" : "RemoveMix", QVariant::fromValue(QDBusObjectPath(pathOf(a[2])))); return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok; }
        if (sub == "duplicate" && !ch) {   // MX-8: mix duplicate <slug> <new name>
            if (!need(4)) return Usage;
            QDBusMessage r = mixer.call("DuplicateMix", QVariant::fromValue(QDBusObjectPath(pathOf(a[2]))), a[3]);
            if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
            out << r.arguments().value(0).value<QDBusObjectPath>().path().section(QLatin1Char('/'), -1) << "\n"; return Ok;
        }
        if (sub == "rename") { if (!need(4)) return Usage; return setProp(pathOf(a[2]), iface, "Name", a[3], &e) ? Ok : fail(Rejected, e); }
        if (sub == "icon") { if (!need(4)) return Usage; return setProp(pathOf(a[2]), iface, "Icon", a[3] == "none" ? QString() : a[3], &e) ? Ok : fail(Rejected, e); }   // UX-8
        if (sub == "group" && cmd == "channel") {   // CH-8: channel group <slug> [<name>|none]
            if (!need(3)) return Usage;
            if (a.size() < 4) { out << unwrap(QDBusInterface(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus()).property("Group")).toString() << "\n"; return Ok; }
            const QString want = a[3] == "none" ? QString() : a[3].trimmed();
            if (!setProp(pathOf(a[2]), iface, "Group", want, &e)) return fail(Rejected, e);
            // property setters cannot return an error over the bus (same as Color) → read back and compare
            const QString got = unwrap(QDBusInterface(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus()).property("Group")).toString();
            if (got != want) return fail(Rejected, "group name: up to 40 characters, no '/'");
            return Ok;
        }
        if (sub == "color" || sub == "colour") {   // MX-5: channel|mix color <slug> <#rrggbb|none>
            if (!need(4)) return Usage;
            const QString want = a[3] == "none" ? QString() : a[3];
            if (!setProp(pathOf(a[2]), iface, "Color", want, &e)) return fail(Rejected, e);
            const QString got = unwrap(QDBusInterface(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus()).property("Color")).toString();
            if (got != want.toLower()) return fail(Rejected, "colour must be #rrggbb or none");
            return Ok;
        }
        if (sub == "move") {   // UX-9: channel|mix move <slug> <index|up|down|top|bottom>
            if (!need(4)) return Usage;
            const QStringList order = unwrap(o.mixer.value(ch ? "ChannelOrder" : "MixOrder")).toStringList();
            const int cur = order.indexOf(a[2]); if (cur < 0) return fail(NotFound, QStringLiteral("no %1 '%2'").arg(ch ? "channel" : "mix", a[2]));
            int idx; bool okNum;
            if (a[3] == "up") idx = cur - 1; else if (a[3] == "down") idx = cur + 1; else if (a[3] == "top") idx = 0; else if (a[3] == "bottom") idx = order.size() - 1;
            else { idx = a[3].toInt(&okNum); if (!okNum) return fail(Usage, "index|up|down|top|bottom"); }
            const QDBusMessage r = mixer.call(ch ? "MoveChannel" : "MoveMix", QVariant::fromValue(QDBusObjectPath(pathOf(a[2]))), idx);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "trim" && ch) { if (!need(4)) return Usage; double l; if (!parseLevel(a[3], &l)) return fail(Usage, "bad level"); return setProp(pathOf(a[2]), iface, "Trim", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "pan" && ch) {   // DV-22: channel pan <slug> [<-1..1>|L|C|R]
            if (a.size() == 3) { out << QString::number(unwrap(objs.value(pathOf(a[2])).value("Pan")).toDouble()) << "\n"; return Ok; }
            bool okd = false; double v = a[3].toDouble(&okd);
            if (!okd) { const QString w = a[3].toUpper(); v = w == "L" ? -1 : w == "R" ? 1 : w == "C" ? 0 : 2; }
            if (v < -1 || v > 1) return fail(Usage, "pan is -1..1, L, C or R");
            return setProp(pathOf(a[2]), iface, "Pan", v, &e) ? Ok : fail(Rejected, e);
        }
        if (sub == "mute" && ch) { bool b; if (!parseBool(a, 3, &b)) return fail(Usage, "on|off"); return setProp(pathOf(a[2]), iface, "Muted", b, &e) ? Ok : fail(Rejected, e); }
        if (sub == "input" && ch) {   // channel input <slug> [<node[:POS,POS]>|none] — ADR 0009 refs
            if (a.size() < 4) { const QString ref = unwrap(objs.value(pathOf(a[2])).value("InputDevice")).toString(); if (g_json) out << QJsonDocument(QJsonObject{{"InputDevice", ref}}).toJson(); else out << (ref.isEmpty() ? QStringLiteral("none") : ref) << "\n"; return Ok; }
            QString dev = a[3] == "none" ? QString() : a[3];
            if (!dev.isEmpty() && !checkRef(dev, true, &e)) return fail(NotFound, e);
            if (!setProp(pathOf(a[2]), iface, "InputDevice", dev, &e)) return fail(Rejected, e);
            QDBusInterface chObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            if (chObj.property("InputDevice").toString() != dev) return fail(Rejected, QStringLiteral("daemon refused '%1' (see its log)").arg(a[3]));
            return Ok;
        }
        if (sub == "inputs" && ch) {   // ADR 0009 B1: all wires into this channel
            if (!nurLesen(3, "input <slug> <ref>")) return Usage;
            const QStringList ins = unwrap(objs.value(pathOf(a[2])).value("Inputs")).toStringList();
            if (g_json) out << QJsonDocument(QJsonArray::fromStringList(ins)).toJson(); else for (const auto &i2 : ins) out << i2 << "\n";
            return Ok;
        }
        if ((sub == "input-add" || sub == "input-remove") && ch) {
            if (!need(4)) return Usage;
            if (sub == "input-add" && !checkRef(a[3], true, &e)) return fail(NotFound, e);
            QDBusInterface chObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            const QDBusMessage r = chObj.call(sub == "input-add" ? "AddInput" : "RemoveInput", a[3]);
            if (r.type() == QDBusMessage::ErrorMessage) return fail(Rejected, r.errorMessage());
            if (!r.arguments().value(0).toBool()) return fail(Rejected, QStringLiteral("daemon refused '%1'").arg(a[3]));
            return Ok;
        }
        if (sub == "volume" && !ch) { if (!need(4)) return Usage; double l; if (!parseLevel(a[3], &l)) return fail(Usage, "bad level"); return setProp(pathOf(a[2]), iface, "Volume", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "mute" && !ch) { bool b; if (!parseBool(a, 3, &b)) return fail(Usage, "on|off"); return setProp(pathOf(a[2]), iface, "Muted", b, &e) ? Ok : fail(Rejected, e); }
        if (sub == "outputs" && !ch) {
            if (!nurLesen(3, "output <slug> <ref>")) return Usage;
            const QStringList outs = unwrap(objs.value(pathOf(a[2])).value("Outputs")).toStringList();
            if (g_json) out << QJsonDocument(QJsonArray::fromStringList(outs)).toJson(); else for (const auto &o2 : outs) out << o2 << "\n";
            return Ok;
        }
        if (sub == "wire" && a.size() >= 4) {   // <channel|mix> wire <slug> <ref> [trim <level>] [mute on|off] — DV-14, applies to BOTH kinds
            QDBusInterface obj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            const QString ref = a[3];
            QDBusReply<double> cur = obj.call("WireTrim", ref);
            if (!cur.isValid() || cur.value() < 0) return fail(Rejected, QStringLiteral("no wire '%1' on '%2'").arg(ref, a[2]));
            QDBusReply<bool> curM = obj.call("WireMuted", ref);
            double trim = cur.value(); bool muted = curM.isValid() && curM.value();
            if (a.size() == 4) {   // show
                if (g_json) out << QJsonDocument(QJsonObject{{"ref", ref}, {"trim", trim}, {"muted", muted}}).toJson();
                else out << ref << "  trim " << QString::number(trim, 'f', 3) << (muted ? "  muted" : "") << "\n";
                return Ok;
            }
            for (int i = 4; i + 1 < a.size() || (i < a.size() && a[i] == "mute"); ) {
                if (a[i] == "trim" && i + 1 < a.size()) { double lin; if (!parseLevel(a[i + 1], &lin)) return fail(Usage, QStringLiteral("bad level '%1'").arg(a[i + 1])); trim = std::cbrt(lin); i += 2; }
                else if (a[i] == "mute") { if (!parseBool(a, i + 1, &muted)) return fail(Usage, QStringLiteral("bad mute value")); i += (i + 1 < a.size() ? 2 : 1); }
                else return fail(Usage, QStringLiteral("unknown word '%1'").arg(a[i]));
            }
            QDBusReply<bool> r = obj.call("SetWireTrim", ref, trim, muted);
            return (r.isValid() && r.value()) ? Ok : fail(Rejected, QStringLiteral("wire trim refused"));
        }
        if ((sub == "output-add" || sub == "output-remove") && !ch) {
            if (!need(4)) return Usage;
            if (sub == "output-add" && !checkRef(a[3], false, &e)) return fail(NotFound, e);   // DV-31: labels → positions
            QDBusInterface mixObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            const QDBusMessage r = mixObj.call(sub == "output-add" ? "AddOutput" : "RemoveOutput", a[3]);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "fallback" && !ch) {
            if (!need(4)) return Usage;
            return setProp(pathOf(a[2]), iface, "FallbackOutput", a[3] == "none" ? QString() : a[3], &e) ? Ok : fail(Rejected, e);
        }
        if (sub == "loudness" && !ch) {   // UX-18: mix loudness <slug> [on|off|<target LUFS>]
            if (!need(3)) return Usage;
            QDBusInterface mxObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            if (a.size() < 4) {           // read: the flag and its target line
                if (g_json) out << QJsonDocument(QJsonObject{{QStringLiteral("loudness"), mxObj.property("Loudness").toBool()},
                                                             {QStringLiteral("target"), mxObj.property("LoudnessTarget").toDouble()}}).toJson(QJsonDocument::Compact) << "\n";
                else out << (mxObj.property("Loudness").toBool() ? "on" : "off") << "  target " << mxObj.property("LoudnessTarget").toDouble() << " LUFS\n";
                out.flush();
                return Ok;
            }
            if (a[3] == QLatin1String("on") || a[3] == QLatin1String("off"))
                return setProp(pathOf(a[2]), iface, "Loudness", a[3] == QLatin1String("on"), &e) ? Ok : fail(Rejected, e);
            bool okNum = false; const double lufs = a[3].toDouble(&okNum);
            if (!okNum) return fail(Usage, QStringLiteral("expected 'on', 'off' or a target in LUFS, got '%1'").arg(a[3]));
            if (!setProp(pathOf(a[2]), iface, "LoudnessTarget", lufs, &e)) return fail(Rejected, e);
            // the daemon refuses a target outside R128 range silently on the property — verify it took
            if (!qFuzzyCompare(mxObj.property("LoudnessTarget").toDouble(), lufs))
                return fail(Rejected, QStringLiteral("daemon refused %1 LUFS (R128 range is -36…0)").arg(lufs));
            return Ok;
        }
        if (sub == "output" && !ch) {   // mix output <slug> <node[:POS,POS]|none> — ADR 0009 refs
            if (!need(4)) return Usage;
            QString dev = a[3] == "none" ? QString() : a[3];
            if (!dev.isEmpty() && !checkRef(dev, false, &e)) return fail(NotFound, e);
            if (!setProp(pathOf(a[2]), iface, "OutputDevice", dev, &e)) return fail(Rejected, e);
            QDBusInterface mxObj(BUS, pathOf(a[2]), iface, QDBusConnection::sessionBus());
            if (mxObj.property("OutputDevice").toString() != dev) return fail(Rejected, QStringLiteral("daemon refused '%1' (see its log)").arg(a[3]));
            if (dev.isEmpty() && !mxObj.property("Outputs").toStringList().isEmpty()) return fail(Rejected, QStringLiteral("mix still has outputs after 'none'"));
            return Ok;
        }
        if (sub == "get" && !ch) {   // mix get <slug> — all properties (JSON) for scripts/tests
            QJsonObject j; const auto props = objs.value(pathOf(a[2]));
            for (auto it = props.cbegin(); it != props.cend(); ++it) j[it.key()] = QJsonValue::fromVariant(unwrap(it.value()));
            out << QJsonDocument(j).toJson(); return Ok;
        }
        return fail(Usage, "unknown subcommand '" + sub + "'");
    }
    int cmdDuck() {   // FX-9: side-chain ducking per channel
        // duck show <slug> | duck set <slug> --by <trigger> [--depth dB] [--threshold dBFS] [--attack ms] [--release ms]
        // duck clear <slug>
        if (!need(3)) return Usage;
        const QString path = QStringLiteral("%1/channel/%2").arg(ROOT, a[2]);
        if (!o.channels.contains(path)) return fail(NotFound, QStringLiteral("no channel '%1'").arg(a[2]));
        if (sub == "show") {
            const QString d = getProp(path, QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Ducking")).toString();
            const double jetzt = getProp(path, QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("DuckReduction")).toDouble();
            if (g_json) {
                // The live reduction only exists at runtime, so it is merged in rather than stored in the layout.
                QJsonObject j = QJsonDocument::fromJson(d.toUtf8()).object();
                j.insert(QStringLiteral("reduction"), jetzt);
                out << QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Compact)) << "\n";
                return Ok;
            }
            const QJsonObject j = QJsonDocument::fromJson(d.toUtf8()).object();
            const QString by = j.value(QStringLiteral("duckedBy")).toString();
            if (by.isEmpty()) { out << "not ducked\n"; return Ok; }
            out << QStringLiteral("ducked by %1: depth %2 dB, threshold %3 dBFS, attack %4 ms, release %5 ms (now %6 dB)\n")
                       .arg(by).arg(j.value(QStringLiteral("depth")).toDouble())
                       .arg(j.value(QStringLiteral("threshold")).toDouble())
                       .arg(j.value(QStringLiteral("attack")).toDouble())
                       .arg(j.value(QStringLiteral("release")).toDouble())
                       .arg(jetzt);
            return Ok;
        }
        QDBusInterface obj(BUS, path, QStringLiteral("org.kmixdeck1.Channel"), QDBusConnection::sessionBus());
        if (sub == "clear") {
            const QDBusMessage r = obj.call(QStringLiteral("SetDucking"), QStringLiteral("{}"));
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "set") {
            // Start from what is configured so a single flag can be changed without restating the rest.
            QJsonObject j = QJsonDocument::fromJson(
                getProp(path, QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Ducking")).toString().toUtf8()).object();
            auto flagWert = [&](const QString &flag, double *ziel) -> int {
                const int i = a.indexOf(flag);
                if (i < 0) return Ok;
                if (i + 1 >= a.size()) return fail(Usage, flag + " needs a value");
                bool okNum = false; const double v = a[i + 1].toDouble(&okNum);
                if (!okNum) return fail(Usage, flag + " must be a number, got '" + a[i + 1] + "'");
                *ziel = v; return Ok;
            };
            const int iBy = a.indexOf(QStringLiteral("--by"));
            if (iBy >= 0) {
                if (iBy + 1 >= a.size()) return fail(Usage, "--by needs a channel");
                j.insert(QStringLiteral("duckedBy"), a[iBy + 1]);
            }
            if (j.value(QStringLiteral("duckedBy")).toString().isEmpty())
                return fail(Usage, "which channel should trigger the ducking? use --by <channel>");
            double tiefe = j.value(QStringLiteral("depth")).toDouble(-12.0);
            double schwelle = j.value(QStringLiteral("threshold")).toDouble(-40.0);
            double anstieg = j.value(QStringLiteral("attack")).toDouble(10.0);
            double abfall = j.value(QStringLiteral("release")).toDouble(200.0);
            for (const auto &p : {std::pair<QString, double *>{QStringLiteral("--depth"), &tiefe},
                                  {QStringLiteral("--threshold"), &schwelle},
                                  {QStringLiteral("--attack"), &anstieg},
                                  {QStringLiteral("--release"), &abfall}})
                if (const int rc = flagWert(p.first, p.second); rc != Ok) return rc;
            j.insert(QStringLiteral("depth"), tiefe);
            j.insert(QStringLiteral("threshold"), schwelle);
            j.insert(QStringLiteral("attack"), anstieg);
            j.insert(QStringLiteral("release"), abfall);
            const QDBusMessage r = obj.call(QStringLiteral("SetDucking"),
                                            QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Compact)));
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        return fail(Usage, "expected 'show', 'set' or 'clear'");
    }

    int cmdFx() {   // effects per channel/mix (ADR 0008)
        // fx types | fx get <channel|mix> <slug> | fx set <channel|mix> <slug> '<json>' | fx control <channel|mix> <slug> <key> <value>
        if (sub == "types") {
            const QString t = unwrap(o.mixer.value("FxTypes")).toString();
            out << t << "\n"; return Ok;
        }
        if (sub == "presets") {   // FX-4: name → chain JSON
            const QString t = unwrap(o.mixer.value("FxPresets")).toString();
            out << t << "\n"; return Ok;
        }
        if (!need(4)) return Usage;
        const bool ch = a[2] == "channel";
        if (!ch && a[2] != "mix") return fail(Usage, "expected 'channel' or 'mix'");
        const auto &objs = ch ? o.channels : o.mixes;
        const QString iface = ch ? "org.kmixdeck1.Channel" : "org.kmixdeck1.Mix";
        const QString path = QStringLiteral("%1/%2/%3").arg(ROOT, ch ? "channel" : "mix", a[3]);
        if (!objs.contains(path)) return fail(NotFound, QStringLiteral("no %1 '%2'").arg(a[2], a[3]));
        QDBusInterface obj(BUS, path, iface, QDBusConnection::sessionBus());
        if (sub == "get") { out << unwrap(objs.value(path).value("FxChain")).toString() << "\n"; return Ok; }
        if (sub == "set") {
            if (!need(5)) return Usage;
            const QDBusMessage r = obj.call("SetFx", a[4]);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "clear") {
            const QDBusMessage r = obj.call("SetFx", "{}");
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "copy") {   // FX-7: same chain onto another object, even while its device is absent
            if (!need(5)) return Usage;
            const bool toCh = a[4] == "channel";
            if (!toCh && a[4] != "mix") return fail(Usage, "expected 'channel' or 'mix'");
            const QString toPath = QStringLiteral("%1/%2/%3").arg(ROOT, toCh ? "channel" : "mix", a.size() > 5 ? a[5] : QString());
            const auto &toObjs = toCh ? o.channels : o.mixes;
            if (!toObjs.contains(toPath)) return fail(NotFound, "target not found");
            const QString chain = unwrap(objs.value(path).value("FxChain")).toString();
            if (chain.isEmpty() || chain == "{}") return fail(NotFound, "source has no chain");
            QDBusInterface to(BUS, toPath, toCh ? "org.kmixdeck1.Channel" : "org.kmixdeck1.Mix", QDBusConnection::sessionBus());
            const QDBusMessage r = to.call("SetFx", chain);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        if (sub == "control") {   // live: full Props key, gate:Threshold (dB) — quoting is the shell's problem
            if (!need(6)) return Usage;
            bool okNum = false; const double v = a[5].toDouble(&okNum);
            if (!okNum) return fail(Usage, "value must be a number");
            const QDBusMessage r = obj.call("SetFxControl", a[4], v);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        return fail(Usage, "unknown fx subcommand '" + sub + "'");
    }
    int cmdCell() {
        if (!need(4)) return Usage;
        const QString path = cellPath(a[2], a[3]);
        if (!o.cells.contains(path)) return fail(NotFound, QStringLiteral("no cell %1×%2").arg(a[2], a[3]));
        if (sub == "get") { const auto c = o.cells[path]; if (g_json) out << QJsonDocument(QJsonObject::fromVariantMap(c)).toJson(); else out << (c.value("Muted").toBool() ? "muted " : "") << db(c.value("Volume").toDouble()).trimmed() << " dB  (linear " << c.value("Volume").toDouble() << ")\n"; return Ok; }
        if (sub == "set") { if (!need(5)) return Usage; double l; if (!parseLevel(a[4], &l)) return fail(Usage, "bad level '" + a[4] + "'"); return setProp(path, "org.kmixdeck1.Cell", "Volume", l, &e) ? Ok : fail(Rejected, e); }
        if (sub == "mute") { bool b; if (!parseBool(a, 4, &b)) return fail(Usage, "on|off"); return setProp(path, "org.kmixdeck1.Cell", "Muted", b, &e) ? Ok : fail(Rejected, e); }
        if (sub == "link") {
            if (!need(5)) return Usage;
            if (a[4] != "none" && !o.mixes.contains(QStringLiteral("%1/mix/%2").arg(ROOT, a[4]))) return fail(NotFound, QStringLiteral("no mix '%1'").arg(a[4]));
            if (a[4] == a[3]) return fail(Usage, "a cell cannot follow its own mix");
            const QDBusObjectPath p(a[4] == "none" ? QStringLiteral("/") : QStringLiteral("%1/mix/%2").arg(ROOT, a[4]));
            return setProp(path, "org.kmixdeck1.Cell", "Follows", QVariant::fromValue(p), &e) ? Ok : fail(Rejected, e);
        }
        return fail(Usage, "unknown subcommand '" + sub + "'");
    }
    int cmdApp() {
        if (sub == "list") {
            if (g_json) { QJsonArray arr; for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it) { auto v = it.value(); v["Path"] = it.key(); arr.append(QJsonObject::fromVariantMap(v)); } out << QJsonDocument(arr).toJson(); }
            else for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it) {
                const QString chPath = it.value().value("Channel").toString();
                const QStringList all = it.value().value("Channels").toStringList();
                out << QStringLiteral("%1  %2 %3 %4  -> %5\n")
                       .arg(it.value().value("NodeId").toString(), 5)
                       .arg(it.value().value("Running").toBool() ? QStringLiteral("*") : QStringLiteral(" "), -1)
                       .arg(it.value().value("Name").toString().left(24), -24)
                       .arg(it.value().value("MediaName").toString().left(20), -20)
                       .arg(all.isEmpty() ? (chPath == "/" ? QStringLiteral("(not on a channel)") : chPath.section('/', -1)) : all.join(QStringLiteral(",")));
            }
            return Ok;
        }
        auto findApp = [&](const QString &needle) {
            QString appPath;
            for (auto it = o.apps.cbegin(); it != o.apps.cend(); ++it)
                if (it.value().value("NodeId").toString() == needle || it.value().value("Name").toString().compare(needle, Qt::CaseInsensitive) == 0) appPath = it.key();
            return appPath;
        };
        if (sub == "move" || sub == "assign") {   // app assign <id|name> <ch>[,<ch>…] — CH-12
            if (!need(4)) return Usage;
            const QString appPath = findApp(a[2]);
            if (appPath.isEmpty()) return fail(NotFound, "no app '" + a[2] + "'");
            QDBusInterface appIf(BUS, appPath, "org.kmixdeck1.App", QDBusConnection::sessionBus());
            if (sub == "move") {
                const QString chPath = QStringLiteral("%1/channel/%2").arg(ROOT, a[3]);
                if (!o.channels.contains(chPath)) return fail(NotFound, "no channel '" + a[3] + "'");
                QDBusMessage r = appIf.call("MoveTo", QVariant::fromValue(QDBusObjectPath(chPath)));
                return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
            }
            QStringList names = a[3].split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (names.isEmpty()) names << a[3];
            QStringList paths;
            for (const QString &n : names) {
                const QString chPath = QStringLiteral("%1/channel/%2").arg(ROOT, n);
                if (!o.channels.contains(chPath)) return fail(NotFound, "no channel '" + n + "'");
                paths << chPath;
            }
            const QDBusMessage r = appIf.call("Assign", paths, false);
            return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
        }
        return fail(Usage, "app list | app move <id|name> <channel> | app assign <id|name> <ch>[,<ch>…]");
    }
    int cmdListen() {   // UX-2: listen [<node.name>|none] — the device I hear on; "what am I hearing" = mixes routed to it
        if (a.size() < 2) {
            const QString dev = unwrap(o.mixer.value("ListeningDevice")).toString();
            QStringList heard; for (auto it = o.mixes.cbegin(); it != o.mixes.cend(); ++it) if (it.value().value("Outputs").toStringList().contains(dev)) heard << it.value().value("Slug").toString();
            if (g_json) { out << QJsonDocument(QJsonObject{{"device", dev}, {"mixes", QJsonArray::fromStringList(heard)}}).toJson(QJsonDocument::Compact) << "\n"; return Ok; }
            out << (dev.isEmpty() ? QStringLiteral("none") : dev) << "  " << (heard.isEmpty() ? QStringLiteral("(no mix routed here)") : heard.join(QLatin1String(", "))) << "\n"; return Ok;
        }
        return setProp(QString::fromLatin1(ROOT), "org.kmixdeck1.Mixer", "ListeningDevice", a[1] == "none" ? QString() : a[1], &e) ? Ok : fail(Rejected, e);
    }
    int cmdAudition() {   // UX-12: hold one entity on the main output; `none` restores
        if (!need(2)) return Usage;
        const QString target = (a[1] == QLatin1String("none"))
            ? QStringLiteral("/")
            : QStringLiteral("%1/%2/%3").arg(ROOT, a[1], a.value(2));
        if (a[1] != QLatin1String("none")) {
            const bool ok = (a[1] == QLatin1String("channel") && o.channels.contains(target))
                         || (a[1] == QLatin1String("mix") && o.mixes.contains(target));
            if (!ok) return fail(NotFound, "expected existing 'channel <slug>' or 'mix <slug>'");
        }
        const QDBusMessage r = mixer.call("Audition", QVariant::fromValue(QDBusObjectPath(target)));
        return r.type() == QDBusMessage::ErrorMessage ? fail(Rejected, r.errorMessage()) : Ok;
    }
    int cmdLevels() {   // live peaks, 25 Hz; Ctrl-C to stop. --json: one object per tick. --once: a single tick, then exit.
        QDBusInterface lv(BUS, ROOT, "org.kmixdeck1.Levels", QDBusConnection::sessionBus());
        QDBusReply<void> sub = lv.call("Subscribe");
        if (!sub.isValid()) return fail(NoService, sub.error().message());
        const bool once = a.contains(QStringLiteral("--once"));
        g_once = once;
        auto *w = new Watcher; w->setParent(&app);
        QDBusConnection::sessionBus().connect(BUS, ROOT, "org.kmixdeck1.Levels", "Peaks", w, SLOT(peaks(QVariantMap)));
        if (once) {   // scripts and tests want one reading, not a stream — quit after the first non-empty tick
            QObject::connect(w, &Watcher::gotPeaks, &app, [] { QCoreApplication::quit(); });
            QTimer::singleShot(4000, &app, [] { QCoreApplication::exit(int(Rejected)); });   // no tick at all = failure
        }
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&lv] { lv.call("Unsubscribe"); });
        return app.exec();
    }
    int cmdLoudness() {   // UX-18: live LUFS for every mix that has the meter on. --once: one reading, then exit.
        QDBusInterface lv(BUS, ROOT, "org.kmixdeck1.Levels", QDBusConnection::sessionBus());
        QDBusReply<void> sub = lv.call("Subscribe");
        if (!sub.isValid()) return fail(NoService, sub.error().message());
        const bool once = a.contains(QStringLiteral("--once"));
        auto *w = new Watcher; w->setParent(&app);
        QDBusConnection::sessionBus().connect(BUS, ROOT, "org.kmixdeck1.Levels", "Loudness", w, SLOT(loudnessSig(QDBusMessage)));
        if (once) {
            QObject::connect(w, &Watcher::gotLoudness, &app, [] { QCoreApplication::quit(); });
            // No mix with the meter on means no signal at all — say so instead of hanging until the timeout.
            QTimer::singleShot(6000, &app, [] { out << "{}\n"; out.flush(); QCoreApplication::quit(); });
        }
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&lv] { lv.call("Unsubscribe"); });
        return app.exec();
    }
    int cmdWatch() {
        Watcher w; auto bus = QDBusConnection::sessionBus();
        bus.connect(BUS, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged", &w, SLOT(propertiesChanged(QDBusMessage)));
        bus.connect(BUS, ROOT, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", &w, SLOT(interfacesAdded(QDBusObjectPath,InterfaceMap)));
        bus.connect(BUS, ROOT, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", &w, SLOT(interfacesRemoved(QDBusObjectPath,QStringList)));
        return app.exec();
    }

    int run() {
        using Fn = int (Cli::*)();
        static const QMap<QString, Fn> table = {
            {QStringLiteral("status"), &Cli::cmdStatus},
            {QStringLiteral("tree"), &Cli::cmdTree},
            {QStringLiteral("patch"), &Cli::cmdPatch},
            {QStringLiteral("complete"), &Cli::cmdComplete},
            {QStringLiteral("loudness"), &Cli::cmdLoudness},
            {QStringLiteral("streamdeck"), &Cli::cmdStreamdeck},
            {QStringLiteral("setup"), &Cli::cmdSetup},
            {QStringLiteral("export"), &Cli::cmdExport},
            {QStringLiteral("import"), &Cli::cmdImport},
            {QStringLiteral("undo"), &Cli::cmdUndo},
            {QStringLiteral("scene"), &Cli::cmdScene},
            {QStringLiteral("devices"), &Cli::cmdDevices},
            {QStringLiteral("channel"), &Cli::cmdChannelMix},
            {QStringLiteral("mix"), &Cli::cmdChannelMix},
            {QStringLiteral("fx"), &Cli::cmdFx},
            {QStringLiteral("duck"), &Cli::cmdDuck},
            {QStringLiteral("cell"), &Cli::cmdCell},
            {QStringLiteral("app"), &Cli::cmdApp},
            {QStringLiteral("listen"), &Cli::cmdListen},
            {QStringLiteral("audition"), &Cli::cmdAudition},
            {QStringLiteral("levels"), &Cli::cmdLevels},
            {QStringLiteral("watch"), &Cli::cmdWatch},
        };
        const Fn fn = table.value(cmd, nullptr);
        if (!fn) return fail(Usage, "unknown command '" + cmd + "'; see --help");
        return (this->*fn)();
    }
};

/// CL-2 + CL-4: Hilfe ohne Daemon, ohne Bus-Aufruf, und `Usage:` nennt das
/// Werkzeug statt argv[0].
///
/// Warum nicht QCommandLineParser::showHelp(): Qt baut die Usage-Zeile aus
/// argv[0], und in einem Build-Baum steht dann `Usage: /var/tmp/build/bin/
/// kmixdeck [options] command` — Pfad-Rauschen, das CL-4 ausdruecklich
/// verbietet. Der Text kommt ohnehin generiert aus docs/kmixdeck.md, also geben
/// wir ihn selbst aus und Qt kommt nicht dazwischen.
///
/// Am TTY laeuft die Ausgabe durch $PAGER (CL-2). In einer Pipe nicht: sonst
/// blockiert `kmixdeck --help | grep mix` auf less.
static int zeigeHilfe(const char *text, bool istTty)
{
    if (istTty) {
        QString pager = qEnvironmentVariable("PAGER");
        if (pager.isEmpty())
            pager = QStringLiteral("less -FRX");     // -F: kurze Ausgabe direkt durchlassen
        QStringList teile = QProcess::splitCommand(pager);
        if (!teile.isEmpty()) {
            const QString prog = teile.takeFirst();
            QProcess pg;
            pg.setProcessChannelMode(QProcess::ForwardedOutputChannel);
            pg.start(prog, teile);
            if (pg.waitForStarted(2000)) {
                pg.write(text);
                pg.closeWriteChannel();
                pg.waitForFinished(-1);
                return 0;
            }
            // Kein Pager da (z. B. minimaler Container): einfach selbst drucken.
        }
    }
    QTextStream out(stdout);
    out << QString::fromUtf8(text);
    return 0;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kmixdeck"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KMIXDECK_VERSION_STRING));
    qDBusRegisterMetaType<StringMap>(); qDBusRegisterMetaType<PortMap>(); qDBusRegisterMetaType<InterfaceMap>(); qDBusRegisterMetaType<ManagedObjects>();
    QCommandLineParser p;
    // CL-1: der Hilfetext kommt GENERIERT aus docs/kmixdeck.md (hilfe_text.h,
    // erzeugt von docs/generiere-doku.py). Bis 2026-09-21 standen hier 38 Zeilen
    // Handtext — das Duplikat, das CL-1 verbietet, und der Drift war schon da:
    // `devices ports`, `devices virtual`, `channel wire`, `mix wire`, `mix get`
    // und `fx clear` fehlten. tools/pruefe-hilfe.py (CL-9) haelt Doku und
    // Dispatch-Tabelle ab jetzt zusammen.
    p.setApplicationDescription(QString::fromUtf8(kmixdeck::HILFE_TEXT));
    p.addHelpOption(); p.addVersionOption();
    QCommandLineOption json({"j", "json"}, "machine-readable output"); p.addOption(json);
    p.addPositionalArgument("command", "see above");
    p.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);   // so "-12dB" is a value, not options

    // CL-2: --help/-h/help/--version MUESSEN ohne laufenden Daemon gehen und
    // duerfen keinen Bus-Aufruf versuchen. Deshalb hier, VOR p.process() und
    // vor jeder Bus-Pruefung, direkt aus argv gelesen. `help` ohne Striche
    // gehoert dazu: die man page ist nicht ueberall installiert.
    // CL-4: eigene Ausgabe statt p.showHelp(), sonst steht argv[0] in der
    // Usage-Zeile (gemessen: "Usage: /var/tmp/build_cl1/bin/kmixdeck ...").
    {
        const QStringList argumente = app.arguments().mid(1);
        const bool willHilfe = argumente.contains(QStringLiteral("--help"))
                || argumente.contains(QStringLiteral("-h"))
                || (!argumente.isEmpty() && argumente.first() == QStringLiteral("help"));
        const bool willVersion = argumente.contains(QStringLiteral("--version"))
                || argumente.contains(QStringLiteral("-v"));
        // CL-7: `complete` gehoert HIERHIN, aus demselben Grund wie --help: es muss ohne
        // Daemon gehen. Eine Shell, deren Completion auf einen Bus-Fehler laeuft, gibt
        // beim Tab gar nichts mehr aus. Unterkommandos kommen statisch aus dem Hilfetext,
        // Slugs/Geraete/Szenen nur wenn der Bus antwortet.
        if (!argumente.isEmpty() && argumente.first() == QStringLiteral("complete"))
            return cmdComplete(argumente);
        if (willVersion) {
            QTextStream(stdout) << "kmixdeck " << KMIXDECK_VERSION_STRING << "\n";
            return Ok;
        }
        if (willHilfe) {
            // CL-3: `kmixdeck help <cmd>` und `kmixdeck <cmd> --help` zeigen die
            // Hilfe genau dieses Kommandos — Synopsis, Parameter, Beispiele. Der
            // gesuchte Name ist das erste Argument, das kein Schalter und nicht
            // "help" selbst ist; so treffen beide Schreibweisen dieselbe Stelle.
            QString gesucht;
            for (const QString &arg : argumente) {
                if (arg.startsWith(QLatin1Char('-')) || arg == QStringLiteral("help"))
                    continue;
                gesucht = arg;
                break;
            }
            if (!gesucht.isEmpty()) {
                for (const auto &eintrag : kmixdeck::KOMMANDO_HILFE) {
                    if (gesucht == QLatin1String(eintrag.name))
                        return zeigeHilfe(eintrag.text, isatty(STDOUT_FILENO) != 0);
                }
                // Unbekanntes Kommando: das ist ein Bedienfehler (Usage), keine
                // stille Vollhilfe — sonst sucht der Benutzer seinen Tippfehler
                // in 54 Zeilen Text.
                QTextStream(stderr) << "kmixdeck: unknown command `" << gesucht
                                    << "` — `kmixdeck --help` lists them all\n";
                return Usage;
            }
            return zeigeHilfe(kmixdeck::HILFE_TEXT, isatty(STDOUT_FILENO) != 0);
        }
        if (argumente.isEmpty())
            return zeigeHilfe(kmixdeck::HILFE_TEXT, isatty(STDOUT_FILENO) != 0);
    }
    p.process(app);
    g_json = p.isSet(json);
    QStringList a = p.positionalArguments();
    if (a.isEmpty()) { p.showHelp(Usage); }
    // CL-8: ein unbekanntes Kommando ist ein Bedienfehler (Code 1) und muss VOR
    // dem Bus-Zugriff auffallen. Sonst bekommt der Benutzer Code 2 mit einer
    // Meldung ueber fehlende .service-Dateien und sucht am falschen Ende.
    if (!istKommando(a[0]))
        return fail(Usage, "unknown command '" + a[0] + "'; `kmixdeck --help` lists them all");
    if (!QDBusConnection::sessionBus().isConnected()) return fail(NoService, "no session bus");

    const bool offline = a[0] == QLatin1String("streamdeck");   // CT-3: file-system only, works without the daemon

    Cli cli(app, p, a);
    if (!offline && !fetch(cli.o, &cli.e)) return fail(NoService, "service not reachable: " + cli.e);
    return cli.run();
}

#include "main.moc"
